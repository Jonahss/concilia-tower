/* tower.h - Tower grid data structure
 *
 * The tower is a 2D grid of cells. Each cell is 8 pixels wide × 36 pixels tall
 * (matching the original game's coordinate system).
 *
 * Floors range from B9 (-9) to roof (+100 at 5 stars).
 * Width is 63 cells (504 pixels).
 * Floor 0 is the ground/lobby level.
 */
#ifndef TOWER_H
#define TOWER_H

#include <stdint.h>

/* Grid dimensions */
#define TOWER_WIDTH       375   /* Cells wide (375 from decompiled 0x177 lobby constant + online sources) */
#define TOWER_MIN_FLOOR   -9    /* B9 (deepest basement) */
#define TOWER_MAX_FLOOR   100   /* Maximum height at 5 stars */
#define TOWER_FLOOR_COUNT (TOWER_MAX_FLOOR - TOWER_MIN_FLOOR + 1)  /* 110 */
#define TOWER_LOBBY_FLOOR 0

/* Pixel dimensions per cell */
#define CELL_W  8
#define CELL_H  36     /* Total floor height: ceiling (12px) + tenant (24px) */
#define CEIL_H  12     /* Ceiling/roof strip height */
#define TENANT_H 24    /* Tenant content height (below ceiling) */

/* Tenant/item types (from the original game) */
typedef enum {
    ITEM_NONE = 0,
    ITEM_LOBBY,         /* Ground floor lobby (always present) */
    ITEM_FLOOR,         /* Empty floor (walkway) */
    ITEM_OFFICE,        /* Office: 9 cells wide, 1 floor */
    ITEM_CONDO,         /* Condo: 16 cells wide, 1 floor */
    ITEM_HOTEL_SINGLE,  /* Hotel single: 4 cells wide, 1 floor */
    ITEM_HOTEL_TWIN,    /* Hotel twin: 6 cells wide, 1 floor */
    ITEM_HOTEL_SUITE,   /* Hotel suite: 8 cells wide, 1 floor */
    ITEM_RESTAURANT,    /* Restaurant: 24 cells wide, 1 floor */
    ITEM_FAST_FOOD,     /* Fast food: 16 cells wide, 1 floor */
    ITEM_SHOP,          /* Shop: 8 cells wide, 1 floor */
    ITEM_CINEMA,        /* Cinema: 31 cells wide, 2 floors */
    ITEM_PARTY_HALL,    /* Party hall: 24 cells wide, 2 floors */
    ITEM_METRO,         /* Metro station: 30 cells wide, 3 floors, underground only */
    ITEM_PARKING,       /* Parking: 8 cells wide, underground only */
    ITEM_CATHEDRAL,     /* Cathedral: 16 cells wide, 2 floors */
    ITEM_MEDICAL,       /* Medical center: 6 cells wide, 1 floor */
    ITEM_SECURITY,      /* Security office: 6 cells wide, 1 floor */
    ITEM_RECYCLING,     /* Recycling center: 6 cells wide, 2 floors, underground */
    ITEM_STAIRS,        /* Stairs: 8 cells wide, connects 2 floors */
    ITEM_ESCALATOR,     /* Escalator: 8 cells wide, connects 2 floors */
    ITEM_ELEVATOR_SHAFT,/* Standard elevator shaft: 4 cells wide, variable height */
    ITEM_ELEVATOR_SERVICE, /* Service elevator: freight/staff transport */
    ITEM_ELEVATOR_EXPRESS, /* Express elevator: stops only on lobby/sky-lobby floors */
    ITEM_HOUSEKEEPING,  /* Housekeeping: services hotel rooms */
    ITEM_TYPE_COUNT
} ItemType;

/* Item widths in cells — CANONICAL from SimTower (GameFAQs BStuart guide + sprite analysis)
 * Each cell = 8px. Frame widths verified against sprite bitmap dimensions. */
static const int ITEM_WIDTH[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 4,        /* 4-cell segments (extends existing lobby) */
    [ITEM_FLOOR] = 63,
    [ITEM_OFFICE] = 9,       /* 72px — sprite 288/4 frames = 72px ✓ */
    [ITEM_CONDO] = 16,       /* 128px — sprite 128px ✓ */
    [ITEM_HOTEL_SINGLE] = 4, /* 32px — create(9*32) = 9 frames × 32px ✓ */
    [ITEM_HOTEL_TWIN] = 6,   /* 48px — create(9*48) = 9 frames × 48px ✓ */
    [ITEM_HOTEL_SUITE] = 10, /* 80px — sprite 720/9 = 80px per frame (was 8, FIXED) */
    [ITEM_RESTAURANT] = 24,  /* 192px — sprite 768/4 = 192px per frame ✓ */
    [ITEM_FAST_FOOD] = 16,   /* 128px — sprite 512/4 = 128px per frame ✓ */
    [ITEM_SHOP] = 12,        /* 96px — GameFAQs: "Retail Shop 12" (was 8, FIXED) */
    [ITEM_CINEMA] = 31,      /* 248px — OpenSkyscraper: int2(31,2) ✓ */
    [ITEM_PARTY_HALL] = 24,  /* 192px — sprite 576/3 = 192px per frame ✓ */
    [ITEM_METRO] = 30,       /* 240px — OpenSkyscraper: int2(30,3) ✓ */
    [ITEM_PARKING] = 4,      /* 32px — GameFAQs: "Parking 4" (was 8, FIXED) */
    [ITEM_CATHEDRAL] = 16,   /* 128px — only 1 allowed, floor 100 only */
    [ITEM_MEDICAL] = 26,     /* 208px — sprite 208px each, GameFAQs: "Medical 26" (was 6, FIXED) */
    [ITEM_SECURITY] = 16,    /* 128px — sprite 128px, GameFAQs: "Security 16" (was 6, FIXED) */
    [ITEM_RECYCLING] = 25,   /* 200px — sprite 200×60, GameFAQs: "Recycling 25x2" (was 6, FIXED) */
    [ITEM_STAIRS] = 8,       /* 64px — sprite 448/7 = 64px per frame ✓ */
    [ITEM_ESCALATOR] = 8,    /* 64px — sprite 512/8 = 64px per frame ✓ */
    [ITEM_ELEVATOR_SHAFT] = 4, /* 32px (standard) */
    [ITEM_ELEVATOR_SERVICE] = 4,
    [ITEM_ELEVATOR_EXPRESS] = 4,
    [ITEM_HOUSEKEEPING] = 4,
};

/* Item heights in floors */
static const int ITEM_HEIGHT[] = {
    [ITEM_NONE] = 1,
    [ITEM_LOBBY] = 1,
    [ITEM_FLOOR] = 1,
    [ITEM_OFFICE] = 1,
    [ITEM_CONDO] = 1,
    [ITEM_HOTEL_SINGLE] = 1,
    [ITEM_HOTEL_TWIN] = 1,
    [ITEM_HOTEL_SUITE] = 1,
    [ITEM_RESTAURANT] = 1,
    [ITEM_FAST_FOOD] = 1,
    [ITEM_SHOP] = 1,
    [ITEM_CINEMA] = 2,       /* OpenSkyscraper: int2(31,2) */
    [ITEM_PARTY_HALL] = 2,   /* OpenSkyscraper: int2(24,2) */
    [ITEM_METRO] = 3,        /* OpenSkyscraper: int2(30,3) */
    [ITEM_PARKING] = 1,
    [ITEM_CATHEDRAL] = 1,    /* Single floor (placed on floor 100 for TOWER promotion) */
    [ITEM_MEDICAL] = 1,
    [ITEM_SECURITY] = 1,
    [ITEM_RECYCLING] = 2,
    [ITEM_STAIRS] = 2,       /* OpenSkyscraper: int2(8,2) */
    [ITEM_ESCALATOR] = 2,    /* OpenSkyscraper: int2(8,2) */
    [ITEM_ELEVATOR_SHAFT] = 1,
    [ITEM_ELEVATOR_SERVICE] = 1,
    [ITEM_ELEVATOR_EXPRESS] = 1,
    [ITEM_HOUSEKEEPING] = 1,
};

/* Item costs — CANONICAL from SimTower (GameFAQs BStuart guide) */
static const int ITEM_COST[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 5000,       /* Per lobby segment */
    [ITEM_FLOOR] = 500,        /* Per floor section */
    [ITEM_OFFICE] = 40000,
    [ITEM_CONDO] = 80000,      /* Was 200000, guide says 80k (income 50-200k once) */
    [ITEM_HOTEL_SINGLE] = 20000,  /* Guide: 20k */
    [ITEM_HOTEL_TWIN] = 50000,    /* Guide: 50k */
    [ITEM_HOTEL_SUITE] = 100000,  /* Guide: 100k */
    [ITEM_RESTAURANT] = 200000,
    [ITEM_FAST_FOOD] = 100000,
    [ITEM_SHOP] = 100000,
    [ITEM_CINEMA] = 500000,
    [ITEM_PARTY_HALL] = 100000,   /* Guide: 100k (was 500k) */
    [ITEM_METRO] = 1000000,
    [ITEM_PARKING] = 3000,        /* Guide: 3k (was 30k) */
    [ITEM_CATHEDRAL] = 0,         /* Special unlock, free */
    [ITEM_MEDICAL] = 500000,
    [ITEM_SECURITY] = 100000,
    [ITEM_RECYCLING] = 500000,
    [ITEM_STAIRS] = 5000,
    [ITEM_ESCALATOR] = 20000,
    [ITEM_ELEVATOR_SHAFT] = 200000,   /* Standard elevator */
    [ITEM_ELEVATOR_SERVICE] = 80000,  /* Service elevator */
    [ITEM_ELEVATOR_EXPRESS] = 400000, /* Express elevator */
    [ITEM_HOUSEKEEPING] = 100000,
};

/* Which items are underground-only */
static const int ITEM_UNDERGROUND_ONLY[] = {
    [ITEM_NONE] = 0, [ITEM_LOBBY] = 0, [ITEM_FLOOR] = 0,
    [ITEM_OFFICE] = 0, [ITEM_CONDO] = 0,
    [ITEM_HOTEL_SINGLE] = 0, [ITEM_HOTEL_TWIN] = 0, [ITEM_HOTEL_SUITE] = 0,
    [ITEM_RESTAURANT] = 0, [ITEM_FAST_FOOD] = 0, [ITEM_SHOP] = 0,
    [ITEM_CINEMA] = 0, [ITEM_PARTY_HALL] = 0,
    [ITEM_METRO] = 1,       /* Underground only */
    [ITEM_PARKING] = 1,     /* Underground only */
    [ITEM_CATHEDRAL] = 0,
    [ITEM_MEDICAL] = 0, [ITEM_SECURITY] = 0,
    [ITEM_RECYCLING] = 1,   /* Underground only */
    [ITEM_STAIRS] = 0, [ITEM_ESCALATOR] = 0, [ITEM_ELEVATOR_SHAFT] = 0,
    [ITEM_ELEVATOR_SERVICE] = 0, [ITEM_ELEVATOR_EXPRESS] = 0, [ITEM_HOUSEKEEPING] = 0,
};

/* A single cell in the tower grid */
typedef struct {
    ItemType type;
    uint16_t tenant_id;    /* Which tenant instance owns this cell (0 = none) */
    uint8_t  cell_index;   /* Position within the tenant (0 = leftmost) */
    uint8_t  flags;        /* Bit flags: occupied, lit, dirty, etc. */
} TowerCell;

/* Tenant instance — represents one placed item */
typedef struct {
    uint16_t id;           /* Unique ID */
    ItemType type;
    int      floor;        /* Which floor */
    int      x;            /* Left cell position */
    int      width;        /* Width in cells */
    int      height;       /* Height in floors */
    uint8_t  state;        /* Lifecycle state (TenantState enum) */
    uint8_t  capacity;     /* Animation state byte — drives sprite frame selection.
                            * From TenantMake: NOT a people count!
                            * Steps: 0x10→0x18→0x20→0x28→0x30→0x38→0x40
                            * Offices fill during day (ascending), empty at night.
                            * Hotels fill at night (reverse), empty during day. */
    int      construction; /* Construction ticks remaining (0 = built).
                            * From TenantMake: office=2, condo=3, restaurant=48,
                            * hotel=56, cathedral=240, etc. */
    int      population;   /* Number of people currently here */
    int      stress;       /* Satisfaction level (lower = happier, 0-100) */
    int      complaints;   /* Strike count (3 = forced action). From MainteT. */
    int      zone;         /* Commercial zone (0-6, floors/15). From JudgeT. */
    int      upgrade_day;  /* Last upgrade day (3-day cadence). From MainteT. */
    uint8_t  hosted;       /* Hotel room: guests stayed overnight */
    uint8_t  dirty;        /* Hotel room: needs housekeeping before it can re-rent */
    uint8_t  cleaned_today;/* Housekeeping unit: rooms cleaned since dawn */
} Tenant;

#define MAX_TENANTS 4096

/* The tower */
typedef struct {
    TowerCell grid[TOWER_FLOOR_COUNT][TOWER_WIDTH];
    Tenant    tenants[MAX_TENANTS];
    int       tenant_count;
    uint16_t  next_tenant_id;
    
    int       star_rating;     /* 1-5 stars */
    long      money;           /* Current funds */
    int       population;      /* Total people in tower */
    int       day;             /* Game day counter */
    int       quarter;         /* 0-3: Q1, Q2, Q3, Weekend */
    
    /* Camera position (pixel coordinates of top-left corner) */
    int       cam_x;
    int       cam_y;
} Tower;

/* Vertical transport categories */
static inline int item_is_elevator(ItemType t)
{
    return t == ITEM_ELEVATOR_SHAFT || t == ITEM_ELEVATOR_SERVICE ||
           t == ITEM_ELEVATOR_EXPRESS;
}

static inline int item_is_transport(ItemType t)
{
    return t == ITEM_STAIRS || t == ITEM_ESCALATOR || item_is_elevator(t);
}

/* Convert floor number to grid array index */
static inline int floor_to_index(int floor)
{
    return floor - TOWER_MIN_FLOOR;
}

/* Convert grid array index to floor number */
static inline int index_to_floor(int index)
{
    return index + TOWER_MIN_FLOOR;
}

/* Initialize a new tower with lobby on floor 0 */
void tower_init(Tower *tower);

/* Place an item. Returns tenant_id on success, 0 on failure. */
uint16_t tower_place(Tower *tower, ItemType type, int floor, int x);

/* Remove a tenant. Returns 1 on success. */
int tower_remove(Tower *tower, uint16_t tenant_id);

/* Check if placement is valid (no overlap, valid floor, etc.) */
int tower_can_place(Tower *tower, ItemType type, int floor, int x);

/* Get the cell at grid position. Returns NULL if out of bounds. */
TowerCell *tower_cell(Tower *tower, int floor, int x);

/* Get tenant by ID. Returns NULL if not found. */
Tenant *tower_tenant(Tower *tower, uint16_t id);

/* Get human-readable name for an item type */
const char *tower_item_name(ItemType type);

/* Pre-populate a demo tower with one of each building type */
void tower_build_demo(Tower *tower);

#endif /* TOWER_H */
