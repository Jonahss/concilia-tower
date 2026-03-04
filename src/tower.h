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
    ITEM_OFFICE,        /* Office: 4 cells wide (32px), 1 floor */
    ITEM_CONDO,         /* Condo: 4 cells wide, 1 floor */
    ITEM_HOTEL_SINGLE,  /* Hotel single: 2 cells wide, 1 floor */
    ITEM_HOTEL_TWIN,    /* Hotel twin: 3 cells wide, 1 floor */
    ITEM_HOTEL_SUITE,   /* Hotel suite: 4 cells wide, 1 floor */
    ITEM_RESTAURANT,    /* Restaurant: 8 cells wide, 1 floor */
    ITEM_FAST_FOOD,     /* Fast food: 6 cells wide, 1 floor */
    ITEM_SHOP,          /* Shop: 4 cells wide, 1 floor */
    ITEM_CINEMA,        /* Cinema: 10 cells wide, 2 floors */
    ITEM_PARTY_HALL,    /* Party hall: 8 cells wide, 1 floor */
    ITEM_METRO,         /* Metro station: 16 cells wide, basement only */
    ITEM_PARKING,       /* Parking: varies, basement only */
    ITEM_CATHEDRAL,     /* Cathedral: special */
    ITEM_MEDICAL,       /* Medical center */
    ITEM_SECURITY,      /* Security office */
    ITEM_RECYCLING,     /* Recycling center */
    ITEM_STAIRS,        /* Stairs: 4 cells wide, connects 2 floors */
    ITEM_ESCALATOR,     /* Escalator: 4 cells wide, connects 2 floors */
    ITEM_ELEVATOR_SHAFT,/* Elevator shaft: 4 cells wide, variable height */
    ITEM_TYPE_COUNT
} ItemType;

/* Item widths in cells (verified from OpenSkyscraper source)
 * Each cell = 8px. Sizes match p->size.x from Item/*.h files.
 * Sprite frame widths are cells × 8px. */
static const int ITEM_WIDTH[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 63,       /* Full width (auto-extends) */
    [ITEM_FLOOR] = 63,
    [ITEM_OFFICE] = 9,       /* 72px — sprite frame 72×24 */
    [ITEM_CONDO] = 16,       /* 128px — sprite frame 128×24 */
    [ITEM_HOTEL_SINGLE] = 4, /* 32px — sprite 0x84a8 = 32×24 */
    [ITEM_HOTEL_TWIN] = 6,   /* 48px — sprite 0x84e8 = 48×24 */
    [ITEM_HOTEL_SUITE] = 8,  /* 64px — estimated from suite sprites */
    [ITEM_RESTAURANT] = 24,  /* 192px — sprite frame 192×24 */
    [ITEM_FAST_FOOD] = 16,   /* 128px — sprite frame 128×24 */
    [ITEM_SHOP] = 8,         /* 64px — estimated */
    [ITEM_CINEMA] = 31,      /* 248px — 2 floors high */
    [ITEM_PARTY_HALL] = 24,  /* 192px — 2 floors high */
    [ITEM_METRO] = 30,       /* 240px — 3 floors deep */
    [ITEM_PARKING] = 8,      /* 64px — estimated */
    [ITEM_CATHEDRAL] = 8,
    [ITEM_MEDICAL] = 8,
    [ITEM_SECURITY] = 4,
    [ITEM_RECYCLING] = 8,
    [ITEM_STAIRS] = 8,       /* 64px — 2 floors high */
    [ITEM_ESCALATOR] = 8,    /* 64px — 2 floors high */
    [ITEM_ELEVATOR_SHAFT] = 4, /* 32px (standard) */
};

/* Item costs (from original game, approximated from decompilation) */
static const int ITEM_COST[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 0,         /* Free (auto-built) */
    [ITEM_FLOOR] = 5000,      /* Per floor section */
    [ITEM_OFFICE] = 40000,
    [ITEM_CONDO] = 80000,
    [ITEM_HOTEL_SINGLE] = 50000,
    [ITEM_HOTEL_TWIN] = 80000,
    [ITEM_HOTEL_SUITE] = 100000,
    [ITEM_RESTAURANT] = 200000,
    [ITEM_FAST_FOOD] = 100000,
    [ITEM_SHOP] = 100000,
    [ITEM_CINEMA] = 500000,
    [ITEM_PARTY_HALL] = 100000,
    [ITEM_METRO] = 1000000,
    [ITEM_PARKING] = 30000,
    [ITEM_CATHEDRAL] = 0,      /* Special unlock */
    [ITEM_MEDICAL] = 500000,
    [ITEM_SECURITY] = 100000,
    [ITEM_RECYCLING] = 500000,
    [ITEM_STAIRS] = 5000,
    [ITEM_ESCALATOR] = 40000,
    [ITEM_ELEVATOR_SHAFT] = 200000,
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

#endif /* TOWER_H */
