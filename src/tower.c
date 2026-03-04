/* tower.c - Tower grid implementation */
#include "tower.h"
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
    
    /* Underground-only items must be below floor 0 */
    if (ITEM_UNDERGROUND_ONLY[type] && floor >= 0) return 0;
    
    /* Above-ground items (non-transport, non-underground) shouldn't be below floor 0
     * except lobby which is always floor 0, and stairs/escalators which can go anywhere */
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
    
    /* Support check: must have something below OR be on floor 0 (lobby) OR
     * adjacent to existing structure. For underground, need support above. */
    if (floor != 0) {
        int has_support = 0;
        
        /* Check floor below for support */
        int below_idx = floor_to_index(floor - 1);
        if (below_idx >= 0 && below_idx < TOWER_FLOOR_COUNT) {
            for (int cx = x; cx < x + width && !has_support; cx++) {
                if (tower->grid[below_idx][cx].type != ITEM_NONE)
                    has_support = 1;
            }
        }
        
        /* Also check above for underground expansion */
        int above_idx = floor_to_index(floor + height);
        if (!has_support && above_idx >= 0 && above_idx < TOWER_FLOOR_COUNT) {
            for (int cx = x; cx < x + width && !has_support; cx++) {
                if (tower->grid[above_idx][cx].type != ITEM_NONE)
                    has_support = 1;
            }
        }
        
        /* Check same floor adjacency (left/right neighbors) */
        if (!has_support) {
            for (int f = floor; f < floor + height && !has_support; f++) {
                int fidx = floor_to_index(f);
                if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
                if (x > 0 && tower->grid[fidx][x - 1].type != ITEM_NONE)
                    has_support = 1;
                if (x + width < TOWER_WIDTH && tower->grid[fidx][x + width].type != ITEM_NONE)
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
    t->population = 0;
    t->stress = 0;
    
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
    t->state = 0;
    t->population = 0;
    t->stress = 0;
    
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
    printf("\n=== Building demo tower ===\n");
    
    /* Floor 1: Offices + Restaurant */
    tower_force_place(tower, ITEM_OFFICE, 1, 5);
    tower_force_place(tower, ITEM_OFFICE, 1, 14);
    tower_force_place(tower, ITEM_OFFICE, 1, 23);
    tower_force_place(tower, ITEM_RESTAURANT, 1, 32);
    
    /* Floor 2: Condos */
    tower_force_place(tower, ITEM_CONDO, 2, 5);
    tower_force_place(tower, ITEM_CONDO, 2, 21);
    tower_force_place(tower, ITEM_FAST_FOOD, 2, 37);
    
    /* Floor 3: Hotels */
    tower_force_place(tower, ITEM_HOTEL_SINGLE, 3, 5);
    tower_force_place(tower, ITEM_HOTEL_SINGLE, 3, 9);
    tower_force_place(tower, ITEM_HOTEL_TWIN, 3, 13);
    tower_force_place(tower, ITEM_HOTEL_TWIN, 3, 19);
    tower_force_place(tower, ITEM_HOTEL_SUITE, 3, 25);
    tower_force_place(tower, ITEM_HOTEL_SUITE, 3, 33);
    
    /* Floor 4: Security + Medical + Shop */
    tower_force_place(tower, ITEM_SECURITY, 4, 5);
    tower_force_place(tower, ITEM_MEDICAL, 4, 11);
    tower_force_place(tower, ITEM_SHOP, 4, 17);
    tower_force_place(tower, ITEM_SHOP, 4, 25);
    
    /* Floors 5-6: Cinema (2 floors tall, placed at floor 5, extends to 6) */
    tower_force_place(tower, ITEM_CINEMA, 5, 5);
    
    /* Floors 5-6: Party Hall */
    tower_force_place(tower, ITEM_PARTY_HALL, 5, 36);
    
    /* Floors 7-8: Cathedral (2 floors) */
    tower_force_place(tower, ITEM_CATHEDRAL, 7, 20);
    
    /* Stairs connecting floors 1-2, placed at floor 1 */
    tower_force_place(tower, ITEM_STAIRS, 1, 53);
    /* Escalator connecting floors 2-3 */
    tower_force_place(tower, ITEM_ESCALATOR, 2, 53);
    
    /* Underground: B1 - Parking */
    tower_force_place(tower, ITEM_PARKING, -1, 5);
    tower_force_place(tower, ITEM_PARKING, -1, 13);
    tower_force_place(tower, ITEM_PARKING, -1, 21);
    tower_force_place(tower, ITEM_PARKING, -1, 29);
    tower_force_place(tower, ITEM_PARKING, -1, 37);
    tower_force_place(tower, ITEM_PARKING, -1, 45);
    
    /* Underground: B2-B3 - Recycling (2 floors tall) */
    tower_force_place(tower, ITEM_RECYCLING, -3, 5);
    
    /* Underground: B4 to B6 - Metro station (3 floors tall) */
    tower_force_place(tower, ITEM_METRO, -6, 10);
    
    printf("=== Demo tower complete ===\n\n");
}
