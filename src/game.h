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

/* Quarter names */
typedef enum {
    QUARTER_WEEKDAY1 = 0,
    QUARTER_WEEKDAY2,
    QUARTER_WEEKDAY3,
    QUARTER_WEEKEND,
    QUARTER_COUNT
} Quarter;

/* Game speed */
typedef enum {
    SPEED_PAUSED = 0,
    SPEED_NORMAL = 1,   /* Original: game_speed = 2 */
    SPEED_FAST   = 2,   /* Original: game_speed = 3 */
    SPEED_TURBO  = 3,   /* Not in original — our addition */
} GameSpeed;

/* Tenant occupancy state — lifecycle of a placed unit */
typedef enum {
    TENANT_EMPTY = 0,       /* Just placed, no occupants yet */
    TENANT_CONSTRUCTION,    /* Under construction (construction ticks remaining) */
    TENANT_MOVING_IN,       /* People moving in (brief) */
    TENANT_OCCUPIED,        /* Active and generating income/population */
    TENANT_CLOSING,         /* End of day, closing up */
    TENANT_VACANT,          /* Temporarily empty (night for offices, day for hotels) */
    TENANT_STRESSED,        /* High stress — may leave! */
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

/* Convert capacity byte to sprite frame index (0-based) */
static inline int capacity_to_frame(uint8_t cap) {
    if (cap < CAP_MIN) return 0;
    return (cap - CAP_MIN) / CAP_STEP;  /* 0x10→0, 0x18→1, 0x20→2, 0x28→3, 0x30→4, 0x38→5, 0x40→6 */
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

/* Promotion flags — from decompiled seg_1148 (offsets 0xB922-0xB92D) */
typedef struct {
    int has_security;       /* 0xB92A — security office built */
    int has_recycling;      /* 0xB92B — recycling center built */
    int has_metro;          /* 0xB92C — metro station built */
    int has_medical;        /* 0xB3E8 >= 0 — medical center built */
    int hotel_quarters;     /* 0xB3A1 — number of hotel quarters (suite count?) */
    int vip_visited;        /* 0xB92D — VIP has visited */
    int has_cathedral;      /* cathedral built (the TOWER wedding venue) */
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
static const int TENANT_INCOME[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 0,
    [ITEM_FLOOR] = 0,
    [ITEM_OFFICE] = 8000,       /* ~$8k/quarter when occupied */
    [ITEM_CONDO] = 0,           /* One-time sale, no recurring */
    [ITEM_HOTEL_SINGLE] = 5000, /* Per night (per day cycle) */
    [ITEM_HOTEL_TWIN] = 10000,
    [ITEM_HOTEL_SUITE] = 20000,
    [ITEM_RESTAURANT] = 15000,  /* Lunch + dinner revenue */
    [ITEM_FAST_FOOD] = 8000,
    [ITEM_SHOP] = 6000,
    [ITEM_CINEMA] = 25000,
    [ITEM_PARTY_HALL] = 10000,
    [ITEM_METRO] = 0,           /* Service, no direct income */
    [ITEM_PARKING] = 1000,
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

/* Population per tenant type when occupied */
static const int TENANT_POPULATION[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 0,
    [ITEM_FLOOR] = 0,
    [ITEM_OFFICE] = 6,          /* ~6 workers per office */
    [ITEM_CONDO] = 4,           /* Family of 4 */
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
    /* OFFICE */         {0, 1, 1, 0, 0},  /* Morning + afternoon only */
    /* CONDO */          {1, 1, 1, 1, 1},  /* Always occupied */
    /* HOTEL_SINGLE */   {1, 0, 0, 1, 1},  /* Evening → morning */
    /* HOTEL_TWIN */     {1, 0, 0, 1, 1},
    /* HOTEL_SUITE */    {1, 0, 0, 1, 1},
    /* RESTAURANT */     {0, 1, 1, 1, 0},  /* Lunch through evening */
    /* FAST_FOOD */      {0, 1, 1, 1, 0},
    /* SHOP */           {0, 1, 1, 1, 0},
    /* CINEMA */         {0, 0, 1, 1, 0},  /* Afternoon + evening */
    /* PARTY_HALL */     {0, 0, 0, 1, 1},  /* Evening + night */
    /* METRO */          {1, 1, 1, 1, 0},  /* Daytime */
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

/* Zone competition thresholds — how many of each type before stress */
#define ZONE_MAX_RESTAURANTS  2   /* > 2 restaurants in one zone = stress */
#define ZONE_MAX_FASTFOOD     3   /* > 3 fast food = stress */
#define ZONE_MAX_SHOPS        4   /* > 4 shops = stress */

static inline int floor_to_zone(int floor) {
    if (floor < 0) return 0;
    int z = floor / FLOORS_PER_ZONE;
    return (z >= NUM_ZONES) ? NUM_ZONES - 1 : z;
}

/* Zone data — tracked per zone */
typedef struct {
    int restaurant_count;
    int fastfood_count;
    int shop_count;
    int office_count;
    int total_commercial;  /* Sum of above */
} ZoneData;

/* --- Event system (from EventT seg_10c8 + FireT seg_10e8) ---
 * Random disasters: bomb threats and fires.
 * Bomb: security guards race to find it; explodes if they fail.
 * Fire: spreads left and right per tick; burns until extinguished.
 * Both destroy tenants in a blast/burn radius. */
typedef enum {
    EVENT_NONE = 0,
    EVENT_BOMB,        /* Bomb threat — guard must reach target in time */
    EVENT_FIRE,        /* Fire — spreads and burns */
} EventType;

/* Blast radius from EventT: 6 floors × 40 slots */
#define BOMB_BLAST_FLOORS  6
#define BOMB_BLAST_SLOTS   40

/* Fire spread rate: 2 slots per tick in each direction */
#define FIRE_SPREAD_RATE   2

typedef struct {
    EventType type;
    int       active;
    int       target_floor;
    int       target_slot;
    int       timer;           /* Ticks until resolution */
    int       duration;        /* Total event duration */
    int       fire_left;       /* Fire spread: leftmost burning slot */
    int       fire_right;      /* Fire spread: rightmost burning slot */
    int       caught;          /* Security guard caught the bomb? */
    int       damage_cost;     /* Total $ damage from event */
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
    
    /* Stats */
    int           max_population;   /* Peak population reached */
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
    
    /* Day tracking for upgrade cadence (MainteT: 3-day minimum) */
    int           last_stress_day;

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

/* --- Zone system (JudgeT) --- */

/* Recalculate zone competition data from all tenants */
void game_calc_zones(GameSim *sim, Tower *tower);

/* Apply zone-based stress to commercial tenants.
 * Too many competitors in same zone = stress accumulation. */
void game_judge_tenants(GameSim *sim, Tower *tower);

/* --- Events (EventT + FireT) --- */

/* Try to start a random event. Conditions from decompilation:
 * - Star > 2, security exists, daytime, no active event */
void game_try_event(GameSim *sim, Tower *tower);

/* Update active event (spread fire, count down bomb timer) */
void game_update_event(GameSim *sim, Tower *tower);

/* Resolve event: bomb explodes or fire extinguished */
void game_resolve_event(GameSim *sim, Tower *tower);

/* --- Santa Easter egg (SantaT) --- */

/* Launch Santa flying across the sky */
void game_launch_santa(GameSim *sim, int screen_w);

/* Update Santa position (call each tick) */
void game_update_santa(GameSim *sim);

#endif /* GAME_H */
