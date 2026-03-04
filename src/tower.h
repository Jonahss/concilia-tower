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
#define TOWER_WIDTH       63    /* Cells wide */
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
    ITEM_ELEVATOR_SHAFT,/* Elevator shaft: 4 cells wide, variable height */
    ITEM_TYPE_COUNT
} ItemType;

/* Item widths in cells (verified from OpenSkyscraper source)
 * Each cell = 8px. Sizes match p->size.x from Item headers. */
static const int ITEM_WIDTH[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 63,       /* Full width (auto-extends) */
    [ITEM_FLOOR] = 63,
    [ITEM_OFFICE] = 9,       /* 72px — OpenSkyscraper: int2(9,1) */
    [ITEM_CONDO] = 16,       /* 128px — OpenSkyscraper: int2(16,1) */
    [ITEM_HOTEL_SINGLE] = 4, /* 32px — door+room composite */
    [ITEM_HOTEL_TWIN] = 6,   /* 48px — door+room composite */
    [ITEM_HOTEL_SUITE] = 8,  /* 64px — 0x8528+0x8529 composite */
    [ITEM_RESTAURANT] = 24,  /* 192px — OpenSkyscraper: int2(24,1) */
    [ITEM_FAST_FOOD] = 16,   /* 128px — OpenSkyscraper: int2(16,1) */
    [ITEM_SHOP] = 8,         /* 64px */
    [ITEM_CINEMA] = 31,      /* 248px — OpenSkyscraper: int2(31,2) */
    [ITEM_PARTY_HALL] = 24,  /* 192px — OpenSkyscraper: int2(24,2) */
    [ITEM_METRO] = 30,       /* 240px — OpenSkyscraper: int2(30,3) */
    [ITEM_PARKING] = 8,      /* 64px — parking space */
    [ITEM_CATHEDRAL] = 16,   /* 128px — estimated, 2 floors tall */
    [ITEM_MEDICAL] = 6,      /* 48px — 3 bitmaps 0x8728-0x872A */
    [ITEM_SECURITY] = 6,     /* 48px — animated 0x8768 */
    [ITEM_RECYCLING] = 6,    /* 48px — 0x88E8+ */
    [ITEM_STAIRS] = 8,       /* 64px — OpenSkyscraper: int2(8,2) */
    [ITEM_ESCALATOR] = 8,    /* 64px — OpenSkyscraper: int2(8,2) */
    [ITEM_ELEVATOR_SHAFT] = 4, /* 32px (standard) */
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
    [ITEM_CATHEDRAL] = 2,
    [ITEM_MEDICAL] = 1,
    [ITEM_SECURITY] = 1,
    [ITEM_RECYCLING] = 2,
    [ITEM_STAIRS] = 2,       /* OpenSkyscraper: int2(8,2) */
    [ITEM_ESCALATOR] = 2,    /* OpenSkyscraper: int2(8,2) */
    [ITEM_ELEVATOR_SHAFT] = 1,
};

/* Item costs (from original game / OpenSkyscraper prototypes) */
static const int ITEM_COST[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 0,         /* Free (auto-built) */
    [ITEM_FLOOR] = 5000,      /* Per floor section */
    [ITEM_OFFICE] = 40000,    /* OpenSkyscraper: 40000 */
    [ITEM_CONDO] = 200000,    /* OpenSkyscraper: 200000 */
    [ITEM_HOTEL_SINGLE] = 50000,
    [ITEM_HOTEL_TWIN] = 80000,
    [ITEM_HOTEL_SUITE] = 100000,
    [ITEM_RESTAURANT] = 200000, /* OpenSkyscraper: 200000 */
    [ITEM_FAST_FOOD] = 100000,  /* OpenSkyscraper: 100000 */
    [ITEM_SHOP] = 100000,
    [ITEM_CINEMA] = 500000,   /* OpenSkyscraper: 500000 */
    [ITEM_PARTY_HALL] = 500000, /* OpenSkyscraper: 500000 */
    [ITEM_METRO] = 1000000,   /* OpenSkyscraper: 1000000 */
    [ITEM_PARKING] = 30000,
    [ITEM_CATHEDRAL] = 0,      /* Special unlock, free */
    [ITEM_MEDICAL] = 500000,
    [ITEM_SECURITY] = 100000,
    [ITEM_RECYCLING] = 500000,
    [ITEM_STAIRS] = 5000,     /* OpenSkyscraper: 5000 */
    [ITEM_ESCALATOR] = 20000, /* OpenSkyscraper: 20000 */
    [ITEM_ELEVATOR_SHAFT] = 200000,
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
    uint8_t  state;        /* Item-specific state */
    int      population;   /* Number of people currently here */
    int      stress;       /* Satisfaction level (lower = happier) */
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
