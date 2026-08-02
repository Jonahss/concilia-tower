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
 *   - cars use a flat ticks-per-floor speed instead of the 4-level
 *     accel curve
 * (The chain/slot transfer tables are the EXE's own since 2026-08-02 —
 * see the TransferT block below; routing consumes them one transfer
 * deep, exactly as deep as the tables themselves go.)
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

/* Nonzero while the Simulate edit mode's settle pre-sim is running
 * (ElvEditT SnapshotAndSettleGroup 10f0:0318). The EXE nulls the pre-sim's
 * side effects with [0xB3AE] gates in the trip code — wait-stress banking
 * is rerouted/skipped while the flag is set (TripT #48, 10a8:0293;
 * referee_elv_simulate_editmode_2026-08-01 §6: "zero effect on people,
 * money, stress, time"). The port gates the same choke points on this. */
static int     elv_settling;

static void fmt_floor_name(int fidx, char *out, size_t n)
{
    int f = index_to_floor(fidx);
    if (f < 0)       snprintf(out, n, "B%d", -f);
    else if (f == 0) snprintf(out, n, "Lobby");
    else             snprintf(out, n, "%d", f);
}

static void noroute_report(const Person *p)
{
    if (elv_settling) return;   /* settle pre-sim leaves no trace (§6) */
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

/* ---------- transfer chain/slot tables (TransferT seg_11b0, low half) ----
 *
 * The EXE's three precomputed routing structures (transfer-tables referee
 * 2026-08-02, byte-verified):
 *   WALK CHAINS     8 x 0x1e4 @DS:0xBFF1  (BuildWalkChains 11b0:06a4)
 *   ROUTING SLOTS   16 x 6    @DS:0xDB9C  (BuildRoutingSlots 11b0:049f)
 *   TRANSFER TABLES u32[120] per transport (BuildAllTransferTables 11b0:00f2)
 *
 * Transport ids: bits 0..23 = elevator shafts, bits 24..31 = chains.
 * A CHAIN is a walkable zone anchored at the lobby grid, acting as a
 * virtual transport with its own transfer table; a SLOT is a lobby-floor
 * transfer point holding the mask of transports that physically touch
 * that lobby.
 *
 * The EXE dual-encodes ttable[f]: floor NOT served -> MASK of co-located
 * transports that reach f directly; floor served -> slot index + 1. The
 * port splits the encoding into xfer_mask[] + slot_at[] per transport —
 * same content, no packing.
 *
 * These live in a STATIC struct, NOT in PeopleSim: game_save raw-dumps
 * GameSim (which embeds PeopleSim), so PeopleSim's layout is frozen. The
 * EXE re-derives slots+ttables on load anyway (FileT seg27:0a8e), so the
 * tables are rebuilt on demand whenever they fall behind the sim's
 * layout_stamp (which now folds in serviced flags and lobby spans). */

#define MAX_CHAINS 8
#define MAX_SLOTS  16
#define SHAFT_BIT(i) (1u << (i))
#define CHAIN_BIT(k) (1u << (24 + (k)))

typedef struct {            /* one walkable zone anchored at the lobby grid */
    uint8_t  active;
    uint8_t  top;           /* chain+1: up-reach floor index (byte truth —
                             * the old annotation had top/bottom swapped;
                             * erratum in the 2026-08-02 referee) */
    uint8_t  bottom;        /* chain+2: down-reach floor index */
    uint32_t xfer_mask[TOWER_FLOOR_COUNT];  /* floor not covered */
    uint8_t  slot_at[TOWER_FLOOR_COUNT];    /* floor covered: slot idx+1 */
} WalkChain;

typedef struct {            /* one lobby-floor transfer point */
    uint32_t mask;          /* +0: transports touching this lobby */
    int16_t  floor;         /* +4: floor index; -1 = unused (EXE 0xFF) */
} RoutingSlot;

typedef struct {            /* per-shaft transfer table (EXE group+0xC2) */
    uint32_t xfer_mask[TOWER_FLOOR_COUNT];
    uint8_t  slot_at[TOWER_FLOOR_COUNT];
} ShaftTTable;

static struct {
    uint32_t stamp;         /* ps->layout_stamp the tables were built for */
    uint8_t  dirty;         /* forced invalidation (serviced toggles) */
    int      slot_count;
    WalkChain   chains[MAX_CHAINS];
    RoutingSlot slots[MAX_SLOTS];
    ShaftTTable shaft_tt[MAX_SHAFTS];
} XFER;

static int chain_covers(int k, int f)   /* ChainCovers 11b0:08f2 (090a-0920) */
{
    const WalkChain *c = &XFER.chains[k];
    return c->active && f >= c->bottom && f <= c->top;
}

/* WalkReach (11b0:0763): scan gaps outward from the anchor through the
 * per-gap walk map; stop at the first empty gap (0786/07c6); a gap
 * without the escalator bit sets a stairs flag (078f-0796/07cf-07d6),
 * and once set, reach is clamped to anchor+/-3 (07a1-07a8/07e1-07ea);
 * otherwise anchor+/-6 (07b1-07bd/07f2-07f9). Same 6/3 budget as
 * CanWalkPublic, applied per direction — a chain can be asymmetric. */
static int walk_reach(const uint8_t *gap, int anchor, int up)
{
    int reach = anchor, stairs_seen = 0;
    for (int i = 1; i <= TUNING.walk_floors_esc; i++) {
        int f = up ? anchor + i : anchor - i;
        int g = up ? f - 1 : f;             /* the gap crossed to reach f */
        if (f < 0 || f >= TOWER_FLOOR_COUNT) break;
        uint8_t bits = gap[g];
        if (!bits) break;                   /* first empty gap ends the zone */
        if (!(bits & 1)) stairs_seen = 1;   /* stairs-only gap */
        if (stairs_seen && i > TUNING.walk_floors_stair) break;
        reach = f;
    }
    return reach;
}

/* BuildWalkChains (11b0:06a4): anchors are the ground floor plus every
 * sky-lobby GRID floor 15/30/../90 (06bc anchors idx 10, then 0705-0754
 * step 0xf while <= 0x6d). The anchor is the FLOOR NUMBER — no check
 * that a sky lobby is actually built there. Emit {top,bottom} only if
 * bottom < top (06e0-06e3): no walkable gap adjacent to the anchor means
 * no chain. Cap 8 records (7 anchors possible, the cap never binds). */
static void build_walk_chains(const uint8_t *gap)
{
    memset(XFER.chains, 0, sizeof(XFER.chains));
    int n = 0;
    for (int a = 0; a <= 6 && n < MAX_CHAINS; a++) {
        int fidx = (a == 0) ? GROUND_IDX : floor_to_index(a * 15);
        if (fidx >= TOWER_FLOOR_COUNT) break;
        int bot = walk_reach(gap, fidx, 0);
        int top = walk_reach(gap, fidx, 1);
        if (bot >= top) continue;
        WalkChain *c = &XFER.chains[n++];
        c->active = 1;
        c->top    = (uint8_t)top;
        c->bottom = (uint8_t)bot;
    }
}

/* BuildRoutingSlots (11b0:049f): floor-ascending scan over LOBBY tenants.
 * Shaft footprint width for the lobby-overlap test is 6 cells for the
 * express, 4 for standard/service (Ghidra 275-280; transport-choice
 * referee L1). Same-floor mask-intersect MERGE (297-304) — disjoint
 * lobby clusters on one floor stay separate slots. Grand-lobby upper
 * stories are skipped (EXE floor idx 0xB/0xC — moot with the port's
 * single-story lobby tenants, guard kept for later). Service shafts DO
 * enter slot masks; they are excluded at ttable time. */
static void build_routing_slots(const PeopleSim *ps, const Tower *tower)
{
    memset(XFER.slots, 0, sizeof(XFER.slots));
    for (int j = 0; j < MAX_SLOTS; j++) XFER.slots[j].floor = -1;
    XFER.slot_count = 0;

    for (int f = 0; f < TOWER_FLOOR_COUNT; f++) {
        if (f == GROUND_IDX + 1 || f == GROUND_IDX + 2) continue;
        for (int ti = 0; ti < tower->tenant_count; ti++) {
            const Tenant *t = &tower->tenants[ti];
            if (t->type != ITEM_LOBBY || floor_to_index(t->floor) != f)
                continue;
            uint32_t mask = 0;
            for (int i = 0; i < ps->shaft_count; i++) {
                const ElevatorShaft *s = &ps->shafts[i];
                if (!s->active || !shaft_serves(s, f)) continue;
                int width = (s->type == ITEM_ELEVATOR_EXPRESS) ? 6 : 4;
                if (s->x < t->x + t->width && s->x + width > t->x)
                    mask |= SHAFT_BIT(i);
            }
            if (!mask) continue;
            int n = XFER.slot_count;
            if (n > 0 && XFER.slots[n - 1].floor == (int16_t)f &&
                (XFER.slots[n - 1].mask & mask)) {
                XFER.slots[n - 1].mask |= mask;     /* same-floor merge */
                continue;
            }
            /* HARD CAP QUIRK (Ghidra 292-294): when the 17th slot would
             * be emitted the EXE RETURNS — skipping the remaining floors
             * AND the chain pass below, so in a >16-slot tower chains
             * silently stop participating in transfers. Reproduced
             * verbatim (flagged EXE bug candidate, not judged). */
            if (n == MAX_SLOTS) return;
            XFER.slots[n].mask  = mask;
            XFER.slots[n].floor = (int16_t)f;
            XFER.slot_count = n + 1;
        }
    }

    /* Chain pass (tail, Ghidra 311-324): each active chain joins every
     * slot on a floor it covers. */
    for (int k = 0; k < MAX_CHAINS; k++) {
        if (!XFER.chains[k].active) continue;
        for (int j = 0; j < XFER.slot_count; j++)
            if (chain_covers(k, XFER.slots[j].floor))
                XFER.slots[j].mask |= CHAIN_BIT(k);
    }
}

/* The alternatives reachable from a set of co-located transports: for
 * each transport in coloc, set its bit iff it reaches f DIRECTLY —
 * service shafts excluded (nobody transfers into a service car; 00f2
 * Ghidra 140-158). No transitive closure: the table is exactly one
 * transfer deep by construction. */
static uint32_t coloc_reach_mask(const PeopleSim *ps, uint32_t coloc, int f)
{
    uint32_t m = 0;
    for (int h = 0; h < ps->shaft_count; h++) {
        if (!(coloc & SHAFT_BIT(h))) continue;
        const ElevatorShaft *hs = &ps->shafts[h];
        if (!hs->active || hs->type == ITEM_ELEVATOR_SERVICE) continue;
        if (shaft_serves(hs, f)) m |= SHAFT_BIT(h);
    }
    for (int c = 0; c < MAX_CHAINS; c++)
        if ((coloc & CHAIN_BIT(c)) && chain_covers(c, f))
            m |= CHAIN_BIT(c);
    return m;
}

/* Slot index + 1 of the slot at floor f containing this transport, else
 * 0 (the served side of the dual encoding, 00f2 Ghidra 161-169/215-223). */
static uint8_t slot_index_at(uint32_t mybit, int f)
{
    for (int j = 0; j < XFER.slot_count; j++)
        if (XFER.slots[j].floor == (int16_t)f && (XFER.slots[j].mask & mybit))
            return (uint8_t)(j + 1);
    return 0;
}

/* BuildAllTransferTables (11b0:00f2): per transport, coloc = OR of the
 * masks of every slot containing it, minus its own bit (Ghidra 123-132 /
 * 181-190); then per floor, the dual encoding. Service groups are
 * excluded BOTH ways (140-149): their masks stay empty and nothing
 * transfers into them — service trips never transfer. */
static void build_transfer_tables(const PeopleSim *ps)
{
    memset(XFER.shaft_tt, 0, sizeof(XFER.shaft_tt));

    for (int i = 0; i < ps->shaft_count; i++) {
        const ElevatorShaft *s = &ps->shafts[i];
        ShaftTTable *tt = &XFER.shaft_tt[i];
        if (!s->active) continue;
        uint32_t coloc = 0;
        for (int j = 0; j < XFER.slot_count; j++)
            if (XFER.slots[j].mask & SHAFT_BIT(i)) coloc |= XFER.slots[j].mask;
        coloc &= ~SHAFT_BIT(i);
        for (int f = 0; f < TOWER_FLOOR_COUNT; f++) {
            if (shaft_serves(s, f))
                tt->slot_at[f] = slot_index_at(SHAFT_BIT(i), f);
            else if (s->type != ITEM_ELEVATOR_SERVICE)
                tt->xfer_mask[f] = coloc_reach_mask(ps, coloc, f);
        }
    }

    /* The chain half symmetrically links co-located elevators serving f
     * and other co-located chains covering f (196-213) — chain-to-chain
     * transfers are representable. */
    for (int k = 0; k < MAX_CHAINS; k++) {
        WalkChain *c = &XFER.chains[k];
        memset(c->xfer_mask, 0, sizeof(c->xfer_mask));
        memset(c->slot_at, 0, sizeof(c->slot_at));
        if (!c->active) continue;
        uint32_t coloc = 0;
        for (int j = 0; j < XFER.slot_count; j++)
            if (XFER.slots[j].mask & CHAIN_BIT(k)) coloc |= XFER.slots[j].mask;
        coloc &= ~CHAIN_BIT(k);
        for (int f = 0; f < TOWER_FLOOR_COUNT; f++) {
            if (chain_covers(k, f))
                c->slot_at[f] = slot_index_at(CHAIN_BIT(k), f);
            else
                c->xfer_mask[f] = coloc_reach_mask(ps, coloc, f);
        }
    }
}

/* Rebuild the static tables when they fall behind the sim. EXE pipeline
 * order preserved: chains (06a4) -> slots (049f, which zeroes ttables +
 * slots first) -> ttables (00f2). */
static void xfer_ensure(const PeopleSim *ps, const Tower *tower)
{
    if (!XFER.dirty && XFER.stamp == ps->layout_stamp) return;
    build_walk_chains(ps->gap_map);
    build_routing_slots(ps, tower);
    build_transfer_tables(ps);
    XFER.stamp = ps->layout_stamp;
    XFER.dirty = 0;
}

/* ResolveViaSlot (11b0:092f, byte-verified end to end): re-derive the
 * in-shaft ride target at BOARD time — the choice-time verdict may have
 * gone stale while the person queued. Returns the ride floor, or -1 =
 * boarding failure (layout changed: penalty [0xDD7E], re-plan). */
static int resolve_via_slot(const PeopleSim *ps, int shaft, int floor,
                            int dest, int up)
{
    const ElevatorShaft *s = &ps->shafts[shaft];
    if (shaft_serves(s, dest)) return dest;              /* 094b-0955 */
    uint32_t want = XFER.shaft_tt[shaft].xfer_mask[dest];
    if (!want) return -1;                                /* 0962-0971 */
    for (int j = 0; j < XFER.slot_count; j++) {
        const RoutingSlot *sl = &XFER.slots[j];
        if (!(sl->mask & SHAFT_BIT(shaft))) continue;    /* 0988 */
        if (sl->floor == (int16_t)floor) continue;       /* 099a-09a2 */
        if (!((sl->mask & ~SHAFT_BIT(shaft)) & want)) continue; /* 09b3-09df */
        if ((sl->floor > floor) != (up != 0)) continue;  /* direction gate
                                                          * 09e1-09fb */
        return sl->floor;                                /* 09fd-0a08 */
    }
    return -1;                                           /* 0a15 */
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
        int rise = t->height - 1; if (rise < 1) rise = 1;
        for (int g = f; g < f + rise; g++) {   /* tall: every spanned gap */
            if (g < 0 || g + 1 >= TOWER_FLOOR_COUNT) continue;
            gap[g] |= (t->type == ITEM_ESCALATOR) ? 1 : 2;   /* 0xCF10 bits */
        }
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
            /* group+2 from the EXE: 42 express, 21 standard/service
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

    /* Default all stops on, then carry dialog settings (car count,
     * serviced flags) across the rebuild — shafts matched by column+type
     * so extending a shaft doesn't wipe its configuration. Runs BEFORE
     * the stamp because the serviced flags are part of it now. */
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
            /* carry the cars themselves — extending a shaft must not
             * teleport its cars to the bottom (they keep their floor and
             * get re-dispatched from where they stand) */
            memcpy(ns->car, os->car, sizeof(ns->car));
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

    /* Layout stamp: FNV over gap map + shaft extents + per-floor serviced
     * flags + lobby tenant spans. Serviced flags and lobby geometry are
     * folded in because slot geometry and the ttables depend on both —
     * the EXE rebuilds slots+ttables on any stop toggle (ElevatorUI
     * seg21:0138) and on lobby placement (tenant.c finalizer seg64:0e8f,
     * type table includes 0x18). Note the port's stamp-change rebuild is
     * coarser than the EXE's 049f+00f2 (it also resets cars/queues); the
     * common toggle path avoids that via XFER.dirty in
     * people_set_serviced. */
    uint32_t h = 2166136261u;
    for (int i = 0; i < TOWER_FLOOR_COUNT; i++) { h ^= gap[i]; h *= 16777619u; }
    for (int i = 0; i < count; i++) {
        uint32_t v = (uint32_t)(fresh[i].type | fresh[i].lo << 8 |
                                fresh[i].hi << 16) ^ (uint32_t)fresh[i].x << 20;
        h ^= v; h *= 16777619u;
        for (int f = fresh[i].lo; f <= fresh[i].hi; f++) {
            h ^= fresh[i].serviced[f]; h *= 16777619u;
        }
    }
    for (int i = 0; i < tower->tenant_count; i++) {
        const Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_LOBBY) continue;
        uint32_t v = (uint32_t)floor_to_index(t->floor) |
                     (uint32_t)t->x << 8 | (uint32_t)t->width << 20;
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
            ElevatorCar *car = &s->car[c];
            if (!car->active) {
                /* genuinely new car (fresh shaft, or add-a-car) */
                car->floor = s->lo;
                car->dir = 1;
            } else {
                /* carried across a rebuild: hold position (clamped into
                 * the new extent) and let the dispatcher pick fresh work.
                 * Riders were already flipped back to PLANNING below, so
                 * the passenger manifest restarts empty either way. */
                if (car->floor < s->lo) car->floor = s->lo;
                if (car->floor > s->hi) car->floor = s->hi;
            }
            car->target = car->floor;
            car->door_timer = 0;
            car->move_timer = 0;
            car->hold_timer = 0;
            car->passengers = 0;
            car->distinct_dests = 0;
            car->assigned_calls = 0;
            memset(car->pax, 0, sizeof car->pax);
            memset(car->pax_dest, 0, sizeof car->pax_dest);
            memset(car->dest_count, 0, sizeof car->dest_count);
            car->active = 1;
            car->schedule_index = sched_mode_now(ps, s);
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
                          int service, int *score_out, int *hop_out)
{
    /* A unit is entered only at its landings and traverses its whole
     * rise as one leg (TripT: journey_a = from +/- span) — a tall
     * grand-lobby unit (rise 2/3) has no intermediate stops. */
    int best = -1, best_score = COST_NO_ROUTE, best_hop = from;
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_STAIRS && t->type != ITEM_ESCALATOR) continue;
        if (service && t->type != ITEM_STAIRS) continue;
        int rise = t->height - 1; if (rise < 1) rise = 1;
        int bot = floor_to_index(t->floor);
        int hop;
        if (up)      { if (bot != from) continue;        hop = from + rise; }
        else         { if (bot + rise != from) continue; hop = from - rise; }
        int xd = t->x - x; if (xd < 0) xd = -xd;
        int sc = 8 * xd + (t->type == ITEM_STAIRS ? COST_STAIR_BASE : 0);
        if (sc < best_score) { best_score = sc; best = i; best_hop = hop; }
    }
    *score_out = best_score;
    if (hop_out) *hop_out = best_hop;
    return best;
}

static int queue_len(const ElevatorShaft *s, int floor, int up)
{
    const ElevatorStop *st = &s->stop[floor];
    return up ? st->up_count : st->down_count;
}

/* Ground or sky lobby anywhere on this floor? (EXE FUN_10a0_1366 —
 * ShouldTimeout's "may the car dwell here" test) */
static int floor_is_lobby(const Tower *tower, int fidx)
{
    for (int i = 0; i < tower->tenant_count; i++) {
        const Tenant *t = &tower->tenants[i];
        if (t->type == ITEM_LOBBY && floor_to_index(t->floor) == fidx)
            return 1;
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
    xfer_ensure(ps, tower);      /* chains/slots/ttables current */

    /* 1. Walking, if the whole remaining climb is within budget */
    int walk_score = COST_NO_ROUTE, walk_stair = -1, walk_hop = from;
    if (can_walk(ps, from, to, service))
        walk_stair = find_stair_hop(tower, from, up, x, service,
                                    &walk_score, &walk_hop);
    if (walk_stair >= 0 && walk_score < COST_STAIR_BASE) {
        /* escalator within 80 cells wins before elevators are considered */
        r.kind = ROUTE_WALK; r.stair = walk_stair;
        r.hop_to = walk_hop;
        return r;
    }
    /* Staff take any matching stair hop unconditionally (11b0:1139-1151):
     * the EXE scans staff elevators only when NO stair matched. */
    if (service && walk_stair >= 0) {
        r.kind = ROUTE_WALK; r.stair = walk_stair;
        r.hop_to = walk_hop;
        return r;
    }

    /* 1b. Walk chains (FindTransport 0fbe-10e3, ScoreWalkChain 11b0:0805,
     * ChainTransferCheck 0ad4), consulted only when no direct hop early-
     * accepted (transport-choice referee H4: chains run BEFORE the shaft
     * loop). The score is binary: a chain covering both ends makes the
     * in-zone walk free (0843-085c — up to anchor-6..anchor+6 through
     * the lobby grid, beyond CanWalkPublic's own budget); a chain
     * covering `from` whose transfer table reaches `to` walks toward the
     * shared slot lobby. Either way the verdict executes as the single
     * stair hop toward the target (109a-10d4): an escalator hop < 640
     * returns outright without scoring elevators, a stairs hop becomes
     * the incumbent the shaft loop must beat. */
    if (!service && (walk_stair < 0 || walk_score >= COST_STAIR_BASE)) {
        int target = -1;
        for (int k = 0; k < MAX_CHAINS && target < 0; k++) {
            const WalkChain *chn = &XFER.chains[k];
            if (!chn->active || !chain_covers(k, from)) continue;
            if (chain_covers(k, to)) { target = to; break; }  /* free walk */
            uint32_t want = chn->xfer_mask[to];
            if (!want) continue;
            /* Redundancy gate (0805:0876-08c8): the slot where you stand
             * already holds EVERY transport that finishes the trip — the
             * walk would be pointless, fail the chain. */
            int sa = chn->slot_at[from];
            if (sa && (XFER.slots[sa - 1].mask & want) == want) continue;
            /* ChainTransferCheck (0ad4): first slot shared with a
             * finishing transport, away from `from`. */
            for (int j = 0; j < XFER.slot_count; j++) {
                const RoutingSlot *sl = &XFER.slots[j];
                if (!(sl->mask & CHAIN_BIT(k))) continue;
                if (sl->floor == (int16_t)from) continue;
                if (!((sl->mask & ~CHAIN_BIT(k)) & want)) continue;
                target = sl->floor;
                break;
            }
        }
        if (target >= 0 && target != from) {
            int cs, ch, hop_up = target > from;
            int cst = find_stair_hop(tower, from, hop_up, x, service, &cs, &ch);
            if (cst >= 0) {
                if (cs < COST_STAIR_BASE) {      /* escalator: outright win */
                    r.kind = ROUTE_WALK; r.stair = cst; r.hop_to = ch;
                    return r;
                }
                if (cs < walk_score) {           /* stairs: new incumbent */
                    walk_stair = cst; walk_score = cs; walk_hop = ch;
                }
            }
        }
    }

    /* 2. Elevators: direct, then one transfer at a lobby.
     * Staff network = SERVICE elevators only (ScoreElevator gate
     * 11b0:11f0-1201), and service groups have no transfer tables
     * (00f2) — staff trips never transfer. */
    int best_score = walk_score, best_shaft = -1, best_ride = -1;
    for (int i = 0; i < ps->shaft_count; i++) {
        ElevatorShaft *s = &ps->shafts[i];
        if (!s->active || !shaft_serves(s, from)) continue;
        if (service ? (s->type != ITEM_ELEVATOR_SERVICE)
                    : (s->type == ITEM_ELEVATOR_SERVICE)) continue;
        int xd = s->x - x; if (xd < 0) xd = -xd;
        if (shaft_serves(s, to)) {
            int full = queue_len(s, from, up) >= QUEUE_CAP;
            int sc;
            if (s->type == ITEM_ELEVATOR_EXPRESS)
                sc = queue_len(s, from, up) + COST_ELEV_BASE;
            else
                sc = 8 * xd + (full ? COST_ELEV_FULL : COST_ELEV_BASE);
            if (sc < best_score) {
                best_score = sc; best_shaft = i; best_ride = to;
            }
        } else if (!service) {
            /* One transfer via the routing slots (ScoreElevator transfer
             * branch 11b0:12d7-1410): ttable[to] must name a co-located
             * transport that reaches `to` DIRECTLY — the mask is one
             * transfer deep by construction, so a two-transfer
             * destination falls through to no-route. Chain bits in
             * ttable[to] are the ride-then-walk finishes: ride to the
             * chain-covered slot floor, walk the zone from there (the
             * referee's floor-17 example the old geometry re-scan could
             * not represent). Scored with the direct formulas on the
             * 3000/6000 base (13cd-1410), queue read toward the transfer
             * LOBBY, not the final destination (ElevTransferCheck 0aa7,
             * dir = slot.floor > from). */
            uint32_t want = XFER.shaft_tt[i].xfer_mask[to];
            if (!want) continue;
            /* Redundancy gate (12f4-135e): the slot right here already
             * holds every finishing transport — you could board the
             * finisher directly, no transfer offer via this shaft. */
            int sa = XFER.shaft_tt[i].slot_at[from];
            if (sa && (XFER.slots[sa - 1].mask & want) == want) continue;
            /* ElevTransferCheck (0a21): first slot containing this
             * shaft, away from `from`, sharing a finishing transport. */
            int L = -1;
            for (int j = 0; j < XFER.slot_count; j++) {
                const RoutingSlot *sl = &XFER.slots[j];
                if (!(sl->mask & SHAFT_BIT(i))) continue;
                if (sl->floor == (int16_t)from) continue;
                if (!((sl->mask & ~SHAFT_BIT(i)) & want)) continue;
                L = sl->floor;
                break;
            }
            if (L < 0) continue;
            int upL = L > from;
            int fullL = queue_len(s, from, upL) >= QUEUE_CAP;
            int sc;
            if (s->type == ITEM_ELEVATOR_EXPRESS)
                sc = queue_len(s, from, upL) + COST_TRANSFER;
            else
                sc = 8 * xd + (fullL ? COST_TRANSFER_FULL
                                     : COST_TRANSFER);
            if (sc < best_score) {
                best_score = sc; best_shaft = i; best_ride = L;
            }
        }
    }
    if (best_shaft >= 0) {
        r.kind = ROUTE_ELEVATOR; r.shaft = best_shaft; r.ride_to = best_ride;
        return r;
    }
    if (walk_stair >= 0) {     /* stairs were legal, just not cheap */
        r.kind = ROUTE_WALK; r.stair = walk_stair;
        r.hop_to = walk_hop;
        return r;
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
        int nrise = n->height - 1; if (nrise < 1) nrise = 1;
        if (nf != from_fidx && nf + nrise != from_fidx) continue;
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

/* The floor where this car's current sweep runs out of work — the EXE
 * keeps it in car+0x2997; we recompute (furthest dest/owned call in the
 * travel direction, else where it stands). Reversal costs route via it. */
static int car_turnaround(const ElevatorShaft *s, const ElevatorCar *c,
                          int ci)
{
    uint8_t mine = (uint8_t)(ci + 1);
    int turn = c->floor;
    if (c->dir) {
        for (int f = c->floor + 1; f <= s->hi; f++)
            if (c->dest_count[f] || s->up_call_car[f] == mine ||
                s->down_call_car[f] == mine)
                turn = f;
    } else {
        for (int f = c->floor - 1; f >= s->lo; f--)
            if (c->dest_count[f] || s->up_call_car[f] == mine ||
                s->down_call_car[f] == mine)
                turn = f;
    }
    return turn;
}

/* SelectElevator (1090:0dfc, final pick 1053-10cb), the real three
 * categories:
 *  - approaching: direction matches the call and the car hasn't passed
 *    the floor; score = directional distance;
 *  - reversing: everything else in flight; score = distance via the
 *    car's actual turnaround floor;
 *  - idle: NO work, parked at its home floor, not moving.
 * The WORKING car gets the call unless it scores >= threshold floors
 * worse than the idle car — the original piggy-backs calls onto moving
 * cars (that's what makes SCAN batch); a parked car is only woken when
 * the working car is clearly worse. (The port had this backwards for
 * weeks, preferring the parked car on every near-tie — elevator-truths
 * referee 2026-08-01 H1.) Deliberate divergence kept: among idle cars
 * we take the NEAREST; the EXE grabs car 0 unconditionally (raw
 * 1093-1096), which is pure 1994 indifference. */
static int select_car(const PeopleSim *ps, ElevatorShaft *s, int floor,
                      int up)
{
    int best_app = -1, app_cost = 0x7fff;
    int best_rev = -1, rev_cost = 0x7fff;
    int best_idle = -1, idle_cost = 0x7fff;
    for (int ci = 0; ci < s->num_cars; ci++) {
        ElevatorCar *c = &s->car[ci];
        if (!c->active) continue;
        int d = floor - c->floor;
        int toward = up ? d : -d;
        int busy = c->assigned_calls || c->distinct_dests ||
                   c->door_timer || c->target != c->floor;
        if (!busy && c->floor == s->home[ci]) {
            int cost = d < 0 ? -d : d;
            if (cost < idle_cost) { idle_cost = cost; best_idle = ci; }
        } else if (busy && (int)c->dir == up && toward >= 0) {
            if (toward < app_cost) { app_cost = toward; best_app = ci; }
        } else {
            int turn = car_turnaround(s, c, ci);
            int a = c->floor - turn; if (a < 0) a = -a;
            int b = floor - turn;    if (b < 0) b = -b;
            if (a + b < rev_cost) { rev_cost = a + b; best_rev = ci; }
        }
    }
    /* approaching categorically outranks reversing */
    int best_work = best_app >= 0 ? best_app : best_rev;
    int work_cost = best_app >= 0 ? app_cost : rev_cost;
    if (best_idle < 0) return best_work;
    if (best_work < 0) return best_idle;
    int th = sched_threshold_now(ps, s);
    return (work_cost - idle_cost < th) ? best_work : best_idle;
}

static int find_target_floor(ElevatorShaft *s, ElevatorCar *c, int ci);
static void car_start_step(ElevatorShaft *s, ElevatorCar *c);

static void call_elevator(const PeopleSim *ps, ElevatorShaft *s, int floor,
                          int up)
{
    uint8_t *slot = up ? &s->up_call_car[floor] : &s->down_call_car[floor];
    if (*slot) return;                       /* already assigned */
    /* SelectElevator's no-assignment path (raw 1002-1012): a stationary car
     * already sitting at the call floor (sched != 0, or facing the call's
     * direction) assigns NOTHING — its boarding pass just takes them.
     * (referee L3) */
    for (int k = 0; k < s->num_cars; k++) {
        ElevatorCar *c0 = &s->car[k];
        if (!c0->active || c0->floor != floor || c0->target != c0->floor)
            continue;
        if (c0->schedule_index || (int)c0->dir == up) return;
    }
    int ci = select_car(ps, s, floor, up);
    if (ci < 0) return;
    *slot = (uint8_t)(ci + 1);
    ElevatorCar *c = &s->car[ci];
    c->assigned_calls++;
    /* The EXE's CallElevator ends with UpdateCarState: the chosen car
     * re-derives its target ON THE SPOT, even mid-run — that's how an
     * elevator stops for you on its way up instead of sailing past
     * (elevator-truths referee H2). Mid-boarding cars finish the door
     * cycle first; the depart path re-derives anyway. */
    if (c->door_timer == 0) {
        int t = find_target_floor(s, c, ci);
        if (t >= 0) {
            if (c->target == c->floor) {
                /* parked: take the work and face it */
                c->dir = (uint8_t)(t > c->floor);
                c->target = (uint8_t)t;
                c->leg_start = c->floor;
                car_start_step(s, c);
            } else if ((c->dir && t > c->floor && t < c->target) ||
                       (!c->dir && t < c->floor && t > c->target)) {
                /* in flight: pull the stop earlier on the same path */
                c->target = (uint8_t)t;
            }
        }
    }
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

/* Bulldoze on a car (RemoveOneCar 10a0:036e, via the demolish dispatch
 * 10a0:0201): evict its riders where it stands, release its call
 * ownership, compact the car array (remapping surviving owners), dec the
 * count. Orphaned queues re-press the button via the every-tick re-call
 * sweep (the EXE re-dispatches inline at 1090:0a4c — same net effect one
 * tick later). Never removes the last car — that's a whole-shaft demolish
 * (10a0:0201 @023f), which the caller handles. */
void people_remove_car(PeopleSim *ps, int shaft, int ci)
{
    if (shaft < 0 || shaft >= ps->shaft_count) return;
    ElevatorShaft *s = &ps->shafts[shaft];
    if (ci < 0 || ci >= s->num_cars || s->num_cars <= 1) return;
    ElevatorCar *c = &s->car[ci];
    for (int k = 0; k < CAR_SLOTS; k++) {
        if (!c->pax[k]) continue;
        Person *p = &ps->people[c->pax[k] - 1];
        p->cur_floor = c->floor;
        p->state = PERSON_PLANNING;
    }
    for (int f = 0; f < TOWER_FLOOR_COUNT; f++) {
        uint8_t *slots[2] = { &s->up_call_car[f], &s->down_call_car[f] };
        for (int d = 0; d < 2; d++) {
            if (*slots[d] == (uint8_t)(ci + 1)) *slots[d] = 0;
            else if (*slots[d] > (uint8_t)(ci + 1)) (*slots[d])--;
        }
    }
    for (int k = ci; k < s->num_cars - 1; k++) {
        s->car[k] = s->car[k + 1];
        s->home[k] = s->home[k + 1];
    }
    memset(&s->car[s->num_cars - 1], 0, sizeof(ElevatorCar));
    s->home[s->num_cars - 1] = s->lo;
    s->num_cars--;
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
    /* The EXE rebuilds slots+ttables on ANY stop toggle (ElevatorUI
     * seg21:0138/013d -> 049f+00f2). Invalidate so the next route
     * re-derives; the layout stamp folds serviced flags too, so a later
     * people_rebuild_transport re-stamps as well. */
    XFER.dirty = 1;
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

/* Calibration note (2026-08-01): the EXE's wait constants (queue watchdog
 * 300, wait_cap 300, judge bars 150/200) are in ITS frame-time, ~320 ft
 * per daytime game hour — and our NORMAL speed runs ~120 render frames per
 * game hour with a ~2880-frame day, close enough to the EXE's ~2500-ft day
 * that the raw frame counts are already roughly EXE-calibrated. A per-hour
 * exchange-rate conversion was tried and reverted: it made the watchdog
 * pathological at fast speeds (18 frames at TURBO). Queues visibly
 * shrinking without a car IS the original's give-up mechanic. */

static void bank_wait(const PeopleSim *ps, Person *p, int frame)
{
    /* Simulate settle pre-sim: wait-stress banking is rerouted/skipped
     * while in edit mode (the EXE's [0xB3AE] gate — TripT #48, 10a8:0293). */
    if (elv_settling) return;
    int waited = frame - p->wait_start;
    if (waited < 0) waited = 0;
    int acc = p->wait_accum + waited;
    /* Grand-lobby forgiveness (AddNowLobbyStress 11d8:01f1): the 25/50
     * discount applies when banking a wait AT THE GROUND LOBBY — each
     * such bank discounts accumulated+elapsed. Waits banked on other
     * floors get no forgiveness (referee M4; the old whole-trip subtract
     * at delivery let a tall lobby erase floor-40 queue pain). */
    if (p->cur_floor == GROUND_IDX) acc -= ps->lobby_bonus;
    if (acc < 0) acc = 0;
    p->wait_accum = (uint16_t)(acc > WAIT_CAP ? WAIT_CAP : acc);
}

static void add_penalty(Person *p, int amount)
{
    int acc = p->wait_accum + amount;
    p->wait_accum = (uint16_t)(acc > WAIT_CAP ? WAIT_CAP : acc);
}

/* Deliver banked frustration to the home tenant on arrival.
 * Thresholds 150/200 come from the tuning resource (0xDD8A/0xDD8E) and are
 * byte-verified as ONE shared pair for hotels, offices and condos alike
 * (JudgeTenant 1130:@08b9-@090a; JudgeT-bars referee 2026-08-02). */
static void deliver_stress(PeopleSim *ps, Tower *tower, Person *p)
{
    /* Simulate settle pre-sim: no stress reaches tenants or the wait
     * averages (the EXE's [0xB3AE] reroute — referee 2026-08-01 §6). */
    if (elv_settling) return;
    /* Staff stress is never settled — TripCompletionFinalizer skips
     * person type 0xF (deepdive_1aed; referee M3 2026-08-02). Keeps
     * housekeeping churn out of tenant judging AND the wait averages. */
    if (p->service) { p->wait_accum = 0; return; }
    Tenant *t = tower_tenant(tower, p->home_tenant);
    /* Lobby forgiveness already happened per-bank inside bank_wait (M4);
     * what's accumulated here is what the tenant feels. */
    int felt = p->wait_accum;
    if ((int)(p - ps->people) == vip_watch) {      /* the VIP's own book */
        vip_stress_total += (unsigned)felt;
        vip_trips++;
    }
    if (t && t->state == TENANT_ABANDONED) {
        /* A failed attempt to reach a VACANT unit WIPES the person's
         * stress history instead of banking it (UniPeple verdict tables
         * @2612/2638/265f, cut-off referee 2026-08-02) — so a severed
         * vacant unit judges content, stays armed, and retries daily
         * forever rather than souring its own pool. */
        p->wait_accum = 0;
        return;
    }
    if (t) {
        /* Hotel rooms, offices and condos bank the raw felt stress for
         * their demand verdicts — the 5PM pass for rooms, the daily
         * 4:59AM judge for the others. The EXE's "period" (+0x09) is ONE
         * JOURNEY: it increments only in AddTotalStress 11d8:0000, whose
         * callers are trip completions/failures (divisor referee
         * 2026-08-02 — the old +3-per-arrival "periods lived" model was
         * refuted). Residual divergence, documented: the EXE averages
         * per-person (total/count), then over the unit's fixed headcount
         * (0630 loop) — the pooled ratio here matches it exactly when the
         * unit's people travel equally, and skips the zero-journey
         * dilution. */
        if ((item_is_hotel_room(t->type) || t->type == ITEM_OFFICE ||
             t->type == ITEM_CONDO) &&
            t->pool_stress_trips < 0xFFF0) {
            unsigned tot = t->pool_stress_total + (unsigned)felt;
            t->pool_stress_total = (uint16_t)(tot > 0xFFFF ? 0xFFFF : tot);
            t->pool_stress_trips += 1;
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
        int rise = st->height - 1; if (rise < 1) rise = 1;
        /* Stress = gaps x per-gap penalty (TryStartTrip: span * [0xDDB8/BA])
         * — a tall grand-lobby unit charges its whole rise at entry. */
        int span_pen = (st->type == ITEM_STAIRS) ? PENALTY_STAIR_SPAN
                                                 : PENALTY_ESC_SPAN;
        /* Staff trips are never charged: the EXE's charge flag IS the
         * network selector (1210:0054) — staff run with charge=0 for
         * every penalty gate (referee M3, 2026-08-02). */
        if (!p->service) {
            add_penalty(p, span_pen * rise);
            int xd = st->x - p->x; if (xd < 0) xd = -xd;
            if (xd >= 125) add_penalty(p, PENALTY_WALK_125);
            else if (xd >= 80) add_penalty(p, PENALTY_WALK_80);
        }
        p->state = PERSON_WALKING;
        p->leg_floor = (uint8_t)r.hop_to;
        p->walk_stair = st->id;
        /* (EXE dwell is one person-tick regardless of rise; the port
         * models visible walk time, so a tall unit takes rise x longer.) */
        p->walk_timer = ((st->type == ITEM_STAIRS) ? WALK_TICKS_STAIR
                                                   : WALK_TICKS_ESC) * rise;
        break;
    }
    case ROUTE_ELEVATOR: {
        ElevatorShaft *s = &ps->shafts[r.shaft];
        int up = r.ride_to > p->cur_floor;
        if (!join_queue(ps, s, p->cur_floor, up, pi)) {
            if (!p->service) add_penalty(p, PENALTY_QUEUE_FULL);
            break;                       /* stays PLANNING, retries */
        }
        /* Walk-to-shaft penalty charges STANDARD + service riders and
         * exempts the EXPRESS (TryStartTrip 1210:02d8-02e7, type != 0 —
         * consistent with express choice-scoring ignoring distance).
         * The port had the polarity inverted (referee H3, 2026-08-02).
         * Staff are exempt regardless (charge=0, referee M3). */
        if (!p->service && s->type != ITEM_ELEVATOR_EXPRESS) {
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
        if (!p->service) {
            add_penalty(p, PENALTY_NO_ROUTE);   /* = 300: instant cap-out */
            deliver_stress(ps, tower, p);
            noroute_report(p);   /* "People on Floor X need a path to Floor Y" */
        }
        ps->trips_failed++;
        p->state = PERSON_AT_DEST;          /* gives up where they stand */
        break;
    }
}

static void trip_arrived(PeopleSim *ps, Tower *tower, Person *p, int frame)
{
    (void)frame;
    /* Simulate settle pre-sim: an arrival must not check anyone in, park
     * a car, count a trip, or bank stress — the person array is rewound
     * wholesale after the settle, so parking the body AT_DEST is enough
     * (EXE contract: zero effect on people/money/stress — referee §6). */
    if (elv_settling) { p->state = PERSON_AT_DEST; return; }
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
     * it grades the retail service score (Restaurant.c 11a8:1197).
     * (Lobby forgiveness already applied per-bank in bank_wait.) */
    int felt = p->wait_accum;
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

static int shaft_extreme(const ElevatorShaft *s, int top);

/* SCAN: nearest floor needing service in the current direction, else
 * reverse, else the bottom of the shaft (home). */
static int find_target_floor(ElevatorShaft *s, ElevatorCar *c, int ci)
{
    uint8_t mine = (uint8_t)(ci + 1);
    /* SCAN discipline (ElevatorsT): sweep the travel direction serving
     * passenger stops and same-direction hall calls; a hall call for the
     * OPPOSITE direction is only the sweep's reversal point — the deepest
     * one ahead — never a mid-run stop. A FULL car serves only its own
     * passengers' floors (a hall stop can't board anyone anyway). The old
     * "any owned call, nearest first" pick let a huge lobby call yank a
     * full up-bound car back down: 21 hostages bouncing between ground
     * and floor 1 while floors 5+ starved (Jonah's tower, 2026-08-01). */
    int full = c->passengers >= s->capacity;
    /* Modes 1/2 (FindTargetFloor raw 1322-1363 / 1490-1526) are one-way
     * shuttles, not SCAN: "Express Up" (1) runs NONSTOP to the top, then
     * serves onboard stops and its own calls of BOTH directions on the way
     * down; "Express Down" (2) is the mirror. The no-work->home check in
     * car_depart_or_idle already ran (raw 1313-1319 precedes these), so a
     * workless shuttle car is parked at home, not here. */
    if (c->schedule_index == 1 || c->schedule_index == 2) {
        /* no-work->home runs FIRST in the EXE (raw 1313-1319 precedes the
         * mode branches): a workless shuttle car parks at home, it does
         * not patrol the extremes. */
        if (!c->passengers && !c->distinct_dests && !c->assigned_calls)
            return -1;
        int express_up = c->schedule_index == 1;   /* nonstop leg direction */
        if ((int)c->dir == express_up) {           /* nonstop leg */
            int end = shaft_extreme(s, express_up);
            if (end != c->floor) return end;
            /* at the terminus: fall through to the serve leg */
        }
        int f = c->floor;                          /* serve leg */
        while (1) {
            f += express_up ? -1 : 1;
            if (f < s->lo || f > s->hi) break;
            if (c->dest_count[f]) return f;
            if (!full && (s->up_call_car[f] == mine ||
                          s->down_call_car[f] == mine))
                return f;
        }
        /* default target = the serve leg's far end (raw 1330/1497) */
        int end = shaft_extreme(s, !express_up);
        return end != c->floor ? end : -1;
    }
    for (int pass = 0; pass < 2; pass++) {
        int up = pass == 0 ? c->dir : !c->dir;
        int far_opp = -1;
        int f = c->floor;
        while (1) {
            f += up ? 1 : -1;
            if (f < s->lo || f > s->hi) break;
            if (c->dest_count[f]) return f;
            if (!full) {
                if ((up ? s->up_call_car[f] : s->down_call_car[f]) == mine)
                    return f;
                if ((up ? s->down_call_car[f] : s->up_call_car[f]) == mine)
                    far_opp = f;
            }
        }
        if (far_opp >= 0) return far_opp;
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

/* Board one person from the queue in the given direction only. Returns 1
 * if someone boarded. Implements the service-elevators-don't-bank-stress
 * rule. */
static int board_one_dir(PeopleSim *ps, Tower *tower, ElevatorShaft *s,
                         ElevatorCar *c, int frame, int up)
{
    if (c->passengers >= s->capacity) return 0;
    if (queue_len(s, c->floor, up) == 0) return 0;
    int pi = dequeue(s, c->floor, up);
    if (pi < 0) return 0;
    Person *p = &ps->people[pi];
    if (s->type != ITEM_ELEVATOR_SERVICE)
        bank_wait(ps, p, frame);        /* staff never accrue wait stress */
    /* The in-shaft destination is re-resolved AT BOARD time (the EXE's
     * BoardOnePerson 1210:0f0e calls ResolveViaSlot at all five call
     * paths), not trusted from choice time: serves dest -> dest, else
     * the first shared slot in the riding direction, else failure. */
    xfer_ensure(ps, tower);
    int target = resolve_via_slot(ps, (int)(s - ps->shafts), c->floor,
                                  p->dest_floor, up);
    if (target < 0) {
        /* Layout changed while queued: the EXE charges [0xDD7E] (= 0 in
         * the tuning resource, nothing to add) and re-dispatches the
         * person to re-plan from where they stand. */
        p->state = PERSON_PLANNING;
        return 1;                       /* the door tick was spent */
    }
    int slot = -1;
    for (int i = 0; i < CAR_SLOTS; i++) if (!c->pax[i]) { slot = i; break; }
    if (slot < 0) return 0;
    p->leg_floor = (uint8_t)target;
    c->pax[slot] = (uint16_t)(pi + 1);
    c->pax_dest[slot] = (uint8_t)target;
    if (c->dest_count[target] == 0) c->distinct_dests++;
    if (c->dest_count[target] < 255) c->dest_count[target]++;
    c->passengers++;
    p->state = PERSON_RIDING;
    return 1;
}

/* Board one person, preferring the car's direction. Mode 0 adopts the
 * waiting direction when idle with no work; sched != 0 falls back to the
 * opposite queue (per-tick pacing for that mode lives in car_tick,
 * referee L7 — this wrapper serves the bulk tick and mode 0). */
static int board_one(PeopleSim *ps, Tower *tower, ElevatorShaft *s,
                     ElevatorCar *c, int frame)
{
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
    return board_one_dir(ps, tower, s, c, frame, up);
}

/* ClearFloorCall (1090:12c9, raw 1202-1225): at mode 0 only the DEPARTING
 * direction's slot is released (up slot when dir==up, down when dir==down);
 * the opposite call stays assigned and is served on the return sweep.
 * Shuttle modes (sched != 0) clear both. (referee L1) */
static void clear_call(ElevatorShaft *s, ElevatorCar *c, int ci, int floor)
{
    uint8_t mine = (uint8_t)(ci + 1);
    int sched = c->schedule_index != 0;
    if ((sched || c->dir) && s->up_call_car[floor] == mine) {
        s->up_call_car[floor] = 0;
        if (c->assigned_calls) c->assigned_calls--;
    }
    if ((sched || !c->dir) && s->down_call_car[floor] == mine) {
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
/* CalcMoveSpeed (1090:209f) + StopAndDispatch execution (1090:10e4):
 * gear 0 = 1 floor + 5-tick tail (~6 ticks/floor, the docking crawl),
 * gear 1 = 1 floor + 2-tick tail, gear 2 = a floor EVERY tick, and the
 * express-only gear 3 = THREE floors per tick (cur +/- 3, raw 1150-66)
 * — the original's signature rocket. The old 5/4/3/2-tick costs ran
 * standard cars ~3x and the express ~6x too slow (referee H3), which
 * also made the EXE-calibrated 300-tick watchdog feel merciless.
 * Returns the tail ticks; *stride = floors covered by this step. */
static int car_move_ticks(const ElevatorShaft *s, const ElevatorCar *c,
                          int *stride)
{
    int dt = c->target - c->floor;   if (dt < 0) dt = -dt;
    int ds = c->floor - c->leg_start; if (ds < 0) ds = -ds;
    *stride = 1;
    if (s->type == ITEM_ELEVATOR_EXPRESS) {
        if (dt < 2 || ds < 2) return 5;   /* gear 0 */
        if (dt > 4 && ds > 4) { *stride = 3; return 0; }   /* gear 3 */
        return 0;                         /* gear 2: floor per tick */
    }
    if (dt < 2 || ds < 2) return 5;       /* gear 0 */
    if (dt < 4 || ds < 4) return 2;       /* gear 1 */
    return 0;                             /* gear 2 */
}

static void car_start_step(ElevatorShaft *s, ElevatorCar *c)
{
    int stride;
    c->move_timer = (uint8_t)car_move_ticks(s, c, &stride);
    c->move_total = c->move_timer;
}

/* UpdateDirection (1090:1d2f) essence: a car that stopped to answer a call
 * it owns turns to face the call's direction before boarding.
 * SCAN discipline: the flip applies only when NOTHING of the car's own
 * work (onboard dest or owned call) remains AHEAD in its direction. A
 * call at the current floor must never reverse a mid-sweep car —
 * otherwise a full car livelocks between busy near floors while far
 * passengers ride hostage forever (Jonah's tower, 2026-08-02: 17 of 21
 * riders bound for the top floor, car ping-ponging lobby<->mid floors
 * for whole game days). */
static void adopt_call_direction(ElevatorShaft *s, ElevatorCar *c, int ci)
{
    if (car_turnaround(s, c, ci) != c->floor) return;   /* work ahead */
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

/* Re-derive a car's target in place (the UpdateCarState tail used by
 * CallElevator/ReassignCalls). Mid-boarding cars finish the door cycle
 * first; an in-flight car can only pull its stop EARLIER on the same
 * path — the depart path re-derives everything else at the next stop. */
static void car_retarget(ElevatorShaft *s, ElevatorCar *c, int ci)
{
    if (c->door_timer) return;
    int tgt = find_target_floor(s, c, ci);
    if (tgt < 0 && !c->passengers && c->floor != s->home[ci])
        tgt = s->home[ci];
    if (tgt < 0) { if (c->target == c->floor) return; tgt = c->floor; }
    if (tgt == c->target) return;
    if (c->target == c->floor) {
        if (tgt == c->floor) return;
        c->dir = (uint8_t)(tgt > c->floor);
        c->target = (uint8_t)tgt;
        c->leg_start = c->floor;
        car_start_step(s, c);
    } else if ((c->dir && tgt > c->floor && tgt < c->target) ||
               (!c->dir && tgt < c->floor && tgt > c->target)) {
        c->target = (uint8_t)tgt;
    }
}

/* ReassignCalls (1090:13cc): when a car opens its doors, any call at this
 * floor still assigned to a DIFFERENT car is stolen — this car is here and
 * will board them — and the robbed car retargets on the spot (DecrementWait
 * 1090:151c). Kills the stale-assignment busy-score skew in multi-car
 * banks. (referee L2) */
static void reassign_calls(ElevatorShaft *s, ElevatorCar *c, int ci)
{
    uint8_t mine = (uint8_t)(ci + 1);
    for (int d = 0; d < 2; d++) {
        uint8_t *slot = d ? &s->down_call_car[c->floor]
                          : &s->up_call_car[c->floor];
        if (!*slot || *slot == mine) continue;
        ElevatorCar *oc = &s->car[*slot - 1];
        int oci = *slot - 1;
        if (oc->assigned_calls) oc->assigned_calls--;
        *slot = mine;
        c->assigned_calls++;
        car_retarget(s, oc, oci);
    }
}

static void car_depart_or_idle(PeopleSim *ps, ElevatorShaft *s,
                               ElevatorCar *c, int ci)
{
    /* Mode refresh (referee L9): the EXE re-reads the schedule table only
     * during a door cycle at the shaft's top/bottom (raw 692-700) — a
     * departure decision at an extreme is the tail of such a cycle. Mode
     * changes elsewhere wait for the next turnaround. */
    if (c->floor == s->lo || c->floor == s->hi)
        c->schedule_index = sched_mode_now(ps, s);
    clear_call(s, c, ci, c->floor);
    /* people still queued here with no assigned car press the button again */
    if (queue_len(s, c->floor, 1) && !s->up_call_car[c->floor])
        call_elevator(ps, s, c->floor, 1);
    if (queue_len(s, c->floor, 0) && !s->down_call_car[c->floor])
        call_elevator(ps, s, c->floor, 0);

    int tgt = find_target_floor(s, c, ci);
    /* FindTargetFloor (1090:1553): a car with no work returns to its
     * home floor (group +0xBA[8]) — in EVERY mode; the raw no-work->home
     * check (1313-1319) precedes the shuttle branches, so idle shuttle
     * cars park at home, not at the shaft extremes. */
    if (tgt < 0 && !c->passengers && c->floor != s->home[ci])
        tgt = s->home[ci];
    if (tgt < 0) { c->target = c->floor; return; }   /* idle in place */
    c->target = (uint8_t)tgt;
    c->dir = (uint8_t)(tgt > c->floor);
    c->leg_start = c->floor;
    car_start_step(s, c);
}

static void car_tick(PeopleSim *ps, Tower *tower, ElevatorShaft *s,
                     int ci, int frame)
{
    ElevatorCar *c = &s->car[ci];
    if (!c->active) return;

    if (c->door_timer) {
        if (c->door_timer == DOOR_OPEN_TICKS) {
            /* ReassignCalls runs as the doors open (referee L2) */
            reassign_calls(s, c, ci);
            /* Schedule refresh cadence (referee L9): the EXE re-reads the
             * mode table only at ResetOneCar and when opening doors at the
             * shaft's top/bottom (raw 692-700) — mode changes take effect
             * at the next turnaround, not mid-sweep. */
            if (c->floor == s->lo || c->floor == s->hi)
                c->schedule_index = sched_mode_now(ps, s);
            /* Arrival "ding" (#6001): the EXE gates on the car's requested-stop
             * byte [bx+0x2A6C] for this floor (our dest_count), read before
             * unboarding clears it — so a car dings once on arriving at a
             * requested stop, not on every door re-open.
             * referee_sound_events_2026-07-13.md row 9. */
            if (c->dest_count[c->floor])
                play_snd(SND_ELEV_DING);
            unboard_at_floor(ps, tower, s, c, frame);
            /* ShouldTimeout (1090:23a5): arm the patience dwell as the
             * doors open; it only ever runs its course at the car's home
             * floor or a lobby (checked below, per FUN_10a0_1366). */
            c->hold_timer = (uint8_t)(sched_patience_now(ps, s) * 30);
        }
        if (c->door_timer & 1) {
            if (c->door_timer == 1) {
                while (board_one(ps, tower, s, c, frame)) {}
            } else if (c->schedule_index) {
                /* sched != 0 boards one from EACH direction per odd tick
                 * (TripT raw 1210:0351 second loop — referee L7) */
                board_one_dir(ps, tower, s, c, frame, c->dir);
                board_one_dir(ps, tower, s, c, frame, !c->dir);
            } else {
                board_one(ps, tower, s, c, frame);
            }
        }
        /* Patience dwell, the EXE way: at the home floor or a lobby a car
         * with patience left and room aboard HOLDS THE DOORS OPEN —
         * MoveElevator re-arms door_timer=1 each tick (raw 743-752), so
         * it bulk-boards stragglers every tick until the dwell runs out.
         * A full car, or any other floor, departs immediately. */
        if (c->door_timer == 1 && c->hold_timer &&
            c->passengers < s->capacity &&
            (c->floor == s->home[ci] || floor_is_lobby(tower, c->floor))) {
            c->hold_timer--;
            return;                         /* doors stay open */
        }
        c->door_timer--;
        if (c->door_timer == 0) {
            c->hold_timer = 0;
            car_depart_or_idle(ps, s, c, ci);
            /* departure motor #6002 on leaving a serviced stop
             * (StopAndDispatch raw 1181-1186; referee L6) */
            if (c->target != c->floor)
                play_snd(SND_ELEV_DEPART);
        }
        return;
    }

    if (c->target != c->floor) {
        if (c->move_timer) { c->move_timer--; return; }
        {
            int stride;
            car_move_ticks(s, c, &stride);      /* gear for THIS step */
            int left = c->dir ? c->target - c->floor : c->floor - c->target;
            if (stride > left) stride = left;
            c->floor = (uint8_t)(c->floor + (c->dir ? stride : -stride));
        }
        if (c->floor == c->target) {
            adopt_call_direction(s, c, ci);
            c->door_timer = DOOR_OPEN_TICKS;
            c->move_total = 0;
        } else {
            car_start_step(s, c);
        }
        return;
    }

    /* idle at a floor: the EXE's parked cars rest DOORS OPEN and re-run
     * the open-door pass every tick (MoveElevator idle branch raw 684-711,
     * referee L8) — so a walk-up boards straight away (no full open cycle,
     * which only exists to unboard, and an idle car carries no one) and a
     * car parked at the shaft's top/bottom keeps re-reading the mode table
     * (referee L9). */
    if (c->floor == s->lo || c->floor == s->hi)
        c->schedule_index = sched_mode_now(ps, s);
    if (c->dest_count[c->floor] ||
        queue_len(s, c->floor, 1) || queue_len(s, c->floor, 0)) {
        reassign_calls(s, c, ci);
        adopt_call_direction(s, c, ci);
        c->door_timer = c->dest_count[c->floor] ? DOOR_OPEN_TICKS : 1;
        return;
    }
    car_depart_or_idle(ps, s, c, ci);   /* schedule-aware idle targeting */
}

/* ---------- elevator "Simulate" edit mode (ElvEditT seg_10f0) ----------
 *
 * Sim-side half of the Simulate freeze-frame mode (the UI half lives in
 * main.c). Byte-verified against seg_10f0 — referee_elv_simulate_editmode_
 * 2026-08-01, three adversarial passes, zero deviations:
 *   EnterElevatorEditMode  10f0:0000   snapshot + isolate + settle
 *   SnapshotAndSettleGroup 10f0:0318   the invisible pre-sim
 *   RollbackGroupKeepSettings 10f0:0719  exit: revert all but the schedule
 *   ExitElevatorEditMode   10f0:009c   un-isolate, unfreeze
 *
 * All mode state is in these statics, NOT in PeopleSim — the .sav format
 * raw-dumps GameSim and its layout is frozen. */
static int           elv_edit_shaft = -1;      /* [0xB3A8]; -1 = not in mode */
static ElevatorShaft elv_edit_snap;            /* [0xB3B0]: the 0x345A group copy */
static uint8_t       elv_edit_saved_active[MAX_SHAFTS];   /* [0xB3B4] */
/* The EXE's settle leaves person records and the stat side-channels
 * untouched by construction (the [0xB3AE] gates above); the port's merged
 * trip code additionally moves people between queue/car/planning states,
 * so the same "zero effect on people" contract is implemented by rewinding
 * the person array wholesale after the settle. */
static Person        elv_edit_people[MAX_PEOPLE];

int people_edit_shaft(void) { return elv_edit_shaft; }

void people_edit_enter(PeopleSim *ps, Tower *tower, int shaft, int star,
                       int frame)
{
    if (elv_edit_shaft >= 0 || shaft < 0 || shaft >= ps->shaft_count) return;
    ElevatorShaft *s = &ps->shafts[shaft];
    if (!s->active) return;
    elv_edit_shaft = shaft;

    /* Isolation loop (10f0:0042-0074): save every group's active flag,
     * force all but the selected to 0 — removes them from every "for each
     * active group" iterator (draw, and routing during the settle). */
    for (int i = 0; i < ps->shaft_count; i++) {
        elv_edit_saved_active[i] = ps->shafts[i].active;
        ps->shafts[i].active = (uint8_t)(i == shaft);
    }

    /* SnapshotAndSettleGroup (10f0:0318), step 1: snapshot the whole
     * group record (memcpy of 0x345A bytes, 10f0:0327) + the port's
     * person-array snapshot (see elv_edit_people above). */
    elv_edit_snap = *s;
    memcpy(elv_edit_people, ps->people,
           (size_t)ps->people_high * sizeof(Person));

    /* Step 2, the pre-sim (10f0:0349-03e4): 2*[0xDD78] iterations, where
     * [0xDD78] is the star-scaled tuning value 150 (1-3 stars) / 200 (4+)
     * loaded by LevelT 1140:0104-0115 — so 300-400 ticks of frame_time++
     * then MoveElevator + Unboard/Board per active car (car_tick merges
     * exactly those calls). Sound is muted for the duration (the EXE
     * leaves the door-ding path ungated; referee §6 port note: mute). */
    SoundHookFn saved_hook = g_sound_hook;
    g_sound_hook = NULL;
    elv_settling = 1;
    int iters = 2 * (star >= 4 ? 200 : 150);
    for (int t = 0; t < iters; t++)
        for (int ci = 0; ci < s->num_cars; ci++)
            car_tick(ps, tower, s, ci, frame + 1 + t);
    elv_settling = 0;
    g_sound_hook = saved_hook;

    /* Step 3, restore logical state (10f0:03ee-070c): everything the
     * settle touched EXCEPT car position/direction/door state comes back
     * from the snapshot — per-floor call assignments (+0x2A2/+0x31A), the
     * stop queues (the EXE restores just the 4-byte ring headers; the
     * port's join path overwrites ring slots, so whole stop records are
     * restored — same visible contract: queues read as at the moment
     * Simulate was pressed), per-car scheduling fields (0x298A-0x2998),
     * passenger refs (+0x299A) and dests (+0x2A42), per-dest counts
     * (+0x2A6C). The cars keep the fast-forwarded arrangement — that
     * representative mid-operation still is the point of the mode. */
    memcpy(ps->people, elv_edit_people,
           (size_t)ps->people_high * sizeof(Person));
    memcpy(s->stop, elv_edit_snap.stop, sizeof(s->stop));
    memcpy(s->up_call_car, elv_edit_snap.up_call_car,
           sizeof(s->up_call_car));
    memcpy(s->down_call_car, elv_edit_snap.down_call_car,
           sizeof(s->down_call_car));
    for (int ci = 0; ci < CARS_PER_SHAFT; ci++) {
        ElevatorCar *live = &s->car[ci];
        const ElevatorCar *snap = &elv_edit_snap.car[ci];
        live->passengers     = snap->passengers;
        live->distinct_dests = snap->distinct_dests;
        live->assigned_calls = snap->assigned_calls;
        live->schedule_index = snap->schedule_index;
        live->hold_timer     = snap->hold_timer;
        memcpy(live->pax, snap->pax, sizeof(live->pax));
        memcpy(live->pax_dest, snap->pax_dest, sizeof(live->pax_dest));
        memcpy(live->dest_count, snap->dest_count, sizeof(live->dest_count));
        /* NOT restored (10f0:03ee list ends before them): floor, target,
         * dir, door_timer, move_timer, move_total, leg_start. */
    }
}

void people_edit_exit(PeopleSim *ps)
{
    if (elv_edit_shaft < 0) return;
    ElevatorShaft *s = &ps->shafts[elv_edit_shaft];

    /* RollbackGroupKeepSettings (10f0:0719): the dialog widgets that stay
     * usable during the mode are exactly the fields preserved here — copy
     * live -> snapshot the schedule matrices (threshold +0x12, mode +0x20,
     * patience +0x2E, 10f0:0728-07e7) and the SHOW word (+0x3C), then
     * restore the group wholesale (10f0:07e8). The settle's car scrambling
     * is discarded; the frozen interval cost zero game time. (The EXE also
     * preserves a fourth matrix at group+4 whose reader was never located
     * — referee §8, LOW — with no port equivalent.) */
    memcpy(elv_edit_snap.sched_mode, s->sched_mode, sizeof(s->sched_mode));
    memcpy(elv_edit_snap.sched_threshold, s->sched_threshold,
           sizeof(s->sched_threshold));
    memcpy(elv_edit_snap.sched_patience, s->sched_patience,
           sizeof(s->sched_patience));
    elv_edit_snap.hidden = s->hidden;
    *s = elv_edit_snap;

    /* ExitElevatorEditMode (10f0:00ad-0120): restore the saved group
     * active flags; the caller unfreezes and forces a full repaint. */
    for (int i = 0; i < ps->shaft_count; i++)
        ps->shafts[i].active = elv_edit_saved_active[i];
    elv_edit_shaft = -1;
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

/* As above, but with the EXE's maid partition: each maid works floors
 * congruent to her member index mod 6 first (MainteT's tower split),
 * falling back to anywhere dirty. */
static int find_dirty_room_floor_for(Tower *tower, int member)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < tower->tenant_count; i++) {
            Tenant *t = &tower->tenants[i];
            if (!item_is_hotel_room(t->type) || t->condition != ROOM_DIRTY)
                continue;
            int f = floor_to_index(t->floor);
            if (f < 0 || f >= TOWER_FLOOR_COUNT) continue;
            if (pass == 0 && (f % 6) != (member % 6)) continue;
            return f;
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

/* Aggregate of the EXE's per-person dice at the venue level: `pool`
 * people each roll 1-in-`width` per 16-frame visit, so the venue sees
 * one departure with per-tick odds pool/(width*16). The port rolls one
 * tenant-level die with that combined probability — the exact sum of
 * the individual dice, including the natural slowdown as the pool
 * drains. width 1 = the EXE's "deterministic" arm (everyone leaves on
 * their next visit). */
static int pool_roll(int frame, int idx, int pool, int width)
{
    if (pool <= 0) return 0;
    int n = (width * 16) / pool;
    if (n < 1) n = 1;
    return depart_roll(frame, idx, n);
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
            /* The EXE's exact per-person widths, aggregated over the
             * remaining pool (= today's undispatched quota): FF/shops
             * rand%36 through the day and rand%6 in the 5-9PM rush;
             * restaurants rand%12 in the rush, then the DETERMINISTIC
             * straggler arm until 11PM (2026-07-11 referee). */
            int width = 0;
            if (t->type == ITEM_RESTAURANT)
                width = (hour >= 17 && hour < 21) ? 12
                      : (hour >= 21 && hour < 23) ? 1 : 0;
            else
                width = (hour >= 10 && hour < 17) ? 36
                      : (hour >= 17 && hour < 21) ? 6 : 0;
            if (width && pool_roll(frame, i, t->retail_quota, width))
                walkin = 1;
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
             * each office hit a FASTFOOD in their zone, weekdays only —
             * offices never patronize restaurants. Exact dice: rand%12
             * per visit through the noon period, DETERMINISTIC 13:00-
             * 17:00, aggregated over the workers still at their desks. */
            if (ps->sched_day || hour < 12 || hour >= 17) continue;
            if (ps->spawned[i] >= 5 ||
                !pool_roll(frame, i, 5 - ps->spawned[i],
                           hour >= 13 ? 1 : 12)) continue;
            v = pick_retail(tower, ITEM_FAST_FOOD, t->floor, seed,
                            reach_public);
            if (v) ps->spawned[i]++;
        } else if (item_is_hotel_room(t->type)) {
            /* Hotel dinners (1220:382c): guests of EVEN tenant slots go
             * out for a RESTAURANT dinner, 5-9PM, once per stay. */
            if (!t->hosted || (i & 1) || hour < 17 || hour >= 21) continue;
            if (ps->dinner_sent[i >> 3] & (1 << (i & 7))) continue;
            if (!pool_roll(frame, i, 1, 6)) continue;   /* rand%6 per visit */
            v = pick_retail(tower, ITEM_RESTAURANT, t->floor, seed,
                            reach_public);
            if (v) ps->dinner_sent[i >> 3] |= (uint8_t)(1 << (i & 7));
        } else if (t->type == ITEM_CONDO) {
            /* Condo excursions (1220:3a1a): weekdays the Homebody's one
             * shopping trip after 10AM (rand%12 per visit); weekends
             * every 4th condo dines out 5-9PM (rand%6) and the rest
             * grab fast food on the daytime dice. Weekday trips latch
             * on the dinner_sent bitmask, NOT spawned[] — that counter
             * belongs to the residents' own return spawns and sharing
             * it starved the kid whenever the shopper rolled first. */
            if (!ps->sched_day) {
                if (ps->dinner_sent[i >> 3] & (1 << (i & 7))) continue;
                if (hour < 10 || hour >= 21 || !pool_roll(frame, i, 1, 12))
                    continue;
                v = pick_retail(tower, ITEM_SHOP, t->floor, seed,
                                reach_public);
                if (v) ps->dinner_sent[i >> 3] |= (uint8_t)(1 << (i & 7));
            } else if ((i & 3) == 0) {
                if (ps->spawned[i] >= 2) continue;
                if (hour < 17 || hour >= 21 || !pool_roll(frame, i, 1, 6))
                    continue;
                v = pick_retail(tower, ITEM_RESTAURANT, t->floor, seed,
                                reach_public);
                if (v) ps->spawned[i]++;
            } else {
                if (ps->spawned[i] >= 2) continue;
                if (hour < 10 || hour >= 17 || !pool_roll(frame, i, 1, 12))
                    continue;
                v = pick_retail(tower, ITEM_FAST_FOOD, t->floor, seed,
                                reach_public);
                if (v) ps->spawned[i]++;
            }
        } else if (t->type == ITEM_CATHEDRAL) {
            /* Weekend congregation (UniPeple 1220:5edd): period-0
             * mornings only, rand-gated from 8:00 — 40 guests ride all
             * the way to floor 100 and head home before lunch. */
            if (!ps->sched_day || hour < 8 || hour >= 11) continue;
            if (ps->spawned[i] >= 40) continue;
            /* rand-gated until 10:00, deterministic after — aggregated
             * over the congregation still to arrive */
            if (!pool_roll(frame, i, 40 - ps->spawned[i],
                           hour >= 10 ? 1 : 12)) continue;
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
            /* 1/16-LOD pacing (1220:0daf): the EXE touches each person
             * every 16 frames, so a full-queue rejection costs its 5-point
             * penalty at most once per 16 — the old %4 accrued queue-full
             * stress 4x too fast (referee M5). */
            if ((frame + i) % 16 == 0)
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
            if (frame - p->wait_start >= WAIT_CAP) {   /* EXE reads [0xDD7A],
                                                        * the same tuning cap */
                queue_remove(&ps->shafts[p->shaft], p->cur_floor, p->dir, i);
                bank_wait(ps, p, frame);
                deliver_stress(ps, tower, p);
                ps->trips_failed++;
                if (p->errand == 1 || p->errand == 3) p->errand = 4;
                p->state = PERSON_AT_DEST;
                break;
            }
            queued++; break;
        case PERSON_RIDING:  riding++; break;
        case PERSON_AT_DEST:
            /* Give-up recovery: someone who abandoned a trip (queue
             * watchdog / no route) rests where they stand with their
             * intent still set — dest_floor != cur_floor. Retry on the
             * usual 1/16-LOD dice so they finish once a car frees up or
             * new transport arrives. Without this, condo residents
             * wedged forever: the commute window needs !going_home, the
             * phase-flip replanner only covers offices/hotels, and the
             * spawn dedup saw the stuck body as "already home" — whole
             * upper floors went silent (Jonah's tower, 2026-08-01). */
            if (!p->stay && p->dest_floor != p->cur_floor &&
                (frame + i) % 16 == 0 && depart_roll(frame, i + 7, 12)) {
                p->state = PERSON_PLANNING;
                break;
            }
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
                /* Maids cycle rooms ALL DAY (MainteT: idle->room->next,
                 * mod-6 floor partition, no new cleans from 16:00) —
                 * the old single dawn trip understated the service-net
                 * load a tall hotel really generates. */
                if (ht && ht->type == ITEM_HOUSEKEEPING && p->service &&
                    hour < 16) {
                    int nf = find_dirty_room_floor_for(tower, p->member);
                    if (nf >= 0) {
                        p->stay = 4;                 /* next room's dwell */
                        if (nf != p->cur_floor) {
                            p->dest_floor = (uint8_t)nf;
                            p->state = PERSON_PLANNING;
                        }
                        break;
                    }
                }
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
