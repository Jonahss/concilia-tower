/* tower.c - Tower grid implementation */
#include "tower.h"
#include <stdio.h>
#include <string.h>

void tower_init(Tower *tower)
{
    memset(tower, 0, sizeof(*tower));
    
    tower->star_rating = 1;
    tower->money = 2000000;  /* $2,000,000 starting money (original game) */
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
    int cost = ITEM_COST[type];
    
    /* Check bounds */
    if (x < 0 || x + width > TOWER_WIDTH) return 0;
    if (floor < TOWER_MIN_FLOOR || floor > TOWER_MAX_FLOOR) return 0;
    
    /* Check funds */
    if (tower->money < cost) return 0;
    
    /* Check for overlap — stairs/escalators can overlay existing items */
    int is_transport = (type == ITEM_STAIRS || type == ITEM_ESCALATOR);
    int idx = floor_to_index(floor);
    if (!is_transport) {
        for (int cx = x; cx < x + width; cx++) {
            if (tower->grid[idx][cx].type != ITEM_NONE) return 0;
        }
    }
    
    /* Must be adjacent to existing structure (above, below, or beside) */
    /* For now, just require that there's something on the floor below OR
     * this is floor 0 (lobby) OR this is floor 1 (above lobby) */
    if (floor != 0) {
        int below_idx = floor_to_index(floor - 1);
        int has_support = 0;
        if (below_idx >= 0 && below_idx < TOWER_FLOOR_COUNT) {
            for (int cx = x; cx < x + width && !has_support; cx++) {
                if (tower->grid[below_idx][cx].type != ITEM_NONE)
                    has_support = 1;
            }
        }
        /* Also check above for basement construction */
        int above_idx = floor_to_index(floor + 1);
        if (!has_support && above_idx >= 0 && above_idx < TOWER_FLOOR_COUNT) {
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
    t->state = 0;
    t->population = 0;
    t->stress = 0;
    
    /* Fill grid cells — transport items (stairs/escalators) don't overwrite
     * existing cells, they're stored as overlays in the tenant list only */
    int is_transport = (type == ITEM_STAIRS || type == ITEM_ESCALATOR);
    int fidx = floor_to_index(floor);
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
    
    printf("Placed %d at floor %d, x=%d (cost $%d, balance $%ld)\n",
           type, floor, x, cost, tower->money);
    
    /* Auto-add floor/walkway above lobby if building above ground */
    /* (In the original game, each floor gets an automatic floor structure) */
    
    return id;
}

int tower_remove(Tower *tower, uint16_t tenant_id)
{
    Tenant *t = tower_tenant(tower, tenant_id);
    if (!t) return 0;
    
    /* Clear grid cells */
    int idx = floor_to_index(t->floor);
    for (int cx = t->x; cx < t->x + t->width; cx++) {
        TowerCell *cell = &tower->grid[idx][cx];
        memset(cell, 0, sizeof(*cell));
    }
    
    /* Remove tenant (swap with last) */
    int ti = (int)(t - tower->tenants);
    tower->tenants[ti] = tower->tenants[--tower->tenant_count];
    
    return 1;
}
