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
 *     accel curve; schedules (per-day car counts, both-direction
 *     pickup) are not modeled yet
 *   - people exist only as commuters (office in/out, hotel in/out);
 *     restaurant/shop patrons and staff trips come later
 */
#include <string.h>
#include <stdio.h>

#include "people.h"
#include "game.h"

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
        .capacity_standard = 42,
        .capacity_service = 21,
        .judge_moderate = 150,
        .judge_stressed = 200,
        .star_pop = { 300, 1000, 5000, 10000 },
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

static int shaft_serves(const ElevatorShaft *s, int fidx)
{
    if (fidx < s->lo || fidx > s->hi) return 0;
    if (s->type != ITEM_ELEVATOR_EXPRESS) return 1;
    int wf = index_to_floor(fidx);
    return wf <= 0 || (wf % 15) == 0;    /* basements, ground, sky lobbies */
}

/* Rebuild gap map + shaft list. Cars and queues reset only when the
 * transport layout actually changed (the EXE rebuild pipeline also resets
 * ElvPeple state on stair/elevator placement). */
void people_rebuild_transport(PeopleSim *ps, Tower *tower)
{
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
            s->capacity = (ty == ITEM_ELEVATOR_SHAFT) ? 42 : 21;
            s->num_cars = 1;          /* TODO: car count upgrades */
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
            if (s->type == ITEM_ELEVATOR_SHAFT)
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

/* ---------- calls + car selection (ElevatorsT 0a4c/0dfc) ---------- */

/* SelectElevator, condensed to its three categories: a car already moving
 * the right way beats an idle car beats a car that must turn around. */
static int select_car(ElevatorShaft *s, int floor, int up)
{
    int best = -1, best_cost = 0x7fff;
    for (int ci = 0; ci < s->num_cars; ci++) {
        ElevatorCar *c = &s->car[ci];
        if (!c->active) continue;
        int d = floor - c->floor;
        int toward = up ? d : -d;
        int busy = c->assigned_calls || c->distinct_dests;
        int cost;
        if (!busy)
            cost = (d < 0 ? -d : d);                       /* idle: distance */
        else if ((int)c->dir == up && toward >= 0)
            cost = toward;                                  /* approaching */
        else
            cost = (d < 0 ? -d : d) + (s->hi - s->lo) + 8;  /* must reverse */
        if (cost < best_cost) { best_cost = cost; best = ci; }
    }
    return best;
}

static void call_elevator(ElevatorShaft *s, int floor, int up)
{
    uint8_t *slot = up ? &s->up_call_car[floor] : &s->down_call_car[floor];
    if (*slot) return;                       /* already assigned */
    int ci = select_car(s, floor, up);
    if (ci < 0) return;
    *slot = (uint8_t)(ci + 1);
    s->car[ci].assigned_calls++;
}

/* ---------- queue ops (TripT 11c2/1332) ---------- */

static int join_queue(ElevatorShaft *s, int floor, int up, int person_idx)
{
    ElevatorStop *st = &s->stop[floor];
    uint8_t *count = up ? &st->up_count : &st->down_count;
    uint8_t *head  = up ? &st->up_head  : &st->down_head;
    uint16_t *ring = up ? st->up_ring   : st->down_ring;
    if (*count >= QUEUE_CAP) return 0;
    ring[(*head + *count) % QUEUE_CAP] = (uint16_t)(person_idx + 1);
    if (*count == 0) call_elevator(s, floor, up);  /* the call button */
    (*count)++;
    return 1;
}

int people_join_queue(PeopleSim *ps, int shaft, int floor, int up,
                      int person_idx)
{
    return join_queue(&ps->shafts[shaft], floor, up, person_idx);
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
 * Thresholds 150/200 are the probable JudgeTenant pair from the tuning
 * resource (0xDD8A/0xDD8E) — consumers not yet verified in the decomp. */
static void deliver_stress(PeopleSim *ps, Tower *tower, Person *p)
{
    Tenant *t = tower_tenant(tower, p->home_tenant);
    if (t) {
        if (p->wait_accum >= TUNING.judge_stressed)      t->stress += 15;
        else if (p->wait_accum >= TUNING.judge_moderate) t->stress += 5;
        if (t->stress > 100) t->stress = 100;
    }
    ps->wait_total += p->wait_accum;
    ps->wait_samples++;
    p->wait_accum = 0;
}

/* ---------- trip planning (TryStartTrip port) ---------- */

static void trip_arrived(PeopleSim *ps, Tower *tower, Person *p, int frame);

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
        p->walk_timer = (st->type == ITEM_STAIRS) ? WALK_TICKS_STAIR
                                                  : WALK_TICKS_ESC;
        break;
    }
    case ROUTE_ELEVATOR: {
        ElevatorShaft *s = &ps->shafts[r.shaft];
        int up = r.ride_to > p->cur_floor;
        if (!join_queue(s, p->cur_floor, up, pi)) {
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
        p->state = PERSON_AT_DEST;          /* gives up where they stand */
        break;
    }
}

static void trip_arrived(PeopleSim *ps, Tower *tower, Person *p, int frame)
{
    (void)frame;
    ps->trips_done++;
    if (p->going_home && p->cur_floor == GROUND_IDX) {
        deliver_stress(ps, tower, p);
        p->home_tenant = 0;                 /* left the tower */
        p->state = PERSON_FREE;
        return;
    }
    deliver_stress(ps, tower, p);
    /* Hotel guests checking in mark the room as hosted (housekeeping loop) */
    Tenant *t = tower_tenant(tower, p->home_tenant);
    if (t && !p->going_home &&
        (t->type == ITEM_HOTEL_SINGLE || t->type == ITEM_HOTEL_TWIN ||
         t->type == ITEM_HOTEL_SUITE))
        t->hosted = 1;
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
        if (c->assigned_calls || c->distinct_dests) return 0;
        if (queue_len(s, c->floor, !up) == 0) return 0;
        c->dir = (uint8_t)!up;          /* adopt the waiting direction */
        up = c->dir;
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
 * the middle of a long haul; express/service get a medium band instead of
 * the standard's top gear. dist-from-start needs the run's departure
 * floor (last_floor in the EXE; leg_start here). */
static int car_move_ticks(const ElevatorShaft *s, const ElevatorCar *c)
{
    int dt = c->target - c->floor;   if (dt < 0) dt = -dt;
    int ds = c->floor - c->leg_start; if (ds < 0) ds = -ds;
    if (s->type == ITEM_ELEVATOR_SHAFT) {
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

static void car_depart_or_idle(PeopleSim *ps, ElevatorShaft *s,
                               ElevatorCar *c, int ci)
{
    clear_call(s, c, ci, c->floor);
    /* people still queued here with no assigned car press the button again */
    if (queue_len(s, c->floor, 1) && !s->up_call_car[c->floor])
        call_elevator(s, c->floor, 1);
    if (queue_len(s, c->floor, 0) && !s->down_call_car[c->floor])
        call_elevator(s, c->floor, 0);

    int tgt = find_target_floor(s, c, ci);
    if (tgt < 0) { c->target = c->floor; return; }   /* idle in place */
    c->target = (uint8_t)tgt;
    c->dir = (uint8_t)(tgt > c->floor);
    c->leg_start = c->floor;
    car_start_step(s, c);
    (void)ps;
}

static void car_tick(PeopleSim *ps, Tower *tower, ElevatorShaft *s,
                     int ci, int frame)
{
    ElevatorCar *c = &s->car[ci];
    if (!c->active) return;

    if (c->door_timer) {
        if (c->door_timer == DOOR_OPEN_TICKS)
            unboard_at_floor(ps, tower, s, c, frame);
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
    int tgt = find_target_floor(s, c, ci);
    if (tgt >= 0) {
        c->target = (uint8_t)tgt;
        c->dir = (uint8_t)(tgt > c->floor);
        c->leg_start = c->floor;
        car_start_step(s, c);
    }
}

/* ---------- spawning (UniPeple-lite: commuters only) ---------- */

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
    p->state = PERSON_PLANNING;
    p->cur_floor = (uint8_t)from;
    p->dest_floor = (uint8_t)to;
    p->going_home = (uint8_t)going_home;
    p->x = t->x;
    (void)tower;
    return 1;
}

static int tenant_commuters(const Tenant *t)
{
    int n = TENANT_POPULATION[t->type];
    return n > 8 ? 8 : n;       /* cap per tenant to keep entity counts sane */
}

/* Phase transitions drive trips:
 *   MORNING: office workers arrive       EVENING: they go home
 *   EVENING: hotel guests check in       DAWN:    they check out */
static void spawn_phase(PeopleSim *ps, Tower *tower, int frame, int tod)
{
    if ((uint8_t)tod != ps->cur_phase) {
        ps->cur_phase = (uint8_t)tod;
        memset(ps->spawned, 0, sizeof(ps->spawned));
        /* flip people already at their destination into the new phase */
        for (int i = 0; i < ps->people_high; i++) {
            Person *p = &ps->people[i];
            if (!p->home_tenant || p->state != PERSON_AT_DEST) continue;
            Tenant *t = tower_tenant(tower, p->home_tenant);
            if (!t) { p->home_tenant = 0; p->state = PERSON_FREE; continue; }
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
        if (t->state != TENANT_OCCUPIED) continue;
        int inbound = (t->type == ITEM_OFFICE && tod == TOD_MORNING) ||
                      ((t->type == ITEM_HOTEL_SINGLE ||
                        t->type == ITEM_HOTEL_TWIN ||
                        t->type == ITEM_HOTEL_SUITE) && tod == TOD_EVENING &&
                       !t->dirty);
        if (!inbound) continue;
        if (ps->spawned[i] >= tenant_commuters(t)) continue;
        if ((frame + i) % 8) continue;
        int fidx = floor_to_index(t->floor);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
        if (spawn_person(ps, tower, t, GROUND_IDX, fidx, 0))
            ps->spawned[i]++;
    }
}

/* ---------- main tick ---------- */

void people_update(PeopleSim *ps, Tower *tower, int frame, int tod,
                   const uint8_t *reach_public, const uint8_t *reach_service)
{
    (void)reach_public; (void)reach_service;

    spawn_phase(ps, tower, frame, tod);

    for (int i = 0; i < ps->shaft_count; i++) {
        ElevatorShaft *s = &ps->shafts[i];
        if (!s->active) continue;
        /* capacity tracks the live tuning table (clamped to slot count) */
        int cap = (s->type == ITEM_ELEVATOR_SHAFT) ? TUNING.capacity_standard
                                                   : TUNING.capacity_service;
        if (cap > CAR_SLOTS) cap = CAR_SLOTS;
        if (cap < 1) cap = 1;
        s->capacity = (uint8_t)cap;
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
            p->state = PERSON_PLANNING;
            plan_person(ps, tower, p, i, frame);
            break;
        case PERSON_QUEUED:  queued++; break;
        case PERSON_RIDING:  riding++; break;
        default: break;
        }
        if (p->state == PERSON_WALKING) walking++;
    }
    ps->population_now = alive;
    ps->queued_now = queued;
    ps->riding_now = riding;
    ps->walking_now = walking;
}
