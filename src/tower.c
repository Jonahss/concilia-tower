/* tower.c - Tower grid implementation */
#include <stdlib.h>
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
    
    /* Place initial lobby — centered, 16 cells wide (4 segments).
     * Player extends it from here. From original: lobby starts small. */
    int lobby_idx = floor_to_index(TOWER_LOBBY_FLOOR);
    int lobby_x = (TOWER_WIDTH - 16) / 2;  /* Centered */
    int lobby_w = 16;
    uint16_t lobby_id = tower->next_tenant_id++;
    
    Tenant *lobby = &tower->tenants[tower->tenant_count++];
    lobby->id = lobby_id;
    lobby->type = ITEM_LOBBY;
    lobby->floor = TOWER_LOBBY_FLOOR;
    lobby->x = lobby_x;
    lobby->width = lobby_w;
    lobby->height = 1;
    lobby->state = 0;
    
    for (int x = lobby_x; x < lobby_x + lobby_w; x++) {
        TowerCell *cell = &tower->grid[lobby_idx][x];
        cell->type = ITEM_LOBBY;
        cell->tenant_id = lobby_id;
        cell->cell_index = x - lobby_x;
        cell->flags = CELL_OCCUPIED;
    }
    
    printf("Tower initialized: $%ld, %d star(s)\n", tower->money, tower->star_rating);
    printf("  Lobby: x=%d w=%d (cells %d-%d of %d)\n", 
           lobby_x, lobby_w, lobby_x, lobby_x + lobby_w - 1, TOWER_WIDTH);
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
    if (x < 0 || x + width > TOWER_WIDTH) {
        printf("  [reject] %s at F%d x%d: bounds (x+w=%d > TW=%d)\n",
               tower_item_name(type), floor, x, x+width, TOWER_WIDTH);
        return 0;
    }
    if (floor < TOWER_MIN_FLOOR || floor > TOWER_MAX_FLOOR) return 0;
    if (floor + height - 1 > TOWER_MAX_FLOOR) return 0;
    
    /* Check funds */
    if (tower->money < cost) {
        printf("  [reject] %s at F%d x%d: insufficient funds ($%ld < $%d)\n",
               tower_item_name(type), floor, x, tower->money, cost);
        return 0;
    }
    
    /* Lobby placement: 4-cell segments on every 15th floor.
     * Can overlap existing lobby cells (extends the lobby).
     * From LobbyMake (seg_11e8) + OpenSkyscraper Game.cpp. */
    if (type == ITEM_LOBBY) {
        if (floor % 15 != 0) {
            printf("  [reject] Lobby at F%d: not a lobby floor (F%%15=%d)\n", floor, floor%15);
            return 0;
        }
        if (floor < TOWER_MIN_FLOOR || floor > TOWER_MAX_FLOOR) return 0;
        if (x < 0 || x + width > TOWER_WIDTH) {
            printf("  [reject] Lobby at F%d x%d: bounds (x+w=%d > TW=%d)\n", floor, x, x+width, TOWER_WIDTH);
            return 0;
        }
        if (tower->money < cost) return 0;
        /* Lobby segments CAN overlap existing lobby — that's how extension works.
         * But they can't overlap non-lobby items. */
        int fidx = floor_to_index(floor);
        if (fidx >= 0 && fidx < TOWER_FLOOR_COUNT) {
            for (int cx = x; cx < x + width; cx++) {
                ItemType existing = tower->grid[fidx][cx].type;
                if (item_is_elevator(existing)) continue;  /* lobby coexists with shafts */
                if (existing != ITEM_NONE && existing != ITEM_LOBBY &&
                    existing != ITEM_FLOOR) {
                    printf("  [reject] Lobby at F%d x%d: cell %d has %s\n",
                           floor, x, cx, tower_item_name(existing));
                    return 0;
                }
            }
        }
        /* Floor 0: always allowed. Upper lobbies: need floor below. */
        if (floor != 0) {
            int below_idx = floor_to_index(floor - 1);
            int has_support = 0;
            if (below_idx >= 0) {
                for (int cx = x; cx < x + width && !has_support; cx++) {
                    if (tower->grid[below_idx][cx].type != ITEM_NONE)
                        has_support = 1;
                }
            }
            if (!has_support) return 0;
        }
        return 1;
    }
    
    /* Stairs/escalators OVERLAY existing floors; elevators occupy their own
     * cells but share transports' placement freedoms (any floor, basement). */
    int is_transport = (type == ITEM_STAIRS || type == ITEM_ESCALATOR);

    /* Floor 0 is lobby-only — except transports: elevators and stairs connect
     * at the ground lobby in the original. */
    if (floor == 0 && type != ITEM_LOBBY && type != ITEM_FLOOR && !item_is_transport(type)) {
        printf("  [reject] %s at F0: only lobbies on ground floor\n", tower_item_name(type));
        return 0;
    }

    /* Elevator placement has NO lobby/adjacency/content requirement — verified
     * against the binary (MakeElevator 11f8:0fea, globals.md #51/#52): a
     * standard/service shaft can be placed on any floor in the buildable range,
     * and the express-only sky-lobby anchor (below) is the single floor rule.
     * The motor room / pit extend one floor above/below the served range,
     * which is expected. (My earlier "must connect to the tower" gate was
     * invented and wrong — Jonah caught it.) */

    /* Express shafts anchor at lobby levels: a NEW express shaft must
     * start on the ground floor, a basement floor, or a sky-lobby floor
     * (every 15th) — MakeElevator (11f8:0ff9) gates type 0 above ground
     * on IsSkyLobbyFloor (10a0:12e0, (displayed%15)==0). Segments that
     * touch an existing express column are extensions, which the EXE
     * allows at any floor (that's the arrow/drag path, not MakeElevator). */
    if (type == ITEM_ELEVATOR_EXPRESS && floor > 0 && floor % 15 != 0) {
        int touches = 0;
        for (int df = -1; df <= 1 && !touches; df += 2) {
            int fidx2 = floor_to_index(floor + df);
            if (fidx2 < 0 || fidx2 >= TOWER_FLOOR_COUNT) continue;
            if (tower->grid[fidx2][x].type == ITEM_ELEVATOR_EXPRESS)
                touches = 1;
        }
        if (!touches) {
            printf("  [reject] Express at F%d: new express shafts anchor at "
                   "lobby floors (ground/basement/every 15th)\n", floor);
            return 0;
        }
    }

    /* Underground-only items must be below floor 0 */
    if (ITEM_UNDERGROUND_ONLY[type] && floor >= 0) return 0;
    if (!item_is_transport(type) && !ITEM_UNDERGROUND_ONLY[type] &&
        type != ITEM_LOBBY && type != ITEM_FLOOR && floor < 0) return 0;

    /* Parking (ParkingT, byte-verified 2026-07-11 referee):
     * one ramp strip per basement floor (real .TDT saves store exactly
     * one); spaces need a same-floor ramp FIRST (CheckParkingGate,
     * build msg 0x22) and respect the global 512-space cap (msg 0x1E).
     * Chain usability is NOT a build gate — the EXE lets you place a
     * disconnected ramp and just marks its spaces unusable at the next
     * CheckAllParking sweep. */
    if (type == ITEM_RAMP) {
        for (int i = 0; i < tower->tenant_count; i++) {
            Tenant *t = &tower->tenants[i];
            if (t->type == ITEM_RAMP && t->floor == floor) {
                printf("  [reject] Ramp at F%d: floor already has a ramp\n",
                       floor);
                return 0;
            }
        }
    }
    if (type == ITEM_PARKING) {
        int has_ramp = 0, spaces = 0;
        for (int i = 0; i < tower->tenant_count; i++) {
            Tenant *t = &tower->tenants[i];
            if (t->type == ITEM_RAMP && t->floor == floor) has_ramp = 1;
            if (t->type == ITEM_PARKING) spaces++;
        }
        if (!has_ramp) {
            printf("  [reject] Parking at F%d: build a ramp on this floor "
                   "first\n", floor);
            return 0;
        }
        if (spaces >= 512) {
            printf("  [reject] Parking: the 512-space cap is reached\n");
            return 0;
        }
    }
    
    /* Check for overlap on ALL floors this item occupies */
    if (!is_transport) {
        int elev = item_is_elevator(type);
        for (int f = floor; f < floor + height; f++) {
            int fidx = floor_to_index(f);
            if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) return 0;
            for (int cx = x; cx < x + width; cx++) {
                ItemType occ = tower->grid[fidx][cx].type;
                if (occ == ITEM_NONE) continue;
                /* Plain floorspace is buildable-over: the EXE's placement gate
                 * (ValidatePlacementArea 11f8:2e64) is floor-DECK based — a
                 * built floor with nothing on it is exactly where you build. */
                if (occ == ITEM_FLOOR && type != ITEM_FLOOR) continue;
                /* An elevator shaft passes THROUGH everything except another
                 * shaft — the same deck-extent gate is the EXE's only overlap
                 * rule for elevators, and real .TWR saves legally overlap
                 * shaft columns with tenants. The shaft stamps the grid cells
                 * (the tenant keeps rendering from the tenant array). */
                if (elev && !item_is_elevator(occ)) continue;
                /* Symmetrically, a room spans through an existing shaft
                 * column (the shaft keeps those grid cells — see
                 * tower_place). */
                if (!elev && item_is_elevator(occ)) continue;
                printf("  [reject] %s at F%d x%d: cell F%d,x%d has %s\n",
                       tower_item_name(type), floor, x, f, cx,
                       tower_item_name(occ));
                return 0;
            }
        }
    } else {
        /* Stairs/escalators overlay a floor, so two of them can SHARE a single
         * landing floor to form a chain — but they must not FULLY overlap
         * another transport (share both of their floors). Reject only when both
         * the lower and upper floors already carry a transport overlay. */
        int lf = floor_to_index(floor);
        int uf = floor_to_index(floor + height - 1);
        int ov_lower = 0, ov_upper = 0;
        if (lf >= 0 && lf < TOWER_FLOOR_COUNT)
            for (int cx = x; cx < x + width; cx++)
                if (cell_has_transport_overlay(&tower->grid[lf][cx])) ov_lower = 1;
        if (uf >= 0 && uf < TOWER_FLOOR_COUNT)
            for (int cx = x; cx < x + width; cx++)
                if (cell_has_transport_overlay(&tower->grid[uf][cx])) ov_upper = 1;
        if (ov_lower && ov_upper) {
            printf("  [reject] %s at F%d x%d: overlaps an existing stair/escalator\n",
                   tower_item_name(type), floor, x);
            return 0;
        }
    }

    /* Support check:
     * - Elevators: none — a shaft is its own vertical structure, placeable on
     *   any floor inside the tower's extent (it connects floors, doesn't rest
     *   on one).
     * - Floor 0 (lobby level): always supported
     * - Above ground (floor > 0): must have support directly BELOW (floor - 1)
     * - Underground (floor < 0): must have support directly ABOVE (floor + height)
     * - Stairs/escalators can bridge floors (exempt from strict support)
     *   but still need SOME connection to existing structure */
    if (item_is_elevator(type) || floor == 0) {
        /* Elevator shafts and the ground floor are always placeable */
    } else if (is_transport) {
        /* Transport (stairs/escalators) connect two floors — need content on BOTH.
         * Stairs/escalators span 2 floors (floor and floor+1).
         * Both floors must have existing content (lobby, office, etc.)
         * for the stairs to be useful. */
        int has_lower = 0, has_upper = 0;
        int lower_idx = floor_to_index(floor);
        int upper_idx = floor_to_index(floor + height - 1);
        
        /* Both connected floors must actually be BUILT (have content on the
         * floor itself). The old code also accepted a floor one level
         * below/above as a fallback — which let you drop a stair/escalator
         * onto an empty dirt floor as long as some neighbouring level was
         * built. That's the "escalator into dirt" bug; require real content
         * on the two floors the transport connects. */
        if (lower_idx >= 0 && lower_idx < TOWER_FLOOR_COUNT) {
            for (int cx = 0; cx < TOWER_WIDTH && !has_lower; cx++) {
                if (tower->grid[lower_idx][cx].type != ITEM_NONE)
                    has_lower = 1;
            }
        }
        if (upper_idx >= 0 && upper_idx < TOWER_FLOOR_COUNT) {
            for (int cx = 0; cx < TOWER_WIDTH && !has_upper; cx++) {
                if (tower->grid[upper_idx][cx].type != ITEM_NONE)
                    has_upper = 1;
            }
        }

        if (!has_lower || !has_upper) {
            printf("  [reject] %s at F%d: needs content on both floors (lower=%d upper=%d)\n",
                   tower_item_name(type), floor, has_lower, has_upper);
            return 0;
        }
    } else if (floor > 0) {
        /* Above ground: needs SOME support directly below (the floor-below has
         * content somewhere under the footprint). Empty floorspace on a built
         * floor stays freely buildable — Jonah's rule. */
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

/* Fill empty cells BETWEEN the outermost built cells on a floor with plain
 * floor, so a row never has gaps (swiss-cheese) between its tenants. Floor is
 * a cell-only type — no tenant record needed (renderer + reachability read the
 * grid). Cells outside the built span are left as open sky/dirt. */
static void fill_floor_gaps(Tower *tower, int floor)
{
    int fidx = floor_to_index(floor);
    if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) return;
    int left = -1, right = -1;
    for (int cx = 0; cx < TOWER_WIDTH; cx++) {
        if (tower->grid[fidx][cx].type != ITEM_NONE) {
            if (left < 0) left = cx;
            right = cx;
        }
    }
    if (left < 0) return;
    for (int cx = left; cx <= right; cx++) {
        TowerCell *cell = &tower->grid[fidx][cx];
        if (cell->type == ITEM_NONE) {
            cell->type = ITEM_FLOOR;
            cell->tenant_id = 0;
            cell->cell_index = 0;
            cell->flags = CELL_OCCUPIED;
        }
    }
}

uint16_t tower_place(Tower *tower, ItemType type, int floor, int x)
{
    if (!tower_can_place(tower, type, floor, x)) return 0;
    if (tower->tenant_count >= MAX_TENANTS) return 0;
    
    int width = ITEM_WIDTH[type];
    int height = ITEM_HEIGHT[type];
    int cost = ITEM_COST[type];
    
    /* Lobby segments: check if extending an existing lobby on this floor.
     * If so, just fill the new cells — don't create a duplicate tenant.
     * From LobbyMake: TWO records per slot, but we simplify to one tenant
     * that grows. Cost = $5,000 per segment (not per cell). */
    if (type == ITEM_LOBBY) {
        int fidx = floor_to_index(floor);
        /* Check if there's already a lobby on this floor */
        Tenant *existing_lobby = NULL;
        for (int i = 0; i < tower->tenant_count; i++) {
            if (tower->tenants[i].type == ITEM_LOBBY && tower->tenants[i].floor == floor) {
                existing_lobby = &tower->tenants[i];
                break;
            }
        }
        if (existing_lobby) {
            /* Extend existing lobby to span [final_left, final_right). The
             * original (OpenSkyscraper Game.cpp ICON_LOBBY) grows the single
             * lobby item's size to cover a far click — the lobby is ONE
             * CONTIGUOUS structure, never a span with holes. So fill EVERY
             * cell in the final span as lobby, not just the clicked segment;
             * leaving the gap empty was the bug (no floor support there →
             * couldn't build straddling, and the endcap tiling broke). */
            int old_left = existing_lobby->x;
            int old_right = existing_lobby->x + existing_lobby->width;
            int new_left = x;
            int new_right = x + width;
            int final_left = (new_left < old_left) ? new_left : old_left;
            int final_right = (new_right > old_right) ? new_right : old_right;

            /* Count cells that aren't lobby yet (the gap + the new segment),
             * so we charge per newly-built segment rather than per click. */
            int new_cells = 0;
            for (int cx = final_left; cx < final_right; cx++) {
                ItemType e = tower->grid[fidx][cx].type;
                if (e != ITEM_LOBBY && !item_is_elevator(e)) new_cells++;
            }

            existing_lobby->x = final_left;
            existing_lobby->width = final_right - final_left;
            /* Fill the WHOLE contiguous span as lobby — except shaft cells,
             * which the shaft keeps (the lobby passes behind it). */
            for (int cx = final_left; cx < final_right; cx++) {
                TowerCell *cell = &tower->grid[fidx][cx];
                if (item_is_elevator(cell->type)) continue;
                cell->type = ITEM_LOBBY;
                cell->tenant_id = existing_lobby->id;
                cell->cell_index = cx - final_left;
                cell->flags = CELL_OCCUPIED |
                              (cell->flags & CELL_TRANSPORT_OVERLAY);
            }
            if (new_cells > 0) {
                /* $cost per width-sized segment newly covered (round up). */
                int segs = (new_cells + width - 1) / width;
                int charge = cost * segs;
                tower->money -= charge;
                tower->built_value += charge;
                printf("Extended lobby on F%d: now x=%d w=%d (+%d cells, cost $%d, balance $%ld)\n",
                       floor, final_left, existing_lobby->width, new_cells, charge, tower->money);
            }
            return existing_lobby->id;
        }
        /* Otherwise fall through to create a new lobby tenant */
    }
    
    /* Deduct cost */
    tower->money -= cost;
    tower->built_value += cost;

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
    /* Transports appear instantly — the EXE shows no build animation on
     * shafts or stairs, and update_tenants skips transports entirely, so a
     * transport left in TENANT_CONSTRUCTION would never finish (the
     * everlasting-construction-workers-on-the-shaft bug). */
    if (item_is_transport(type)) t->construction = 0;
    t->population = 0;
    t->stress = 0;
    t->complaints = 0;
    t->zone = (floor >= 0) ? floor / 15 : 0;  /* JudgeT: 7 zones of 15 floors */
    t->upgrade_day = 0;
    t->rent_class = 1;
    /* Fresh units start on the market with no verdict yet (creation
     * defaults, 2026-07-11 vacancy referee: armed=1, category=0xFF) */
    t->demand_armed = 1;
    t->demand_category = 0xFF;
    if (type == ITEM_CINEMA)          t->movie_id = (uint8_t)(rand() % 14);
    else if (type == ITEM_PARTY_HALL) t->movie_id = 0xFF;  /* Average until JudgeT moves it */
    
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
                /* an existing shaft keeps its grid cells — the room spans
                 * through it (tower_remove's repair pass restores these
                 * cells to the room if the shaft goes) */
                if (item_is_elevator(cell->type) && !item_is_elevator(type))
                    continue;
                cell->type = type;
                cell->tenant_id = id;
                cell->cell_index = cx - x;
                /* keep any stair/escalator overlay riding on this cell */
                cell->flags = CELL_OCCUPIED |
                              (cell->flags & CELL_TRANSPORT_OVERLAY);
            }
        } else {
            /* Mark transport presence with a flag but keep existing cell data */
            for (int cx = x; cx < x + width; cx++) {
                TowerCell *cell = &tower->grid[fidx][cx];
                cell->flags |= CELL_TRANSPORT_OVERLAY;
            }
        }
    }
    
    /* No swiss-cheese: fill any gaps between tenants on each floor this item
     * occupies with plain floor. (Deliberate multi-tower gaps are a future mod.) */
    if (!is_transport && type != ITEM_FLOOR) {
        for (int f = floor; f < floor + height; f++)
            fill_floor_gaps(tower, f);
    }

    printf("Placed %s at floor %d, x=%d (cost $%d, balance $%ld)\n",
           tower_item_name(type), floor, x, cost, tower->money);

    return id;
}

int tower_remove(Tower *tower, uint16_t tenant_id)
{
    Tenant *t = tower_tenant(tower, tenant_id);
    if (!t) return 0;

    /* The lobby is structural and permanent — the bulldozer can't remove it
     * (matches the original: you can never demolish the ground lobby). */
    if (t->type == ITEM_LOBBY) return 0;

    /* Value accounting: bulldozed construction is lost value */
    tower->built_value -= ITEM_COST[t->type];
    tower->lost_value += ITEM_COST[t->type];

    /* Stairs/escalators are stored as an OVERLAY (flag bit 1), not as the cell
     * type — they sit on top of the floor/tenant. Removing one must only drop
     * the overlay, never wipe what's underneath. */
    int is_transport = (t->type == ITEM_STAIRS || t->type == ITEM_ESCALATOR);

    /* Clear grid cells on all floors */
    for (int f = t->floor; f < t->floor + t->height; f++) {
        int idx = floor_to_index(f);
        if (idx < 0 || idx >= TOWER_FLOOR_COUNT) continue;
        for (int cx = t->x; cx < t->x + t->width; cx++) {
            TowerCell *cell = &tower->grid[idx][cx];
            /* A shaft stamped over this tenant's cells owns them now —
             * removing the tenant must not punch holes in the shaft. */
            if (!is_transport && cell->tenant_id != t->id) continue;
            if (is_transport) {
                cell->flags &= ~CELL_TRANSPORT_OVERLAY;  /* drop overlay, keep cell */
            } else if (t->type != ITEM_FLOOR && f >= 0) {
                /* Bulldozing a facility removes the TENANT but leaves the build
                 * floor behind (Jonah's ask: "delete tenants, not the floor"). */
                cell->type = ITEM_FLOOR;
                cell->tenant_id = 0;
                cell->cell_index = 0;
                cell->flags = (cell->flags & CELL_TRANSPORT_OVERLAY) | CELL_OCCUPIED;  /* preserve any overlay */
            } else {
                /* Explicitly removing a FLOOR tile (or an underground cell) ->
                 * back to bare dirt, but don't clobber a transport overlay. */
                uint8_t keep_overlay = cell->flags & CELL_TRANSPORT_OVERLAY;
                memset(cell, 0, sizeof(*cell));
                cell->flags = keep_overlay;
            }
        }
    }
    
    /* Restore cells the removed item had stamped over other tenants (a
     * bulldozed shaft that passed through rooms): re-assert every surviving
     * non-overlay tenant whose footprint intersects the cleared rect. */
    if (item_is_elevator(t->type)) {
        int rf0 = t->floor, rf1 = t->floor + t->height - 1;
        int rx0 = t->x, rx1 = t->x + t->width - 1;
        for (int i = 0; i < tower->tenant_count; i++) {
            Tenant *o = &tower->tenants[i];
            if (o == t || o->type == ITEM_NONE ||
                o->type == ITEM_STAIRS || o->type == ITEM_ESCALATOR) continue;
            if (o->floor > rf1 || o->floor + o->height - 1 < rf0) continue;
            if (o->x > rx1 || o->x + o->width - 1 < rx0) continue;
            for (int f = o->floor; f < o->floor + o->height; f++) {
                int idx = floor_to_index(f);
                if (idx < 0 || idx >= TOWER_FLOOR_COUNT) continue;
                for (int cx = o->x; cx < o->x + o->width; cx++) {
                    TowerCell *cell = &tower->grid[idx][cx];
                    if (cell->tenant_id) continue;   /* not a cleared cell */
                    cell->type = o->type;
                    cell->tenant_id = o->id;
                    cell->cell_index = (uint8_t)(cx - o->x);
                    cell->flags = CELL_OCCUPIED |
                                  (cell->flags & CELL_TRANSPORT_OVERLAY);
                }
            }
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
        "Stairs", "Escalator", "Elevator",
        "Service Elev", "Express Elev", "Housekeeping", "Ramp"
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
                /* an existing shaft keeps its grid cells — the room spans
                 * through it (tower_remove's repair pass restores these
                 * cells to the room if the shaft goes) */
                if (item_is_elevator(cell->type) && !item_is_elevator(type))
                    continue;
                cell->type = type;
                cell->tenant_id = id;
                cell->cell_index = cx - x;
                /* keep any stair/escalator overlay riding on this cell */
                cell->flags = CELL_OCCUPIED |
                              (cell->flags & CELL_TRANSPORT_OVERLAY);
            }
        } else {
            for (int cx = x; cx < x + width; cx++) {
                tower->grid[fidx][cx].flags |= CELL_TRANSPORT_OVERLAY;
            }
        }
    }
    
    return id;
}

/* Import placement: explicit width, no money, bounds-check only.
 * Same cell stamping as tower_force_place; .TDT floor/lobby strips have
 * file-defined widths that don't match ITEM_WIDTH. */
uint16_t tower_import_item(Tower *tower, ItemType type, int floor, int x,
                           int width)
{
    if (tower->tenant_count >= MAX_TENANTS) return 0;
    int height = ITEM_HEIGHT[type];
    if (width <= 0) width = ITEM_WIDTH[type];
    if (x < 0 || x + width > TOWER_WIDTH) return 0;
    /* imports go to the storage top: the cathedral lives above the ceiling */
    if (floor < TOWER_MIN_FLOOR || floor + height - 1 > TOWER_TOP_FLOOR) return 0;

    uint16_t id = tower->next_tenant_id++;
    Tenant *t = &tower->tenants[tower->tenant_count++];
    memset(t, 0, sizeof(*t));
    t->id = id;
    t->type = type;
    t->floor = floor;
    t->x = x;
    t->width = width;
    t->height = height;
    t->state = TENANT_OCCUPIED;
    t->capacity = CAP_MIN;
    t->zone = (floor >= 0) ? floor / 15 : 0;
    t->rent_class = 1;
    /* Fresh units start on the market with no verdict yet (creation
     * defaults, 2026-07-11 vacancy referee: armed=1, category=0xFF) */
    t->demand_armed = 1;
    t->demand_category = 0xFF;
    if (type == ITEM_CINEMA)          t->movie_id = (uint8_t)(rand() % 14);
    else if (type == ITEM_PARTY_HALL) t->movie_id = 0xFF;

    int is_transport = (type == ITEM_STAIRS || type == ITEM_ESCALATOR);
    for (int f = floor; f < floor + height; f++) {
        int fidx = floor_to_index(f);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
        for (int cx = x; cx < x + width; cx++) {
            TowerCell *cell = &tower->grid[fidx][cx];
            if (is_transport) {
                cell->flags |= CELL_TRANSPORT_OVERLAY;
            } else {
                cell->type = type;
                cell->tenant_id = id;
                cell->cell_index = cx - x;
                cell->flags = CELL_OCCUPIED;
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

void tower_floor_extents(const Tower *tower, int16_t *left, int16_t *right)
{
    for (int i = 0; i < TOWER_FLOOR_COUNT; i++) {
        left[i] = TOWER_WIDTH;
        right[i] = 0;
    }
    for (int i = 0; i < tower->tenant_count; i++) {
        const Tenant *t = &tower->tenants[i];
        if (t->type == ITEM_NONE || item_is_transport(t->type)) continue;
        for (int f = t->floor; f < t->floor + t->height; f++) {
            int fi = floor_to_index(f);
            if (fi < 0 || fi >= TOWER_FLOOR_COUNT) continue;
            if (t->x < left[fi]) left[fi] = t->x;
            if (t->x + t->width > right[fi]) right[fi] = t->x + t->width;
        }
    }
}
