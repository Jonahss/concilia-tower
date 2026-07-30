/* people.c - Person entities, trips, queues, and elevator cars
 *
 * The decoded pipeline (simtower-decomp, segs 11b0/1210/1090/11d8):
 *
 *   person decides to travel (UniPeple)
 *     -> TryStartTrip -> FindTransport (walk budget / cost table)
 *     -> walk a stair hop, re-plan on arrival       (legs, not paths)
 *     -> or JoinQueue at a shaft stop (ring buffer, cap 40/direction;
 *        the FIRST joiner of an empty queue presses the call button)
 *     -> CallElevator -> SelectElevator (3-category scoring) -> car
 *     -> car state machine: move / doors(5..0) / idle
 *     -> doors tick 5: everyone for this floor exits; odd ticks: one
 *        boarder; tick 1: bulk board
 *     -> boarding banks (now - wait_start) into the person's stress
 *        accumulator (cap 300) — EXCEPT on service elevators
 *     -> riders travel to their in-shaft target (destination, or the
 *        transfer lobby), unboard, re-plan if not home yet
 *     -> arrival delivers banked frustration to the home tenant
 *
 * Simplifications vs the EXE (documented, to revisit):
 *   - walk chains are approximated by "walk hop toward a lobby that has
 *     a connecting elevator" instead of precomputed chain/slot tables
 *   - cars use a flat ticks-per-floor speed instead of the 4-level
 *     accel curve
 */
#include <string.h>
#include <stdio.h>

#include "people.h"
#include "game.h"
#include "sound_hook.h"

static uint8_t sched_mode_now(const PeopleSim *ps, const ElevatorShaft *s);
static void release_car(Tower *tower, Person *p);

#define GROUND_IDX  (TOWER_LOBBY_FLOOR - TOWER_MIN_FLOOR)

/* The live tuning table — defaults are the EXE's own values (res 0x7F05) */
Tuning TUNING;

void tuning_reset(void)
{
    TUNING = (Tuning){
        .wait_cap = 300,
        .penalty_queue_full = 5,
        .penalty_no_route = 300,
        .penalty_esc_span = 16,
        .penalty_stair_span = 35,
        .penalty_walk_80 = 30,
        .penalty_walk_125 = 60,
        .cost_stair_base = 640,
        .cost_elev_base = 640,
        .cost_elev_full = 1000,
        .cost_transfer = 3000,
        .cost_transfer_full = 6000,
        .walk_floors_esc = 6,
        .walk_floors_stair = 3,
        .capacity_express = 42,
        .capacity_standard = 21,
        .judge_moderate = 150,
        .judge_stressed = 200,
        .star_pop = { 300, 1000, 5000, 10000 },
        .car_cost_std = 80000,
        .car_cost_express = 150000,
        .car_cost_service = 50000,
        .maint_car_std = 10000,
        .maint_car_express = 20000,
        .maint_car_service = 10000,
        .maint_escalator = 5000,
        .star_bonus = { 200000, 300000, 500000 },
        .lobby_fee_star3 = 300,
        .lobby_fee_star4 = 1000,
    };
}

/* Ticks for a walking hop across one floor gap */
#define WALK_TICKS_STAIR 10
#define WALK_TICKS_ESC   8
#define DOOR_OPEN_TICKS  5

/* ---------- init ---------- */

void people_init(PeopleSim *ps)
{
    memset(ps, 0, sizeof(*ps));
    ps->cur_phase = 0xff;
    tuning_reset();
}

/* ---------- transport layout ---------- */

/* Floor-pair route diagnostic (ShowNoRouteMessage 10a8:1b58, strings
 * STRL 0x2CD). Latched per origin floor ([0x77C4] array); cleared on
 * transport rebuild. NOT save-state: a restart merely re-arms the
 * warnings, same as the EXE relaunching. */
static uint8_t noroute_latch[TOWER_FLOOR_COUNT];
static char    noroute_msg[80];
static int     noroute_pending;

static void fmt_floor_name(int fidx, char *out, size_t n)
{
    int f = index_to_floor(fidx);
    if (f < 0)       snprintf(out, n, "B%d", -f);
    else if (f == 0) snprintf(out, n, "Lobby");
    else             snprintf(out, n, "%d", f);
}

static void noroute_report(const Person *p)
{
    int from = p->cur_floor;
    if (from < 0 || from >= TOWER_FLOOR_COUNT || noroute_latch[from]) return;
    noroute_latch[from] = 1;
    char a[16], b[16];
    fmt_floor_name(from, a, sizeof a);
    fmt_floor_name(p->dest_floor, b, sizeof b);
    snprintf(noroute_msg, sizeof noroute_msg,
             "People on Floor %s need a path to Floor %s", a, b);
    noroute_pending = 1;
}

const char *people_take_noroute_msg(void)
{
    if (!noroute_pending) return NULL;
    noroute_pending = 0;
    return noroute_msg;
}

/* ---------- VIP visit (VipT seg_1240, byte-traced 2026-07-29) ----------
 * The VIP is a real suite guest: member 1 (the driver) of tonight's
 * suite check-in. Judged on HIS OWN banked elevator stress vs the
 * demand bar (VipEvaluate 1240:0130 via JudgeT 1130:0360). Not
 * save-state: a mid-visit reload quietly voids the day, like the EXE's
 * CheckVipDay reset across relaunches. */
static int vip_armed;             /* watching for tonight's suite guest */
static int vip_tagged = -1;       /* person just tagged (game.c collects) */
static int vip_watch  = -1;       /* person under judgment */
static unsigned vip_stress_total;
static int vip_trips;
static int vip_result;            /* 0 pending / 1 favorable / 2 not */

void people_vip_arm(int on)
{
    vip_armed = on;
    if (on) {
        vip_watch = -1; vip_tagged = -1;
        vip_result = 0; vip_stress_total = 0; vip_trips = 0;
    }
}
int people_vip_take_tagged(void)
{
    int t = vip_tagged; vip_tagged = -1; return t;
}
int people_vip_take_result(void)
{
    int r = vip_result;
    if (r) { vip_result = 0; vip_watch = -1; }
    return r;
}

static int shaft_serves(const ElevatorShaft *s, int fidx)
{
    if (fidx < s->lo || fidx > s->hi) return 0;
    if (!s->serviced[fidx]) return 0;    /* stop disabled in the dialog */
    if (s->type != ITEM_ELEVATOR_EXPRESS) return 1;
    int wf = index_to_floor(fidx);
    return wf <= 0 || (wf % 15) == 0;    /* basements, ground, sky lobbies */
}

/* Rebuild gap map + shaft list. Cars and queues reset only when the
 * transport layout actually changed (the EXE rebuild pipeline also resets
 * ElvPeple state on stair/elevator placement). */
void people_rebuild_transport(PeopleSim *ps, Tower *tower)
{
    /* Transport changed — re-arm the route warnings (the EXE clears its
     * [0x77C4] latches on the TransferT rebuild). */
    memset(noroute_latch, 0, sizeof(noroute_latch));

    uint8_t gap[TOWER_FLOOR_COUNT];
    memset(gap, 0, sizeof(gap));
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_STAIRS && t->type != ITEM_ESCALATOR) continue;
        int f = floor_to_index(t->floor);
        if (f < 0 || f + 1 >= TOWER_FLOOR_COUNT) continue;
        gap[f] |= (t->type == ITEM_ESCALATOR) ? 1 : 2;   /* 0xCF10 bits */
    }

    /* Contiguous same-type column runs = shafts (as in reachability) */
    ElevatorShaft fresh[MAX_SHAFTS];
    memset(fresh, 0, sizeof(fresh));
    int count = 0;
    for (int x = 0; x < TOWER_WIDTH && count < MAX_SHAFTS; x++) {
        for (int f = 0; f < TOWER_FLOOR_COUNT; ) {
            TowerCell *c = &tower->grid[f][x];
            if (!item_is_elevator(c->type) || c->cell_index != 0) { f++; continue; }
            ItemType ty = c->type;
            int lo = f;
            while (f < TOWER_FLOOR_COUNT && tower->grid[f][x].type == ty &&
                   tower->grid[f][x].cell_index == 0) f++;
            ElevatorShaft *s = &fresh[count++];
            s->active = 1;
            s->type = ty;
            s->lo = (uint8_t)lo;
            s->hi = (uint8_t)(f - 1);
            s->x = x;
            /* group+2 from the EXE: 42 standard, 21 express/service
             * (refreshed from TUNING every tick; clamped to slots) */
            s->capacity = (ty == ITEM_ELEVATOR_EXPRESS) ? 42 : 21;
            s->num_cars = 1;          /* fresh shaft; the dialog's car count is
                                       * restored below when a rebuild matches an
                                       * existing shaft (add-a-car is implemented) */
            /* schedule defaults from MakeElevator (11f8:12ff loop) */
            memset(s->sched_mode, 0, sizeof(s->sched_mode));
            memset(s->sched_threshold, 5, sizeof(s->sched_threshold));
            memset(s->sched_patience, 0, sizeof(s->sched_patience));
            if (count >= MAX_SHAFTS) break;
        }
    }

    /* Layout stamp: FNV over gap map + shaft extents */
    uint32_t h = 2166136261u;
    for (int i = 0; i < TOWER_FLOOR_COUNT; i++) { h ^= gap[i]; h *= 16777619u; }
    for (int i = 0; i < count; i++) {
        uint32_t v = (uint32_t)(fresh[i].type | fresh[i].lo << 8 |
                                fresh[i].hi << 16) ^ (uint32_t)fresh[i].x << 20;
        h ^= v; h *= 16777619u;
    }
    if (h == ps->layout_stamp) return;
    ps->layout_stamp = h;

    /* Default all stops on, then carry dialog settings (car count,
     * serviced flags) across the rebuild — shafts matched by column+type
     * so extending a shaft doesn't wipe its configuration. */
    for (int i = 0; i < count; i++) {
        ElevatorShaft *ns = &fresh[i];
        for (int f = ns->lo; f <= ns->hi; f++) ns->serviced[f] = 1;
        for (int k = 0; k < CARS_PER_SHAFT; k++) ns->home[k] = ns->lo;
        for (int j = 0; j < ps->shaft_count; j++) {
            ElevatorShaft *os = &ps->shafts[j];
            if (!os->active || os->x != ns->x || os->type != ns->type)
                continue;
            ns->num_cars = os->num_cars;
            ns->hidden = os->hidden;
            int lo = os->lo > ns->lo ? os->lo : ns->lo;
            int hi = os->hi < ns->hi ? os->hi : ns->hi;
            for (int f = lo; f <= hi; f++) ns->serviced[f] = os->serviced[f];
            for (int k = 0; k < CARS_PER_SHAFT; k++) {
                int h = os->home[k];
                ns->home[k] = (uint8_t)(h < ns->lo ? ns->lo
                                      : h > ns->hi ? ns->hi : h);
            }
            memcpy(ns->sched_mode, os->sched_mode, sizeof(ns->sched_mode));
            memcpy(ns->sched_threshold, os->sched_threshold,
                   sizeof(ns->sched_threshold));
            memcpy(ns->sched_patience, os->sched_patience,
                   sizeof(ns->sched_patience));
            break;
        }
    }

    memcpy(ps->gap_map, gap, sizeof(gap));
    memcpy(ps->shafts, fresh, sizeof(fresh));
    ps->shaft_count = count;
    for (int i = 0; i < ps->shaft_count; i++) {
        ElevatorShaft *s = &ps->shafts[i];
        for (int c = 0; c < s->num_cars; c++) {
            s->car[c].active = 1;
            s->car[c].floor = s->lo;
            s->car[c].target = s->lo;
            s->car[c].dir = 1;
            s->car[c].schedule_index = sched_mode_now(ps, s);
        }
    }
    /* Anyone queued or riding lost their shaft — replan from where they are */
    for (int i = 0; i < ps->people_high; i++) {
        Person *p = &ps->people[i];
        if (p->home_tenant &&
            (p->state == PERSON_QUEUED || p->state == PERSON_RIDING))
            p->state = PERSON_PLANNING;
    }
}

/* ---------- walk budget (TransferT 0x0dc0 / 0x0e80) ---------- */

static int can_walk(const PeopleSim *ps, int from, int to, int service)
{
    int d = to - from, step = d > 0 ? 1 : -1, n = d > 0 ? d : -d;
    if (n == 0) return 1;
    if (n > TUNING.walk_floors_esc) return 0;
    int stairs_seen = 0;
    for (int i = 0, f = from; i < n; i++, f += step) {
        int g = (step > 0) ? f : f - 1;
        if (g < 0 || g >= TOWER_FLOOR_COUNT) return 0;
        uint8_t bits = ps->gap_map[g];
        if (service) {
            if (!(bits & 2)) return 0;        /* staff: stairs only */
            stairs_seen = 1;
        } else {
            if (!bits) return 0;
            if (!(bits & 1)) stairs_seen = 1; /* stairs-only gap */
        }
        if (stairs_seen && n > TUNING.walk_floors_stair) return 0;
    }
    return 1;
}

/* Find the cheapest stair/escalator tenant covering the gap from->from±1.
 * Returns tenant index or -1; score per the EXE: esc 8*xd, stairs 8*xd+640. */
static int find_stair_hop(Tower *tower, int from, int up, int x,
                          int service, int *score_out)
{
    int best = -1, best_score = COST_NO_ROUTE;
    int gap = up ? from : from - 1;
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_STAIRS && t->type != ITEM_ESCALATOR) continue;
        if (service && t->type != ITEM_STAIRS) continue;
        if (floor_to_index(t->floor) != gap) continue;
        int xd = t->x - x; if (xd < 0) xd = -xd;
        int sc = 8 * xd + (t->type == ITEM_STAIRS ? COST_STAIR_BASE : 0);
        if (sc < best_score) { best_score = sc; best = i; }
    }
    *score_out = best_score;
    return best;
}

static int queue_len(const ElevatorShaft *s, int floor, int up)
{
    const ElevatorStop *st = &s->stop[floor];
    return up ? st->up_count : st->down_count;
}

/* Does a lobby tenant on floor fidx overlap this shaft's footprint? */
static int lobby_overlaps_shaft(Tower *tower, int fidx, const ElevatorShaft *s)
{
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_LOBBY) continue;
        if (floor_to_index(t->floor) != fidx) continue;
        if (s->x < t->x + t->width && s->x + 4 > t->x) return 1;
    }
    return 0;
}

/* Routing verdict for one leg */
typedef struct {
    enum { ROUTE_NONE, ROUTE_ARRIVED, ROUTE_WALK, ROUTE_ELEVATOR } kind;
    int shaft;       /* ROUTE_ELEVATOR */
    int ride_to;     /* in-shaft target floor (dest or transfer lobby) */
    int stair;       /* ROUTE_WALK: tenant index of the stair/escalator */
    int hop_to;      /* ROUTE_WALK: floor after this hop */
} Route;

/* FindTransport port: pick the cheapest next leg from `from` toward `to`.
 * Walking is planned hop by hop (the EXE re-plans at every stair landing). */
static Route find_transport(PeopleSim *ps, Tower *tower, int from, int to,
                            int x, int service)
{
    Route r = { ROUTE_NONE, -1, -1, -1, -1 };
    if (from == to) { r.kind = ROUTE_ARRIVED; return r; }
    int up = to > from;

    /* 1. Walking, if the whole remaining climb is within budget */
    int walk_score = COST_NO_ROUTE, walk_stair = -1;
    if (can_walk(ps, from, to, service))
        walk_stair = find_stair_hop(tower, from, up, x, service, &walk_score);
    if (walk_stair >= 0 && walk_score < COST_STAIR_BASE) {
        /* escalator within 80 cells wins before elevators are considered */
        r.kind = ROUTE_WALK; r.stair = walk_stair;
        r.hop_to = from + (up ? 1 : -1);
        return r;
    }

    /* 2. Elevators: direct, then one transfer at a lobby */
    int best_score = walk_score, best_shaft = -1, best_ride = -1;
    for (int i = 0; i < ps->shaft_count; i++) {
        ElevatorShaft *s = &ps->shafts[i];
        if (!s->active || !shaft_serves(s, from)) continue;
        if (service ? 0 : (s->type == ITEM_ELEVATOR_SERVICE)) continue;
        if (service && s->type != ITEM_ELEVATOR_SERVICE &&
            s->type != ITEM_ELEVATOR_SHAFT) continue;
        int xd = s->x - x; if (xd < 0) xd = -xd;
        int full = queue_len(s, from, up) >= QUEUE_CAP;
        if (shaft_serves(s, to)) {
            int sc;
            if (s->type == ITEM_ELEVATOR_EXPRESS)
                sc = queue_len(s, from, up) + COST_ELEV_BASE;
            else
                sc = 8 * xd + (full ? COST_ELEV_FULL : COST_ELEV_BASE);
            if (sc < best_score) {
                best_score = sc; best_shaft = i; best_ride = to;
            }
        } else {
            /* one transfer: ride to a lobby floor this shaft serves that
             * x-overlaps a second shaft serving `to` */
            for (int L = s->lo; L <= s->hi && best_score > COST_TRANSFER; L++) {
                if (L == from || !shaft_serves(s, L)) continue;
                if (!lobby_overlaps_shaft(tower, L, s)) continue;
                for (int j = 0; j < ps->shaft_count; j++) {
                    if (j == i) continue;
                    ElevatorShaft *s2 = &ps->shafts[j];
                    if (!s2->active || !shaft_serves(s2, L) ||
                        !shaft_serves(s2, to)) continue;
                    if (!service && s2->type == ITEM_ELEVATOR_SERVICE) continue;
                    if (!lobby_overlaps_shaft(tower, L, s2)) continue;
                    int sc = full ? COST_TRANSFER_FULL : COST_TRANSFER;
                    if (sc < best_score) {
                        best_score = sc; best_shaft = i; best_ride = L;
                    }
                    break;
                }
            }
        }
    }
    if (best_shaft >= 0) {
        r.kind = ROUTE_ELEVATOR; r.shaft = best_shaft; r.ride_to = best_ride;
        return r;
    }
    if (walk_stair >= 0) {     /* stairs were legal, just not cheap */
        r.kind = ROUTE_WALK; r.stair = walk_stair;
        r.hop_to = from + (up ? 1 : -1);
        return r;
    }

    /* 3. Walk-chain fallback: hop toward a walkable lobby floor that has a
     * connecting elevator (approximates the EXE's chain transfer tables) */
    for (int L = 0; L < TOWER_FLOOR_COUNT; L++) {
        if (L == from || !can_walk(ps, from, L, service)) continue;
        int has_lobby = 0;
        for (int i = 0; i < ps->shaft_count && !has_lobby; i++) {
            ElevatorShaft *s = &ps->shafts[i];
            if (s->active && shaft_serves(s, L) && shaft_serves(s, to) &&
                !(service ? 0 : s->type == ITEM_ELEVATOR_SERVICE) &&
                lobby_overlaps_shaft(tower, L, s))
                has_lobby = 1;
        }
        if (!has_lobby) continue;
        int sc, hop_up = L > from;
        int st = find_stair_hop(tower, from, hop_up, x, service, &sc);
        if (st >= 0) {
            r.kind = ROUTE_WALK; r.stair = st;
            r.hop_to = from + (hop_up ? 1 : -1);
            return r;
        }
    }
    return r;   /* ROUTE_NONE */
}

/* Info-dialog transport-distance (seg_1108:014b): the nearest vertical
 * transport serving this unit's floor — the one its occupants walk to for
 * their first leg toward the lobby — and the horizontal cell-distance to it.
 * Returns 0 = none serves the floor (stranded) / 1 = walk (stairs/escalator)
 * / 2 = elevator. On a hit *out_dist = |transport_x - x| and *is_stairs marks
 * stairs (1) vs escalator (0). A local nearest scan, not a full route — the
 * port's one-transfer router under-reports connectivity in sky-lobby towers,
 * and the EXE's complaint is about the walk to the FIRST transport anyway. */
int people_nearest_transport(PeopleSim *ps, Tower *tower, int from_fidx,
                             int x, int *out_dist, int *is_stairs)
{
    int best = 1 << 30, kind = 0, stairs = 0;
    /* public (non-service) elevators that stop on this floor */
    for (int i = 0; i < ps->shaft_count; i++) {
        ElevatorShaft *s = &ps->shafts[i];
        if (!s->active || s->type == ITEM_ELEVATOR_SERVICE) continue;
        if (!shaft_serves(s, from_fidx)) continue;
        int d = s->x - x; if (d < 0) d = -d;
        if (d < best) { best = d; kind = 2; stairs = 0; }
    }
    /* stairs / escalators touching this floor (a unit at floor F is served by
     * one anchored at F, going up, or at F-1, arriving from below) */
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *n = &tower->tenants[i];
        if (n->type != ITEM_STAIRS && n->type != ITEM_ESCALATOR) continue;
        int nf = floor_to_index(n->floor);
        if (nf != from_fidx && nf != from_fidx - 1) continue;
        int d = n->x - x; if (d < 0) d = -d;
        if (d < best) { best = d; kind = 1; stairs = (n->type == ITEM_STAIRS); }
    }
    if (kind == 0) return 0;
    if (out_dist)  *out_dist = best;
    if (is_stairs) *is_stairs = stairs;
    return kind;
}

/* ---------- calls + car selection (ElevatorsT 0a4c/0dfc) ---------- */

/* Live schedule entries for this shaft (clock = EXE 0xB3A0/0xB3A1) */
static uint8_t sched_mode_now(const PeopleSim *ps, const ElevatorShaft *s)
{ return s->sched_mode[ps->sched_day][ps->sched_period]; }

static uint8_t sched_threshold_now(const PeopleSim *ps,
                                   const ElevatorShaft *s)
{ return s->sched_threshold[ps->sched_day][ps->sched_period]; }
static uint8_t sched_patience_now(const PeopleSim *ps,
                                  const ElevatorShaft *s)
{ return s->sched_patience[ps->sched_day][ps->sched_period]; }

/* SelectElevator, condensed to its three categories: a car already moving
 * the right way beats an idle car beats a car that must turn around —
 * except that an idle car wins unless the working car beats it by the
 * schedule's threshold (the EXE's final pick, 1090:1053-10cb). */
static int select_car(const PeopleSim *ps, ElevatorShaft *s, int floor,
                      int up)
{
    int best_work = -1, work_cost = 0x7fff;
    int best_idle = -1, idle_cost = 0x7fff;
    for (int ci = 0; ci < s->num_cars; ci++) {
        ElevatorCar *c = &s->car[ci];
        if (!c->active) continue;
        int d = floor - c->floor;
        int toward = up ? d : -d;
        int busy = c->assigned_calls || c->distinct_dests;
        if (!busy) {
            int cost = (d < 0 ? -d : d);                   /* idle: distance */
            if (cost < idle_cost) { idle_cost = cost; best_idle = ci; }
            continue;
        }
        int cost;
        if ((int)c->dir == up && toward >= 0)
            cost = toward;                                  /* approaching */
        else
            cost = (d < 0 ? -d : d) + (s->hi - s->lo) + 8;  /* must reverse */
        if (cost < work_cost) { work_cost = cost; best_work = ci; }
    }
    if (best_idle < 0) return best_work;
    if (best_work < 0) return best_idle;
    /* prefer the idle car unless the working car is threshold-better */
    int th = sched_threshold_now(ps, s);
    return (idle_cost - work_cost >= th) ? best_work : best_idle;
}

static void call_elevator(const PeopleSim *ps, ElevatorShaft *s, int floor,
                          int up)
{
    uint8_t *slot = up ? &s->up_call_car[floor] : &s->down_call_car[floor];
    if (*slot) return;                       /* already assigned */
    int ci = select_car(ps, s, floor, up);
    if (ci < 0) return;
    *slot = (uint8_t)(ci + 1);
    s->car[ci].assigned_calls++;
}

/* ---------- queue ops (TripT 11c2/1332) ---------- */

static int join_queue(const PeopleSim *ps, ElevatorShaft *s, int floor,
                      int up, int person_idx)
{
    ElevatorStop *st = &s->stop[floor];
    uint8_t *count = up ? &st->up_count : &st->down_count;
    uint8_t *head  = up ? &st->up_head  : &st->down_head;
    uint16_t *ring = up ? st->up_ring   : st->down_ring;
    if (*count >= QUEUE_CAP) return 0;
    ring[(*head + *count) % QUEUE_CAP] = (uint16_t)(person_idx + 1);
    if (*count == 0) call_elevator(ps, s, floor, up); /* the call button */
    (*count)++;
    return 1;
}

int people_join_queue(PeopleSim *ps, int shaft, int floor, int up,
                      int person_idx)
{
    return join_queue(ps, &ps->shafts[shaft], floor, up, person_idx);
}

/* ---------- elevator dialog controls (ElvDlogT) ---------- */

void people_set_num_cars(PeopleSim *ps, int shaft, int n)
{
    if (shaft < 0 || shaft >= ps->shaft_count) return;
    ElevatorShaft *s = &ps->shafts[shaft];
    if (n < 1) n = 1;
    if (n > CARS_PER_SHAFT) n = CARS_PER_SHAFT;

    for (int ci = s->num_cars; ci < n; ci++) {   /* new cars park at the pit */
        ElevatorCar *c = &s->car[ci];
        memset(c, 0, sizeof(*c));
        c->active = 1;
        c->floor = c->target = s->lo;
        c->dir = 1;
        s->home[ci] = s->lo;
    }
    for (int ci = n; ci < s->num_cars; ci++) {   /* retired cars dump riders */
        ElevatorCar *c = &s->car[ci];
        uint8_t mine = (uint8_t)(ci + 1);
        for (int k = 0; k < CAR_SLOTS; k++) {
            if (!c->pax[k]) continue;
            Person *p = &ps->people[c->pax[k] - 1];
            p->cur_floor = c->floor;
            p->state = PERSON_PLANNING;
        }
        for (int f = 0; f < TOWER_FLOOR_COUNT; f++) {
            if (s->up_call_car[f] == mine) s->up_call_car[f] = 0;
            if (s->down_call_car[f] == mine) s->down_call_car[f] = 0;
        }
        memset(c, 0, sizeof(*c));
    }
    s->num_cars = (uint8_t)n;
}

void people_set_home(PeopleSim *ps, int shaft, int car, int fidx)
{
    if (shaft < 0 || shaft >= ps->shaft_count) return;
    ElevatorShaft *s = &ps->shafts[shaft];
    if (car < 0 || car >= s->num_cars) return;
    if (fidx < s->lo || fidx > s->hi || !s->serviced[fidx]) return;
    s->home[car] = (uint8_t)fidx;
}

void people_set_serviced(PeopleSim *ps, int shaft, int fidx, int on)
{
    if (shaft < 0 || shaft >= ps->shaft_count) return;
    ElevatorShaft *s = &ps->shafts[shaft];
    if (fidx < s->lo || fidx > s->hi) return;
    s->serviced[fidx] = on ? 1 : 0;
    if (on) return;

    /* Stop disabled: flush its queues to replan; riders already aboard
     * still get dropped here (cars honor dest_count for unloading). */
    ElevatorStop *st = &s->stop[fidx];
    for (int d = 0; d < 2; d++) {
        uint8_t   cnt  = d ? st->down_count : st->up_count;
        uint8_t   head = d ? st->down_head  : st->up_head;
        uint16_t *ring = d ? st->down_ring  : st->up_ring;
        for (int k = 0; k < cnt; k++) {
            uint16_t pe = ring[(head + k) % QUEUE_CAP];
            if (pe) ps->people[pe - 1].state = PERSON_PLANNING;
        }
    }
    memset(st, 0, sizeof(*st));
    uint8_t up_owner = s->up_call_car[fidx], dn_owner = s->down_call_car[fidx];
    if (up_owner && s->car[up_owner - 1].assigned_calls)
        s->car[up_owner - 1].assigned_calls--;
    if (dn_owner && s->car[dn_owner - 1].assigned_calls)
        s->car[dn_owner - 1].assigned_calls--;
    s->up_call_car[fidx] = s->down_call_car[fidx] = 0;
    /* cars homed on a disabled stop fall back to the bottom */
    for (int k = 0; k < s->num_cars; k++)
        if (s->home[k] == fidx) s->home[k] = s->lo;
}

/* Pull one person out of a stop ring mid-queue (the 300-frame watchdog).
 * Compacts the ring in place; the pending call (if any) stays with the
 * car and clears normally when it arrives to an empty stop. */
static void queue_remove(ElevatorShaft *s, int floor, int up, int person_idx)
{
    ElevatorStop *st = &s->stop[floor];
    uint8_t *count = up ? &st->up_count : &st->down_count;
    uint8_t *head  = up ? &st->up_head  : &st->down_head;
    uint16_t *ring = up ? st->up_ring   : st->down_ring;
    uint16_t needle = (uint16_t)(person_idx + 1);
    for (int k = 0; k < *count; k++) {
        int pos = (*head + k) % QUEUE_CAP;
        if (ring[pos] != needle) continue;
        for (int m = k; m + 1 < *count; m++) {
            int a = (*head + m) % QUEUE_CAP, b = (*head + m + 1) % QUEUE_CAP;
            ring[a] = ring[b];
        }
        (*count)--;
        return;
    }
}

static int dequeue(ElevatorShaft *s, int floor, int up)
{
    ElevatorStop *st = &s->stop[floor];
    uint8_t *count = up ? &st->up_count : &st->down_count;
    uint8_t *head  = up ? &st->up_head  : &st->down_head;
    uint16_t *ring = up ? st->up_ring   : st->down_ring;
    if (*count == 0) return -1;
    int p = ring[*head] - 1;
    *head = (uint8_t)((*head + 1) % QUEUE_CAP);
    (*count)--;
    return p;
}

/* ---------- stress (WaitT -> tenant) ---------- */

static void bank_wait(Person *p, int frame)
{
    int waited = frame - p->wait_start;
    if (waited < 0) waited = 0;
    int acc = p->wait_accum + waited;
    p->wait_accum = (uint16_t)(acc > WAIT_CAP ? WAIT_CAP : acc);
}

static void add_penalty(Person *p, int amount)
{
    int acc = p->wait_accum + amount;
    p->wait_accum = (uint16_t)(acc > WAIT_CAP ? WAIT_CAP : acc);
}

/* Deliver banked frustration to the home tenant on arrival.
 * Thresholds 150/200 come from the tuning resource (0xDD8A/0xDD8E). One
 * consumer is now byte-verified: the hotel demand bar [0xDD78] is this
 * same pair, star-selected (150 at stars 1-3, 200 at 4+) — R4 referee
 * 2026-07-09. The office/condo-side consumers remain unverified. */
static void deliver_stress(PeopleSim *ps, Tower *tower, Person *p)
{
    Tenant *t = tower_tenant(tower, p->home_tenant);
    /* The grand lobby forgives some waiting before it stings (WaitT). The
     * forgiveness is what the tenant *feels*; the raw wait still feeds the
     * elevator-performance average below. */
    int felt = p->wait_accum - ps->lobby_bonus;
    if (felt < 0) felt = 0;
    if ((int)(p - ps->people) == vip_watch) {      /* the VIP's own book */
        vip_stress_total += (unsigned)felt;
        vip_trips++;
    }
    if (t) {
        if (felt >= TUNING.judge_stressed)      t->stress += 15;
        else if (felt >= TUNING.judge_moderate) t->stress += 5;
        if (t->stress > 100) t->stress = 100;
        /* Hotel rooms, offices and condos bank the raw felt stress for
         * their demand verdicts — the 5PM pass for rooms, the daily
         * 4:59AM judge for the others. The EXE keeps this per-person as
         * total(+0x0E) / PERIODS LIVED(+0x09) — JudgeT 1130:0360 — not
         * per-trip: an office worker banks 2 trips' stress but lives ~6
         * periods a day, so the divisor grows 3 per arrival. A hotel
         * guest's stay spans about as many periods as trips, so rooms
         * keep the 1:1 divisor (their live-verified behavior). */
        if ((item_is_hotel_room(t->type) || t->type == ITEM_OFFICE ||
             t->type == ITEM_CONDO) &&
            t->pool_stress_trips < 0xFFF0) {
            unsigned tot = t->pool_stress_total + (unsigned)felt;
            t->pool_stress_total = (uint16_t)(tot > 0xFFFF ? 0xFFFF : tot);
            t->pool_stress_trips += item_is_hotel_room(t->type) ? 1 : 3;
        }
    }
    ps->wait_total += p->wait_accum;
    ps->wait_samples++;
    p->wait_accum = 0;
}

/* ---------- trip planning (TryStartTrip port) ---------- */

static void trip_arrived(PeopleSim *ps, Tower *tower, Person *p, int frame);
static int is_retail_kind(ItemType ty);

static void plan_person(PeopleSim *ps, Tower *tower, Person *p, int pi, int frame)
{
    if (p->cur_floor == p->dest_floor) { trip_arrived(ps, tower, p, frame); return; }
    Route r = find_transport(ps, tower, p->cur_floor, p->dest_floor,
                             p->x, p->service);
    switch (r.kind) {
    case ROUTE_WALK: {
        Tenant *st = &tower->tenants[r.stair];
        int span_pen = (st->type == ITEM_STAIRS) ? PENALTY_STAIR_SPAN
                                                 : PENALTY_ESC_SPAN;
        add_penalty(p, span_pen);
        int xd = st->x - p->x; if (xd < 0) xd = -xd;
        if (xd >= 125) add_penalty(p, PENALTY_WALK_125);
        else if (xd >= 80) add_penalty(p, PENALTY_WALK_80);
        p->state = PERSON_WALKING;
        p->leg_floor = (uint8_t)r.hop_to;
        p->walk_stair = st->id;
        p->walk_timer = (st->type == ITEM_STAIRS) ? WALK_TICKS_STAIR
                                                  : WALK_TICKS_ESC;
        break;
    }
    case ROUTE_ELEVATOR: {
        ElevatorShaft *s = &ps->shafts[r.shaft];
        int up = r.ride_to > p->cur_floor;
        if (!join_queue(ps, s, p->cur_floor, up, pi)) {
            add_penalty(p, PENALTY_QUEUE_FULL);
            break;                       /* stays PLANNING, retries */
        }
        if (s->type != ITEM_ELEVATOR_SHAFT) {
            int xd = s->x - p->x; if (xd < 0) xd = -xd;
            if (xd >= 125) add_penalty(p, PENALTY_WALK_125);
            else if (xd >= 80) add_penalty(p, PENALTY_WALK_80);
        }
        p->state = PERSON_QUEUED;
        p->shaft = (uint8_t)r.shaft;
        p->dir = (uint8_t)up;
        p->leg_floor = (uint8_t)r.ride_to;
        p->wait_start = frame;
        break;
    }
    case ROUTE_ARRIVED:
        trip_arrived(ps, tower, p, frame);
        break;
    default:
        add_penalty(p, PENALTY_NO_ROUTE);   /* = 300: instant cap-out */
        deliver_stress(ps, tower, p);
        ps->trips_failed++;
        noroute_report(p);   /* "People on Floor X need a path to Floor Y" */
        p->state = PERSON_AT_DEST;          /* gives up where they stand */
        break;
    }
}

static void trip_arrived(PeopleSim *ps, Tower *tower, Person *p, int frame)
{
    (void)frame;
    ps->trips_done++;
    if (p->going_home) {
        /* commuters/patrons leave at ground; staff arrive back at their
         * unit — either way the return trip ends the entity */
        deliver_stress(ps, tower, p);
        release_car(tower, p);
        /* VIP checkout: judge his stay on his own banked average vs the
         * 3-star demand bar (1240:0158; [0xDD78] = 150 below 4 stars) */
        if ((int)(p - ps->people) == vip_watch && !vip_result) {
            int avg = vip_trips ? (int)(vip_stress_total / (unsigned)vip_trips)
                                : 0;
            vip_result = (avg <= TUNING.judge_moderate) ? 1 : 2;
        }
        p->home_tenant = 0;
        p->state = PERSON_FREE;
        return;
    }
    /* Capture the felt wait BEFORE deliver_stress zeroes the accumulator —
     * it grades the retail service score (Restaurant.c 11a8:1197). */
    int felt = p->wait_accum - ps->lobby_bonus;
    if (felt < 0) felt = 0;
    deliver_stress(ps, tower, p);
    /* Sales-errand legs (UniPeple office statuses 0x40/0x61): the lobby
     * arrival parks the salesman making calls; the return leg puts him
     * back at his desk. Neither is a "worker at the desk" arrival for
     * the hooks below. */
    if (p->errand == 1 || p->errand == 3) {
        p->errand = (p->errand == 1) ? 2 : 4;
        p->state = PERSON_AT_DEST;
        return;
    }
    if (p->errand == 5 || p->errand == 7) {   /* clinic trip legs */
        p->errand = (p->errand == 5) ? 6 : 0;
        p->state = PERSON_AT_DEST;
        return;
    }
    /* Hotel guests checking in mark the room hosted (housekeeping loop)
     * and reset its neglect fuse — check-in is the ONLY thing that resets
     * the fuse (HotelCheckIn 1178:0e65 zeroes tenure; maids never do). */
    Tenant *t = tower_tenant(tower, p->home_tenant);
    if (t && !p->going_home && item_is_hotel_room(t->type)) {
        t->hosted = 1;
        t->tenure = 0;
        /* VIP registration (VipArrival 1240:0000): tonight's first
         * suite check-in by member 1 — the guest who drives. */
        if (vip_armed && t->type == ITEM_HOTEL_SUITE && p->member == 1) {
            vip_armed = 0;
            vip_watch = vip_tagged = (int)(p - ps->people);
        }
    }
    /* A worker at their desk — candidate for the sick-worker roll */
    if (t && !p->going_home && t->type == ITEM_OFFICE &&
        ps->office_arrivals < (int)(sizeof ps->office_arrival_floor))
        ps->office_arrival_floor[ps->office_arrivals++] = (int8_t)t->floor;
    /* A patron at the box office — the venue pass counts attendance */
    if (t && !p->going_home &&
        (t->type == ITEM_CINEMA || t->type == ITEM_PARTY_HALL) &&
        ps->venue_arrivals < (int)(sizeof ps->venue_arrival_tenant /
                                   sizeof ps->venue_arrival_tenant[0]))
        ps->venue_arrival_tenant[ps->venue_arrivals++] = p->home_tenant;
    /* A mover reaching a vacant, armed unit — the re-let event. */
    if (t && !p->going_home && t->state == TENANT_ABANDONED &&
        t->demand_armed &&
        (t->type == ITEM_OFFICE || t->type == ITEM_CONDO ||
         t->type == ITEM_SHOP) &&
        ps->relet_arrivals < (int)(sizeof ps->relet_arrival_tenant /
                                   sizeof ps->relet_arrival_tenant[0]))
        ps->relet_arrival_tenant[ps->relet_arrivals++] = p->home_tenant;
    /* A customer at a retail door — queued for the InRestPeple pass
     * (game_retail_arrivals): admission, customer count, service grade.
     * Street walk-ins are the ones who entered at ground. */
    if (t && !p->going_home && is_retail_kind(t->type) &&
        ps->retail_arrivals < (int)(sizeof ps->retail_arrival_tenant /
                                    sizeof ps->retail_arrival_tenant[0])) {
        int a = ps->retail_arrivals++;
        ps->retail_arrival_tenant[a] = p->home_tenant;
        ps->retail_arrival_wait[a] = (uint16_t)(felt > 0xFFFF ? 0xFFFF : felt);
        ps->retail_arrival_walkin[a] = (p->entry_floor == GROUND_IDX);
    }
    p->state = PERSON_AT_DEST;
}

/* ---------- car state machine (ElevatorsT MoveElevator port) ---------- */

/* SCAN: nearest floor needing service in the current direction, else
 * reverse, else the bottom of the shaft (home). */
static int find_target_floor(ElevatorShaft *s, ElevatorCar *c, int ci)
{
    uint8_t mine = (uint8_t)(ci + 1);
    for (int pass = 0; pass < 2; pass++) {
        int up = pass == 0 ? c->dir : !c->dir;
        int f = c->floor;
        while (1) {
            f += up ? 1 : -1;
            if (f < s->lo || f > s->hi) break;
            if (c->dest_count[f] ||
                s->up_call_car[f] == mine || s->down_call_car[f] == mine)
                return f;
        }
    }
    return -1;
}

static void unboard_at_floor(PeopleSim *ps, Tower *tower, ElevatorShaft *s,
                             ElevatorCar *c, int frame)
{
    (void)s;
    for (int i = 0; i < CAR_SLOTS; i++) {
        if (!c->pax[i] || c->pax_dest[i] != c->floor) continue;
        int pi = c->pax[i] - 1;
        Person *p = &ps->people[pi];
        c->pax[i] = 0;
        c->pax_dest[i] = 0xff;
        c->passengers--;
        p->cur_floor = c->floor;
        p->state = PERSON_PLANNING;     /* re-plan: done, or transfer leg 2 */
        plan_person(ps, tower, p, pi, frame);
    }
    if (c->dest_count[c->floor]) {
        if (c->distinct_dests) c->distinct_dests--;
        c->dest_count[c->floor] = 0;
    }
}

/* Board one person from the queue in the car's direction. Returns 1 if
 * someone boarded. Implements idle-direction-adoption and the
 * service-elevators-don't-bank-stress rule. */
static int board_one(PeopleSim *ps, Tower *tower, ElevatorShaft *s,
                     ElevatorCar *c, int frame)
{
    (void)tower;
    if (c->passengers >= s->capacity) return 0;
    int up = c->dir;
    if (queue_len(s, c->floor, up) == 0) {
        if (queue_len(s, c->floor, !up) == 0) return 0;
        if (c->schedule_index) {
            up = !up;       /* both-direction pickup (TripT, sched != 0) */
        } else {
            if (c->assigned_calls || c->distinct_dests) return 0;
            c->dir = (uint8_t)!up;      /* adopt the waiting direction */
            up = c->dir;
        }
    }
    int pi = dequeue(s, c->floor, up);
    if (pi < 0) return 0;
    Person *p = &ps->people[pi];
    if (s->type != ITEM_ELEVATOR_SERVICE)
        bank_wait(p, frame);            /* staff never accrue wait stress */
    /* in-shaft destination: leg target (dest or transfer lobby) */
    int slot = -1;
    for (int i = 0; i < CAR_SLOTS; i++) if (!c->pax[i]) { slot = i; break; }
    if (slot < 0) return 0;
    c->pax[slot] = (uint16_t)(pi + 1);
    c->pax_dest[slot] = p->leg_floor;
    if (c->dest_count[p->leg_floor] == 0) c->distinct_dests++;
    if (c->dest_count[p->leg_floor] < 255) c->dest_count[p->leg_floor]++;
    c->passengers++;
    p->state = PERSON_RIDING;
    return 1;
}

static void clear_call(ElevatorShaft *s, ElevatorCar *c, int ci, int floor)
{
    uint8_t mine = (uint8_t)(ci + 1);
    if (s->up_call_car[floor] == mine) {
        s->up_call_car[floor] = 0;
        if (c->assigned_calls) c->assigned_calls--;
    }
    if (s->down_call_car[floor] == mine) {
        s->down_call_car[floor] = 0;
        if (c->assigned_calls) c->assigned_calls--;
    }
}

/* CalcMoveSpeed (1090:209f): the accel/decel curve, as ticks per floor.
 * The EXE's 4 levels: slow near either end of the run, full speed only in
 * the middle of a long haul. The gear-3 turbo belongs to the EXPRESS
 * (type 0 — globals.md #52); standard/service get a medium band instead.
 * dist-from-start needs the run's departure floor (last_floor in the
 * EXE; leg_start here). */
static int car_move_ticks(const ElevatorShaft *s, const ElevatorCar *c)
{
    int dt = c->target - c->floor;   if (dt < 0) dt = -dt;
    int ds = c->floor - c->leg_start; if (ds < 0) ds = -ds;
    if (s->type == ITEM_ELEVATOR_EXPRESS) {
        if (dt < 2 || ds < 2) return 4;   /* speed 0: crawl at the ends */
        if (dt > 4 && ds > 4) return 1;   /* speed 3: full tilt mid-run */
        return 2;                         /* speed 2: cruise */
    }
    if (dt < 2 || ds < 2) return 4;       /* speed 0 */
    if (dt < 4 || ds < 4) return 3;       /* speed 1: medium */
    return 2;                             /* speed 2 */
}

static void car_start_step(ElevatorShaft *s, ElevatorCar *c)
{
    c->move_timer = (uint8_t)car_move_ticks(s, c);
    c->move_total = c->move_timer;
}

/* UpdateDirection (1090:1d2f) essence: a car that stopped to answer a call
 * it owns turns to face the call's direction before boarding. */
static void adopt_call_direction(ElevatorShaft *s, ElevatorCar *c, int ci)
{
    uint8_t mine = (uint8_t)(ci + 1);
    if (s->up_call_car[c->floor] == mine) c->dir = 1;
    else if (s->down_call_car[c->floor] == mine) c->dir = 0;
}

/* highest/lowest serviced floor — the shuttle mode's loop ends */
static int shaft_extreme(const ElevatorShaft *s, int top)
{
    if (top) { for (int f = s->hi; f >= s->lo; f--)
                   if (s->serviced[f]) return f; }
    else     { for (int f = s->lo; f <= s->hi; f++)
                   if (s->serviced[f]) return f; }
    return s->lo;
}

static void car_depart_or_idle(PeopleSim *ps, ElevatorShaft *s,
                               ElevatorCar *c, int ci)
{
    /* turnaround = where the EXE refreshes schedule_index (1090:08e7) */
    c->schedule_index = sched_mode_now(ps, s);
    clear_call(s, c, ci, c->floor);
    /* people still queued here with no assigned car press the button again */
    if (queue_len(s, c->floor, 1) && !s->up_call_car[c->floor])
        call_elevator(ps, s, c->floor, 1);
    if (queue_len(s, c->floor, 0) && !s->down_call_car[c->floor])
        call_elevator(ps, s, c->floor, 0);

    int tgt = find_target_floor(s, c, ci);
    /* FindTargetFloor (1090:1553): a car with no work returns to its
     * home floor (group +0xBA[8]); in shuttle mode (schedule 1, 1090:15c0)
     * it runs to the far end of the shaft instead. */
    if (tgt < 0 && !c->passengers) {
        int rest = (c->schedule_index == 1)
                       ? shaft_extreme(s, c->dir)
                       : s->home[ci];
        if (c->floor != rest) tgt = rest;
    }
    if (tgt < 0) { c->target = c->floor; return; }   /* idle in place */
    c->target = (uint8_t)tgt;
    c->dir = (uint8_t)(tgt > c->floor);
    c->leg_start = c->floor;
    /* ShouldTimeout (1090:23a5): the schedule's patience holds the car at
     * the floor before it departs — unless it's full. */
    if (c->passengers < s->capacity)
        c->hold_timer = (uint8_t)(sched_patience_now(ps, s) * 30);
    if (!c->hold_timer)
        car_start_step(s, c);
}

static void car_tick(PeopleSim *ps, Tower *tower, ElevatorShaft *s,
                     int ci, int frame)
{
    ElevatorCar *c = &s->car[ci];
    if (!c->active) return;

    if (c->door_timer) {
        if (c->door_timer == DOOR_OPEN_TICKS) {
            /* Arrival "ding" (#6001): the EXE gates on the car's requested-stop
             * byte [bx+0x2A6C] for this floor (our dest_count), read before
             * unboarding clears it — so a car dings once on arriving at a
             * requested stop, not on every door re-open.
             * referee_sound_events_2026-07-13.md row 9. */
            if (c->dest_count[c->floor])
                play_snd(SND_ELEV_DING);
            unboard_at_floor(ps, tower, s, c, frame);
        }
        if (c->door_timer & 1) {
            if (c->door_timer == 1) {
                while (board_one(ps, tower, s, c, frame)) {}
            } else {
                board_one(ps, tower, s, c, frame);
            }
        }
        c->door_timer--;
        if (c->door_timer == 0)
            car_depart_or_idle(ps, s, c, ci);
        return;
    }

    if (c->hold_timer) {
        /* patience dwell: linger for stragglers; new activity at this
         * floor reopens the doors, otherwise depart when it runs out */
        if (queue_len(s, c->floor, 1) || queue_len(s, c->floor, 0) ||
            c->dest_count[c->floor]) {
            c->hold_timer = 0;
            adopt_call_direction(s, c, ci);
            c->door_timer = DOOR_OPEN_TICKS;
            return;
        }
        if (--c->hold_timer == 0 && c->target != c->floor)
            car_start_step(s, c);
        return;
    }

    if (c->target != c->floor) {
        if (c->move_timer) { c->move_timer--; return; }
        c->floor += (c->dir ? 1 : -1);
        if (c->floor == c->target) {
            adopt_call_direction(s, c, ci);
            c->door_timer = DOOR_OPEN_TICKS;
            c->move_total = 0;
        } else {
            car_start_step(s, c);
        }
        return;
    }

    /* idle at a floor: serve it if anyone wants on/off */
    if (c->dest_count[c->floor] ||
        queue_len(s, c->floor, 1) || queue_len(s, c->floor, 0)) {
        adopt_call_direction(s, c, ci);
        c->door_timer = DOOR_OPEN_TICKS;
        return;
    }
    car_depart_or_idle(ps, s, c, ci);   /* schedule-aware idle targeting */
}

/* ---------- spawning (UniPeple-lite: commuters only) ---------- */

/* Returns slot index + 1 (0 = no slot), so callers can find the person */
static int spawn_person(PeopleSim *ps, Tower *tower, Tenant *t,
                        int from, int to, int going_home)
{
    int slot = -1;
    for (int i = 0; i < MAX_PEOPLE; i++)
        if (!ps->people[i].home_tenant) { slot = i; break; }
    if (slot < 0) return 0;
    if (slot >= ps->people_high) ps->people_high = slot + 1;
    Person *p = &ps->people[slot];
    memset(p, 0, sizeof(*p));
    p->home_tenant = t->id;
    /* numInTenant: position among this tenant's live people — commuters
     * respawn in the same order daily, so the index is stable for
     * workers/residents/staff (what the person-type classifier and the
     * name registry key on). */
    {
        int member = 0;
        for (int i = 0; i < ps->people_high; i++)
            if (ps->people[i].home_tenant == t->id && i != slot) member++;
        p->member = (uint8_t)(member > 255 ? 255 : member);
    }
    p->state = PERSON_PLANNING;
    p->cur_floor = (uint8_t)from;
    p->dest_floor = (uint8_t)to;
    p->going_home = (uint8_t)going_home;
    p->entry_floor = GROUND_IDX;   /* street level by default; metro overrides */
    p->x = t->x;
    (void)tower;
    return slot + 1;
}

/* Lowest floor with a DIRTY hotel room (housekeeper dispatch target).
 * Infested rooms are not on the maids' list — they never clean those
 * (MainteT's picker matches the dirty band exactly). */
static int find_dirty_room_floor(Tower *tower)
{
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (item_is_hotel_room(t->type) && t->condition == ROOM_DIRTY) {
            int f = floor_to_index(t->floor);
            if (f >= 0 && f < TOWER_FLOOR_COUNT) return f;
        }
    }
    return -1;
}

static int tenant_commuters(const Tenant *t)
{
    int n = TENANT_POPULATION[t->type];
    return n > 8 ? 8 : n;       /* cap per tenant to keep entity counts sane */
}

/* Staggered-departure dice. The EXE doesn't hold a spawn-rate table: in the
 * per-person AI (UniPeple sim funcs, e.g. office 0x2e92) each person, processed
 * at 1/16 LOD, rolls rand()%12==0 inside a time-of-day window to decide whether
 * to set off — so arrivals trickle in irregularly across the window instead of
 * bursting all at once. We fold (frame, index) into a hash for the same
 * irregular trickle, but reproducibly (the sim stays replayable). Returns true
 * on a 1-in-n hit. */
static int depart_roll(int frame, int idx, int n)
{
    uint32_t h = (uint32_t)frame * 2654435761u ^ (uint32_t)idx * 40503u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (h % (uint32_t)n) == 0;
}

static int is_retail_kind(ItemType ty)
{
    return ty == ITEM_SHOP || ty == ITEM_RESTAURANT || ty == ITEM_FAST_FOOD;
}

/* Pick a same-kind retail venue for a customer on `floor` — the EXE's
 * PickRestaurant (11a8:12dc): uniform random over the 15-floor zone's
 * venues of that kind, falling back to the ground zone, and THEN the
 * validity check — a closed pick fails the whole attempt, no re-roll.
 * This selection IS the competition mechanic: more same-kind venues in a
 * zone = fewer expected customers each. Returns NULL on a failed attempt. */
static Tenant *pick_retail(Tower *tower, ItemType kind, int floor, int seed,
                           const uint8_t *reach)
{
    int zone = floor_to_zone(floor);
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            if (zone == 0) return NULL;    /* ground zone already searched */
            zone = 0;
        }
        int n = 0;
        for (int i = 0; i < tower->tenant_count; i++) {
            Tenant *t = &tower->tenants[i];
            if (t->type == kind && t->state == TENANT_OCCUPIED &&
                floor_to_zone(t->floor) == zone) n++;
        }
        if (!n) continue;                  /* empty zone -> ground fallback */
        int pick = ((seed % n) + n) % n, k = 0;
        for (int i = 0; i < tower->tenant_count; i++) {
            Tenant *t = &tower->tenants[i];
            if (t->type == kind && t->state == TENANT_OCCUPIED &&
                floor_to_zone(t->floor) == zone && k++ == pick) {
                int f = floor_to_index(t->floor);
                if (!t->retail_open) return NULL;   /* closed pick = fail */
                if (f < 0 || f >= TOWER_FLOOR_COUNT || !reach[f]) return NULL;
                return t;
            }
        }
        return NULL;
    }
    return NULL;
}

/* Assign a car (UseCarPerson 1198:06e7 + the gate counts 002f/00d9):
 * pick a UNIFORM RANDOM usable space — not the nearest — and count the
 * car against the category's quota (quota = 2 x usable spaces for BOTH
 * admitting categories; double-parking is real, the quota is the only
 * limiter). Returns the space's floor index and increments the counter,
 * or -1 (lot full / no chain / unreachable pick). Parking is the
 * original's elevator-bypass valve: drivers enter and leave at their
 * car's floor, splitting off the lobby/express crush.
 * PORT GUARD (divergence, deliberate): a space whose floor has no
 * public-transport reach rejects the car — the EXE would strand the
 * person; we treat it like a failed park instead. */
int people_parking_assign(Tower *tower, const uint8_t *reach, int suite,
                          int seed)
{
    int *cars = suite ? &tower->cars_suite : &tower->cars_office;
    if (tower->usable_spaces <= 0 ||
        *cars >= 2 * tower->usable_spaces) return -1;
    int n = tower->usable_spaces;
    int pick = ((seed % n) + n) % n, k = 0;
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_PARKING || !t->space_usable) continue;
        if (k++ != pick) continue;
        int f = floor_to_index(t->floor);
        if (f < 0 || f >= TOWER_FLOOR_COUNT || !reach[f]) return -1;
        (*cars)++;
        return f;
    }
    return -1;
}

/* The car leaves with its owner (InParkingCar 1198:031a releases the
 * gate count). Called on every path that retires a person for good. */
static void release_car(Tower *tower, Person *p)
{
    if (p->parked_cat == 1 && tower->cars_office > 0) tower->cars_office--;
    else if (p->parked_cat == 2 && tower->cars_suite > 0) tower->cars_suite--;
    p->parked_cat = 0;
}

/* METRO_VISITORS_PER_PHASE lives in people.h now — the metro's "Crowded
 * with passengers" comment (game.c) reads the same cap. */

/* Phase transitions drive trips:
 *   MORNING: office workers arrive       EVENING: they go home
 *   EVENING: hotel guests check in       DAWN:    they check out */
static void spawn_phase(PeopleSim *ps, Tower *tower, int frame, int tod,
                        int hour, const uint8_t *reach_public)
{
    if ((uint8_t)tod != ps->cur_phase) {
        ps->cur_phase = (uint8_t)tod;
        memset(ps->spawned, 0, sizeof(ps->spawned));
        memset(ps->dinner_sent, 0, sizeof(ps->dinner_sent));
        /* flip people already at their destination into the new phase */
        for (int i = 0; i < ps->people_high; i++) {
            Person *p = &ps->people[i];
            if (!p->home_tenant || p->state != PERSON_AT_DEST) continue;
            Tenant *t = tower_tenant(tower, p->home_tenant);
            if (!t) { release_car(tower, p);
                      /* abnormal exit voids a live VIP visit (1240:0198) */
                      if (i == vip_watch && !vip_result) vip_result = 2;
                      p->home_tenant = 0; p->state = PERSON_FREE; continue; }
            int is_office = t->type == ITEM_OFFICE;
            int is_hotel = t->type == ITEM_HOTEL_SINGLE ||
                           t->type == ITEM_HOTEL_TWIN ||
                           t->type == ITEM_HOTEL_SUITE;
            if (!p->going_home &&
                ((is_office && tod == TOD_EVENING) ||
                 (is_hotel && tod == TOD_DAWN))) {
                p->going_home = 1;
                p->dest_floor = GROUND_IDX;
                p->state = PERSON_PLANNING;
            }
        }
    }

    /* stagger inbound spawns: one person per tenant per 8 ticks */
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        /* Re-let movers (2026-07-11 vacancy referee): a VACANT office/
         * condo/shop dispatches its pool ONLY while the demand flag is
         * armed — the same first-let code path in the EXE. One mover;
         * their arrival banks the move-in and flips the unit occupied.
         * Windows: office weekday morning, condo morning (no dice gate
         * worth modeling at one mover), shop from 10AM. */
        if (t->state == TENANT_ABANDONED && t->demand_armed &&
            (t->type == ITEM_OFFICE || t->type == ITEM_CONDO)) {
            int window =
                (t->type == ITEM_OFFICE && tod == TOD_MORNING &&
                 tower->day % 3 != 2) ||
                (t->type == ITEM_CONDO && tod == TOD_MORNING) ||
                (t->type == ITEM_SHOP && hour >= 10 && hour < 17);
            if (!window || ps->spawned[i] >= 1) continue;
            if (!depart_roll(frame, i, 12)) continue;
            int vfx = floor_to_index(t->floor);
            if (vfx < 0 || vfx >= TOWER_FLOOR_COUNT) continue;
            if (spawn_person(ps, tower, t, GROUND_IDX, vfx, 0))
                ps->spawned[i]++;
            continue;
        }
        /* vacant-but-armed shops keep trading (their pool re-lets them) */
        if (t->state != TENANT_OCCUPIED &&
            !(t->type == ITEM_SHOP && t->state == TENANT_ABANDONED &&
              t->demand_armed)) continue;
        /* Hotel guests spawn only for rooms the 5PM demand pass armed —
         * the EXE's +0x14 booking gate (UniPeple 1220:3032). This is what
         * keeps dirty and infested rooms guest-free: they can never arm. */
        /* Condo residents coming home (UniPeple 1220:3b0c, weekdays):
         * the kid (member 2) returns 13:00-17:00, an adult (member 0)
         * in the evening; they stay home overnight and ride down in
         * the morning. (Member 1, the Homebody, keeps her existing
         * shopping-trip behavior rather than a standing entity.) */
        int condo_in = t->type == ITEM_CONDO && !game_is_weekend(tower) &&
                       (tod == TOD_AFTERNOON || tod == TOD_EVENING);
        int inbound = (t->type == ITEM_OFFICE && tod == TOD_MORNING &&
                       !game_is_weekend(tower)) ||
                      (item_is_hotel_room(t->type) && tod == TOD_EVENING &&
                       t->demand_armed) ||
                      condo_in;

        /* Retail walk-ins: the venue's own street pool, gated by today's
         * quota (Restaurant.c: quota gate 10b3 counts down at dispatch).
         * Windows and pacing per the 2026-07-11 referee: FF/shops trickle
         * 10AM-5PM and rush 5-9PM; restaurants 5-9PM, then the stragglers
         * pour in until 11PM. */
        int walkin = 0;
        /* A vacant, armed shop's pool flows exactly like a live one's —
         * the first walk-in to arrive IS the re-let (shop-judge referee:
         * the own pool re-lets the same day the residual score arms). */
        int shop_relet = t->type == ITEM_SHOP &&
                         t->state == TENANT_ABANDONED && t->demand_armed;
        if ((is_retail_kind(t->type) && t->retail_open &&
             t->retail_quota > 0) || (shop_relet && t->retail_quota > 0)) {
            int roll = 0;
            if (t->type == ITEM_RESTAURANT)
                roll = (hour >= 17 && hour < 21) ? 12
                     : (hour >= 21 && hour < 23) ? 4 : 0;
            else
                roll = (hour >= 10 && hour < 17) ? 32
                     : (hour >= 17 && hour < 21) ? 8 : 0;
            if (roll && depart_roll(frame, i, roll)) walkin = 1;
        }

        /* show venues draw a quota-sized crowd per showing (VenueT summons
         * 56-person pools; the daily quotas do the real gating): cinemas
         * fill the matinee through the afternoon and the evening show after
         * five; the party hall summons its 50 guests in the evening */
        int show_cap = 0;
        if (t->type == ITEM_CINEMA)
            show_cap = (tod == TOD_AFTERNOON) ? t->quota_matinee
                     : (tod == TOD_EVENING)   ? t->quota_evening : 0;
        else if (t->type == ITEM_PARTY_HALL)
            show_cap = (tod == TOD_EVENING) ? t->quota_evening : 0;
        int patron = show_cap > 0;
        /* housekeepers ride the service net to dirty rooms each dawn */
        int staff = t->type == ITEM_HOUSEKEEPING &&
                    (tod == TOD_DAWN || tod == TOD_MORNING);
        if (!inbound && !patron && !staff && !walkin) continue;
        if (!walkin) {
            int cap = show_cap > 0 ? show_cap
                    : condo_in    ? 1            /* one resident per phase */
                                  : tenant_commuters(t);
            if (ps->spawned[i] >= cap) continue;
            if (!depart_roll(frame, i, 8)) continue;  /* irregular trickle */
        }
        int fidx = floor_to_index(t->floor);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;

        /* one kid / one adult per condo — skip if they're already home */
        if (condo_in) {
            uint8_t want = (tod == TOD_AFTERNOON) ? 2 : 0;
            int dup = 0;
            for (int k = 0; k < ps->people_high; k++)
                if (ps->people[k].home_tenant == t->id &&
                    ps->people[k].member == want) { dup = 1; break; }
            if (dup) continue;
        }

        if (staff) {
            int dirty = find_dirty_room_floor(tower);
            if (dirty < 0) continue;
            int sp = spawn_person(ps, tower, t, fidx, dirty, 0);
            if (sp) {
                Person *np = &ps->people[sp - 1];
                np->service = 1;             /* stairs-only + service cars */
                np->stay = 4;
                ps->spawned[i]++;
            }
            continue;
        }
        /* Cars (UseCarPerson 1198:06e7, byte-verified 2026-07-11): at
         * star>=3, REAL suite guests and office worker #2 of offices
         * where (floor + person_id) % 4 == 1 drive in, entering at
         * their parked car's floor. A suite guest who can't park
         * CANCELS the visit (the EXE voids a pending VIP visit the
         * same way) — with no parking, suites host only their carless
         * first guest. PROXY: person ids = spawn order. The old
         * "alternate commuters drive" rule was the port's invention. */
        int entry = GROUND_IDX, cat = 0;
        if (inbound && tower->star_rating >= 3) {
            if (t->type == ITEM_HOTEL_SUITE && ps->spawned[i] >= 1) {
                int park = people_parking_assign(tower, reach_public, 1, frame + i);
                if (park < 0) continue;       /* visit canceled */
                entry = park; cat = 2;
            } else if (t->type == ITEM_OFFICE && ps->spawned[i] == 2 &&
                       ((t->floor + i) & 3) == 1) {
                int park = people_parking_assign(tower, reach_public, 0, frame + i);
                if (park >= 0) { entry = park; cat = 1; }
                /* failed park = the worker walks in via the lobby */
            }
        }
        int sp = spawn_person(ps, tower, t, entry, fidx, 0);
        if (!sp && cat) {   /* people table full — put the car back */
            if (cat == 1) tower->cars_office--; else tower->cars_suite--;
        }
        if (sp) {
            ps->people[sp - 1].entry_floor = (uint8_t)entry;
            ps->people[sp - 1].parked_cat = (uint8_t)cat;
            if (condo_in)   /* the classifier's fixed family slots */
                ps->people[sp - 1].member = (tod == TOD_AFTERNOON) ? 2 : 0;
            if (patron)
                ps->people[sp - 1].stay = (uint8_t)(6 + (i * 5) % 18);
            if (walkin) {
                ps->people[sp - 1].stay = (uint8_t)(8 + (i * 5) % 12);
                /* The EXE counts walk-ins at DISPATCH (quota gate
                 * 11a8:1148), not at the door — an evening walk-in
                 * still riding at close still counts toward the day's
                 * demand. (No-route trips roll back in the EXE; the
                 * port's rare late failures overcount by a hair.) */
                t->retail_quota--;         /* the dispatch gate (+6) */
                if (t->customers_today < 0xFFFF) t->customers_today++;
                if (t->walkins_today < 0xFF) t->walkins_today++;
            } else {
                ps->spawned[i]++;
            }
        }
    }

    /* --- External retail customers (the four decoded flows). Each one
     * spawns a real person whose trip rides the real elevators; their
     * arrival grades the venue's service score. They leave through the
     * floor they came from (entry_floor). --- */
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->state != TENANT_OCCUPIED) continue;
        int fidx = floor_to_index(t->floor);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;

        Tenant *v = NULL;
        int seed = frame * 31 + i;

        if (t->type == ITEM_OFFICE) {
            /* Office lunch rush (UniPeple 1220:2288): workers 1..5 of
             * each office hit a FASTFOOD in their zone, noon-5PM,
             * weekdays only — offices never patronize restaurants. */
            if (ps->sched_day || hour < 12 || hour >= 17) continue;
            if (ps->spawned[i] >= 5 || !depart_roll(frame, i, 12)) continue;
            v = pick_retail(tower, ITEM_FAST_FOOD, t->floor, seed,
                            reach_public);
            if (v) ps->spawned[i]++;
        } else if (item_is_hotel_room(t->type)) {
            /* Hotel dinners (1220:382c): guests of EVEN tenant slots go
             * out for a RESTAURANT dinner, 5-9PM, once per stay. */
            if (!t->hosted || (i & 1) || hour < 17 || hour >= 21) continue;
            if (ps->dinner_sent[i >> 3] & (1 << (i & 7))) continue;
            if (!depart_roll(frame, i, 6)) continue;
            v = pick_retail(tower, ITEM_RESTAURANT, t->floor, seed,
                            reach_public);
            if (v) ps->dinner_sent[i >> 3] |= (uint8_t)(1 << (i & 7));
        } else if (t->type == ITEM_CONDO) {
            /* Condo excursions (1220:3a1a): weekdays a shopping trip
             * after 10AM; weekends every 4th condo dines out 5-9PM and
             * the rest grab fast food on the daytime schedule. */
            if (ps->spawned[i] >= 2) continue;
            if (!ps->sched_day) {
                if (hour < 10 || hour >= 21 || !depart_roll(frame, i, 24))
                    continue;
                v = pick_retail(tower, ITEM_SHOP, t->floor, seed,
                                reach_public);
            } else if ((i & 3) == 0) {
                if (hour < 17 || hour >= 21 || !depart_roll(frame, i, 6))
                    continue;
                v = pick_retail(tower, ITEM_RESTAURANT, t->floor, seed,
                                reach_public);
            } else {
                if (hour < 10 || hour >= 17 || !depart_roll(frame, i, 24))
                    continue;
                v = pick_retail(tower, ITEM_FAST_FOOD, t->floor, seed,
                                reach_public);
            }
            if (v) ps->spawned[i]++;
        } else if (t->type == ITEM_CATHEDRAL) {
            /* Weekend congregation (UniPeple 1220:5edd): period-0
             * mornings only, rand-gated from 8:00 — 40 guests ride all
             * the way to floor 100 and head home before lunch. */
            if (!ps->sched_day || hour < 8 || hour >= 11) continue;
            if (ps->spawned[i] >= 40) continue;
            if (!depart_roll(frame, i, 3)) continue;
            fidx = floor_to_index(0);       /* worshippers come off the street */
            v = t;
            ps->spawned[i]++;
        } else if (t->type == ITEM_METRO) {
            /* Metro visitors (1220:51dc): outside traffic, 10AM-5PM, a
             * random retail KIND in the GROUND zone. Riders enter and
             * leave on the TOP station floor — the port anchors the
             * 3-piece station at its bottom platform, so top = +2
             * (MakeMetroStation 11f8:2181, byte-verified 2026-07-11). */
            if (hour < 10 || hour >= 17) continue;
            if (ps->spawned[i] >= METRO_VISITORS_PER_PHASE) continue;
            if (!depart_roll(frame, i, 3)) continue;  /* ~the EXE's 300/day */
            fidx = floor_to_index(t->floor + 2);
            if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT ||
                !reach_public[fidx]) continue;
            static const ItemType KINDS[3] =
                { ITEM_RESTAURANT, ITEM_FAST_FOOD, ITEM_SHOP };
            v = pick_retail(tower, KINDS[(seed >> 4) % 3], 0, seed,
                            reach_public);
            if (v) ps->spawned[i]++;
        } else {
            continue;
        }
        if (!v) continue;                  /* failed attempt — no re-roll */
        int vf = floor_to_index(v->floor);
        if (vf < 0 || vf >= TOWER_FLOOR_COUNT) continue;
        /* home = the venue (anchors the visit + arrival grading); they
         * enter and leave through their own floor. */
        int sp = spawn_person(ps, tower, v, fidx, vf, 0);
        if (sp) {
            Person *np = &ps->people[sp - 1];
            np->stay = (uint8_t)(8 + (i * 3) % 10);   /* min-stay ~60fr */
            np->entry_floor = (uint8_t)fidx;
        }
    }
}

/* Send one at-desk office worker (member >= 2 — the sick-roll pool,
 * office arm 1220:27a1) from an office on `office_floor` to the medical
 * center: errand 5 = traveling, 6 = at the clinic, 7 = returning. */
void people_medical_dispatch(PeopleSim *ps, Tower *tower,
                             int office_floor, int center_floor)
{
    int cfx = floor_to_index(center_floor);
    if (cfx < 0 || cfx >= TOWER_FLOOR_COUNT) return;
    for (int i = 0; i < ps->people_high; i++) {
        Person *p = &ps->people[i];
        if (!p->home_tenant || p->state != PERSON_AT_DEST) continue;
        if (p->member < 2 || p->errand || p->going_home || p->stay) continue;
        Tenant *t = tower_tenant(tower, p->home_tenant);
        if (!t || t->type != ITEM_OFFICE || t->floor != office_floor) continue;
        if (p->cur_floor != (uint8_t)floor_to_index(t->floor)) continue;
        p->errand = 5;
        p->dest_floor = (uint8_t)cfx;
        p->state = PERSON_PLANNING;
        return;
    }
}

/* ---------- main tick ---------- */

void people_update(PeopleSim *ps, Tower *tower, int frame, int tod, int hour,
                   const uint8_t *reach_public, const uint8_t *reach_service)
{
    (void)reach_service;

    spawn_phase(ps, tower, frame, tod, hour, reach_public);

    for (int i = 0; i < ps->shaft_count; i++) {
        ElevatorShaft *s = &ps->shafts[i];
        if (!s->active) continue;
        /* capacity tracks the live tuning table (clamped to slot count) */
        int cap = (s->type == ITEM_ELEVATOR_EXPRESS) ? TUNING.capacity_express
                                                     : TUNING.capacity_standard;
        if (cap > CAR_SLOTS) cap = CAR_SLOTS;
        if (cap < 1) cap = 1;
        s->capacity = (uint8_t)cap;
        /* ShowElevator's re-call pass: a queued floor with no owning car
         * gets re-called (covers calls orphaned by car-count changes). */
        for (int f = s->lo; f <= s->hi; f++) {
            if (!s->serviced[f]) continue;
            if (queue_len(s, f, 1) && !s->up_call_car[f])
                call_elevator(ps, s, f, 1);
            if (queue_len(s, f, 0) && !s->down_call_car[f])
                call_elevator(ps, s, f, 0);
        }
        for (int ci = 0; ci < s->num_cars; ci++)
            car_tick(ps, tower, s, ci, frame);
    }

    int queued = 0, riding = 0, walking = 0, alive = 0;
    for (int i = 0; i < ps->people_high; i++) {
        Person *p = &ps->people[i];
        if (!p->home_tenant) continue;
        alive++;
        switch (p->state) {
        case PERSON_PLANNING:
            /* retry pacing so a full queue doesn't get hammered every tick */
            if ((frame + i) % 4 == 0)
                plan_person(ps, tower, p, i, frame);
            break;
        case PERSON_WALKING:
            if (p->walk_timer) { p->walk_timer--; break; }
            p->cur_floor = p->leg_floor;
            p->walk_stair = 0;
            p->state = PERSON_PLANNING;
            plan_person(ps, tower, p, i, frame);
            break;
        case PERSON_QUEUED:
            /* 300-frame watchdog (1220:1637 -> 1210:1b41 -> 1220:1aed):
             * a person stuck in a queue that long gives up — banks the
             * full wait, storms off, and rests where they stand. */
            if (frame - p->wait_start >= 300) {
                queue_remove(&ps->shafts[p->shaft], p->cur_floor, p->dir, i);
                bank_wait(p, frame);
                deliver_stress(ps, tower, p);
                ps->trips_failed++;
                if (p->errand == 1 || p->errand == 3) p->errand = 4;
                p->state = PERSON_AT_DEST;
                break;
            }
            queued++; break;
        case PERSON_RIDING:  riding++; break;
        case PERSON_AT_DEST:
            /* Office sales errand (UniPeple statuses 0/0x40/0x21/0x61):
             * members 0 and 1 of every occupied office make a midday
             * round-trip to the ground lobby. Member 0 rolls from the
             * morning (deterministic after noon); member 1 goes in the
             * early afternoon; both return during 13:00-17:00. Anyone
             * still at the lobby at 17:00 just heads home from there
             * (the evening phase flip already handles that). */
            if (!p->stay && !p->going_home && p->member <= 1 &&
                hour >= 7 && hour < 17) {
                Tenant *ht = tower_tenant(tower, p->home_tenant);
                if (ht && ht->type == ITEM_OFFICE && (frame + i) % 16 == 0) {
                    /* the EXE simulates each person every 16 frames (1/16
                     * LOD, 1220:0daf) — pace the dice the same way */
                    int hf = floor_to_index(ht->floor);
                    if (p->errand == 0 && p->cur_floor == (uint8_t)hf) {
                        int go = (p->member == 0)
                            ? (hour >= 12 || depart_roll(frame, i + 77, 12))
                            : (hour >= 13 && depart_roll(frame, i + 77, 12));
                        if (go) {
                            p->errand = 1;
                            p->dest_floor = (uint8_t)GROUND_IDX;
                            p->state = PERSON_PLANNING;
                            break;
                        }
                    } else if (p->errand == 2 && hour >= 13 && hf >= 0 &&
                               depart_roll(frame, i + 191, 12)) {
                        p->errand = 3;
                        p->dest_floor = (uint8_t)hf;
                        p->state = PERSON_PLANNING;
                        break;
                    }
                }
            }
            /* Clinic visit over: back to the desk (return status 0x63,
             * leave-check 1170:0414; return by 17:00). */
            if (p->errand == 6 && (frame + i) % 16 == 0 &&
                (hour >= 16 || depart_roll(frame, i + 55, 12))) {
                Tenant *mh = tower_tenant(tower, p->home_tenant);
                int mf = mh ? floor_to_index(mh->floor) : -1;
                if (mf >= 0) {
                    p->errand = 7;
                    p->dest_floor = (uint8_t)mf;
                    p->state = PERSON_PLANNING;
                    break;
                }
            }
            /* Condo bedtime/wake (UniPeple 3b97/41de, wake 3d14): from
             * 21:00 the kid turns in on his next visit, adults roll
             * 1-in-12 until 1:00 then deterministically; everyone wakes
             * from 6:00. errand 8 = asleep (condo residents don't use
             * the office errand codes). The render pass darkens a unit
             * when no resident is awake — the staggered evening look. */
            if ((frame + i) % 16 == 0 && !p->stay && !p->going_home &&
                (p->errand == 0 || p->errand == 8)) {
                Tenant *cb = tower_tenant(tower, p->home_tenant);
                if (cb && cb->type == ITEM_CONDO) {
                    if (p->errand == 8) {
                        if (hour >= 6 && hour < 21 &&
                            (hour >= 7 || depart_roll(frame, i + 11, 12)))
                            p->errand = 0;
                    } else if (hour >= 21 || hour == 0) {
                        if (p->member == 2 || depart_roll(frame, i + 11, 12))
                            p->errand = 8;
                    } else if (hour >= 1 && hour < 6) {
                        p->errand = 8;    /* 1:00 = deterministic bedtime */
                    }
                }
            }
            /* Condo weekday commute out (UniPeple 1220:3e10): residents
             * ride down through the morning and leave the tower via the
             * lobby ("Lobby to leave", status 0x40). They return via the
             * afternoon/evening spawns. */
            if (!p->stay && !p->going_home && p->errand == 0 &&
                hour >= 7 && hour < 12 &&
                (frame + i) % 16 == 0 && !ps->sched_day &&
                depart_roll(frame, i + 33, 12)) {
                Tenant *ch = tower_tenant(tower, p->home_tenant);
                if (ch && ch->type == ITEM_CONDO &&
                    p->cur_floor == (uint8_t)floor_to_index(ch->floor)) {
                    p->going_home = 1;
                    p->dest_floor = (uint8_t)GROUND_IDX;
                    p->state = PERSON_PLANNING;
                    break;
                }
            }
            /* patrons/staff: stay a while, then head back (staff return
             * to their unit, patrons leave via the ground) */
            if (p->stay && (frame + i) % 8 == 0 && --p->stay == 0) {
                Tenant *ht = tower_tenant(tower, p->home_tenant);
                int hf = ht ? floor_to_index(ht->floor) : -1;
                /* a retail patron heading out (OutRestPeple) */
                if (ht && is_retail_kind(ht->type))
                    game_retail_customer_out(ht);
                p->going_home = 1;
                p->dest_floor = (p->service && hf >= 0) ? (uint8_t)hf
                                                        : p->entry_floor;
                p->state = PERSON_PLANNING;
            }
            break;
        default: break;
        }
        if (p->state == PERSON_WALKING) walking++;
    }
    ps->population_now = alive;
    ps->queued_now = queued;
    ps->riding_now = riding;
    ps->walking_now = walking;
}
