/* game.h - Simulation engine
 *
 * Time, money, population, star rating — the beating heart of SimTower.
 * Mechanics decoded from SIMTOWER.EXE decompilation (segs 1140, 1148, 1118).
 *
 * Time model (from original):
 *   - Each "day" has 4 quarters: Q1 (morning), Q2 (midday), Q3 (evening), Weekend
 *   - Actually it's: Weekday1, Weekday2, Weekday3, Weekend (repeating)
 *   - Each quarter has ~360 ticks (frames at normal speed)
 *   - Tick rate affected by game speed
 */
#ifndef GAME_H
#define GAME_H

#include "tower.h"
#include "people.h"

/* Time of day — affects tenant activity and sky rendering */
typedef enum {
    TOD_DAWN = 0,      /* 5:00-7:00 — people arriving */
    TOD_MORNING,        /* 7:00-12:00 — offices active */
    TOD_AFTERNOON,      /* 12:00-17:00 — restaurants busy */
    TOD_EVENING,        /* 17:00-21:00 — people leaving, hotels checking in */
    TOD_NIGHT,          /* 21:00-5:00 — hotels active, offices dark */
    TOD_COUNT
} TimeOfDay;

/* --- The two clocks, reconciled (2026-07-12) ---
 * The EXE calendar (TimeT): a DAY is one 2600-frame cycle; days run
 * 1st WD, 2nd WD, WE ([0xB3A0] weekend flag = day % 3 == 2); the
 * financial quarter IS that 3-day cycle (settlement at its dawn); a
 * displayed year is 4 cycles = 12 days. Everything with weekday/
 * weekend semantics must read tower->day via game_is_weekend().
 * The enum below is the port's OWN intra-day bookkeeping: four 6h
 * slices of the 24h sim day (tick counters, analytics samples, the
 * finance sub-logs). It carries NO weekday/weekend meaning — the
 * June-era names that conflated the two calendars are gone. */
typedef enum {
    QUARTER_1 = 0,      /* 5am-11am */
    QUARTER_2,          /* 11am-5pm */
    QUARTER_3,          /* 5pm-11pm */
    QUARTER_4,          /* 11pm-5am */
    QUARTER_COUNT
} Quarter;

/* Financial-report category lists, in the exact top-to-bottom order of the
 * EXE's report art (bitmap 0x81f4). Left = revenue (population + income),
 * right = infrastructure (maintenance expense). */
enum {
    FINCAT_OFFICE = 0, FINCAT_HOTEL_SINGLE, FINCAT_HOTEL_TWIN, FINCAT_HOTEL_SUITE,
    FINCAT_SHOP, FINCAT_FAST_FOOD, FINCAT_RESTAURANT, FINCAT_PARTY_HALL,
    FINCAT_CINEMA, FINCAT_CONDO, FIN_INCOME_CATS
};
enum {
    FINEXP_LOBBY = 0, FINEXP_ELEVATOR, FINEXP_EXP_ELEVATOR, FINEXP_SER_ELEVATOR,
    FINEXP_ESCALATOR, FINEXP_PARKING_RAMP, FINEXP_RECYCLING, FINEXP_METRO,
    FINEXP_HOUSEKEEPING, FINEXP_SECURITY, FIN_EXPENSE_CATS
};

/* Game speed. The original's model (menu resource + TimeT 1200:01a5,
 * 2026-07-29): pause, or a 6ms-per-tick throttle, or Options -> Fast
 * Mode = unthrottled ("as fast as the machine can", 1994 hardware).
 * NORMAL is our throttled rate; FAST approximates era-unthrottled at
 * 2x and is the UI cap. TURBO is kept for old saves/debug only — no
 * menu entry; finer control is a mod idea. */
typedef enum {
    SPEED_PAUSED = 0,
    SPEED_NORMAL = 1,
    SPEED_FAST   = 2,
    SPEED_TURBO  = 3,
} GameSpeed;

/* Tenant occupancy state — lifecycle of a placed unit */
typedef enum {
    TENANT_EMPTY = 0,       /* Just placed, no occupants yet */
    TENANT_CONSTRUCTION,    /* Under construction (construction ticks remaining) */
    TENANT_MOVING_IN,       /* People moving in (brief) */
    TENANT_OCCUPIED,        /* Active and generating income/population */
    TENANT_CLOSING,         /* End of day, closing up */
    TENANT_VACANT,          /* Temporarily empty (night for offices, day for hotels) */
    TENANT_STRESSED,        /* RETIRED (legacy-stress referee 2026-08-02:
                             * no such machine in the EXE). Never entered;
                             * kept so saved state values keep meaning —
                             * game_load migrates it to OCCUPIED. */
    TENANT_ABANDONED,       /* Left the tower (stress too high) */
} TenantState;

/* Capacity byte values (from TenantMake decompilation).
 * These are sprite frame selectors, NOT people counts.
 * Offices: 0x00 (empty) → 0x10 → 0x18 → 0x20 → 0x28 → 0x30 → 0x38 → 0x40 (full)
 * Hotels:  reverse cycle (fill at night, empty by day)
 * Step size = 8 (0x08), range = 0x00 to 0x40 */
#define CAP_EMPTY   0x00
#define CAP_MIN     0x10   /* First occupied frame */
#define CAP_STEP    0x08   /* Step between frames */
#define CAP_MAX     0x40   /* Fully occupied */

/* Persistent occupancy tiers (cap_peak). The original packs a persistent
 * tier + a daily oscillation into the one capacity byte: a low-tier office
 * cycles 0x10..0x20, a mid-tier 0x28..0x30, a thriving one 0x38..0x40. The
 * tier is the persistent "how successful is this office" level; the ±0x08
 * within it is just the day/night fill. cap_peak stores that tier's top. */
#define CAP_PEAK_LOW         0x20   /* new / modest office */
#define CAP_PEAK_MID         0x30   /* established */
#define CAP_PEAK_HIGH_CAPPED 0x38   /* office top tier when below 4★ (MakeTenant star cap) */
#define CAP_PEAK_HIGH        0x40   /* thriving / forced-up / gentrified (0x38 if <4★) */

/* The three persistent occupancy tiers a cap_peak can sit in. The top tier is
 * reached at either 0x38 (offices capped below 4★) or 0x40 (4★+ / unmanaged
 * full) — both read as HIGH. Used for rendering (window variant) and the debug
 * label; the exact numeric peak still drives income/headcount scaling. */
typedef enum {
    OCC_TIER_LOW = 0,   /* new / modest      (peak < CAP_PEAK_MID) */
    OCC_TIER_MID,       /* established       (CAP_PEAK_MID..0x37)  */
    OCC_TIER_HIGH,      /* thriving / exec   (peak >= CAP_PEAK_HIGH_CAPPED) */
} OccupancyTier;

static inline OccupancyTier occupancy_tier(uint8_t cap_peak) {
    if (cap_peak >= CAP_PEAK_HIGH_CAPPED) return OCC_TIER_HIGH;
    if (cap_peak >= CAP_PEAK_MID)         return OCC_TIER_MID;
    return OCC_TIER_LOW;
}

static inline const char *occupancy_tier_name(uint8_t cap_peak) {
    switch (occupancy_tier(cap_peak)) {
    case OCC_TIER_HIGH: return "High";
    case OCC_TIER_MID:  return "Mid";
    default:            return "Low";
    }
}

/* The tier top for an arbitrary capacity byte — used to reconstruct cap_peak
 * from imported .TDT towers, where the byte carries the combined value. */
static inline uint8_t cap_tier_top(uint8_t cap) {
    if (cap <= CAP_PEAK_LOW) return CAP_PEAK_LOW;
    if (cap <= CAP_PEAK_MID) return CAP_PEAK_MID;
    return CAP_PEAK_HIGH;
}

/* The daily floor the live capacity falls to beneath a given peak: low-tier
 * offices bottom out at 0x10 (DayStartUpdate), higher tiers one step below
 * their peak (0x28 under 0x30, 0x38 under 0x40) — matches TenantMake. */
static inline uint8_t cap_daily_floor(uint8_t peak) {
    return (peak <= CAP_PEAK_LOW) ? CAP_MIN : (uint8_t)(peak - CAP_STEP);
}

/* The baseline cap_peak a freshly-built unit of this type sits at — the
 * denominator for scaling income/occupancy as cap_peak grows above it.
 * 0 = type isn't occupancy-scaled. (Hotels: their star<4 floor; growth above
 * it comes from the room upgrade.) */
static inline uint8_t cap_base_peak(ItemType type) {
    switch (type) {
    case ITEM_OFFICE:                              return CAP_PEAK_LOW;  /* 0x20 */
    case ITEM_HOTEL_SINGLE: case ITEM_HOTEL_TWIN:  return 0x10;
    case ITEM_HOTEL_SUITE:                         return 0x18;
    default:                                        return 0;
    }
}

/* Convert capacity byte to sprite frame index (0-based) */
static inline int capacity_to_frame(uint8_t cap) {
    if (cap < CAP_MIN) return 0;
    return (cap - CAP_MIN) / CAP_STEP;  /* 0x10→0, 0x18→1, 0x20→2, 0x28→3, 0x30→4, 0x38→5, 0x40→6 */
}

/* Live-occupancy byte transitions: one daily animation step toward a target,
 * clamped. The byte only ever holds multiples of CAP_STEP, so these are the
 * exact `+= / -= CAP_STEP` arithmetic the day/night fill curve always used —
 * named so the update logic reads as prose. */
static inline uint8_t cap_step_up_to(uint8_t cap, uint8_t ceil) {
    if (cap < ceil) cap += CAP_STEP;
    if (cap > ceil) cap = ceil;
    return cap;
}
static inline uint8_t cap_step_down_to(uint8_t cap, uint8_t floor) {
    return (cap > floor) ? (uint8_t)(cap - CAP_STEP) : cap;
}
/* Drain one step toward empty regardless of tier floor (an unreachable venue
 * with no patrons): step down until the last occupied frame, then go vacant. */
static inline uint8_t cap_drain_step(uint8_t cap) {
    return (cap > CAP_STEP) ? (uint8_t)(cap - CAP_STEP) : CAP_EMPTY;
}

/* Construction times from TenantMake (GetConstructionTime) */
static const int CONSTRUCTION_TIME[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 0,          /* Instant */
    [ITEM_FLOOR] = 0,          /* Instant */
    [ITEM_OFFICE] = 2,
    [ITEM_CONDO] = 3,
    [ITEM_HOTEL_SINGLE] = 56,
    [ITEM_HOTEL_TWIN] = 56,
    [ITEM_HOTEL_SUITE] = 56,
    [ITEM_RESTAURANT] = 48,
    [ITEM_FAST_FOOD] = 48,
    [ITEM_SHOP] = 48,
    [ITEM_CINEMA] = 56,
    [ITEM_PARTY_HALL] = 48,
    [ITEM_METRO] = 56,
    [ITEM_PARKING] = 8,
    [ITEM_CATHEDRAL] = 240,    /* Takes FOREVER */
    [ITEM_MEDICAL] = 56,
    [ITEM_SECURITY] = 40,
    [ITEM_RECYCLING] = 56,
    [ITEM_STAIRS] = 40,
    [ITEM_ESCALATOR] = 56,
    [ITEM_ELEVATOR_SHAFT] = 8,
    [ITEM_ELEVATOR_SERVICE] = 8,
    [ITEM_ELEVATOR_EXPRESS] = 8,
    [ITEM_HOUSEKEEPING] = 40,
};

/* Promotion flags — from decompiled seg_1148 (offsets 0xB922-0xB92D).
 * Flag bank byte-verified 2026-07-09 (decomp referee_b924_b92c report):
 * the old field comments here had the globals scrambled. */
typedef struct {
    int has_security;        /* 0xB92A — security office built */
    int has_suite;           /* 0xB92B — a hotel suite exists (VIP prerequisite) */
    int recycling_adequate;  /* 0xB92C — TrashT: centers keep up with population
                              * (>= 2500 pop per center -> inadequate, trucks
                              * stop, star 4/5 blocked). Dynamic, can flip back. */
    int has_metro;           /* 0xB3E8 >= 0 — metro station built */
    int medical_adequate;    /* 0xB92D — armed =1 every 7AM at star>=3
                              * (MedicalDailyTick 1170:011f tail), cleared
                              * ONLY when a sick worker finds no center
                              * (their 15-floor band + band-0 fallback both
                              * empty, or the assigned center was demolished
                              * — MoreMedicalPlease 1170:061c). Patient
                              * overflow never clears it. Required 3->4 AND
                              * 4->5. [byte-verified 2026-07-10 referee] */
    int vip_visited;         /* 0xB923 — VIP verdict favorable (gates 3->4 ONLY) */
    int has_cathedral;       /* cathedral built (the TOWER wedding venue) */
} PromotionFlags;

/* Star rating thresholds — from decompiled seg_1140 (LevelT)
 * Population at 0xB8C6, thresholds at 0xDDBC-0xDDC8 */
static const int STAR_POP_THRESHOLD[] = {
    0,      /* ★ (start) */
    300,    /* ★★ — 300 population */
    1000,   /* ★★★ — 1,000 population */
    5000,   /* ★★★★ — 5,000 population */
    10000,  /* ★★★★★ — 10,000 population */
    15000,  /* TOWER — 15,000 (HARDCODED in original, not in threshold array!) */
};

/* Income per tenant type per quarter (approximated from game mechanics) */
/* --- Rent income by rate class — THE REAL TABLE (EXE resource 0x3E9) ---
 * 0x200 bytes at file 0x26dc00, BE u32 x$100, indexed
 * [item*0x10 + rate_class*4] (MoneyT 1178:0854/08ec). Dumped 2026-07-10;
 * the office row 150/100/50/20 matches the MoneyT decode's citation, and
 * the flat-array neighbors (0x3E8 build costs, 0x3EA maintenance) both
 * cross-check against known values. Only RENT tenants have rows —
 * restaurants/fast food/cinema/party hall earn patron sales instead
 * (VenueT + the tuning sales ladders), which the port approximates below.
 * Condos are the one-time SALE price by class, banked at move-in. */
static const int TENANT_RENT_BY_CLASS[][4] = {
    /* rate class:                 0 High     1 Avg      2 Low   3 V.Low */
    [ITEM_OFFICE]       = {       15000,     10000,      5000,     2000 },
    [ITEM_HOTEL_SINGLE] = {        3000,      2000,      1500,      500 },
    [ITEM_HOTEL_TWIN]   = {        4500,      3000,      2000,      800 },
    [ITEM_HOTEL_SUITE]  = {        9000,      6000,      4000,     1500 },
    [ITEM_SHOP]         = {       20000,     15000,     10000,     4000 },
    [ITEM_CONDO]        = {      200000,    150000,    100000,    40000 },
    [ITEM_HOUSEKEEPING] = { 0 },  /* pad the designated array to full size */
};

/* Venue income approximations, per quarter — NOT rent: these types earn
 * per-patron sales in the EXE (no 0x3E9 row), a system the port hasn't
 * built yet. Values are the port's stand-ins pending the patron-AI work. */
static const int TENANT_INCOME[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 0,
    [ITEM_FLOOR] = 0,
    [ITEM_OFFICE] = 0,          /* real rent: TENANT_RENT_BY_CLASS */
    [ITEM_CONDO] = 0,           /* real one-time sale: TENANT_RENT_BY_CLASS */
    [ITEM_HOTEL_SINGLE] = 0,    /* real rent: TENANT_RENT_BY_CLASS */
    [ITEM_HOTEL_TWIN] = 0,
    [ITEM_HOTEL_SUITE] = 0,
    [ITEM_RESTAURANT] = 0,      /* real: nightly patron-sales settle (11PM) */
    [ITEM_FAST_FOOD] = 0,       /* real: nightly patron-sales settle (9PM) */
    [ITEM_SHOP] = 0,            /* real rent: TENANT_RENT_BY_CLASS */
    [ITEM_CINEMA] = 0,          /* real: tiered show income (VenueT) */
    [ITEM_PARTY_HALL] = 0,      /* real: tiered event income (VenueT) */
    [ITEM_METRO] = 0,           /* Service, no direct income */
    [ITEM_PARKING] = 0,         /* $0 PROVEN (2026-07-11 referee: no 0x3E9
                                   row, no cash-writer anywhere in ParkingT
                                   — parking is a pure cost center + the
                                   lobby-bypass traffic valve) */
    [ITEM_CATHEDRAL] = 0,
    [ITEM_MEDICAL] = 0,         /* Service */
    [ITEM_SECURITY] = 0,        /* Service */
    [ITEM_RECYCLING] = 0,       /* Service */
    [ITEM_STAIRS] = 0,
    [ITEM_ESCALATOR] = 0,
    [ITEM_ELEVATOR_SHAFT] = 0,
    [ITEM_ELEVATOR_SERVICE] = 0,
    [ITEM_ELEVATOR_EXPRESS] = 0,
    [ITEM_HOUSEKEEPING] = 0,    /* Service, no direct income */
};

/* The rent a tenant pays at its current rate class (0 for venue/service
 * types — they use TENANT_INCOME above). VALUES are byte-real; the payout
 * CADENCE is the port's quarter — the EXE-side consumers (MoneyT
 * 1178:0854/08ec) have an undecoded call schedule, queued as a decomp
 * loose end. */
/* The EXE weekend flag [0xB3A0]: every 3rd day is the weekend. */
static inline int game_is_weekend(const Tower *t) { return t->day % 3 == 2; }

static inline int tenant_rent(const ItemType type, int rent_class)
{
    if (type >= (int)(sizeof TENANT_RENT_BY_CLASS / sizeof TENANT_RENT_BY_CLASS[0]))
        return 0;
    if (rent_class < 0) rent_class = 0;
    if (rent_class > 3) rent_class = 3;
    return TENANT_RENT_BY_CLASS[type][rent_class];
}

/* Population per tenant type when occupied */
static const int TENANT_POPULATION[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 0,
    [ITEM_FLOOR] = 0,
    [ITEM_OFFICE] = 6,          /* ~6 workers per office */
    [ITEM_CONDO] = 3,           /* family of 3 — EXE spawns members 0/1/2
                                 * per condo (condo-economy referee
                                 * 2026-08-01; the old 4 overcounted pop) */
    [ITEM_HOTEL_SINGLE] = 1,
    [ITEM_HOTEL_TWIN] = 2,
    [ITEM_HOTEL_SUITE] = 3,
    [ITEM_RESTAURANT] = 10,     /* Staff + diners at peak */
    [ITEM_FAST_FOOD] = 5,
    [ITEM_SHOP] = 3,
    [ITEM_CINEMA] = 30,
    [ITEM_PARTY_HALL] = 20,
    [ITEM_METRO] = 0,
    [ITEM_PARKING] = 0,
    [ITEM_CATHEDRAL] = 5,
    [ITEM_MEDICAL] = 10,
    [ITEM_SECURITY] = 2,
    [ITEM_RECYCLING] = 3,
    [ITEM_STAIRS] = 0,
    [ITEM_ESCALATOR] = 0,
    [ITEM_ELEVATOR_SHAFT] = 0,
    [ITEM_ELEVATOR_SERVICE] = 0,
    [ITEM_ELEVATOR_EXPRESS] = 0,
    [ITEM_HOUSEKEEPING] = 3,    /* Cleaning staff on shift */
};

/* Which times of day each tenant type is active */
static const int TENANT_ACTIVE_TIMES[][TOD_COUNT] = {
    /* NONE */           {0, 0, 0, 0, 0},
    /* LOBBY */          {1, 1, 1, 1, 1},
    /* FLOOR */          {0, 0, 0, 0, 0},
    /* OFFICE */         {0, 1, 1, 1, 0},  /* arrive ~9AM, leave 5-9PM
                                            * (status-5 arm 1220:235f) —
                                            * evening presence is real;
                                            * weekday gate in game.c */
    /* CONDO */          {1, 1, 1, 1, 1},  /* Always occupied */
    /* HOTEL_SINGLE */   {1, 0, 0, 1, 1},  /* Evening → morning */
    /* HOTEL_TWIN */     {1, 0, 0, 1, 1},
    /* HOTEL_SUITE */    {1, 0, 0, 1, 1},
    /* RESTAURANT */     {0, 0, 0, 1, 1},  /* real hours 5-11PM — dinner
                                            * venue, NOT lunch (economy
                                            * referee 2026-08-02) */
    /* FAST_FOOD */      {0, 1, 1, 1, 0},
    /* SHOP */           {0, 1, 1, 1, 0},
    /* CINEMA */         {0, 0, 1, 1, 0},  /* Afternoon + evening */
    /* PARTY_HALL */     {0, 0, 1, 0, 0},  /* 1-5PM afternoon event,
                                            * income at the 5PM close
                                            * (economy referee) */
    /* METRO */          {0, 1, 1, 0, 0},  /* 10AM-5PM (economy referee;
                                            * pre-10AM commuter injection
                                            * is a separate mechanic) */
    /* PARKING */        {1, 1, 1, 1, 0},
    /* CATHEDRAL */      {0, 1, 1, 0, 0},
    /* MEDICAL */        {1, 1, 1, 1, 1},  /* 24/7 */
    /* SECURITY */       {1, 1, 1, 1, 1},  /* 24/7 */
    /* RECYCLING */      {0, 1, 1, 0, 0},
    /* STAIRS */         {0, 0, 0, 0, 0},
    /* ESCALATOR */      {0, 0, 0, 0, 0},
    /* ELEVATOR */       {0, 0, 0, 0, 0},
    /* ELEV_SERVICE */   {0, 0, 0, 0, 0},
    /* ELEV_EXPRESS */   {0, 0, 0, 0, 0},
    /* HOUSEKEEPING */   {0, 1, 1, 0, 0},  /* Cleans after morning checkout */
};

/* --- Zone system (from JudgeT seg_11a8) ---
 * Tower divided into 7 vertical zones of 15 floors each.
 * Commercial tenants within same zone compete for customers.
 * Too many restaurants/shops in one zone = stress. */
#define NUM_ZONES       7
#define FLOORS_PER_ZONE 15

/* (ZONE_MAX_OFFICES is gone: no zone office-count stressor exists in the
 * EXE — zones are 15-floor commercial routing bands only. Legacy-stress
 * referee 2026-08-02.) */

static inline int floor_to_zone(int floor) {
    if (floor < 0) return 0;
    int z = floor / FLOORS_PER_ZONE;
    return (z >= NUM_ZONES) ? NUM_ZONES - 1 : z;
}

/* DEAD since 2026-08-02 (zone stressor deleted) — the type and the
 * GameSim.zones field below stay only to freeze the .sav layout; remove
 * both at the next save-version bump. */
typedef struct {
    int restaurant_count;
    int fastfood_count;
    int shop_count;
    int office_count;
    int total_commercial;  /* Sum of above */
} ZoneData;

/* --- Disasters (EventT seg_10c8 + FireT seg_10e8) ---
 * SCHEDULED, not random (byte-verified 2026-07-09/10 referees + the TimeT
 * dispatcher map): TimeT's 10:00 AM block starts a fire every 84th day
 * ("a fire every 7 game-years", day % 84 == 83) and offers a bomb threat
 * every 60th day (day % 60 == 59). Both require a security office — a
 * tower without one never sees either disaster.
 *
 * FIRE (StartFire 10e8:0029): also needs star > 2 and NO cathedral
 * ([0xB3EC] < 0) — building the cathedral retires fires for good. The
 * origin floor is uniform in [first floor above the ground lobby .. top
 * built floor]; the floor's tenant extent must be at least 32 cells wide,
 * and the fire ignites 32 cells in from its right edge. It burns as
 * per-floor left/right FRONTS: each front destroys the cell it stands on
 * and advances 1 cell every 7 EXE frames (tuning 0xDDD0); each floor
 * above the origin ignites at the same cell 80 EXE frames later (0xDDD2)
 * — fire never spreads DOWN. A front dies at the floor-extent edge; the
 * fire ends when no fronts remain, or hard-stops at 9:00 PM (EXE frame
 * 2000). The player's one lever is the helicopter offer (10e8:0147,
 * shown right after ignition): pay $500,000 (tuning 0xDE14) and a chopper
 * sweeps right-to-left from the origin floor's right edge, 1 cell per EXE
 * frame (0xDDD4), dousing every front to its right. (The EXE also has a
 * "fire department" branch keyed on global 0xB3EA, but nothing in the
 * binary ever builds one — the facility was cut, so that branch is dead.)
 *
 * BOMB (TryStartEvent 10c8:006e): only at star 2/3/4 — the ransom switch
 * has no case for other stars, so 5-star towers never get bomb threats.
 * Ransom $200k/$300k/$1M by star (tuning 0xDE1C/1E/20). Pay: threat ends.
 * Refuse: the bomb arms and detonates at 1:00 PM (EXE frame 0x4B0)
 * unless security reaches it first. The blast destroys everything
 * touching floors [target-2 .. target+3] × cells [target-20 .. target+20]
 * (DestroyTenants 10c8:02bd). Detonation charges no cash — the loss is
 * the buildings themselves. */
typedef enum {
    EVENT_NONE = 0,
    EVENT_BOMB,        /* Bomb threat — pay it off, or guards race the clock */
    EVENT_FIRE,        /* Fire — advancing fronts, helicopters for hire */
} EventType;

/* Blast box from DestroyTenants (10c8:02bd) */
#define BOMB_BLAST_FLOORS_DOWN  2    /* floors [target-2 .. target+3] */
#define BOMB_BLAST_FLOORS_UP    3
#define BOMB_BLAST_HALF_CELLS   20   /* cells  [target-20 .. target+20] */

/* Fire pacing — the EXE's own tuning values, in EXE frame units */
#define FIRE_FRONT_CELLS    12   /* a flame front is 12 cells (one 96px strip) */
#define FIRE_SPREAD_FRAMES  7    /* front advances 1 cell / 7 frames (0xDDD0) */
#define FIRE_FLOOR_FRAMES   80   /* next floor up ignites +80 frames (0xDDD2) */
#define FIRE_END_FRAME      1760 /* ignition (10AM, ft 240) -> hard stop at ft 2000 = 9PM */
#define FIRE_CHOPPER_COST   500000  /* helicopter response (0xDE14 = 5000, x$100) */

/* --- The bomb hunt (GuardT seg_10f8, byte-verified 2026-07-11) ---
 * Fully deterministic — no dice anywhere; only the bomb's location is
 * random. Every security office (max 10) fields 6 guards who already
 * exist as staff: on arming, guards 0-2 take the office's floor and 3-5
 * the floor below, each office expanding its own search bubble outward,
 * floor by floor. Guards don't use elevators — floor changes are a
 * teleport with a 3-frames-per-floor transit delay (invisible in
 * transit), landing at the floor's right extent - 2 and sweeping
 * right-to-left at 1 cell per 2 EXE frames. Stepping onto the exact bomb
 * cell = caught (CheckEventTarget 10c8:01c4 is exact equality). The
 * normal people simulation freezes for the duration. Guards do NOT know
 * the bomb's floor (the seeded-at-the-bomb path is dead 0xB3EA code) —
 * placement and office count matter strictly and deterministically. */
#define GUARDS_PER_OFFICE   6
#define GUARD_OFFICES_MAX   10
#define GUARD_SWEEP_FRAMES  2    /* 1 cell / 2 EXE frames (linger 0xDDCC=1) */
#define GUARD_FLOOR_FRAMES  3    /* transit frames per floor (0xDDCE=3) */

typedef struct {
    int8_t  floor;      /* current floor */
    int16_t x;          /* current cell (sweeping right->left) */
    int16_t transit;    /* EXE frames until materialize; invisible if > 0 */
    int8_t  pause;      /* frames until the next 1-cell step */
    int8_t  below;      /* started below the office: expands downward first */
    int8_t  retired;    /* both frontier directions exhausted */
} GuardState;

typedef struct {
    uint16_t   office;        /* tenant id of the security office */
    int8_t     office_floor;
    int8_t     claimed_up;    /* highest floor already assigned */
    int8_t     claimed_down;  /* lowest floor already assigned */
    GuardState g[GUARDS_PER_OFFICE];
} GuardOffice;

typedef struct {
    int         active;
    int         frame_accum;  /* EXE-frame pacing, shared with the clock */
    int         noffices;
    GuardOffice o[GUARD_OFFICES_MAX];
} GuardHunt;

typedef struct {
    EventType type;
    int       active;
    int       pending;         /* modal open: fire = helicopter offer, bomb = pay/deploy.
                                  The EXE dialogs are modal, so nothing advances
                                  until the player answers. */
    int       target_floor;    /* fire origin floor / bomb floor */
    int       target_slot;     /* fire origin cell / bomb cell */
    int       caught;          /* bomb: neutralized (guard find or ransom paid) */
    int       damage_cost;     /* $ of buildings destroyed (informational —
                                  the EXE never charges cash for the damage) */
    int       ransom_cost;     /* bomb pay-off by star / fire helicopter fee */
    /* Fire: per-floor advancing fronts, paced in EXE frame units.
     * TimeT's clock is non-uniform: 320 frames/game-hour from 10AM-1PM
     * (ft 240->1200), then 100/hour to 9PM — so the fire visibly races
     * before lunch and crawls after. fire_accum carries the remainder
     * (one frame per ticks-per-hour of accumulated frames-per-hour). */
    int       fire_frame;      /* EXE frames since ignition; >= 1760 = 9PM stop */
    int       fire_accum;
    int16_t   fire_left[TOWER_FLOOR_COUNT];   /* leftmost burning cell, -1 = none */
    int16_t   fire_right[TOWER_FLOOR_COUNT];  /* right front cell (flames span +12) */
    int       chopper_x;       /* > 0: helicopter at this cell, flying left */
    GuardHunt hunt;            /* the deterministic guard sweep (bomb only) */
} EventState;

/* --- Santa system (from SantaT seg_11b8) ---
 * Santa flies diagonally across the sky: x -= 10, y += 1 per tick.
 * Triggered on certain days or as Easter egg. */
typedef struct {
    int  active;    /* 0 = off, 1 = flying */
    int  x;         /* Pixel position (decreases) */
    int  y;         /* Pixel position (increases) */
} SantaState;

/* --- Analytics: per-quarter time series for the stats window ---
 * One sample at the end of every quarter (4/day). 1024 samples = 256
 * game days of history in a ring buffer. */
#define STATS_MAX 1024

typedef struct {
    int32_t day;            /* game day of this sample */
    int8_t  quarter;        /* 0-3 */
    int8_t  star;
    int32_t population;
    int32_t commuters;      /* live person entities */
    int32_t avg_wait;       /* avg banked elevator wait this quarter */
    int64_t balance;
    int64_t income;         /* that quarter's income */
    int64_t expenses;
    int64_t built_value;    /* construction value standing */
    int64_t lost_value;     /* cumulative bulldozed value */
} StatSample;

typedef struct {
    StatSample s[STATS_MAX];
    int count;              /* samples stored (<= STATS_MAX) */
    int head;               /* oldest sample index once full */
} StatsHistory;

/* i-th sample, oldest first (0 .. count-1) */
static inline const StatSample *stats_at(const StatsHistory *h, int i)
{
    return &h->s[(h->head + i) % STATS_MAX];
}

#define MODE_CAMPAIGN  0   /* star-gated unlocks, starts on an empty lot */
#define MODE_SANDBOX   1   /* everything unlocked from the start */

/* In-tenant occupant sprites (AnimPeple seg_1028, dispatch + randomizers
 * byte-verified 2026-07-02). Each occupied unit shows a few little people
 * whose position/pose re-roll every 16 frames — desk-sitters and roamers
 * in offices, residents (and a dog) in condos, guests in hotel rooms,
 * counter staff in food venues, one worker on every construction site.
 * Frame codes index the type-8 people band: DIB resources 0x85E8..0x85EE
 * concatenated in id order (office 0x00-0x1D, condo/home 0x1E-0x38,
 * construction 0x39-0x3E, fast food 0x3F-0x48, restaurant 0x49-0x4F,
 * hotel 0x50-0x5F + 0x60-0x62) — every randomizer's range lands exactly
 * inside its resource, which is how the band order was verified. */
#define OCCUPANTS_MAX 6
typedef struct {
    uint8_t count;                  /* occupants rolled for this tenant */
    uint8_t x[OCCUPANTS_MAX];       /* cells from the tenant's left edge */
    uint8_t frame[OCCUPANTS_MAX];   /* people-band frame code 0x00-0x62 */
} TenantOccupants;

/* The simulation state */
typedef struct {
    /* Time */
    int           tick;             /* Current tick within quarter */
    int           ticks_per_quarter;/* ~360 at normal speed */
    TimeOfDay     time_of_day;
    Quarter       quarter;
    GameSpeed     speed;
    
    /* Derived time info */
    int           hour;             /* 0-23 */
    int           minute;           /* 0-59 */
    
    /* Star rating */
    PromotionFlags promo;

    /* Game mode: 0 = Campaign (star-gated unlocks), 1 = Sandbox (all unlocked) */
    int           mode;

    /* --- TOWER wedding ceremony (ChurchT: OpenChurch/StartMarry/
     * CheckMarry) --- The 5-star -> TOWER promotion is a special event,
     * not the daily star check (LevelUp 5->T always returns 0): once a
     * 5-star tower reaches 15,000 population with a cathedral built and
     * a satisfied VIP visit, the wedding is held the next day. While
     * active the cathedral shows the ceremony art (cherubs + "Welcome
     * to Tower" banner) and the procession walks the entrance floor;
     * at the following dawn the tower is crowned TOWER (star 6). */
    struct {
        int active;          /* ceremony running today */
        int done;            /* TOWER awarded, never repeats */
        int day;             /* day the ceremony ran */
    } wedding;
    int           pending_star_up;  /* Star level pending promotion animation */
    
    /* Finance */
    long          income_this_quarter;
    long          expenses_this_quarter;
    long          total_income;
    long          total_expenses;
    /* Per-day rollup (the 4 quarters of a day, for daily stats) */
    long          day_income;        /* accumulating across today's quarters */
    long          day_expenses;
    long          last_day_income;   /* the previous full day's totals */
    long          last_day_expenses;
    int           last_day_num;      /* which day last_day_* summarizes (0 = none yet) */
    int           cash_pending;      /* queued cha-chings from the rent settlement,
                                        drained over frames so a rich morning rings
                                        a run of them (the EXE's income-refresh chime) */
    int           cash_snd_timer;    /* ticks until the next queued cha-ching */

    /* --- Quarterly finance breakdown for the CountT report (bitmap 0x81f4) ---
     * Two category lists, exactly as the report's baked art: a REVENUE list
     * (population + income per category) and an INFRASTRUCTURE list (maintenance
     * expense per category). These are write-only accumulators mirrored next to
     * every sim income/expense bank, so their sums equal the real flows. They
     * reset each 3-day settlement quarter (the EXE's financial cycle), NOT the
     * intra-day income_this_quarter. Population is counted on demand from live
     * tenants — the EXE never resets it either. */
    long          fin_income_q[FIN_INCOME_CATS];    /* rent/venue income this quarter */
    long          fin_expense_q[FIN_EXPENSE_CATS];  /* maintenance this quarter */
    long          fin_other_income_q;   /* non-rent income (star bonuses, parking) */
    long          fin_construction_q;   /* non-build capital costs this quarter, e.g. condo buyback */
    long          fin_built_at_q_start; /* tower->built_value snapshot at quarter start;
                                           build spend this quarter = built_value - this */
    long          fin_last_balance;     /* money at the start of this quarter */
    
    /* Stats */
    int           max_population;   /* Peak population reached */
    int           medical_adequate; /* persistent 0xB92D state (see
                                       PromotionFlags.medical_adequate, which
                                       mirrors this each scan) */
    int           medical_nag;      /* one-shot for the UI feed: a sick worker
                                       found no medical center */
    int           standing_population; /* time-of-day-independent count:
                                          everyone who lives/works here (hotel
                                          guests only while hosted) — what the
                                          EXE's population global measures and
                                          what star promotion reads */
    int           tenants_occupied; /* Currently occupied tenant count */
    int           tenants_total;    /* Total placed tenants */
    
    /* Tower width check — from seg_1148 FUN_1148_01a8
     * Width measured on underground floors; must be > star*25 */
    int           tower_width;      /* Measured underground width in cells */
    
    /* Animation */
    int           frame;            /* Global animation frame counter */
    
    /* Zone competition (from JudgeT) */
    ZoneData      zones[NUM_ZONES];
    
    /* Santa Easter egg */
    SantaState    santa;
    
    /* Random events */
    EventState    event;

    
    /* VIP system (from VipT seg_1240) */
    int           vip_visiting;    /* VIP currently in tower */
    int           vip_satisfied;   /* VIP was satisfied (for star promotion) */
    int           vip_last_day;    /* Last day VIP visited */
    int           vip_notice;      /* one-shot for UI: 1=arrived 2=satisfied 3=not */
    
    /* Day tracking for upgrade cadence (MainteT: 3-day minimum) */
    int           last_stress_day;

    /* Day the 5PM hotel pass (roach spread → neglect fuse → demand
     * arm/disarm) last ran. The EXE fires it from TimeT at frame_time
     * 0x640 = 17:00 sharp (calls 1130:01e2 then 1130:0109). */
    int           hotel_pass_day;

    /* Day the 10AM disaster dispatch (fire every 84th day, bomb offer
     * every 60th — TimeT ft 0xF0 block) last ran. */
    int           disaster_sched_day;

    /* Transport reachability (recomputed from the tower layout each tick).
     * public  = tenants/visitors commuting from the ground entrance
     * service = staff (housekeeping/security), may also use service elevators */
    uint8_t       reach_public[TOWER_FLOOR_COUNT];
    uint8_t       reach_service[TOWER_FLOOR_COUNT];
    int           unreachable_tenants; /* Units cut off from the entrance */
    int           dirty_rooms;         /* Hotel rooms waiting on housekeeping */

    /* People + elevator simulation (people.c) */
    PeopleSim     people;

    /* Analytics time series (sampled at each quarter end) */
    StatsHistory  stats;
    long          stats_prev_wait_total;   /* for per-quarter wait deltas */
    long          stats_prev_wait_samples;

    /* In-tenant occupant sprites (AnimPeple seg_1028) — the little
     * people inside offices, condos, hotel rooms, venue counters and
     * construction sites. Pure cosmetics, re-rolled every 16 frames
     * staggered by tenant index; carried in the save only because the
     * sim is dumped wholesale (stale values re-roll within a second). */
    TenantOccupants occupants[MAX_TENANTS];
} GameSim;

/* Initialize simulation */
void game_init(GameSim *sim);

/* Save/load the whole game state (versioned native format).
 * Returns 0 on success. */
int game_save(const GameSim *sim, const Tower *tower, const char *path);
int game_load(GameSim *sim, Tower *tower, const char *path);

/* Advance simulation by one frame. Call each frame (~60fps). */
void game_update(GameSim *sim, Tower *tower);

/* Check star rating based on population + requirements.
 * Ported from seg_1140 FUN_1140_0411 (star calculator). */
int game_check_star_rating(GameSim *sim, Tower *tower);

/* TOWER wedding daily step: run at each dawn (see GameSim.wedding) */
void game_wedding_daily(GameSim *sim, Tower *tower);

/* Check if promotion requirements are met for next star level.
 * Ported from seg_1148 FUN_1148_0000. */
int game_check_promotion(GameSim *sim, Tower *tower, int target_star);

/* Recalculate population from all tenants */
int game_calc_population(GameSim *sim, Tower *tower);

/* Recompute floor reachability (public + service networks) from transports */
void game_update_reachability(GameSim *sim, Tower *tower);

/* Measure tower width on underground floors (for promotion check) */
int game_measure_width(Tower *tower);

/* Get sky color tint for current time of day */
void game_sky_tint(GameSim *sim, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

/* Format time as HH:MM string */
void game_format_time(GameSim *sim, char *buf, int bufsize);

/* Get quarter name string */
const char *game_quarter_name(Quarter q);

/* Financial-report category mapping: tenant type -> a FINCAT_ / FINEXP_ index,
 * or -1 for types that don't belong to that list. */
int game_fin_income_cat(ItemType t);
int game_fin_expense_cat(ItemType t);

/* --- Zone system (JudgeT) --- */

/* Recalculate zone competition data from all tenants */

/* Apply zone-based stress to office overcrowding. (Retail zone stress
 * removed 2026-07-11: it was an invention — see game_retail_hourly.) */



/* The daily 5PM hotel pass, in the EXE's order: roach spread
 * (ExpandoBadHotel 1130:01e2), then the neglect fuse (HotelNeglectCheck
 * 1130:0e5c), then demand arm/disarm (JudgeAllHotel pass 2, 1130:0f57).
 * game_update calls this once per day at 17:00; exposed for tests. */
void game_hotel_demand_pass(GameSim *sim, Tower *tower);

/* The 3rd-day stressed move-out (JudgeT 1130:09e5): stressed offices,
 * condos and shops vacate — condos charge the rate-class buy-back — and
 * decline drags one content floor-mate to the middle band. */
void game_stressed_moveout(GameSim *sim, Tower *tower);
/* The occupancy lifecycle (2026-07-11 vacancy referee): the daily
 * 4:59AM category judge + vacant-unit re-arming, and the mover-arrival
 * consumer that executes a re-let. */
void game_judge_daily(GameSim *sim, Tower *tower);
void game_relet_arrivals(GameSim *sim, Tower *tower);
/* The info-dialog price control (InfoDlgT 1100:0bfe): write the rate
 * class and immediately re-judge the unit. Returns 1 if the change
 * re-armed a dark vacancy. */
int game_set_rent_class(GameSim *sim, Tower *tower, Tenant *t, int cls);

/* The live 0-300 "Eval" metric the info dialog draws as a gauge bar
 * (seg_1100:1fad) — the unit's banked felt-wait; higher = worse. */
int game_tenant_eval_metric(GameSim *sim, Tower *tower, const Tenant *t);

/* Up to `max` diagnostic comment lines for the info dialog, in the EXE's
 * fixed producer priority order (seg_1108). Returns the count written. */
int game_tenant_comments(GameSim *sim, Tower *tower, const Tenant *t,
                         char lines[][48], int max);

/* Route-loss guardrail (res 0x3ed; TransferT 11b0:0b8b / 11b0:0cfe).
 * Stop-toggle: returns the confirm-string number (1 key route / 2
 * housekeeping / 3 lobby route) or 0 for a silent toggle. Removal:
 * returns 1 when demolishing this shaft's segment at fidx would strand
 * the floor (confirm with string #5). */
int game_stop_route_loss(GameSim *sim, Tower *tower, int shaft, int fidx);
int game_remove_route_loss(GameSim *sim, Tower *tower, int shaft, int fidx);

/* Star-requirement nags (res 0x3f2): one-shot code for the UI feed —
 * 1 security, 2 suites, 3 recycling center, 4 recycling full, 5 parking.
 * Emitted at most once a day, from the hourly star eval (gate misses)
 * and the daily parking-quota check. */
int game_take_star_nag(void);
void game_parking_nag_daily(GameSim *sim, Tower *tower);

/* Person-name purge (NameT): hotels=1 drops hotel-guest names (4PM),
 * visitors=1 drops venue-visitor names (day boundary); dead-tenant
 * names always drop. */
void game_purge_person_names(Tower *tower, int hotels, int visitors);

/* Parking usability (ParkingT CheckAllParking): mark each space usable
 * iff its floor's ramp chains from B1 (vertical same-x stack) and no
 * >=4-cell bare gap cuts the floor. Runs daily at 7AM + on build/
 * demolish/import. */
void game_parking_recompute(GameSim *sim, Tower *tower);

/* The persistent peak a freshly-built unit of this type starts at, by star
 * level (TenantMake MakeTenant). 0 = not peak-managed. */
uint8_t game_init_cap_peak(ItemType type, int star);

/* --- Disasters (EventT + FireT) --- */

/* TimeT's 10:00 AM dispatch: a fire every 84th day, a bomb threat every
 * 60th (fire first — an active fire blocks the bomb offer, like the EXE's
 * game-flags gate). Called once per day when the clock reaches 10AM. */
void game_schedule_disasters(GameSim *sim, Tower *tower);

/* StartFire (10e8:0029). forced_floor <= 0 = the EXE's uniform random pick
 * (disaster floors are always above the ground lobby, never basements);
 * otherwise ignite that floor (still subject to every gate — this mirrors
 * the EXE, whose debug-menu caller goes through the same checks). */
void game_start_fire(GameSim *sim, Tower *tower, int forced_floor);

/* TryStartEvent (10c8:006e): open the bomb-threat offer. forced_floor as above. */
void game_offer_bomb(GameSim *sim, Tower *tower, int forced_floor);

/* Advance the active disaster one tick (fire fronts + chopper, bomb clock).
 * No-op while `pending` — the EXE's dialogs are modal. */
void game_update_event(GameSim *sim, Tower *tower);

/* Bomb detonation (ResolveEvent(0) + DestroyTenants) */
void game_resolve_event(GameSim *sim, Tower *tower);

/* Player's answer to the pending modal.
 * proceed: the free path — let the fire burn / send guards after the bomb.
 * ransom:  the paid path — $500k helicopters (fire) / the star-scaled
 *          pay-off (bomb, no blast). */
void game_event_proceed(GameSim *sim, Tower *tower);
void game_event_ransom(GameSim *sim, Tower *tower);
/* Forward clock jump within the day (EXE EventCleanup: caught bomb ->
 * 4PM). Crossed quarter boundaries still close out their books. */
void game_clock_jump(GameSim *sim, Tower *tower, int hour);

/* Venues (VenueT): the hourly show-cycle step (reset/open/show/income)
 * and the attendance counter fed by the people sim's arrivals. */
void game_venue_hourly(GameSim *sim, Tower *tower);
void game_venue_arrivals(GameSim *sim, Tower *tower);
/* Change-movie dialog actions (InfoDlgT 1100:432f/4377): step to the
 * next hit ($300k) or ordinary ($150k) film, deterministically. */
void game_change_movie(GameSim *sim, Tower *tower, Tenant *t, int hit);
int  game_movie_quota(const Tenant *t);
int  game_venue_today_income(const Tenant *t);

/* Retail patron economy (Restaurant.c seg_11a8, byte-verified 2026-07-11):
 * open/close rows + the nightly tiered income settle; the door check and
 * departure hooks the people sim calls; the period class (0 weekday /
 * 1 weekend / 2 rainy-day) that picks score slot and quota cap. */
void game_retail_hourly(GameSim *sim, Tower *tower);
void game_retail_arrivals(GameSim *sim, Tower *tower);
int  game_retail_period(const GameSim *sim, const Tower *tower);
int  game_retail_customer_in(GameSim *sim, Tower *tower, Tenant *t,
                             int walkin, int felt_wait);
void game_retail_customer_out(Tenant *t);

/* Re-roll in-tenant occupant sprites (AnimPeple): every tenant refreshes
 * on a 16-frame cadence, staggered by index so the tower never twitches
 * in sync ((day_clock + person_ref) % 16 in the EXE). */
void game_animate_occupants(GameSim *sim, Tower *tower);

/* A sick office worker seeks a medical center (UniPeple 1220:2b55 medical
 * path). Returns 0 = none found (adequacy cleared + nag), 1 = turned away
 * (center full — silent), 2 = admitted. Exposed for tests; game_update
 * rolls it on 1-in-10 of office arrivals at star>=3. */
int game_medical_seek(GameSim *sim, Tower *tower, int from_floor);
/* As above, but reports the admitting center's floor (for the patient's
 * physical trip). */
int game_medical_seek_at(GameSim *sim, Tower *tower, int from_floor,
                         int *center_floor);

/* Stories of the ground lobby (0 if none, capped at 3) — drives the
 * grand-lobby wait-forgiveness bonus. */
int game_lobby_height(Tower *tower);

/* --- Santa Easter egg (SantaT) --- */

/* Launch Santa flying across the sky */
void game_launch_santa(GameSim *sim, int screen_w);

/* Update Santa position (call each tick) */
void game_update_santa(GameSim *sim);

#endif /* GAME_H */
