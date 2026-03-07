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
    TENANT_MOVING_IN,       /* People moving in (brief) */
    TENANT_OCCUPIED,        /* Active and generating income/population */
    TENANT_CLOSING,         /* End of day, closing up */
    TENANT_VACANT,          /* Temporarily empty (night for offices, day for hotels) */
    TENANT_STRESSED,        /* High stress — may leave! */
    TENANT_ABANDONED,       /* Left the tower (stress too high) */
} TenantState;

/* Promotion flags — from decompiled seg_1148 (offsets 0xB922-0xB92D) */
typedef struct {
    int has_security;       /* 0xB92A — security office built */
    int has_recycling;      /* 0xB92B — recycling center built */
    int has_metro;          /* 0xB92C — metro station built */
    int has_medical;        /* 0xB3E8 >= 0 — medical center built */
    int hotel_quarters;     /* 0xB3A1 — number of hotel quarters (suite count?) */
    int vip_visited;        /* 0xB92D — VIP has visited */
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
};

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
} GameSim;

/* Initialize simulation */
void game_init(GameSim *sim);

/* Advance simulation by one frame. Call each frame (~60fps). */
void game_update(GameSim *sim, Tower *tower);

/* Check star rating based on population + requirements.
 * Ported from seg_1140 FUN_1140_0411 (star calculator). */
int game_check_star_rating(GameSim *sim, Tower *tower);

/* Check if promotion requirements are met for next star level.
 * Ported from seg_1148 FUN_1148_0000. */
int game_check_promotion(GameSim *sim, Tower *tower, int target_star);

/* Recalculate population from all tenants */
int game_calc_population(GameSim *sim, Tower *tower);

/* Measure tower width on underground floors (for promotion check) */
int game_measure_width(Tower *tower);

/* Get sky color tint for current time of day */
void game_sky_tint(GameSim *sim, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);

/* Format time as HH:MM string */
void game_format_time(GameSim *sim, char *buf, int bufsize);

/* Get quarter name string */
const char *game_quarter_name(Quarter q);

#endif /* GAME_H */
