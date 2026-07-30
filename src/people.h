/* people.h - Person entities, elevator cars, and trips
 *
 * Ported from the SIMTOWER.EXE decompilation:
 *   - TransferT (seg_11b0): route scoring, walk budgets, transfer slots
 *   - TripT (seg_1210): TryStartTrip, queue join, boarding/unboarding
 *   - ElevatorsT (seg_1090): car state machine, call assignment
 *   - WaitT (seg_11d8): wait-time -> stress banking
 *
 * Tuning constants are the REAL values from EXE resource 0x7F05
 * (simtower-decomp globals.md #49).
 */
#ifndef PEOPLE_H
#define PEOPLE_H

#include "tower.h"

#define MAX_PEOPLE     4096
#define MAX_SHAFTS     24          /* original: 24 elevator groups */

/* How many visitors one metro pumps into the tower per time-of-day phase.
 * The EXE pool is 240 persons per station at ~1-in-36 dice per 16-frame
 * sim visit over the 10AM-5PM window (1220:51dc, byte-verified 2026-07-11)
 * — roughly 300 visitor trips a day. The port's per-tick pacing roll
 * yields ~70 per phase against this cap, landing in the right range
 * (the old cap of 8 starved every ground-zone shop into eviction). */
#define METRO_VISITORS_PER_PHASE 80
#define CARS_PER_SHAFT 8           /* original: 8 cars per group */
#define QUEUE_CAP      40          /* people per direction per floor stop */
#define CAR_SLOTS      42          /* passenger slots per car (= standard capacity) */

/* --- The master tuning table ---
 * Defaults are the REAL values from EXE resource 0x7F05 (the game loads
 * its balance constants from a big-endian data resource — everything here
 * was data in the original too, so runtime tuning is faithful modding).
 * The macros below alias the live struct so sim code reads naturally. */
typedef struct {
    int wait_cap;            /* frustration accumulator hard cap (300) */
    int penalty_queue_full;  /* rejected at a full queue (5) */
    int penalty_no_route;    /* no route = instant cap-out (300) */
    int penalty_esc_span;    /* per escalator span walked (16) */
    int penalty_stair_span;  /* per stair span walked (35) */
    int penalty_walk_80;     /* walk >= 80 cells to a transport (30) */
    int penalty_walk_125;    /* walk >= 125 cells (60) */
    int cost_stair_base;     /* route scoring (640) */
    int cost_elev_base;      /* (640) */
    int cost_elev_full;      /* (1000) */
    int cost_transfer;       /* (3000) */
    int cost_transfer_full;  /* (6000) */
    int walk_floors_esc;     /* walk budget, all-escalator (6) */
    int walk_floors_stair;   /* walk budget once stairs involved (3) */
    int capacity_express;    /* people per express car (42 — MakeElevator
                                literal, the wide 48px car) */
    int capacity_standard;   /* people per standard/service car (21) */
    int judge_moderate;      /* avg wait -> mild stress (150, probable) */
    int judge_stressed;      /* avg wait -> heavy stress (200, probable) */
    int star_pop[4];         /* population for 2..5 stars (300/1k/5k/10k) */
    /* Money values below are dollars; the EXE stores them /100.
     * Add-a-car prices live in the tuning resource at +0x90 (globals #54),
     * NOT in the build-cost table — moddable like everything else here. */
    int car_cost_std;        /* add a standard car ($80k) */
    int car_cost_express;    /* add an express car ($150k) */
    int car_cost_service;    /* add a service car ($50k) */
    int maint_car_std;       /* upkeep per standard car per sweep ($10k, res 0x3ea) */
    int maint_car_express;   /* per express car ($20k) */
    int maint_car_service;   /* per service car ($10k) */
    int maint_escalator;     /* per escalator ($5k); stairs are free */
    int star_bonus[3];       /* award on promotion to star 2/3/4
                                ($200k/$300k/$500k — tuning res +0xa8) */
    int lobby_fee_star3;     /* lobby upkeep $/cell/sweep at 3 stars ($300) */
    int lobby_fee_star4;     /* at 4+ stars ($1000); free below 3 stars */
} Tuning;

extern Tuning TUNING;

/* Reset every field to the EXE's values */
void tuning_reset(void);

#define WAIT_CAP            (TUNING.wait_cap)
#define PENALTY_QUEUE_FULL  (TUNING.penalty_queue_full)
#define PENALTY_NO_ROUTE    (TUNING.penalty_no_route)
#define PENALTY_ESC_SPAN    (TUNING.penalty_esc_span)
#define PENALTY_STAIR_SPAN  (TUNING.penalty_stair_span)
#define PENALTY_WALK_80     (TUNING.penalty_walk_80)
#define PENALTY_WALK_125    (TUNING.penalty_walk_125)
#define COST_STAIR_BASE     (TUNING.cost_stair_base)
#define COST_ELEV_BASE      (TUNING.cost_elev_base)
#define COST_ELEV_FULL      (TUNING.cost_elev_full)
#define COST_TRANSFER       (TUNING.cost_transfer)
#define COST_TRANSFER_FULL  (TUNING.cost_transfer_full)
#define COST_NO_ROUTE       0x7fff

typedef enum {
    PERSON_FREE = 0,
    PERSON_PLANNING,    /* needs a route toward dest_floor */
    PERSON_WALKING,     /* on a stair/escalator leg */
    PERSON_QUEUED,      /* waiting in an elevator stop queue */
    PERSON_RIDING,      /* inside a car */
    PERSON_AT_DEST,     /* arrived; waits for the next phase of the day */
} PersonState;

typedef struct {
    uint16_t home_tenant;   /* tenant id; 0 = slot free */
    uint8_t  state;         /* PersonState */
    uint8_t  cur_floor;     /* grid floor index */
    uint8_t  dest_floor;    /* final destination of the current trip */
    uint8_t  leg_floor;     /* arrival floor of the current leg */
    uint8_t  dir;           /* queue direction while QUEUED (1 = up) */
    uint8_t  shaft;         /* shaft index while QUEUED/RIDING */
    uint8_t  service;       /* staff: stairs-only walking, service elevators */
    uint8_t  going_home;    /* trip intent: 0 = inbound, 1 = outbound */
    uint8_t  walk_timer;    /* ticks left on the walking leg */
    uint8_t  stay;          /* visit countdown at destination (patrons/staff,
                             * 8-tick units); 0 = resident/commuter */
    uint8_t  entry_floor;   /* floor this visitor leaves through (ground for
                             * street patrons/commuters; the metro floor for
                             * metro visitors). Set at spawn. */
    uint8_t  parked_cat;    /* car on the lot: 0 none / 1 office / 2 suite —
                             * released against the tower's category counter
                             * when the person leaves for good (ParkingT
                             * gate counts, 1198:002f/031a) */
    uint8_t  member;        /* index within the home tenant's people pool —
                             * the EXE's numInTenant, which drives the
                             * person-type classifier (InfoPeple: office
                             * member 0/1 = Salesman, condo 1 = Mother
                             * with Baby, ...). Assigned at spawn. */
    uint16_t wait_accum;    /* frustration, capped at WAIT_CAP (person+0xC) */
    uint16_t walk_stair;    /* tenant id of the stair/escalator carrying this
                             * person while WALKING; 0 = none. Feeds the
                             * rider list (people-list family 1100:327f).
                             * Occupies former struct padding so sizeof and
                             * every field offset are unchanged — v13 saves
                             * still load (the slot reads as 0/garbage there,
                             * harmless: it's only trusted while WALKING and
                             * is restamped when the next leg starts). */
    int      wait_start;    /* sim frame when the queue was joined (+0xA) */
    int      x;             /* walking-origin x (home tenant x, GetPersonX) */
} Person;

/* Waiting queues at one floor stop — ring buffers exactly as in the EXE
 * (stop record: counts/heads + u32 ring[40] per direction) */
typedef struct {
    uint8_t  up_count, up_head, down_count, down_head;
    uint16_t up_ring[QUEUE_CAP];     /* person index + 1; 0 = empty */
    uint16_t down_ring[QUEUE_CAP];
} ElevatorStop;

typedef struct {
    uint8_t  active;
    uint8_t  floor;         /* current floor (grid index) */
    uint8_t  target;        /* floor the car is heading to */
    uint8_t  dir;           /* 1 = up, 0 = down */
    uint8_t  door_timer;    /* 5..0; 5 = just opened (unboard), odd = board 1,
                             * 1 = bulk board, 0 = closed */
    uint8_t  move_timer;    /* ticks until the next floor step */
    uint8_t  move_total;    /* ticks this floor step started with (render lerp) */
    uint8_t  leg_start;     /* floor this run departed from (accel curve) */
    uint8_t  passengers;    /* people on board (car+0x298D) */
    uint8_t  distinct_dests;/* distinct destination floors aboard (+0x2996) */
    uint16_t assigned_calls;/* floor calls owned by this car (+0x2994) */
    uint8_t  schedule_index;/* mode for the current period (car+0x2998):
                             * copied from sched_mode at resets/turnarounds.
                             * 0 normal; 1 shuttle (idle to shaft extremes);
                             * nonzero also = both-direction pickup */
    uint8_t  hold_timer;    /* patience dwell before departing (x30 ticks) */
    uint16_t pax[CAR_SLOTS];        /* person index + 1; 0 = empty */
    uint8_t  pax_dest[CAR_SLOTS];   /* destination floor per slot (+0x2A42) */
    uint8_t  dest_count[TOWER_FLOOR_COUNT]; /* requested stops (+0x2A6C) */
} ElevatorCar;

typedef struct {
    uint8_t  active;
    ItemType type;          /* ITEM_ELEVATOR_SHAFT / _SERVICE / _EXPRESS */
    uint8_t  lo, hi;        /* shaft floor range (grid indices) */
    uint8_t  hidden;        /* dialog Show Off: world render draws only the
                             * two guide rails so what's behind shows through.
                             * Lives in the struct's alignment hole before x —
                             * sizeof unchanged, old saves read 0 = shown. */
    int      x;             /* shaft x cell */
    uint8_t  capacity;      /* 42 express, 21 standard/service (group+2) */
    uint8_t  num_cars;
    uint8_t  serviced[TOWER_FLOOR_COUNT]; /* per-floor car-stop flags — the
                             * dialog grid (EXE group +0x40/+0x41 + ElvDlogT);
                             * survives layout rebuilds, matched by column */
    uint8_t  home[CARS_PER_SHAFT]; /* car home floors (EXE group +0xBA[8]):
                             * an idle car with no work returns here — the
                             * dialog's red diamond markers */
    /* Car schedules (EXE group +0x12/+0x20/+0x2e — globals.md #53), all
     * indexed [is_weekend][period 0..6]; survive rebuilds like serviced[]:
     * mode: 0 normal SCAN, 1 shuttle, nonzero = both-direction pickup
     * threshold: prefer an idle/home car over an approaching one unless
     *            the approaching car is this much closer (default 5)
     * patience: door dwell before departing = value*30 ticks (0..3) */
    uint8_t  sched_mode[2][7];
    uint8_t  sched_threshold[2][7];
    uint8_t  sched_patience[2][7];
    ElevatorCar  car[CARS_PER_SHAFT];
    ElevatorStop stop[TOWER_FLOOR_COUNT];
    uint8_t  up_call_car[TOWER_FLOOR_COUNT];   /* car index + 1; 0 = none */
    uint8_t  down_call_car[TOWER_FLOOR_COUNT];
} ElevatorShaft;

typedef struct {
    Person   people[MAX_PEOPLE];
    int      people_high;       /* slots ever used (scan bound) */

    ElevatorShaft shafts[MAX_SHAFTS];
    int      shaft_count;

    /* Per-gap walk map (floor f <-> f+1), faithful to 0xCF10:
     * bit0 = escalator on this gap, bit1 = stairs */
    uint8_t  gap_map[TOWER_FLOOR_COUNT];

    uint32_t layout_stamp;      /* transport layout change detection */

    /* Schedule clock, set by game_update each tick (EXE 0xB3A0/0xB3A1):
     * sched_day 0 weekday / 1 weekend; sched_period 0..6 across the day */
    uint8_t  sched_day;
    uint8_t  sched_period;

    /* Day-phase spawn bookkeeping (one byte per tenant slot) */
    uint8_t  spawned[MAX_TENANTS];
    uint8_t  cur_phase;

    /* Stats for UI / digests */
    int      population_now;    /* live person entities */
    int      queued_now;
    int      riding_now;
    int      walking_now;
    long     trips_done;
    long     trips_failed;
    long     wait_total;        /* sum of banked waits (for avg) */
    long     wait_samples;

    /* Grand-lobby prestige bonus (WaitT LobbyBonusAdjust, seg_11d8): a
     * 2/3-story ground lobby forgives 25/50 ticks of waiting before it
     * registers as frustration. Recomputed each tick from the lobby height. */
    int      lobby_bonus;

    /* Office arrivals since the sim last consumed them — the floors where
     * workers just reached their desks. game_update rolls each one for the
     * 1-in-10 sick-worker chance (UniPeple medical path). Capped; a tick
     * never sees anywhere near 32 arrivals. */
    int8_t   office_arrival_floor[32];
    int      office_arrivals;

    /* Patrons who just walked into a cinema / party hall — tenant ids,
     * consumed by the venue pass (game.c) for attendance counting. */
    uint16_t venue_arrival_tenant[32];
    int      venue_arrivals;

    /* Customers who just reached a restaurant / fast food / shop —
     * consumed by game_retail_arrivals (InRestPeple semantics). wait =
     * the felt elevator wait that grades the venue's service score;
     * walkin = the venue's own street pool (entered at ground). */
    uint16_t retail_arrival_tenant[64];
    uint16_t retail_arrival_wait[64];
    uint8_t  retail_arrival_walkin[64];
    int      retail_arrivals;

    /* One dinner excursion per hosted room per evening (UniPeple sets
     * the "wants dinner" status ONCE at check-in). One bit per tenant,
     * cleared with the phase reset. */
    uint8_t  dinner_sent[MAX_TENANTS / 8];

    /* Movers who just reached a VACANT, armed office/condo/shop — the
     * re-let events, consumed by game_relet_arrivals (banks the move-in
     * lump and flips the unit occupied). */
    uint16_t relet_arrival_tenant[16];
    int      relet_arrivals;
} PeopleSim;

void people_init(PeopleSim *ps);

/* Rebuild gap map + shafts from the tower. Cheap when nothing changed
 * (diffs the transport layout; resets cars/queues only on change). */
void people_rebuild_transport(PeopleSim *ps, Tower *tower);
/* Car assignment (UseCarPerson 1198:06e7): uniform-random usable space,
 * 2N-per-category quota. Returns the space's floor index (and counts
 * the car) or -1. suite selects the category. Exposed for tests. */
int people_parking_assign(Tower *tower, const uint8_t *reach, int suite,
                          int seed);

/* One simulation tick: spawning, trips, queues, cars, stress delivery.
 * frame = global tick counter, tod = TimeOfDay (game.h value), hour =
 * the clock hour (the retail windows are finer than time-of-day). */
void people_update(PeopleSim *ps, Tower *tower, int frame, int tod, int hour,
                   const uint8_t *reach_public, const uint8_t *reach_service);

/* Join the waiting queue at a stop (exposed for tests).
 * Returns 1 on success, 0 if the queue is full (cap 40/direction). */
int people_join_queue(PeopleSim *ps, int shaft, int floor, int up,
                      int person_idx);

/* Elevator dialog (ElvDlogT) controls.
 * set_num_cars: clamps to 1..CARS_PER_SHAFT; new cars park at the bottom,
 * retired cars dump their riders to replan from the car's floor.
 * set_serviced: toggles a floor stop; turning it off flushes that floor's
 * queues (riders already aboard still get dropped there). */
void people_set_num_cars(PeopleSim *ps, int shaft, int n);
void people_set_serviced(PeopleSim *ps, int shaft, int fidx, int on);
void people_set_home(PeopleSim *ps, int shaft, int car, int fidx);

/* Info-dialog transport-distance: nearest transport serving a floor + its
 * cell-distance. Returns 0 none / 1 walk (stairs/escalator) / 2 elevator. */
int people_nearest_transport(PeopleSim *ps, Tower *tower, int from_fidx,
                             int x, int *out_dist, int *is_stairs);

/* Average banked wait (0 if no samples yet) */
static inline int people_avg_wait(const PeopleSim *ps)
{
    return ps->wait_samples ? (int)(ps->wait_total / ps->wait_samples) : 0;
}

#endif /* PEOPLE_H */
