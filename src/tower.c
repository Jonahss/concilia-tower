/* tower.c - Tower grid implementation */
#include <stdlib.h>
#include "tower.h"
#include "game.h"  /* For CONSTRUCTION_TIME[], CAP_* defines */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* ---- Player-facing rejection reason (res 0x3eb strings) ----
 * Every refusal in tower_can_place/tower_remove records the original game's
 * placement-error text here; the UI shows it when a click actually fails.
 * Module-static (not in Tower) so the save format stays free of UI state. */
static char last_reject[160];

const char *tower_reject_reason(void)
{
    return last_reject;
}

static void set_reject(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(last_reject, sizeof last_reject, fmt, ap);
    va_end(ap);
    printf("  [reject] %s\n", last_reject);
}

#define REJECT(...) do { set_reject(__VA_ARGS__); return 0; } while (0)

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

/* ---- Person-name registry (NameT seg_1188: 20 slots, 15-char names) ---- */

static struct PersonNameSlot *person_name_find(Tower *tower,
                                               uint16_t tid, int member)
{
    for (int i = 0; i < 20; i++) {
        struct PersonNameSlot *s = &tower->person_names[i];
        if (s->tenant_id == tid && s->member == (uint8_t)member)
            return s;
    }
    return NULL;
}

const char *tower_person_name(const Tower *tower, uint16_t tid, int member)
{
    struct PersonNameSlot *s = person_name_find((Tower *)tower, tid, member);
    return (s && s->name[0]) ? s->name : NULL;
}

int tower_person_name_set(Tower *tower, uint16_t tid, int member,
                          const char *name)
{
    if (!name || !name[0]) {
        tower_person_name_clear(tower, tid, member);
        return 0;
    }
    struct PersonNameSlot *s = person_name_find(tower, tid, member);
    if (!s) {
        if (tower->person_name_count >= 20) return -1;
        for (int i = 0; i < 20 && !s; i++)
            if (!tower->person_names[i].tenant_id)
                s = &tower->person_names[i];
        if (!s) return -1;
        tower->person_name_count++;
        s->tenant_id = tid;
        s->member = (uint8_t)member;
    }
    snprintf(s->name, sizeof s->name, "%s", name);
    return 0;
}

void tower_person_name_clear(Tower *tower, uint16_t tid, int member)
{
    struct PersonNameSlot *s = person_name_find(tower, tid, member);
    if (!s || !s->tenant_id) return;
    memset(s, 0, sizeof *s);
    if (tower->person_name_count > 0) tower->person_name_count--;
}

/* Count elevator groups the way the transport collector does: one group per
 * contiguous same-type vertical run of shaft cells (left column only). */
int tower_shaft_group_count(const Tower *tower)
{
    int count = 0;
    for (int x = 0; x < TOWER_WIDTH; x++) {
        for (int f = 0; f < TOWER_FLOOR_COUNT; ) {
            const TowerCell *c = &tower->grid[f][x];
            if (!item_is_elevator(c->type) || c->cell_index != 0) { f++; continue; }
            ItemType ty = c->type;
            while (f < TOWER_FLOOR_COUNT && tower->grid[f][x].type == ty &&
                   tower->grid[f][x].cell_index == 0) f++;
            count++;
        }
    }
    return count;
}

/* ==== Floor-deck economics (MoneyT TerrainCost 1178:0583, byte-verified
 * 2026-07-29) ====
 * A floor's deck is ONE contiguous span; building charges only cells
 * outside the current span, at $500/cell — identical above and below
 * ground (the EXE has no excavation premium) — except the ground-lobby
 * band, where a cell costs $5,000 x band height. Every placement pays
 * this on top of its item cost (ChargeBuild = ItemCost + TerrainCost),
 * which is how the EXE prices the gap-fill and the overhang cells. */

/* Ground-lobby band height (seg21:0x1310 in_lobby_band): floors 0..2
 * carrying a lobby. The build path can only make a 1-high lobby today,
 * but .TDT imports bring in 2-3-high grand lobbies. */
static int deck_lobby_band(const Tower *tower)
{
    int h = 1;
    for (int f = 1; f <= 2; f++) {
        int found = 0;
        for (int i = 0; i < tower->tenant_count && !found; i++)
            if (tower->tenants[i].type == ITEM_LOBBY &&
                tower->tenants[i].floor == f)
                found = 1;
        if (!found) break;
        h++;
    }
    return h;
}

int tower_deck_price(const Tower *tower, int floor)
{
    if (floor >= 0 && floor < deck_lobby_band(tower))
        return ITEM_COST[ITEM_LOBBY] * deck_lobby_band(tower);
    return ITEM_COST[ITEM_FLOOR];
}

/* Current deck extent [*left, *right) on a floor row, from the grid
 * (any built cell counts — shafts carry their own deck stubs in the
 * EXE, EnsureFloorDeckUnderShaft). Returns 0 when the floor is bare. */
static int deck_extent(const Tower *tower, int fidx, int *left, int *right)
{
    int L = -1, R = -1;
    for (int cx = 0; cx < TOWER_WIDTH; cx++) {
        if (tower->grid[fidx][cx].type != ITEM_NONE) {
            if (L < 0) L = cx;
            R = cx + 1;
        }
    }
    if (L < 0) return 0;
    *left = L; *right = R;
    return 1;
}

long tower_deck_extend_cost(const Tower *tower, int floor, int x1, int x2)
{
    int fidx = floor_to_index(floor);
    if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) return 0;
    if (x1 < 0) x1 = 0;
    if (x2 > TOWER_WIDTH) x2 = TOWER_WIDTH;
    if (x2 <= x1) return 0;
    int L, R;
    long cells;
    if (!deck_extent(tower, fidx, &L, &R))
        cells = x2 - x1;
    else
        cells = (L > x1 ? L - x1 : 0) + (x2 > R ? x2 - R : 0);
    return cells * (long)tower_deck_price(tower, floor);
}

/* Validate + price a floor-tool span. Returns the charge, or -1 with the
 * reject reason set. u1 and u2 get the resulting union span to stamp. */
static long deck_check(Tower *tower, int floor, int x1, int x2,
                       int *u1, int *u2)
{
    if (floor < TOWER_MIN_FLOOR || floor > TOWER_MAX_FLOOR) {
        set_reject("Maximum height has been reached");
        return -1;
    }
    int fidx = floor_to_index(floor);
    if (x1 < 0) x1 = 0;
    if (x2 > TOWER_WIDTH) x2 = TOWER_WIDTH;
    if (x2 <= x1) { set_reject("Cannot place item there"); return -1; }

    long cost = tower_deck_extend_cost(tower, floor, x1, x2);

    int L, R;
    if (deck_extent(tower, fidx, &L, &R)) {
        *u1 = x1 < L ? x1 : L;
        *u2 = x2 > R ? x2 : R;
    } else {
        *u1 = x1; *u2 = x2;
    }

    /* Decks grow only over decks (below-ground: under them): every NEW
     * cell needs deck directly below (above ground) or above (basement).
     * Floor 0 rests on the ground itself. */
    if (floor != 0) {
        int sup = floor_to_index(floor > 0 ? floor - 1 : floor + 1);
        if (sup < 0 || sup >= TOWER_FLOOR_COUNT) {
            set_reject("Cannot place item there");
            return -1;
        }
        for (int cx = *u1; cx < *u2; cx++) {
            if (tower->grid[fidx][cx].type != ITEM_NONE) continue;
            if (tower->grid[sup][cx].type == ITEM_NONE) {
                set_reject(floor > 0
                           ? "Cannot place items wider than floor below"
                           : "Cannot place item there");
                return -1;
            }
        }
    }

    if (tower->money < cost) {
        set_reject("Not enough money to build floor");
        return -1;
    }
    return cost;
}

int tower_extend_deck(Tower *tower, int floor, int x1, int x2)
{
    last_reject[0] = '\0';
    int u1, u2;
    long cost = deck_check(tower, floor, x1, x2, &u1, &u2);
    if (cost < 0) return 0;
    int fidx = floor_to_index(floor);
    for (int cx = u1; cx < u2; cx++) {
        TowerCell *cell = &tower->grid[fidx][cx];
        if (cell->type != ITEM_NONE) continue;
        cell->type = ITEM_FLOOR;
        cell->tenant_id = 0;
        cell->cell_index = 0;
        cell->flags = CELL_OCCUPIED | (cell->flags & CELL_TRANSPORT_OVERLAY);
    }
    tower->money -= cost;
    tower->built_value += cost;
    printf("Extended deck on floor %d to x=%d..%d (cost $%ld, balance $%ld)\n",
           floor, u1, u2, cost, tower->money);
    return 1;
}

/* An elevator segment touching a same-type segment above or below at the
 * same column is an EXTENSION of that shaft, not a new one — it charges
 * no item cost (ExtendUp/Down charge pure TerrainCost, seg21 drag trace
 * 2026-07-28) and doesn't count against the 24-group cap. */
static int elevator_extends_column(const Tower *tower, ItemType type,
                                   int floor, int x)
{
    for (int df = -1; df <= 1; df += 2) {
        int fi = floor_to_index(floor + df);
        if (fi >= 0 && fi < TOWER_FLOOR_COUNT &&
            tower->grid[fi][x].type == type)
            return 1;
    }
    return 0;
}

/* The lobby's charge (LobbyMake via ChargeBuild): per-cell, not per
 * 4-cell segment. Ground lobby has NO item cost — it is priced entirely
 * through TerrainCost's lobby-band row ($5,000 x band height per newly
 * decked cell; converting existing deck to lobby is free). Sky lobbies
 * pay $5,000 per new lobby cell plus normal deck cost for any extent
 * growth. Charges the whole union span (a lobby is one contiguous run —
 * a far click fills the gap too, and pays for it). */
static long lobby_charge(const Tower *tower, int floor, int x, int width)
{
    int fidx = floor_to_index(floor);
    if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) return 0;
    int u1 = x, u2 = x + width;
    for (int i = 0; i < tower->tenant_count; i++) {
        const Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_LOBBY || t->floor != floor) continue;
        if (t->x < u1) u1 = t->x;
        if (t->x + t->width > u2) u2 = t->x + t->width;
        break;
    }
    if (u2 > TOWER_WIDTH) u2 = TOWER_WIDTH;
    if (u1 < 0) u1 = 0;
    long deck = tower_deck_extend_cost(tower, floor, u1, u2);
    if (floor == 0) return deck;
    long cells = 0;
    for (int cx = u1; cx < u2; cx++) {
        ItemType e = tower->grid[fidx][cx].type;
        if (e != ITEM_LOBBY && !item_is_elevator(e)) cells++;
    }
    return cells * (long)ITEM_COST[ITEM_LOBBY] + deck;
}

int tower_can_place(Tower *tower, ItemType type, int floor, int x)
{
    last_reject[0] = '\0';
    if (type <= ITEM_NONE || type >= ITEM_TYPE_COUNT) return 0;

    int width = ITEM_WIDTH[type];
    int height = ITEM_HEIGHT[type];
    int cost = ITEM_COST[type];

    /* The floor tool is deck extension, not an item: per-cell charge for
     * cells outside the floor's extent (build dispatch 0dc4 -> deck
     * builder 17fd -> ChargeBuild(0,...) with ItemCost(0) = 0). */
    if (type == ITEM_FLOOR) {
        int u1, u2;
        return deck_check(tower, floor, x, x + width, &u1, &u2) >= 0;
    }

    /* Check bounds — multi-floor items extend upward from placement floor */
    if (x < 0 || x + width > TOWER_WIDTH)
        REJECT("Cannot place item there");
    if (floor < TOWER_MIN_FLOOR || floor > TOWER_MAX_FLOOR)
        REJECT("Maximum height has been reached");
    /* The cathedral is the one item exempt from the build ceiling (its
     * pieces are excluded from the max-height gate at 11f8:2f95) — it
     * stands F100..F104 in the storage range above the ceiling. */
    if (floor + height - 1 > (type == ITEM_CATHEDRAL ? TOWER_TOP_FLOOR
                                                     : TOWER_MAX_FLOOR))
        REJECT("Maximum height has been reached");

    /* Cathedral placement is pinned to a base at the build ceiling
     * (11f8:308f: hardcoded compare — base lands on the original's top
     * buildable floor, spire rising above the ceiling). The port's
     * ceiling is its floor 100, so the gate is floor == 100 here; note
     * .TDT imports land one lower (file 109 = port 99) because the
     * port numbers ground as 0 where the original displays 1F. The
     * 5-star unlock stays on the toolbar layer. */
    if (type == ITEM_CATHEDRAL && floor != TOWER_MAX_FLOOR)
        REJECT("Cathedral is available only on 100th floor");

    /* Check funds — the EXE's two-stage gate (CanAffordBuild 1178:009e):
     * item cost alone unaffordable -> msg #7; item + deck (TerrainCost
     * for footprint cells outside each floor's extent) -> msg #8. An
     * elevator segment extending an existing column has no item cost. */
    {
        long icost = cost;
        if (item_is_elevator(type) &&
            elevator_extends_column(tower, type, floor, x))
            icost = 0;
        long terrain = 0;
        for (int f = floor; f < floor + height; f++)
            terrain += tower_deck_extend_cost(tower, f, x, x + width);
        if (tower->money < icost)
            REJECT("Not enough money for construction");
        if (tower->money < icost + terrain)
            REJECT("Not enough money to build floor");
    }

    /* Lobby placement: 4-cell segments on every 15th floor.
     * Can overlap existing lobby cells (extends the lobby).
     * From LobbyMake (seg_11e8) + OpenSkyscraper Game.cpp. */
    if (type == ITEM_LOBBY) {
        if (floor % 15 != 0)
            REJECT("Lobbys are only every 15 floors");
        if (floor < TOWER_MIN_FLOOR || floor > TOWER_MAX_FLOOR)
            REJECT("Maximum height has been reached");
        if (x < 0 || x + width > TOWER_WIDTH)
            REJECT("Cannot place item there");
        if (tower->money < lobby_charge(tower, floor, x, width))
            REJECT(floor == 0 ? "Not enough money to build floor"
                              : "Not enough money for construction");
        /* Lobby segments CAN overlap existing lobby — that's how extension works.
         * But they can't overlap non-lobby items. */
        int fidx = floor_to_index(floor);
        if (fidx >= 0 && fidx < TOWER_FLOOR_COUNT) {
            for (int cx = x; cx < x + width; cx++) {
                ItemType existing = tower->grid[fidx][cx].type;
                if (item_is_elevator(existing)) continue;  /* lobby coexists with shafts */
                if (existing != ITEM_NONE && existing != ITEM_LOBBY &&
                    existing != ITEM_FLOOR)
                    REJECT("Cannot place on top of other items");
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
            if (!has_support)
                REJECT("Cannot place item there");
        }
        return 1;
    }
    
    /* Stairs/escalators OVERLAY existing floors; elevators occupy their own
     * cells but share transports' placement freedoms (any floor, basement). */
    int is_transport = (type == ITEM_STAIRS || type == ITEM_ESCALATOR);

    /* Floor 0 is lobby-only — except transports: elevators and stairs connect
     * at the ground lobby in the original. */
    if (floor == 0 && type != ITEM_LOBBY && type != ITEM_FLOOR && !item_is_transport(type))
        REJECT("First floor is only for Lobby");

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
        if (!touches)
            REJECT("New express shafts anchor at lobby floors (every 15th)");
    }

    /* Shaft clearance (CheckElevatorClearance 10a0:10e8 — seg44 drag trace
     * 2026-07-28, correcting the earlier "no spacing rule" note): the
     * candidate rect, inflated 8 CELLS horizontally and (-2,+1) floors
     * vertically, must not touch ANOTHER shaft; stairs/escalators are
     * checked against the un-inflated rect. The EXE runs this on the
     * initial click AND on every drag extension (msg 0x16 "too close"). */
    if (item_is_elevator(type)) {
        for (int f = floor - 2; f <= floor + 1; f++) {
            int fi2 = floor_to_index(f);
            if (fi2 < 0 || fi2 >= TOWER_FLOOR_COUNT) continue;
            for (int cx = x - 8; cx < x + width + 8; cx++) {
                if (cx < 0 || cx >= TOWER_WIDTH) continue;
                const TowerCell *c = &tower->grid[fi2][cx];
                int inside = (cx >= x && cx < x + width);
                if (item_is_elevator(c->type) &&
                    (c->type != type || !inside))
                    REJECT("Item requires more space on both sides");
                if (inside && (c->flags & CELL_TRANSPORT_OVERLAY))
                    REJECT("Cannot place over other transportation items");
            }
        }

        /* Standard/service shafts serve at most 30 floors (ExtendUp's
         * 29-floor span clamp, msg 0x23 — express is exempt, its reach is
         * bounded by the tower instead). Reject the segment that would
         * stretch this column's contiguous run past the limit. */
        if (type != ITEM_ELEVATOR_EXPRESS) {
            int fi0 = floor_to_index(floor), run = 1;
            for (int f = fi0 - 1; f >= 0 &&
                 tower->grid[f][x].type == type; f--) run++;
            for (int f = fi0 + 1; f < TOWER_FLOOR_COUNT &&
                 tower->grid[f][x].type == type; f++) run++;
            if (run > 30)
                REJECT("Elevator shaft can cover only 30 floors");
        }

        /* Hard cap: 24 elevator groups tower-wide (the EXE's new-shaft slot
         * scan, seg_11f8_tenant.c:527-530 — "max 0x18 = 24 groups; none
         * free -> reject"). Only a segment that STARTS a new column run
         * counts; touching an existing same-type run vertically is an
         * extension of that group. Without this gate a 25th shaft takes the
         * player's money and silently never gets cars (the sim-side
         * MAX_SHAFTS collector stops at 24). */
        if (!elevator_extends_column(tower, type, floor, x) &&
            tower_shaft_group_count(tower) >= TOWER_MAX_SHAFT_GROUPS)
            REJECT("No more elevator shafts available");
    }

    /* Underground-only items must be below floor 0 */
    if (ITEM_UNDERGROUND_ONLY[type] && floor >= 0)
        REJECT("Item unavailable above ground");
    if (!item_is_transport(type) && !ITEM_UNDERGROUND_ONLY[type] &&
        type != ITEM_LOBBY && type != ITEM_FLOOR && floor < 0)
        REJECT("Item not available underground");

    /* Singletons and fixed-table caps (placement dispatcher seg_11f8 +
     * StairsT seg_10c0): one metro ([0xB3E8], msg "Only one Metro Station
     * allowed"), one cathedral ([0xB3EC]), 16 venue records (cinemas +
     * party halls, [0xB400]), and ONE shared 64-record table for stairs
     * and escalators together (0xBD70 — the reject message differs by
     * type, the table doesn't). */
    if (type == ITEM_METRO || type == ITEM_CATHEDRAL || type == ITEM_CINEMA ||
        type == ITEM_PARTY_HALL || type == ITEM_STAIRS || type == ITEM_ESCALATOR ||
        type == ITEM_RESTAURANT || type == ITEM_SHOP || type == ITEM_FAST_FOOD ||
        type == ITEM_MEDICAL || type == ITEM_SECURITY) {
        int metros = 0, cathedrals = 0, venues = 0, walks = 0;
        int commercial = 0, medicals = 0, securities = 0;
        for (int i = 0; i < tower->tenant_count; i++) {
            switch (tower->tenants[i].type) {
            case ITEM_METRO:      metros++;     break;
            case ITEM_CATHEDRAL:  cathedrals++; break;
            case ITEM_CINEMA:
            case ITEM_PARTY_HALL: venues++;     break;
            case ITEM_STAIRS:
            case ITEM_ESCALATOR:  walks++;      break;
            case ITEM_RESTAURANT:
            case ITEM_SHOP:
            case ITEM_FAST_FOOD:  commercial++; break;
            case ITEM_MEDICAL:    medicals++;   break;
            case ITEM_SECURITY:   securities++; break;
            default: break;
            }
        }
        if (type == ITEM_METRO && metros > 0)
            REJECT("Only one Metro Station allowed");
        if (type == ITEM_CATHEDRAL && cathedrals > 0)
            REJECT("Only one Cathedral allowed");
        if ((type == ITEM_CINEMA || type == ITEM_PARTY_HALL) &&
            venues >= TOWER_MAX_VENUES)
            REJECT("Item no longer available");
        if ((type == ITEM_STAIRS || type == ITEM_ESCALATOR) &&
            walks >= TOWER_MAX_WALK_TRANSPORTS)
            REJECT(type == ITEM_STAIRS ? "No more stairs available"
                                       : "No more escalators available");
        /* Fixed-table caps from the pass-3 dispatch trace (2026-07-29):
         * restaurants + shops + fast food share ONE 512-record table
         * ([0xB3F8] < 0x200); medical and security cap at 10 each.
         * Recycling is genuinely uncapped in the EXE. */
        if ((type == ITEM_RESTAURANT || type == ITEM_SHOP ||
             type == ITEM_FAST_FOOD) && commercial >= 512)
            REJECT("Item no longer available");
        if (type == ITEM_MEDICAL && medicals >= 10)
            REJECT("Item no longer available");
        if (type == ITEM_SECURITY && securities >= 10)
            REJECT("Item no longer available");
    }

    /* Metro area rules (11f8:2fab + the metro handler at 11f8:3010):
     * nothing may sit at or below the platform level — the [0xB3E8] gate,
     * which ExtendDown also carries — and the station itself must bottom
     * in virgin, un-excavated ground below the current dig. */
    {
        const Tenant *metro = NULL;
        for (int i = 0; i < tower->tenant_count; i++)
            if (tower->tenants[i].type == ITEM_METRO) {
                metro = &tower->tenants[i];
                break;
            }
        if (metro && type != ITEM_METRO && floor <= metro->floor)
            REJECT("Cannot place items under Metro");
        if (type == ITEM_METRO) {
            int fidx = floor_to_index(floor);
            if (fidx >= 0 && fidx < TOWER_FLOOR_COUNT) {
                for (int cx = 0; cx < TOWER_WIDTH; cx++) {
                    ItemType e = tower->grid[fidx][cx].type;
                    if (e == ITEM_NONE) continue;
                    /* the EXE tolerates a lone bare-deck record on the
                     * very bottom floor and wipes it */
                    if (floor == TOWER_MIN_FLOOR && e == ITEM_FLOOR) continue;
                    REJECT("Place Metro station on bottom floor");
                }
            }
        }
    }

    /* Parking (ParkingT, byte-verified 2026-07-11 referee):
     * one ramp strip per basement floor (real .TDT saves store exactly
     * one); spaces need a same-floor ramp FIRST (CheckParkingGate,
     * build msg 0x22) and respect the global 512-space cap (msg 0x1E).
     * Chain usability is NOT a build gate — the EXE lets you place a
     * disconnected ramp and just marks its spaces unusable at the next
     * CheckAllParking sweep. */
    if (type == ITEM_RAMP) {
        /* Ramps form ONE vertical stack (pass-3 trace 11f8:0aa0): the
         * first ramp must sit on B1 ([0xB3EE] records its column, err
         * 0x1F), and every later ramp must share that column (err 0x20).
         * One per floor follows from the geometry; real saves agree. */
        int ramp_x = -1;
        for (int i = 0; i < tower->tenant_count; i++) {
            Tenant *t = &tower->tenants[i];
            if (t->type != ITEM_RAMP) continue;
            if (t->floor == floor)
                REJECT("This floor already has a Parking Ramp");
            ramp_x = t->x;
        }
        if (ramp_x < 0) {
            if (floor != -1)
                REJECT("Parking Ramps must connect to the 1st floor");
        } else if (x != ramp_x) {
            REJECT("Parking Ramps must be connected vertically");
        }
    }
    if (type == ITEM_PARKING) {
        int has_ramp = 0, spaces = 0;
        for (int i = 0; i < tower->tenant_count; i++) {
            Tenant *t = &tower->tenants[i];
            if (t->type == ITEM_RAMP && t->floor == floor) has_ramp = 1;
            if (t->type == ITEM_PARKING) spaces++;
        }
        if (!has_ramp)
            REJECT("Parking Ramps must be placed on this level");
        if (spaces >= 512)
            REJECT("Item no longer available");
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
                REJECT("Cannot place on top of other items");
            }
        }
    } else {
        /* Stairs/escalators — the traced StairsT model (2026-07-29;
         * validators 10c0:0775/087d, overlap 10c0:0983, driver 11f8:1452).
         * `floor` is the LOWER landing (the UI translates the click, which
         * the original treats as the UPPER landing). */
        int lf = floor_to_index(floor);
        int uf = floor_to_index(floor + 1);
        if (lf < 0 || uf < 0 || uf >= TOWER_FLOOR_COUNT)
            REJECT("Cannot place stairs here");

        if (type == ITEM_STAIRS) {
            /* Stairs test ONLY deck extents — no content requirement, no
             * commercial rule (players were right): lower deck must cover
             * x..x+7, upper deck x..x+8 (one cell past the exit, the
             * EXE's strict rightEdge > x+8). */
            for (int cx = x; cx < x + width; cx++)
                if (tower->grid[lf][cx].type == ITEM_NONE)
                    REJECT("Cannot place stairs here");
            for (int cx = x; cx < x + width + 1; cx++)
                if (cx >= TOWER_WIDTH || tower->grid[uf][cx].type == ITEM_NONE)
                    REJECT("Cannot place stairs here");
        } else {
            /* Escalators check the two LANDING cells only — entry (lower
             * floor, x) and exit (upper floor, x+7) — against a whitelist:
             * bare floor deck, restaurant, shop, fast food, cinema, lobby,
             * party hall, metro (cs:0855). So "commercial spaces" really
             * means "landings may not sit inside a non-commercial tenant";
             * two empty built floors are legal. Under-construction fails.
             * An unbuilt landing fails with the same message (the EXE finds
             * no record and emits 0x1c). Shaft-stamped cells pass here —
             * the elevator overlap check right after rejects them. */
            const struct { int f; int cx; } land[2] =
                { { lf, x }, { uf, x + width - 1 } };
            for (int i = 0; i < 2; i++) {
                const TowerCell *c = &tower->grid[land[i].f][land[i].cx];
                if (item_is_elevator(c->type)) continue;
                int ok;
                switch (c->type) {
                case ITEM_FLOOR: case ITEM_RESTAURANT: case ITEM_SHOP:
                case ITEM_FAST_FOOD: case ITEM_CINEMA: case ITEM_LOBBY:
                case ITEM_PARTY_HALL: case ITEM_METRO:
                    ok = 1; break;
                default:
                    ok = 0; break;
                }
                if (ok && c->tenant_id) {
                    const Tenant *lt = tower_tenant(tower, c->tenant_id);
                    if (lt && lt->state == TENANT_CONSTRUCTION) ok = 0;
                }
                if (!ok)
                    REJECT("Escalators available only at commercial spaces");
            }
        }

        /* Overlap (err 0x17). Elevator shafts block their pit/motor margin
         * too: any shaft cell on floors [lower-1, upper+2] in the
         * footprint's columns collides (candidate rect vs shaft rect
         * padded bottom-2 / top+1). */
        for (int f = floor - 1; f <= floor + 3; f++) {
            int fi = floor_to_index(f);
            if (fi < 0 || fi >= TOWER_FLOOR_COUNT) continue;
            for (int cx = x; cx < x + width; cx++)
                if (item_is_elevator(tower->grid[fi][cx].type))
                    REJECT("Cannot place over other transportation items");
        }

        /* Other stairs/escalators: HALF-TILE model — a unit is a lower-left
         * 4-cell half on its lower floor and an upper-right half on its
         * upper floor; only half-vs-half collisions on the same floor band
         * reject. Two units may share a floor pair 4 cells apart, and
         * same-column zigzag chains are legal. */
        int half = width / 2;
        for (int i = 0; i < tower->tenant_count; i++) {
            const Tenant *o = &tower->tenants[i];
            if (o->type != ITEM_STAIRS && o->type != ITEM_ESCALATOR) continue;
            /* candidate halves: (floor, [x,x+4)) and (floor+1, [x+4,x+8)) */
            const struct { int f; int x0; } mine[2] =
                { { floor, x }, { floor + 1, x + half } };
            const struct { int f; int x0; } theirs[2] =
                { { o->floor, o->x }, { o->floor + 1, o->x + half } };
            for (int a = 0; a < 2; a++)
                for (int b = 0; b < 2; b++)
                    if (mine[a].f == theirs[b].f &&
                        mine[a].x0 < theirs[b].x0 + half &&
                        theirs[b].x0 < mine[a].x0 + half)
                        REJECT("Cannot place over other transportation items");
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
        /* Stairs/escalators already ran their deck-extent (and escalator
         * landing) gates in the overlap section above — the EXE has no
         * further support rule for them. */
    } else if (floor > 0) {
        /* Above ground: the WHOLE footprint needs support directly below —
         * a floor's extent can't overhang the one under it (the EXE's
         * deck-extent gate, ValidatePlacementArea: a span must fit inside a
         * built floor, and decks grow only over decks). Partial support let
         * units cantilever off the tower edge — Jonah's overhang bug. */
        int below_idx = floor_to_index(floor - 1);
        if (below_idx < 0 || below_idx >= TOWER_FLOOR_COUNT)
            REJECT("Cannot place item there");
        for (int cx = x; cx < x + width; cx++) {
            if (tower->grid[below_idx][cx].type == ITEM_NONE)
                REJECT("Cannot place items wider than floor below");
        }
    } else {
        /* Underground (floor < 0): the whole footprint needs support above */
        int above_idx = floor_to_index(floor + height);
        if (above_idx < 0 || above_idx >= TOWER_FLOOR_COUNT)
            REJECT("Cannot place item there");
        for (int cx = x; cx < x + width; cx++) {
            if (tower->grid[above_idx][cx].type == ITEM_NONE)
                REJECT("Cannot place item there");
        }
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
    /* The floor tool builds deck cells, no tenant record — success
     * returns a sentinel id that maps to no tenant. */
    if (type == ITEM_FLOOR)
        return tower_extend_deck(tower, floor, x, x + ITEM_WIDTH[type])
                   ? UINT16_MAX : 0;

    if (!tower_can_place(tower, type, floor, x)) return 0;
    if (tower->tenant_count >= MAX_TENANTS) return 0;

    int width = ITEM_WIDTH[type];
    int height = ITEM_HEIGHT[type];
    int cost = ITEM_COST[type];
    long charged = cost;

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

            /* Price BEFORE stamping — per-cell, via the verified charge
             * (ground: TerrainCost's lobby-band row only; sky: $5,000 per
             * new lobby cell + deck growth). */
            long charge = lobby_charge(tower, floor, x, width);
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
            if (charge > 0) {
                tower->money -= charge;
                tower->built_value += charge;
                printf("Extended lobby on F%d: now x=%d w=%d (+%d cells, cost $%ld, balance $%ld)\n",
                       floor, final_left, existing_lobby->width, new_cells, charge, tower->money);
            }
            return existing_lobby->id;
        }
        /* Otherwise fall through to create a new lobby tenant */
    }
    
    /* Deduct item + deck cost (ChargeBuild 1178:01db = ItemCost +
     * TerrainCost on every floor of the footprint — the overhang and
     * gap-fill cells are paid for here). An elevator segment extending
     * an existing column is pure deck cost; a NEW lobby record prices
     * per-cell exactly like the extension path. */
    {
        long icost = cost;
        if (item_is_elevator(type) &&
            elevator_extends_column(tower, type, floor, x))
            icost = 0;
        long terrain = 0;
        for (int f = floor; f < floor + height; f++)
            terrain += tower_deck_extend_cost(tower, f, x, x + width);
        if (type == ITEM_LOBBY) {
            icost = lobby_charge(tower, floor, x, width);
            terrain = 0;
        }
        charged = icost + terrain;
    }
    tower->money -= charged;
    tower->built_value += charged;

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

    printf("Placed %s at floor %d, x=%d (cost $%ld, balance $%ld)\n",
           tower_item_name(type), floor, x, charged, tower->money);

    return id;
}

int tower_remove(Tower *tower, uint16_t tenant_id)
{
    last_reject[0] = '\0';
    Tenant *t = tower_tenant(tower, tenant_id);
    if (!t) return 0;

    /* The EXE's indestructible set (CanModifyTenant jump table 11f8:33f7):
     * every lobby (ground AND sky), security, housekeeping, metro,
     * cathedral. Everything else — including stairs, venues, medical,
     * recycling — demolishes freely. */
    switch (t->type) {
    case ITEM_LOBBY: case ITEM_SECURITY: case ITEM_HOUSEKEEPING:
    case ITEM_METRO: case ITEM_CATHEDRAL:
        REJECT("Cannot destroy this item");
    default: break;
    }

    /* The bulldozer refuses units still being built (same function,
     * 11f8:33a5: negated type byte = under construction). */
    if (t->state == TENANT_CONSTRUCTION)
        REJECT("Cannot destroy items under construction");

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
    /* Scan the GRID, not the tenant array: floor-tool deck and gap-fill
     * cells carry no tenant record, and shafts stamp their cells (the
     * EXE's auto-deck under shafts, EnsureFloorDeckUnderShaft 11f8:15f7).
     * Stairs/escalators are overlays — they never stamp a cell type, so
     * they naturally don't widen a floor. */
    for (int fi = 0; fi < TOWER_FLOOR_COUNT; fi++) {
        for (int cx = 0; cx < TOWER_WIDTH; cx++) {
            if (tower->grid[fi][cx].type == ITEM_NONE) continue;
            if (cx < left[fi]) left[fi] = (int16_t)cx;
            if (cx + 1 > right[fi]) right[fi] = (int16_t)(cx + 1);
        }
    }
}
