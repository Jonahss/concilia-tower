/* tower.c - Tower grid implementation */
#include "tower.h"
#include "game.h"  /* For CONSTRUCTION_TIME[], CAP_* defines */
#include <stdio.h>
#include <string.h>

void tower_init(Tower *tower)
{
    memset(tower, 0, sizeof(*tower));
    
    tower->star_rating = 1;
    tower->money = 5000000;  /* $5,000,000 for demo (need enough for all buildings) */
    tower->next_tenant_id = 1;
    tower->day = 1;
    tower->quarter = 0;
    
    /* Center camera on lobby */
    tower->cam_x = (TOWER_WIDTH * CELL_W) / 2;
    tower->cam_y = floor_to_index(0) * CELL_H;
    
    /* Place the initial lobby (spans the full width) */
    int lobby_idx = floor_to_index(TOWER_LOBBY_FLOOR);
    uint16_t lobby_id = tower->next_tenant_id++;
    
    Tenant *lobby = &tower->tenants[tower->tenant_count++];
    lobby->id = lobby_id;
    lobby->type = ITEM_LOBBY;
    lobby->floor = TOWER_LOBBY_FLOOR;
    lobby->x = 0;
    lobby->width = TOWER_WIDTH;
    lobby->height = 1;
    lobby->state = 0;
    
    for (int x = 0; x < TOWER_WIDTH; x++) {
        TowerCell *cell = &tower->grid[lobby_idx][x];
        cell->type = ITEM_LOBBY;
        cell->tenant_id = lobby_id;
        cell->cell_index = x;
        cell->flags = 1; /* occupied */
    }
    
    printf("Tower initialized: $%ld, %d star(s)\n", tower->money, tower->star_rating);
}

TowerCell *tower_cell(Tower *tower, int floor, int x)
{
    int idx = floor_to_index(floor);
    if (idx < 0 || idx >= TOWER_FLOOR_COUNT || x < 0 || x >= TOWER_WIDTH)
        return NULL;
    return &tower->grid[idx][x];
}

Tenant *tower_tenant(Tower *tower, uint16_t id)
{
    for (int i = 0; i < tower->tenant_count; i++) {
        if (tower->tenants[i].id == id)
            return &tower->tenants[i];
    }
    return NULL;
}

int tower_can_place(Tower *tower, ItemType type, int floor, int x)
{
    if (type <= ITEM_NONE || type >= ITEM_TYPE_COUNT) return 0;
    
    int width = ITEM_WIDTH[type];
    int height = ITEM_HEIGHT[type];
    int cost = ITEM_COST[type];
    
    /* Check bounds — multi-floor items extend upward from placement floor */
    if (x < 0 || x + width > TOWER_WIDTH) return 0;
    if (floor < TOWER_MIN_FLOOR || floor > TOWER_MAX_FLOOR) return 0;
    if (floor + height - 1 > TOWER_MAX_FLOOR) return 0;
    
    /* Check funds */
    if (tower->money < cost) return 0;
    
    /* Lobby special rules: can only be placed on every 15th floor */
    if (type == ITEM_LOBBY) {
        if (floor % 15 != 0) return 0;
        if (floor < TOWER_MIN_FLOOR || floor > TOWER_MAX_FLOOR) return 0;
        /* Check if lobby already exists on this floor */
        int fidx = floor_to_index(floor);
        if (fidx >= 0 && fidx < TOWER_FLOOR_COUNT) {
            if (tower->grid[fidx][0].type == ITEM_LOBBY) return 0; /* already has lobby */
        }
        return 1; /* Lobbies auto-extend full width, no further checks needed */
    }
    
    /* Underground-only items must be below floor 0 */
    if (ITEM_UNDERGROUND_ONLY[type] && floor >= 0) return 0;
    
    /* Above-ground items (non-transport, non-underground) shouldn't be below floor 0
     * except stairs/escalators which can go anywhere */
    int is_transport = (type == ITEM_STAIRS || type == ITEM_ESCALATOR);
    if (!is_transport && !ITEM_UNDERGROUND_ONLY[type] && 
        type != ITEM_LOBBY && type != ITEM_FLOOR && floor < 0) return 0;
    
    /* Check for overlap on ALL floors this item occupies */
    if (!is_transport) {
        for (int f = floor; f < floor + height; f++) {
            int fidx = floor_to_index(f);
            if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) return 0;
            for (int cx = x; cx < x + width; cx++) {
                if (tower->grid[fidx][cx].type != ITEM_NONE) return 0;
            }
        }
    }
    
    /* Support check:
     * - Floor 0 (lobby level): always supported
     * - Above ground (floor > 0): must have support directly BELOW (floor - 1)
     * - Underground (floor < 0): must have support directly ABOVE (floor + height)
     * - Stairs/escalators can bridge floors (exempt from strict support) 
     *   but still need SOME connection to existing structure */
    if (floor == 0) {
        /* Ground floor is always supported */
    } else if (is_transport) {
        /* Transport (stairs/escalators) need support below OR above */
        int has_support = 0;
        
        int below_idx = floor_to_index(floor - 1);
        if (below_idx >= 0 && below_idx < TOWER_FLOOR_COUNT) {
            for (int cx = x; cx < x + width && !has_support; cx++) {
                if (tower->grid[below_idx][cx].type != ITEM_NONE)
                    has_support = 1;
            }
        }
        
        int above_idx = floor_to_index(floor + height);
        if (!has_support && above_idx >= 0 && above_idx < TOWER_FLOOR_COUNT) {
            for (int cx = x; cx < x + width && !has_support; cx++) {
                if (tower->grid[above_idx][cx].type != ITEM_NONE)
                    has_support = 1;
            }
        }
        
        /* Also check the floors they span for existing content */
        if (!has_support) {
            for (int f = floor; f < floor + height && !has_support; f++) {
                int fidx = floor_to_index(f);
                if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
                for (int cx = x; cx < x + width && !has_support; cx++) {
                    if (tower->grid[fidx][cx].type != ITEM_NONE)
                        has_support = 1;
                }
            }
        }
        
        if (!has_support) return 0;
    } else if (floor > 0) {
        /* Above ground: MUST have support directly below */
        int has_support = 0;
        int below_idx = floor_to_index(floor - 1);
        if (below_idx >= 0 && below_idx < TOWER_FLOOR_COUNT) {
            for (int cx = x; cx < x + width && !has_support; cx++) {
                if (tower->grid[below_idx][cx].type != ITEM_NONE)
                    has_support = 1;
            }
        }
        if (!has_support) return 0;
    } else {
        /* Underground (floor < 0): must have support above */
        int has_support = 0;
        int above_idx = floor_to_index(floor + height);
        if (above_idx >= 0 && above_idx < TOWER_FLOOR_COUNT) {
            for (int cx = x; cx < x + width && !has_support; cx++) {
                if (tower->grid[above_idx][cx].type != ITEM_NONE)
                    has_support = 1;
            }
        }
        if (!has_support) return 0;
    }
    
    return 1;
}

uint16_t tower_place(Tower *tower, ItemType type, int floor, int x)
{
    if (!tower_can_place(tower, type, floor, x)) return 0;
    if (tower->tenant_count >= MAX_TENANTS) return 0;
    
    int width = ITEM_WIDTH[type];
    int height = ITEM_HEIGHT[type];
    int cost = ITEM_COST[type];
    
    /* Lobby special case: auto-extends full width */
    if (type == ITEM_LOBBY) {
        width = TOWER_WIDTH;
        x = 0;
    }
    
    /* Deduct cost */
    tower->money -= cost;
    
    /* Create tenant */
    uint16_t id = tower->next_tenant_id++;
    Tenant *t = &tower->tenants[tower->tenant_count++];
    t->id = id;
    t->type = type;
    t->floor = floor;
    t->x = x;
    t->width = width;
    t->height = height;
    t->state = 0;
    t->capacity = CAP_EMPTY;
    t->construction = (type < ITEM_TYPE_COUNT) ? CONSTRUCTION_TIME[type] : 0;
    t->population = 0;
    t->stress = 0;
    t->complaints = 0;
    t->zone = (floor >= 0) ? floor / 15 : 0;  /* JudgeT: 7 zones of 15 floors */
    t->upgrade_day = 0;
    
    /* Skip construction for instant-build items */
    if (t->construction <= 0) {
        t->state = TENANT_OCCUPIED;
        t->capacity = CAP_MIN;
    } else {
        t->state = TENANT_CONSTRUCTION;
    }
    
    /* Fill grid cells — transport items (stairs/escalators) don't overwrite
     * existing cells, they're stored as overlays in the tenant list only */
    int is_transport = (type == ITEM_STAIRS || type == ITEM_ESCALATOR);
    
    for (int f = floor; f < floor + height; f++) {
        int fidx = floor_to_index(f);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
        
        if (!is_transport) {
            for (int cx = x; cx < x + width; cx++) {
                TowerCell *cell = &tower->grid[fidx][cx];
                cell->type = type;
                cell->tenant_id = id;
                cell->cell_index = cx - x;
                cell->flags = 1;
            }
        } else {
            /* Mark transport presence with a flag but keep existing cell data */
            for (int cx = x; cx < x + width; cx++) {
                TowerCell *cell = &tower->grid[fidx][cx];
                cell->flags |= 2; /* bit 1 = has transport overlay */
            }
        }
    }
    
    printf("Placed %s at floor %d, x=%d (cost $%d, balance $%ld)\n",
           tower_item_name(type), floor, x, cost, tower->money);
    
    return id;
}

int tower_remove(Tower *tower, uint16_t tenant_id)
{
    Tenant *t = tower_tenant(tower, tenant_id);
    if (!t) return 0;
    
    /* Clear grid cells on all floors */
    for (int f = t->floor; f < t->floor + t->height; f++) {
        int idx = floor_to_index(f);
        if (idx < 0 || idx >= TOWER_FLOOR_COUNT) continue;
        for (int cx = t->x; cx < t->x + t->width; cx++) {
            TowerCell *cell = &tower->grid[idx][cx];
            memset(cell, 0, sizeof(*cell));
        }
    }
    
    /* Remove tenant (swap with last) */
    int ti = (int)(t - tower->tenants);
    tower->tenants[ti] = tower->tenants[--tower->tenant_count];
    
    return 1;
}

const char *tower_item_name(ItemType type)
{
    static const char *names[] = {
        "None", "Lobby", "Floor", "Office", "Condo", "Hotel(S)", "Hotel(T)",
        "Hotel(Suite)", "Restaurant", "Fast Food", "Shop", "Cinema", "Party Hall",
        "Metro", "Parking", "Cathedral", "Medical", "Security", "Recycling",
        "Stairs", "Escalator", "Elevator"
    };
    if (type >= 0 && type < ITEM_TYPE_COUNT) return names[type];
    return "Unknown";
}

/* Force-place without validation (for demo tower building) */
static uint16_t tower_force_place(Tower *tower, ItemType type, int floor, int x)
{
    if (tower->tenant_count >= MAX_TENANTS) return 0;
    
    int width = ITEM_WIDTH[type];
    int height = ITEM_HEIGHT[type];
    
    /* Bounds check only */
    if (x < 0 || x + width > TOWER_WIDTH) return 0;
    if (floor < TOWER_MIN_FLOOR || floor + height - 1 > TOWER_MAX_FLOOR) return 0;
    
    /* Create tenant */
    uint16_t id = tower->next_tenant_id++;
    Tenant *t = &tower->tenants[tower->tenant_count++];
    t->id = id;
    t->type = type;
    t->floor = floor;
    t->x = x;
    t->width = width;
    t->height = height;
    t->state = TENANT_OCCUPIED;  /* Demo tenants start occupied */
    t->capacity = CAP_MIN;       /* Start at first animation frame */
    t->construction = 0;         /* Already built */
    t->population = 0;
    t->stress = 0;
    t->complaints = 0;
    t->zone = (floor >= 0) ? floor / 15 : 0;
    t->upgrade_day = 0;
    
    int is_transport = (type == ITEM_STAIRS || type == ITEM_ESCALATOR);
    
    for (int f = floor; f < floor + height; f++) {
        int fidx = floor_to_index(f);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
        
        if (!is_transport) {
            for (int cx = x; cx < x + width; cx++) {
                TowerCell *cell = &tower->grid[fidx][cx];
                cell->type = type;
                cell->tenant_id = id;
                cell->cell_index = cx - x;
                cell->flags = 1;
            }
        } else {
            for (int cx = x; cx < x + width; cx++) {
                tower->grid[fidx][cx].flags |= 2;
            }
        }
    }
    
    return id;
}

void tower_build_demo(Tower *tower)
{
    printf("\n=== Building diagnostic demo tower ===\n");
    printf("One unit type per floor, starting at floor 1\n");
    printf("Each unit placed at x=2 with label info\n\n");
    
    /* Diagnostic layout: one unit per floor at x=2
     * Single-floor items get one floor each
     * Multi-floor items get their required floors
     * Underground items placed below ground
     * 
     * Floor plan:
     *   0: Lobby (auto-built)
     *   1: Office (9w × 1h)
     *   2: Condo (16w × 1h)
     *   3: Fast Food (16w × 1h)
     *   4: Restaurant (24w × 1h)
     *   5: Hotel Single (4w × 1h)
     *   6: Hotel Twin (6w × 1h)
     *   7: Hotel Suite (8w × 1h)
     *   8: Security (6w × 1h)
     *   9: Medical (6w × 1h)
     *  10: Shop (8w × 1h)
     *  11-12: Stairs (8w × 2h)
     *  13-14: Escalator (8w × 2h)
     *  15-16: Cinema (31w × 2h)
     *  17-18: Party Hall (24w × 2h)
     *  19-20: Cathedral (16w × 2h)
     *  B1: Parking (8w × 1h)
     *  B2-B3: Recycling (6w × 2h)
     *  B4-B6: Metro (30w × 3h)
     */
    
    /* Above ground: single-floor items */
    struct { ItemType type; int floor; } demo_items[] = {
        { ITEM_OFFICE,       1 },
        { ITEM_CONDO,        2 },
        { ITEM_FAST_FOOD,    3 },
        { ITEM_RESTAURANT,   4 },
        { ITEM_HOTEL_SINGLE, 5 },
        { ITEM_HOTEL_TWIN,   6 },
        { ITEM_HOTEL_SUITE,  7 },
        { ITEM_SECURITY,     8 },
        { ITEM_MEDICAL,      9 },
        { ITEM_SHOP,        10 },
        /* Multi-floor above ground */
        { ITEM_STAIRS,      11 },   /* 2 floors: 11-12 */
        { ITEM_ESCALATOR,   13 },   /* 2 floors: 13-14 */
        { ITEM_CINEMA,      15 },   /* 2 floors: 15-16 */
        { ITEM_PARTY_HALL,  17 },   /* 2 floors: 17-18 */
        { ITEM_CATHEDRAL,   19 },   /* 1 floor (was 2, corrected) */
        /* Underground items */
        { ITEM_PARKING,     -1 },   /* 1 floor */
        { ITEM_RECYCLING,   -3 },   /* 2 floors: B3-B2 */
        { ITEM_METRO,       -6 },   /* 3 floors: B6-B4 */
    };
    int n_items = (int)(sizeof(demo_items) / sizeof(demo_items[0]));
    
    for (int i = 0; i < n_items; i++) {
        ItemType type = demo_items[i].type;
        int floor = demo_items[i].floor;
        int x = 2; /* All placed at x=2 for consistent left margin */
        
        uint16_t id = tower_force_place(tower, type, floor, x);
        if (id) {
            printf("  F%+3d: %-14s  %2dw × %dh  at x=%d  tenant_id=%d\n",
                   floor, tower_item_name(type), 
                   ITEM_WIDTH[type], ITEM_HEIGHT[type], x, id);
        } else {
            printf("  F%+3d: %-14s  FAILED to place\n", floor, tower_item_name(type));
        }
    }
    
    printf("\n=== Demo tower complete: %d items placed ===\n\n", tower->tenant_count - 1);
}
