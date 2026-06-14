/* game.c - Simulation engine
 *
 * The beating heart: time flows, tenants work, money moves, stars are earned.
 * Core mechanics ported from decompiled SIMTOWER.EXE:
 *   - Star rating: seg_1140 (LevelT) FUN_1140_0411
 *   - Promotion:   seg_1148 (LevelUp) FUN_1148_0000
 *   - Population:  seg_1060 (CountT)
 *   - Time:        seg_11d8 (TimeT) / seg_1020 (AnimeT)
 */
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
static int calc_lobby_maintenance(Tower *tower);

/* Ticks per quarter at each speed */
static const int TICKS_PER_QUARTER[] = {
    [SPEED_PAUSED] = 0,
    [SPEED_NORMAL] = 720,    /* ~12 seconds at 60fps */
    [SPEED_FAST]   = 360,    /* ~6 seconds */
    [SPEED_TURBO]  = 120,    /* ~2 seconds */
};

/* Time of day boundaries (tick within a full day = 4 quarters) */
#define TICKS_PER_DAY(speed) (TICKS_PER_QUARTER[speed] * 4)

/* Map tick position within a day to hour (0-23) */
static void tick_to_time(int tick_in_day, int ticks_total, int *hour, int *minute)
{
    if (ticks_total <= 0) { *hour = 12; *minute = 0; return; }
    /* Day runs 5:00 → 5:00 (24 hours) */
    int minutes_in_day = 24 * 60;
    int elapsed_min = (tick_in_day * minutes_in_day) / ticks_total;
    int h = 5 + elapsed_min / 60;  /* Start at 5am */
    if (h >= 24) h -= 24;
    *hour = h;
    *minute = elapsed_min % 60;
}

static TimeOfDay hour_to_tod(int hour)
{
    if (hour >= 5 && hour < 7)   return TOD_DAWN;
    if (hour >= 7 && hour < 12)  return TOD_MORNING;
    if (hour >= 12 && hour < 17) return TOD_AFTERNOON;
    if (hour >= 17 && hour < 21) return TOD_EVENING;
    return TOD_NIGHT;
}

void game_init(GameSim *sim)
{
    memset(sim, 0, sizeof(*sim));
    sim->speed = SPEED_NORMAL;
    sim->ticks_per_quarter = TICKS_PER_QUARTER[SPEED_NORMAL];
    sim->hour = 7;  /* Start at 7am */
    sim->time_of_day = TOD_MORNING;
    people_init(&sim->people);
}

/* --- Population calculation --- */

int game_calc_population(GameSim *sim, Tower *tower)
{
    int pop = 0;
    int occupied = 0;
    
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type == ITEM_NONE || t->type == ITEM_LOBBY || t->type == ITEM_FLOOR)
            continue;
        if (item_is_transport(t->type)) continue;

        /* Nobody can be somewhere they can't get to */
        {
            int fidx = floor_to_index(t->floor);
            int is_staff = (t->type == ITEM_SECURITY || t->type == ITEM_HOUSEKEEPING);
            if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT ||
                !(is_staff ? sim->reach_service[fidx] : sim->reach_public[fidx])) {
                t->population = 0;
                continue;
            }
            if (t->dirty) {   /* dirty hotel room = no guests tonight */
                t->population = 0;
                continue;
            }
        }

        /* Check if this tenant type is active at current time of day */
        int type_idx = (int)t->type;
        if (type_idx < ITEM_TYPE_COUNT && TENANT_ACTIVE_TIMES[type_idx][sim->time_of_day]) {
            /* Active — contribute population based on state */
            if (t->state >= 1) {  /* At least MOVING_IN */
                int base_pop = (type_idx < ITEM_TYPE_COUNT) ? TENANT_POPULATION[type_idx] : 0;
                /* Office headcount scales with established occupancy (cap_peak):
                 * a thriving office (0x40) holds twice a new one's (0x20) staff. */
                if (t->type == ITEM_OFFICE && t->cap_peak > CAP_PEAK_LOW)
                    base_pop = base_pop * t->cap_peak / CAP_PEAK_LOW;
                /* Occupancy ramp: new tenants start at 50% pop, grow to 100% */
                int effective_pop = base_pop;
                if (t->state == 1) effective_pop = base_pop / 2;  /* moving in */
                t->population = effective_pop;
                pop += effective_pop;
                occupied++;
            }
        } else {
            /* Inactive right now — condos still count (people sleep there) */
            if (t->type == ITEM_CONDO && t->state >= 1) {
                t->population = TENANT_POPULATION[ITEM_CONDO];
                pop += t->population;
                occupied++;
            } else {
                t->population = 0;
            }
        }
    }
    
    tower->population = pop;
    sim->tenants_occupied = occupied;
    if (pop > sim->max_population) sim->max_population = pop;
    
    return pop;
}

/* --- Tower width measurement (for promotion check) ---
 * From seg_1148 FUN_1148_01a8: measures width on underground floors */

int game_measure_width(Tower *tower)
{
    int max_width = 0;
    for (int f = TOWER_MIN_FLOOR; f < 0; f++) {
        int fidx = floor_to_index(f);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
        
        int left = TOWER_WIDTH, right = -1;
        for (int x = 0; x < TOWER_WIDTH; x++) {
            if (tower->grid[fidx][x].type != ITEM_NONE) {
                if (x < left) left = x;
                if (x > right) right = x;
            }
        }
        if (right >= left) {
            int width = right - left + 1;
            if (width > max_width) max_width = width;
        }
    }
    return max_width;
}

/* --- Promotion flags scan --- */

static void scan_promotion_flags(GameSim *sim, Tower *tower)
{
    memset(&sim->promo, 0, sizeof(sim->promo));
    
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        switch (t->type) {
        case ITEM_SECURITY:  sim->promo.has_security = 1;  break;
        case ITEM_RECYCLING: sim->promo.has_recycling = 1; break;
        case ITEM_METRO:     sim->promo.has_metro = 1;     break;
        case ITEM_MEDICAL:   sim->promo.has_medical = 1;   break;
        case ITEM_CATHEDRAL: sim->promo.has_cathedral = 1; break;
        case ITEM_HOTEL_SUITE:
            sim->promo.hotel_quarters++;
            break;
        default: break;
        }
    }
    
    sim->tower_width = game_measure_width(tower);
}

/* --- Star rating check ---
 * Ported from seg_1140 FUN_1140_0411
 * Goes up one star at a time per check (original behavior). */

int game_check_star_rating(GameSim *sim, Tower *tower)
{
    int current = tower->star_rating;
    if (current >= 6) return current;  /* Already TOWER */
    
    /* Population threshold check */
    int pop = tower->population;
    int next_star = current + 1;
    
    if (next_star > 6) return current;
    /* Thresholds for 2-5 stars come from the live tuning table;
     * TOWER's 15,000 stays hardcoded like the original EXE */
    int need = (current >= 1 && current <= 4) ? TUNING.star_pop[current - 1]
                                              : STAR_POP_THRESHOLD[current];
    if (pop < need) return current;
    
    /* Width check: underground width must be > star * 25 
     * From seg_1148 FUN_1148_01a8 */
    if (sim->tower_width <= current * 25) return current;
    
    /* Promotion requirements check */
    if (!game_check_promotion(sim, tower, next_star)) return current;
    
    return next_star;
}

/* --- Promotion requirements ---
 * Ported from seg_1148 FUN_1148_0000
 *
 * ★→★★: just population (no special requirements)
 * ★★→★★★: security office
 * ★★★→★★★★: recycling + metro + facility_3 + quarters≥4 + VIP
 * ★★★★→★★★★★: medical + metro + quarters≥4 + VIP
 * ★★★★★→TOWER: always returns 0 (special event trigger) */

/* TOWER wedding — the special event LevelUp defers to (its 5->T check
 * always returns 0). Called at each dawn: yesterday's ceremony crowns
 * the tower; a newly-eligible tower holds its wedding today. */
void game_wedding_daily(GameSim *sim, Tower *tower)
{
    if (sim->wedding.active) {
        sim->wedding.active = 0;
        sim->wedding.done = 1;
        tower->star_rating = 6;
        sim->pending_star_up = 6;
        printf("\xf0\x9f\x92\x92 The wedding is over — "
               "WELCOME TO TOWER! \xf0\x9f\x8f\x86\n");
    } else if (!sim->wedding.done && tower->star_rating == 5 &&
               tower->population >= STAR_POP_THRESHOLD[5] &&
               sim->promo.has_cathedral && sim->promo.vip_visited) {
        sim->wedding.active = 1;
        sim->wedding.day = tower->day;
        printf("\xf0\x9f\x92\x92 A wedding is being held at the "
               "cathedral today! (Day %d)\n", tower->day);
    }
}

int game_check_promotion(GameSim *sim, Tower *tower, int target_star)
{
    (void)tower;
    switch (target_star) {
    case 1: return 1;  /* Starting star, always OK */
    case 2: return 1;  /* Just need population */
    case 3:
        /* Need security office */
        return sim->promo.has_security;
    case 4:
        /* Recycling + metro + hotel suites ≥ 4 + a satisfied VIP visit
         * (LevelUp 0xB92D requires VIP for both 3→4 and 4→5). */
        return sim->promo.has_recycling &&
               sim->promo.has_metro &&
               sim->promo.hotel_quarters >= 4 &&
               sim->promo.vip_visited;
    case 5:
        /* Need medical + metro + hotel suites ≥ 4 + a satisfied VIP visit */
        return sim->promo.has_medical &&
               sim->promo.has_metro &&
               sim->promo.hotel_quarters >= 4 &&
               sim->promo.vip_visited;
    case 6:
        /* TOWER: special event, not automatic */
        return 0;
    default:
        return 0;
    }
}

/* --- Tenant state update --- */

/* --- Capacity animation update ---
 * From TenantMake annotation: capacity byte drives sprite frame selection.
 * 3-phase daily cycle (DayStartUpdate/DayMiddleUpdate/DayEndUpdate):
 *   Morning: offices start filling (capacity ascending)
 *   Midday:  offices peak, restaurants active
 *   Evening: offices emptying, hotels filling (capacity descending/ascending)
 * Step size = 0x08, range = 0x00 (empty) to 0x40 (full) */
static void update_capacity(Tenant *t, TimeOfDay tod)
{
    int is_day_type = (t->type == ITEM_OFFICE || t->type == ITEM_SHOP ||
                       t->type == ITEM_RESTAURANT || t->type == ITEM_FAST_FOOD ||
                       t->type == ITEM_CINEMA || t->type == ITEM_PARTY_HALL);
    int is_night_type = (t->type == ITEM_HOTEL_SINGLE || t->type == ITEM_HOTEL_TWIN ||
                         t->type == ITEM_HOTEL_SUITE);

    /* The live byte oscillates daily BENEATH the persistent tier ceiling
     * (cap_peak), not up to a hard 0x40. Peak-managed types that arrive
     * without a peak (e.g. an imported .TDT) get theirs reconstructed from
     * the byte's current tier; everything else fills to 0x40 as before. */
    uint8_t peak = t->cap_peak;
    if (peak == 0) {
        if (t->type == ITEM_OFFICE)
            peak = cap_tier_top(t->capacity ? t->capacity : CAP_MIN);
        else if (t->type == ITEM_HOTEL_SINGLE || t->type == ITEM_HOTEL_TWIN)
            peak = 0x10;            /* star<4 baseline; upgrades up */
        else if (t->type == ITEM_HOTEL_SUITE)
            peak = 0x18;
        t->cap_peak = peak;         /* stays 0 for unmanaged types */
    }
    uint8_t ceil = peak ? peak : CAP_MAX;
    uint8_t floor = peak ? cap_daily_floor(peak) : CAP_MIN;

    if (is_day_type) {
        /* Day tenants: fill during morning, peak at afternoon, empty at evening */
        switch (tod) {
        case TOD_DAWN:
        case TOD_MORNING:
            if (t->capacity < ceil) t->capacity += CAP_STEP;
            if (t->capacity > ceil) t->capacity = ceil;
            break;
        case TOD_AFTERNOON:
            /* Peak — hold at current level */
            break;
        case TOD_EVENING:
            if (t->capacity > floor) t->capacity -= CAP_STEP;
            break;
        case TOD_NIGHT:
            t->capacity = CAP_EMPTY;
            break;
        default: break;
        }
    } else if (is_night_type) {
        /* Hotels: fill at evening, peak at night, empty at morning */
        switch (tod) {
        case TOD_EVENING:
            if (t->capacity < ceil) t->capacity += CAP_STEP;
            if (t->capacity > ceil) t->capacity = ceil;
            break;
        case TOD_NIGHT:
            /* Peak — hold */
            break;
        case TOD_DAWN:
            if (t->capacity > floor) t->capacity -= CAP_STEP;
            break;
        case TOD_MORNING:
        case TOD_AFTERNOON:
            t->capacity = CAP_EMPTY;
            break;
        default: break;
        }
    }
    /* Condos, services, etc. stay at their current capacity */
}

static void update_tenants(GameSim *sim, Tower *tower, long *out_income, long *out_expenses)
{
    long income = 0;
    long expenses = 0;
    
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type == ITEM_NONE || t->type == ITEM_LOBBY || t->type == ITEM_FLOOR)
            continue;
        if (item_is_transport(t->type)) continue;

        int type_idx = (int)t->type;
        int is_active = (type_idx < ITEM_TYPE_COUNT) ?
                        TENANT_ACTIVE_TIMES[type_idx][sim->time_of_day] : 0;

        /* Commute gating: space nobody can reach from the entrance never
         * rents, and a dirty hotel room can't take guests until cleaned. */
        int is_hotel = (t->type == ITEM_HOTEL_SINGLE || t->type == ITEM_HOTEL_TWIN ||
                        t->type == ITEM_HOTEL_SUITE);
        {
            int fidx = floor_to_index(t->floor);
            int is_staff = (t->type == ITEM_SECURITY || t->type == ITEM_HOUSEKEEPING);
            if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT ||
                !(is_staff ? sim->reach_service[fidx] : sim->reach_public[fidx]))
                is_active = 0;
            if (is_hotel && t->dirty) is_active = 0;
        }
        
        /* State machine */
        switch ((TenantState)t->state) {
        case TENANT_EMPTY:
            /* New tenant — starts construction */
            if (type_idx < ITEM_TYPE_COUNT && CONSTRUCTION_TIME[type_idx] > 0) {
                t->construction = CONSTRUCTION_TIME[type_idx];
                t->state = TENANT_CONSTRUCTION;
            } else {
                t->state = TENANT_OCCUPIED;
                t->capacity = CAP_MIN;
                t->cap_peak = game_init_cap_peak(t->type, tower->star_rating);
            }
            break;

        case TENANT_CONSTRUCTION:
            /* Under construction — decrement timer */
            t->construction--;
            if (t->construction <= 0) {
                t->state = TENANT_MOVING_IN;
                t->capacity = CAP_MIN;
                t->cap_peak = game_init_cap_peak(t->type, tower->star_rating);
            }
            break;
            
        case TENANT_MOVING_IN:
            /* Brief transition, then occupied */
            t->state = TENANT_OCCUPIED;
            break;
            
        case TENANT_OCCUPIED:
            if (is_active) {
                /* Advance capacity animation (3-phase daily cycle) */
                update_capacity(t, sim->time_of_day);
                
                /* Generate income (per tick, scaled) */
                int base_income = (type_idx < ITEM_TYPE_COUNT) ? TENANT_INCOME[type_idx] : 0;

                /* Retail customer competition: clustering same-type retail in a
                 * zone dilutes each one's revenue (see game_retail_income). */
                base_income = game_retail_income(sim, t, base_income);

                /* Office rent scales with established occupancy: a thriving,
                 * grown office (cap_peak 0x40) holds twice the workers — and
                 * pays twice the rent — of a freshly-built one (0x20). This is
                 * what makes the gentrification/growth mechanic economically
                 * real instead of a sprite-frame change. */
                if (t->type == ITEM_OFFICE && t->cap_peak > CAP_PEAK_LOW)
                    base_income = base_income * t->cap_peak / CAP_PEAK_LOW;
                if (base_income > 0 && sim->ticks_per_quarter > 0) {
                    if (sim->tick % 60 == 0) {
                        int pay = base_income / (sim->ticks_per_quarter / 60);
                        income += pay;
                    }
                }
                
                /* Stress management — from MainteT */
                if (t->stress > 0) t->stress--;

                /* Hotel guests staying the night leave a room to clean */
                if (is_hotel && sim->time_of_day == TOD_NIGHT) t->hosted = 1;
            } else if (t->type != ITEM_CONDO) {
                /* Close for the inactive period */
                t->state = TENANT_VACANT;
                /* Don't zero capacity here — let update_capacity handle the fade */

                /* Checkout: the room needs housekeeping before it re-rents */
                if (is_hotel && t->hosted) {
                    t->dirty = 1;
                    t->hosted = 0;
                }
            }
            break;
            
        case TENANT_CLOSING:
            t->state = TENANT_VACANT;
            break;
            
        case TENANT_VACANT:
            /* Even when vacant, capacity can animate (hotels emptying, etc.) */
            update_capacity(t, sim->time_of_day);
            if (is_active) {
                t->state = TENANT_OCCUPIED;
            }
            break;
            
        case TENANT_STRESSED:
            /* From MainteT: 3-strike system */
            if (t->stress > 100) {
                t->complaints++;
                if (t->complaints >= 3) {
                    if (t->type == ITEM_OFFICE) {
                        /* Offices don't abandon (MainteT OfficeStressCheck):
                         * three strikes force them to top occupancy instead.
                         * The original reads it as the firm finally committing
                         * — overflowing its space rather than leaving. */
                        t->cap_peak = (tower->star_rating >= 4) ? CAP_PEAK_HIGH : 0x38;
                        t->stress = 0;
                        t->complaints = 0;
                        t->state = TENANT_OCCUPIED;
                        printf("🏢 Office on F%d force-upgraded to full occupancy "
                               "(3 strikes — offices don't leave)\n", t->floor);
                    } else {
                        t->state = TENANT_ABANDONED;
                        t->capacity = CAP_EMPTY;
                        printf("⚠ %s on F%d abandoned! (stress=%d, %d complaints)\n",
                               tower_item_name(t->type), t->floor, t->stress, t->complaints);
                    }
                } else {
                    t->stress = 60;  /* Reset after complaint */
                }
            } else if (t->stress < 50) {
                t->state = TENANT_OCCUPIED;
            }
            break;
            
        case TENANT_ABANDONED:
            /* Dead tenant — stays until demolished */
            t->population = 0;
            t->capacity = CAP_EMPTY;
            break;
        }
    }
    
    /* Return income/expenses to caller */
    *out_income += income;
    *out_expenses += expenses;
}

/* --- Transport reachability ---
 * The original never rents space people can't commute to: every floor must
 * connect to the ground entrance via stairs, escalators or elevators.
 * Two networks:
 *   public  — tenants/visitors: stairs/escalators, standard elevators,
 *             express elevators (lobby/sky-lobby/basement stops only)
 *   service — staff (housekeeping/security): all of the above PLUS service
 *             elevators (superset of public)
 * Elevator type semantics from ElevatorsT (+0x0001: 0=standard, 1=express,
 * 2=service). Car movement and wait times are NOT simulated yet — this is
 * pure connectivity. */

/* Does an elevator of this type stop at this floor (grid index)? */
static int elevator_stops_at(ItemType ty, int fidx)
{
    if (ty != ITEM_ELEVATOR_EXPRESS) return 1;
    int wf = index_to_floor(fidx);
    return wf <= 0 || (wf % 15) == 0;   /* basements, ground, sky lobbies */
}

#define MAX_TRANSPORT_LINKS 1024

void game_update_reachability(GameSim *sim, Tower *tower)
{
    memset(sim->reach_public, 0, sizeof(sim->reach_public));
    memset(sim->reach_service, 0, sizeof(sim->reach_service));
    int ground = floor_to_index(TOWER_LOBBY_FLOOR);
    sim->reach_public[ground] = 1;
    sim->reach_service[ground] = 1;

    /* Collect stair/escalator links (floor f <-> f+1) once */
    static int link_a[MAX_TRANSPORT_LINKS];
    int link_count = 0;
    for (int i = 0; i < tower->tenant_count && link_count < MAX_TRANSPORT_LINKS; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_STAIRS && t->type != ITEM_ESCALATOR) continue;
        int a = floor_to_index(t->floor);
        if (a >= 0 && a + 1 < TOWER_FLOOR_COUNT) link_a[link_count++] = a;
    }

    /* Collect elevator shaft runs once: contiguous same-type segments in a
     * column form one shaft. (Each segment's leftmost cell marks presence.) */
    typedef struct { ItemType ty; int lo, hi; } ShaftRun;
    static ShaftRun runs[MAX_TRANSPORT_LINKS];
    int run_count = 0;
    for (int x = 0; x < TOWER_WIDTH && run_count < MAX_TRANSPORT_LINKS; x++) {
        for (int f = 0; f < TOWER_FLOOR_COUNT; ) {
            TowerCell *c = &tower->grid[f][x];
            if (!item_is_elevator(c->type) || c->cell_index != 0) { f++; continue; }
            ItemType ty = c->type;
            int lo = f;
            while (f < TOWER_FLOOR_COUNT && tower->grid[f][x].type == ty &&
                   tower->grid[f][x].cell_index == 0) f++;
            runs[run_count].ty = ty;
            runs[run_count].lo = lo;
            runs[run_count].hi = f - 1;
            if (++run_count >= MAX_TRANSPORT_LINKS) break;
        }
    }

    /* Fixed-point relaxation over the links until nothing new is reached */
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < link_count; i++) {
            int a = link_a[i], b = link_a[i] + 1;
            if (sim->reach_public[a] != sim->reach_public[b]) {
                sim->reach_public[a] = sim->reach_public[b] = 1;
                changed = 1;
            }
            if (sim->reach_service[a] != sim->reach_service[b]) {
                sim->reach_service[a] = sim->reach_service[b] = 1;
                changed = 1;
            }
        }
        for (int i = 0; i < run_count; i++) {
            ShaftRun *r = &runs[i];
            int pub_ok = (r->ty != ITEM_ELEVATOR_SERVICE);
            int any_pub = 0, any_svc = 0;
            for (int s = r->lo; s <= r->hi; s++) {
                if (!elevator_stops_at(r->ty, s)) continue;
                any_pub |= sim->reach_public[s];
                any_svc |= sim->reach_service[s];
            }
            for (int s = r->lo; s <= r->hi; s++) {
                if (!elevator_stops_at(r->ty, s)) continue;
                if (pub_ok && any_pub && !sim->reach_public[s]) {
                    sim->reach_public[s] = 1;
                    changed = 1;
                }
                if (any_svc && !sim->reach_service[s]) {
                    sim->reach_service[s] = 1;
                    changed = 1;
                }
            }
        }
    }

    /* Stats for UI feedback */
    int unreach = 0, dirty = 0;
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type == ITEM_NONE || t->type == ITEM_LOBBY ||
            t->type == ITEM_FLOOR || item_is_transport(t->type)) continue;
        int fidx = floor_to_index(t->floor);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
        int is_staff = (t->type == ITEM_SECURITY || t->type == ITEM_HOUSEKEEPING);
        if (!(is_staff ? sim->reach_service[fidx] : sim->reach_public[fidx]))
            unreach++;
        if (t->dirty) dirty++;
    }
    sim->unreachable_tenants = unreach;
    sim->dirty_rooms = dirty;
}

/* --- Housekeeping ---
 * After checkout, hotel rooms stay dirty (and unrentable) until a
 * housekeeping unit cleans them. Housekeepers work morning/afternoon, travel
 * the service network, and each unit handles a limited number of rooms per
 * day — too few units and rooms sit dirty, losing the night's income. */
#define HK_ROOMS_PER_DAY 12

static void update_housekeeping(GameSim *sim, Tower *tower)
{
    if (sim->time_of_day != TOD_MORNING && sim->time_of_day != TOD_AFTERNOON) {
        /* Off shift — reset the day's quotas; rooms keep their dirty flag */
        for (int i = 0; i < tower->tenant_count; i++)
            if (tower->tenants[i].type == ITEM_HOUSEKEEPING)
                tower->tenants[i].cleaned_today = 0;
        return;
    }

    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *hk = &tower->tenants[i];
        if (hk->type != ITEM_HOUSEKEEPING) continue;
        if (hk->state != TENANT_OCCUPIED) continue;   /* built + on shift */
        if (hk->cleaned_today >= HK_ROOMS_PER_DAY) continue;
        int hf = floor_to_index(hk->floor);
        if (hf < 0 || hf >= TOWER_FLOOR_COUNT || !sim->reach_service[hf]) continue;

        /* Clean one reachable dirty room per pass (paces the work out) */
        for (int j = 0; j < tower->tenant_count; j++) {
            Tenant *room = &tower->tenants[j];
            if (!room->dirty) continue;
            int rf = floor_to_index(room->floor);
            if (rf < 0 || rf >= TOWER_FLOOR_COUNT || !sim->reach_service[rf])
                continue;
            room->dirty = 0;
            hk->cleaned_today++;
            break;
        }
    }
}

/* --- Main simulation update --- */

void game_update(GameSim *sim, Tower *tower)
{
    /* Reachability is layout-driven, so refresh it even while paused (build
     * mode needs current data); throttled — it only changes on placement. */
    static int reach_throttle = 0;
    if (reach_throttle-- <= 0) {
        game_update_reachability(sim, tower);
        people_rebuild_transport(&sim->people, tower);
        reach_throttle = 15;
    }

    if (sim->speed == SPEED_PAUSED) return;
    
    sim->ticks_per_quarter = TICKS_PER_QUARTER[sim->speed];
    sim->frame++;
    sim->tick++;
    
    /* Calculate time of day */
    int day_ticks = TICKS_PER_DAY(sim->speed);
    int tick_in_day = 0;
    if (day_ticks > 0) {
        /* Quarter 0 = first quarter of the day, etc. */
        tick_in_day = (sim->quarter * sim->ticks_per_quarter) + sim->tick;
        tick_to_time(tick_in_day, day_ticks, &sim->hour, &sim->minute);
        sim->time_of_day = hour_to_tod(sim->hour);
    }
    
    /* Schedule clock for the elevator tables (EXE 0xB3A0/0xB3A1: the
     * weekend day-type + the 7 periods that slice the day) */
    sim->people.sched_day = (sim->quarter == QUARTER_WEEKEND);
    if (day_ticks > 0) {
        int per = (int)((long)tick_in_day * 7 / day_ticks);
        sim->people.sched_period = (uint8_t)(per < 0 ? 0 : per > 6 ? 6 : per);
    }

    /* People + elevators run every tick — cars and queues are the game */
    people_update(&sim->people, tower, sim->frame, sim->time_of_day,
                  sim->reach_public, sim->reach_service);

    /* Update tenants every few ticks (not every frame) */
    if (sim->tick % 4 == 0) {
        long tick_income = 0, tick_expenses = 0;
        update_tenants(sim, tower, &tick_income, &tick_expenses);
        update_housekeeping(sim, tower);
        tower->money += tick_income - tick_expenses;
        sim->income_this_quarter += tick_income;
        sim->expenses_this_quarter += tick_expenses;
    }
    
    /* Recalculate population periodically */
    if (sim->tick % 30 == 0) {
        game_calc_population(sim, tower);
    }
    
    /* Zone-based stress evaluation — every 120 ticks (~2 seconds)
     * From JudgeT: commercial tenants accumulate stress from competition */
    if (sim->tick % 120 == 0 && sim->tick > 0) {
        game_judge_tenants(sim, tower);
        
        /* Try starting a random event (fires, bombs) */
        game_try_event(sim, tower);
    }
    
    /* Update active events (fire spread, bomb countdown) */
    game_update_event(sim, tower);
    
    /* Update Santa position */
    game_update_santa(sim);
    
    /* Quarter transition */
    if (sim->tick >= sim->ticks_per_quarter) {
        sim->tick = 0;
        sim->quarter++;

        /* End of quarter — tally finances */
        sim->total_income += sim->income_this_quarter;
        sim->total_expenses += sim->expenses_this_quarter;

        /* Analytics sample (ring buffer, oldest evicted when full) */
        {
            StatsHistory *h = &sim->stats;
            StatSample *smp;
            if (h->count < STATS_MAX) {
                smp = &h->s[(h->head + h->count) % STATS_MAX];
                h->count++;
            } else {
                smp = &h->s[h->head];
                h->head = (h->head + 1) % STATS_MAX;
            }
            long dw = sim->people.wait_total - sim->stats_prev_wait_total;
            long dn = sim->people.wait_samples - sim->stats_prev_wait_samples;
            sim->stats_prev_wait_total = sim->people.wait_total;
            sim->stats_prev_wait_samples = sim->people.wait_samples;
            *smp = (StatSample){
                .day = tower->day,
                .quarter = (int8_t)((sim->quarter - 1 + QUARTER_COUNT) % QUARTER_COUNT),
                .star = (int8_t)tower->star_rating,
                .population = tower->population,
                .commuters = sim->people.population_now,
                .avg_wait = dn ? (int32_t)(dw / dn) : 0,
                .balance = tower->money,
                .income = sim->income_this_quarter,
                .expenses = sim->expenses_this_quarter,
                .built_value = tower->built_value,
                .lost_value = tower->lost_value,
            };
        }
        
        printf("📊 End of %s: Income $%ld, Expenses $%ld, Balance $%ld, Pop %d, "
               "Commuters %d (avg wait %d)\n",
               game_quarter_name((Quarter)((sim->quarter - 1 + QUARTER_COUNT) % QUARTER_COUNT)),
               sim->income_this_quarter, sim->expenses_this_quarter,
               tower->money, tower->population,
               sim->people.population_now, people_avg_wait(&sim->people));
        
        /* Roll this quarter into the running daily totals before clearing. */
        sim->day_income   += sim->income_this_quarter;
        sim->day_expenses += sim->expenses_this_quarter;
        sim->income_this_quarter = 0;
        sim->expenses_this_quarter = 0;

        /* Day transition */
        if (sim->quarter >= QUARTER_COUNT) {
            sim->quarter = 0;
            /* Close out the day: snapshot its totals, then reset the rollup. */
            sim->last_day_num      = tower->day;
            sim->last_day_income   = sim->day_income;
            sim->last_day_expenses = sim->day_expenses;
            sim->day_income = 0;
            sim->day_expenses = 0;
            tower->day++;

            /* Tenant pairing pass — MainteT runs upgrades/pairing every 3rd day. */
            if (tower->day % 3 == 0) {
                game_tenant_pairing(sim, tower);
                game_office_dynamics(sim, tower);
            }

            /* VIP visit check (from VipT seg_1240: day % 9 == 3) */
            if (tower->day % 9 == 3 && tower->star_rating >= 3) {
                sim->vip_visiting = 1;
                sim->vip_last_day = tower->day;
                sim->vip_notice = 1;   /* arrived (consumed by UI feed) */
                printf("👔 VIP is visiting the tower today! (Day %d)\n", tower->day);
            } else {
                /* VIP evaluation at end of visit day */
                if (sim->vip_visiting) {
                    /* VIP satisfied if no stressed/abandoned tenants on hotel floors */
                    int hotel_ok = 1;
                    for (int i = 0; i < tower->tenant_count; i++) {
                        Tenant *t = &tower->tenants[i];
                        if ((t->type == ITEM_HOTEL_SINGLE || t->type == ITEM_HOTEL_TWIN ||
                             t->type == ITEM_HOTEL_SUITE) &&
                            (t->state == TENANT_STRESSED || t->state == TENANT_ABANDONED)) {
                            hotel_ok = 0;
                            break;
                        }
                    }
                    if (hotel_ok) {
                        sim->vip_satisfied = 1;
                        sim->promo.vip_visited = 1;
                        sim->vip_notice = 2;   /* satisfied */
                        printf("👔 VIP was satisfied! ⭐ (Helps with star promotion)\n");
                    } else {
                        sim->vip_notice = 3;   /* not satisfied */
                        printf("👔 VIP was NOT satisfied. Hotels need improvement.\n");
                    }
                    sim->vip_visiting = 0;
                }
            }
            
            /* Upkeep sweep (MoneyT 1178:0b44 — the EXE runs it once per
             * 3-day quarter; our day boundary is the same cadence).
             * Lobbies + per-car elevator upkeep + escalators. */
            int upkeep = calc_lobby_maintenance(tower);
            PeopleSim *ups = &sim->people;
            for (int i = 0; i < ups->shaft_count; i++) {
                ElevatorShaft *sh = &ups->shafts[i];
                if (!sh->active) continue;
                int per = (sh->type == ITEM_ELEVATOR_EXPRESS) ? TUNING.maint_car_express
                        : (sh->type == ITEM_ELEVATOR_SERVICE) ? TUNING.maint_car_service
                        : TUNING.maint_car_std;
                upkeep += sh->num_cars * per;
            }
            for (int i = 0; i < tower->tenant_count; i++)
                if (tower->tenants[i].type == ITEM_ESCALATOR)
                    upkeep += TUNING.maint_escalator;
            if (upkeep > 0) {
                tower->money -= upkeep;
                sim->expenses_this_quarter += upkeep;
            }
            
            /* Santa: launch on day 25 of each "year" (every 12 game-days)
             * Day 25 ≈ Christmas in game time */
            if (tower->day % 12 == 9 && !sim->santa.active) {
                game_launch_santa(sim, 960);
            }
            
            printf("🌅 Day %d begins! Pop: %d, Stars: %d, Money: $%ld\n",
                   tower->day, tower->population, tower->star_rating, tower->money);
            
            /* Check star rating once per day */
            scan_promotion_flags(sim, tower);

            game_wedding_daily(sim, tower);

            int new_rating = game_check_star_rating(sim, tower);
            if (new_rating > tower->star_rating) {
                tower->star_rating = new_rating;
                sim->pending_star_up = new_rating;
                if (new_rating == 6) {
                    printf("🏆🏆🏆 TOWER STATUS ACHIEVED! 🏆🏆🏆\n");
                } else {
                    printf("⭐ Promoted to %d star%s! Population: %d\n",
                           new_rating, new_rating > 1 ? "s" : "", tower->population);
                }
                /* Promotion bonus (LevelUp seg42:020f → MoneyT AwardMoney):
                 * the EXE pays $200k/$300k/$500k on reaching star 2/3/4.
                 * No bonus decoded for star 5 or TOWER. */
                if (new_rating >= 2 && new_rating <= 4) {
                    int bonus = TUNING.star_bonus[new_rating - 2];
                    tower->money += bonus;
                    sim->income_this_quarter += bonus;
                    printf("💰 Promotion bonus: $%d\n", bonus);
                }
            }
        }
    }
    
    /* Count total tenants */
    sim->tenants_total = 0;
    for (int i = 0; i < tower->tenant_count; i++) {
        if (tower->tenants[i].type > ITEM_FLOOR) sim->tenants_total++;
    }
}

/* --- Sky color tint --- */

void game_sky_tint(GameSim *sim, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    /* Tint overlay that gets blended on top of the sky sprites */
    switch (sim->time_of_day) {
    case TOD_DAWN:
        *r = 255; *g = 180; *b = 120; *a = 60;  /* Warm orange */
        break;
    case TOD_MORNING:
        *r = 0; *g = 0; *b = 0; *a = 0;         /* No tint (clear day) */
        break;
    case TOD_AFTERNOON:
        *r = 255; *g = 240; *b = 200; *a = 20;  /* Slight warm */
        break;
    case TOD_EVENING:
        *r = 255; *g = 140; *b = 60; *a = 80;   /* Sunset orange */
        break;
    case TOD_NIGHT:
        *r = 20; *g = 20; *b = 60; *a = 140;    /* Dark blue */
        break;
    default:
        *r = 0; *g = 0; *b = 0; *a = 0;
        break;
    }
}

/* --- Time formatting --- */

void game_format_time(GameSim *sim, char *buf, int bufsize)
{
    int h = sim->hour;
    int m = sim->minute;
    const char *ampm = (h >= 12) ? "PM" : "AM";
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, bufsize, "%d:%02d %s", h12, m, ampm);
}

const char *game_quarter_name(Quarter q)
{
    static const char *names[] = {
        "Weekday 1", "Weekday 2", "Weekday 3", "Weekend"
    };
    if (q >= 0 && q < QUARTER_COUNT) return names[q];
    return "???";
}

/* ================================================================
 * Zone-based commercial satisfaction (from JudgeT seg_11a8)
 * ================================================================
 * Tower is divided into 7 zones of 15 floors each.
 * Too many competitors in the same zone = unhappy tenants.
 * This is THE core balance mechanic of SimTower. */

void game_calc_zones(GameSim *sim, Tower *tower)
{
    /* Reset all zone counts */
    memset(sim->zones, 0, sizeof(sim->zones));
    
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->state == TENANT_ABANDONED || t->state == TENANT_EMPTY ||
            t->state == TENANT_CONSTRUCTION) continue;
        
        int z = floor_to_zone(t->floor);
        ZoneData *zd = &sim->zones[z];
        
        switch (t->type) {
        case ITEM_RESTAURANT:  zd->restaurant_count++; zd->total_commercial++; break;
        case ITEM_FAST_FOOD:   zd->fastfood_count++;   zd->total_commercial++; break;
        case ITEM_SHOP:        zd->shop_count++;        zd->total_commercial++; break;
        case ITEM_OFFICE:      zd->office_count++;      break;
        default: break;
        }
    }
}

/* Retail customer competition (JudgeT seg_11a8 zone model): same-type retail
 * in a 15-floor zone share a limited customer pool, so clustering past the
 * comfortable count dilutes each one's revenue — "variety thrives, clones
 * starve." Returns base_income scaled by threshold/count when over-clustered;
 * unchanged for a well-spread tower (count <= threshold). Fast food is exempt
 * (JudgeT: fast food never goes unsatisfied). Relies on sim->zones, refreshed
 * by game_calc_zones every 120 ticks. */
int game_retail_income(const GameSim *sim, const Tenant *t, int base_income)
{
    if (base_income <= 0) return base_income;
    int z = floor_to_zone(t->floor);
    int cnt = 0, thresh = 0;
    if (t->type == ITEM_RESTAURANT) {
        cnt = sim->zones[z].restaurant_count; thresh = ZONE_MAX_RESTAURANTS;
    } else if (t->type == ITEM_SHOP) {
        cnt = sim->zones[z].shop_count;       thresh = ZONE_MAX_SHOPS;
    }
    if (thresh > 0 && cnt > thresh)
        return base_income * thresh / cnt;
    return base_income;
}

/* Tenant pairing (MainteT, runs every 3rd day): a content tenant (occupied,
 * low stress) reaches out to a stressed same-type neighbour on its floor and
 * eases it back below the stress threshold — a thriving tenant stabilises an
 * unhappy one, breaking move-out cascades. One rescue per content tenant; the
 * relief is temporary (the next judge pass re-stresses it if the underlying
 * cause persists), matching the decomp's stress 2->1 nudge. */
void game_tenant_pairing(GameSim *sim, Tower *tower)
{
    (void)sim;
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->state != TENANT_OCCUPIED || t->stress > 10) continue;  /* content */
        for (int j = 0; j < tower->tenant_count; j++) {
            Tenant *n = &tower->tenants[j];
            if (j == i || n->type != t->type || n->floor != t->floor) continue;
            if (n->state == TENANT_STRESSED) {
                n->stress = 50;             /* below the 70 stress threshold */
                n->state = TENANT_OCCUPIED;
                n->complaints = 0;
                break;                      /* one rescue per content tenant */
            }
        }
    }
}

/* Starting persistent peak for a freshly-built unit (TenantMake MakeTenant:
 * sets the capacity byte by type and star level). Offices begin at the bottom
 * tier and grow; hotels/suites begin at their star-scaled room occupancy. */
uint8_t game_init_cap_peak(ItemType type, int star)
{
    switch (type) {
    case ITEM_OFFICE:
        return CAP_PEAK_LOW;              /* 0x20; thrives up toward 0x40 */
    case ITEM_HOTEL_SINGLE:
    case ITEM_HOTEL_TWIN:
        return (star < 4) ? 0x10 : 0x18; /* MakeTenant single-room occupancy */
    case ITEM_HOTEL_SUITE:
        return (star < 4) ? 0x18 : 0x20; /* MakeTenant suite occupancy */
    default:
        return 0;                        /* retail / services / condo: unmanaged */
    }
}

/* Persistent-occupancy dynamics, run on the same every-3rd-day MainteT cadence
 * as tenant pairing. Three mechanics, all keyed on cap_peak (the capacity
 * byte's persistent tier), all decoded from MainteT:
 *
 *   1. GROWTH — a content, well-served office (low stress) climbs one tier
 *      (0x20→0x30→0x40). This is the engine that lets an office reach the top
 *      tier; without it OfficeStressCheck's "capacity > 0x27" path is
 *      unreachable. Inferred to seed the system the decomp's expansion math
 *      assumes already-grown offices exist.
 *   2. GENTRIFICATION (OfficeExpansion FUN_1130_01e2) — a top-tier office
 *      (cap_peak 0x40) lifts an adjacent same-floor office that's still below
 *      the top straight to the top tier. Success spreads along a floor.
 *   3. ROOM UPGRADE (TenantUpgrade FUN_1130_09e5) — a happy, clean hotel
 *      (cap_peak < 0x18) or suite (< 0x20) raises its occupancy one step.
 *
 * Star level caps the office top tier at 0x38 below 4★ (matching MakeTenant /
 * OfficeStressCheck), 0x40 at 4★+. */
void game_office_dynamics(GameSim *sim, Tower *tower)
{
    (void)sim;
    uint8_t office_top = (tower->star_rating >= 4) ? CAP_PEAK_HIGH : 0x38;

    /* 1. Growth: content offices climb a tier. */
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_OFFICE || t->state != TENANT_OCCUPIED) continue;
        if (t->stress > 10) continue;                 /* must be content */
        uint8_t next = (t->cap_peak < CAP_PEAK_MID)  ? CAP_PEAK_MID :
                       (t->cap_peak < CAP_PEAK_HIGH) ? office_top : t->cap_peak;
        if (next > t->cap_peak) {
            t->cap_peak = next;
            if (next >= CAP_PEAK_HIGH)
                printf("📈 Office on F%d is thriving — grown to full occupancy\n",
                       t->floor);
        }
    }

    /* 2. Gentrification: a top-tier office lifts a same-floor neighbour. One
     * lift per benefactor; reads only floor/type/peak, so adjacency is "same
     * floor" (matches the zone-not-literal-adjacency reading we settled on
     * for retail variety). */
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_OFFICE || t->cap_peak < CAP_PEAK_HIGH) continue;
        for (int j = 0; j < tower->tenant_count; j++) {
            Tenant *n = &tower->tenants[j];
            if (j == i || n->type != ITEM_OFFICE || n->floor != t->floor) continue;
            if (n->state != TENANT_OCCUPIED) continue;
            if (n->cap_peak < office_top) {
                n->cap_peak = office_top;
                printf("✨ Office on F%d gentrified by a thriving neighbour\n",
                       n->floor);
                break;
            }
        }
    }

    /* 3. Hotel / suite room upgrade for happy, clean rooms. */
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->state != TENANT_OCCUPIED || t->stress > 10 || t->dirty) continue;
        uint8_t cap = t->cap_peak;
        if ((t->type == ITEM_HOTEL_SINGLE || t->type == ITEM_HOTEL_TWIN) && cap < 0x18)
            t->cap_peak = cap + CAP_STEP;
        else if (t->type == ITEM_HOTEL_SUITE && cap < 0x20)
            t->cap_peak = cap + CAP_STEP;
    }
}

void game_judge_tenants(GameSim *sim, Tower *tower)
{
    /* Recalculate zones first */
    game_calc_zones(sim, tower);
    
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->state != TENANT_OCCUPIED && t->state != TENANT_STRESSED) continue;
        
        int z = floor_to_zone(t->floor);
        ZoneData *zd = &sim->zones[z];
        int stress_add = 0;
        
        /* Zone competition stress — from JudgeT annotation */
        switch (t->type) {
        case ITEM_RESTAURANT:
            if (zd->restaurant_count > ZONE_MAX_RESTAURANTS)
                stress_add += (zd->restaurant_count - ZONE_MAX_RESTAURANTS) * 5;
            break;
        case ITEM_FAST_FOOD:
            /* Fast food never gets unsatisfied (from JudgeT: returns 0) */
            break;
        case ITEM_SHOP:
            if (zd->shop_count > ZONE_MAX_SHOPS)
                stress_add += (zd->shop_count - ZONE_MAX_SHOPS) * 3;
            break;
        case ITEM_OFFICE:
            /* Offices don't compete by zone, but overcrowding matters */
            if (zd->office_count > 8)
                stress_add += (zd->office_count - 8) * 2;
            break;
        default: break;
        }
        
        /* Apply stress */
        if (stress_add > 0) {
            t->stress += stress_add;
            if (t->stress > 70 && t->state == TENANT_OCCUPIED) {
                t->state = TENANT_STRESSED;
                /* Only print first time */
                if (t->stress < 75) {
                    printf("😰 %s on F%d stressed! (zone %d competition, stress=%d)\n",
                           tower_item_name(t->type), t->floor, z, t->stress);
                }
            }
        }
    }
}

/* ================================================================
 * Lobby maintenance (MoneyT FUN_1178_0a6a, dis16-verified 2026-06-11)
 * ================================================================
 * charge = lobby_span_cells * fee / 10, fee by star from the tuning
 * resource (0xde16/18/1a = 0/30/100, x$100): below 3 stars lobbies are
 * FREE, 3 stars = $300/cell, 4+ stars = $1000/cell. The old
 * $100/300/500-per-segment numbers were folklore. */

static int calc_lobby_maintenance(Tower *tower)
{
    int per_cell;
    if (tower->star_rating < 3)      return 0;
    else if (tower->star_rating < 4) per_cell = TUNING.lobby_fee_star3;
    else                             per_cell = TUNING.lobby_fee_star4;

    int lobby_segments = 0;
    for (int i = 0; i < tower->tenant_count; i++) {
        if (tower->tenants[i].type == ITEM_LOBBY) lobby_segments++;
    }

    return lobby_segments * ITEM_WIDTH[ITEM_LOBBY] * per_cell;
}

/* ================================================================
 * Santa Easter egg (from SantaT seg_11b8)
 * ================================================================
 * Santa flies diagonally: x -= 10, y += 1 per tick.
 * Velocity: 10 horizontal, 1 vertical = very shallow angle. */

void game_launch_santa(GameSim *sim, int screen_w)
{
    sim->santa.active = 1;
    sim->santa.x = screen_w + 100;  /* Start off-screen right */
    sim->santa.y = 20;              /* Near top of sky */
    printf("🎅 Ho ho ho! Santa flies across the tower!\n");
}

/* ================================================================
 * Random Events (from EventT seg_10c8 + FireT seg_10e8)
 * ================================================================
 * Events trigger at star > 2 with security present.
 * Bomb: timed countdown, guard races to defuse. Blast radius = 6 floors × 40 slots.
 * Fire: spreads left/right per tick. Burns until timer expires. */

void game_try_event(GameSim *sim, Tower *tower)
{
    if (sim->event.active) return;
    
    /* From decompiled: fires only at star > 2, with security, during daytime */
    if (tower->star_rating < 3) return;
    if (sim->time_of_day == TOD_NIGHT || sim->time_of_day == TOD_DAWN) return;
    
    /* Check security exists */
    int has_security = 0;
    for (int i = 0; i < tower->tenant_count; i++) {
        if (tower->tenants[i].type == ITEM_SECURITY &&
            tower->tenants[i].state != TENANT_ABANDONED) {
            has_security = 1;
            break;
        }
    }
    if (!has_security) return;
    
    /* Random chance: ~1% per evaluation (every 120 ticks) */
    if ((rand() % 100) != 0) return;
    
    /* Pick random floor with tenants */
    int max_floor = 0;
    for (int i = 0; i < tower->tenant_count; i++) {
        if (tower->tenants[i].floor > max_floor &&
            tower->tenants[i].state == TENANT_OCCUPIED)
            max_floor = tower->tenants[i].floor;
    }
    if (max_floor < 2) return;
    
    int target_floor = 1 + (rand() % max_floor);
    int target_slot = 5 + (rand() % (TOWER_WIDTH - 10));
    
    /* Pick event type: 60% bomb, 40% fire */
    EventType etype = (rand() % 10 < 6) ? EVENT_BOMB : EVENT_FIRE;
    
    sim->event.type = etype;
    sim->event.active = 1;
    sim->event.target_floor = target_floor;
    sim->event.target_slot = target_slot;
    sim->event.caught = 0;
    sim->event.damage_cost = 0;
    
    if (etype == EVENT_BOMB) {
        /* Bomb: 600 ticks to defuse (~10 seconds at normal speed) */
        sim->event.duration = 600;
        sim->event.timer = sim->event.duration;
        printf("💣 BOMB THREAT on floor %d! Security is responding...\n", target_floor);
    } else {
        /* Fire: burns for 800 ticks, spreads outward */
        sim->event.duration = 800;
        sim->event.timer = sim->event.duration;
        sim->event.fire_left = target_slot;
        sim->event.fire_right = target_slot;
        printf("🔥 FIRE on floor %d at slot %d! Spreading...\n", target_floor, target_slot);
    }
}

void game_update_event(GameSim *sim, Tower *tower)
{
    if (!sim->event.active) return;
    
    sim->event.timer--;
    
    if (sim->event.type == EVENT_FIRE) {
        /* Fire spreads left and right each tick */
        sim->event.fire_left -= FIRE_SPREAD_RATE;
        sim->event.fire_right += FIRE_SPREAD_RATE;
        if (sim->event.fire_left < 0) sim->event.fire_left = 0;
        if (sim->event.fire_right >= TOWER_WIDTH) sim->event.fire_right = TOWER_WIDTH - 1;
        
        /* Destroy tenants in fire path (check every 30 ticks) */
        if (sim->event.timer % 30 == 0) {
            int fi = floor_to_index(sim->event.target_floor);
            for (int x = sim->event.fire_left; x <= sim->event.fire_right; x++) {
                TowerCell *cell = &tower->grid[fi][x];
                if (cell->tenant_id > 0) {
                    Tenant *t = tower_tenant(tower, cell->tenant_id);
                    if (t && t->state != TENANT_ABANDONED) {
                        sim->event.damage_cost += ITEM_COST[(int)t->type];
                        printf("🔥 %s on F%d destroyed by fire!\n",
                               tower_item_name(t->type), t->floor);
                        t->state = TENANT_ABANDONED;
                        t->capacity = CAP_EMPTY;
                        t->population = 0;
                        t->burned = 1;   /* leaves rubble until rebuilt */
                    }
                }
            }
        }
    } else if (sim->event.type == EVENT_BOMB) {
        /* Bomb: security has a chance to catch it each tick */
        /* Simplified: 0.5% chance per tick that guard reaches it */
        if ((rand() % 200) == 0) {
            sim->event.caught = 1;
            sim->event.active = 0;
            printf("🛡️ Security caught the bomb on floor %d! Crisis averted.\n",
                   sim->event.target_floor);
            return;
        }
    }
    
    /* Timer expired — resolve */
    if (sim->event.timer <= 0) {
        game_resolve_event(sim, tower);
    }
}

void game_resolve_event(GameSim *sim, Tower *tower)
{
    if (sim->event.type == EVENT_BOMB && !sim->event.caught) {
        /* BOOM — destroy tenants in blast radius */
        int min_f = sim->event.target_floor - BOMB_BLAST_FLOORS / 2;
        int max_f = sim->event.target_floor + BOMB_BLAST_FLOORS / 2;
        int min_s = sim->event.target_slot - BOMB_BLAST_SLOTS / 2;
        int max_s = sim->event.target_slot + BOMB_BLAST_SLOTS / 2;
        
        if (min_f < TOWER_MIN_FLOOR) min_f = TOWER_MIN_FLOOR;
        if (max_f > TOWER_MAX_FLOOR) max_f = TOWER_MAX_FLOOR;
        if (min_s < 0) min_s = 0;
        if (max_s >= TOWER_WIDTH) max_s = TOWER_WIDTH - 1;
        
        int destroyed = 0;
        for (int i = 0; i < tower->tenant_count; i++) {
            Tenant *t = &tower->tenants[i];
            if (t->state == TENANT_ABANDONED) continue;
            if (t->floor >= min_f && t->floor <= max_f &&
                t->x >= min_s && t->x + t->width <= max_s + 1) {
                sim->event.damage_cost += ITEM_COST[(int)t->type];
                t->state = TENANT_ABANDONED;
                t->capacity = CAP_EMPTY;
                t->population = 0;
                destroyed++;
            }
        }
        
        tower->money -= sim->event.damage_cost;
        printf("💥 BOMB EXPLODED on floor %d! %d tenants destroyed, $%d damage!\n",
               sim->event.target_floor, destroyed, sim->event.damage_cost);
    } else if (sim->event.type == EVENT_FIRE) {
        int spread = sim->event.fire_right - sim->event.fire_left;
        printf("🧯 Fire extinguished on floor %d (spread %d slots, $%d damage)\n",
               sim->event.target_floor, spread, sim->event.damage_cost);
    }
    
    sim->event.active = 0;
    sim->event.type = EVENT_NONE;
}

void game_update_santa(GameSim *sim)
{
    if (!sim->santa.active) return;
    
    sim->santa.x -= 3;   /* Fly left (slower than original's 10 for visibility) */
    sim->santa.y += 1;   /* Drift down slightly */
    
    if (sim->santa.x < -200) {
        sim->santa.active = 0;  /* Off screen */
    }
}

/* ---------- Save / load (native format) ----------
 * Whole-state dump: Tower, GameSim, and the live tuning table are flat,
 * pointer-free structs, so a versioned binary image round-trips the whole
 * game — people mid-ride, queues, stats history, dialog settings, mods.
 * Struct layout drift is caught by the size fields. (.TWR import from
 * the original's FileT format is a separate, future milestone.) */
#define SAVE_MAGIC   0x52575443u    /* "CTWR" */
#define SAVE_VERSION 1u

int game_save(const GameSim *sim, const Tower *tower, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t hdr[5] = { SAVE_MAGIC, SAVE_VERSION,
                        (uint32_t)sizeof(Tower), (uint32_t)sizeof(GameSim),
                        (uint32_t)sizeof(Tuning) };
    int ok = fwrite(hdr, sizeof(hdr), 1, f) == 1 &&
             fwrite(tower, sizeof(*tower), 1, f) == 1 &&
             fwrite(sim, sizeof(*sim), 1, f) == 1 &&
             fwrite(&TUNING, sizeof(TUNING), 1, f) == 1;
    fclose(f);
    return ok ? 0 : -1;
}

int game_load(GameSim *sim, Tower *tower, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t hdr[5];
    int ok = fread(hdr, sizeof(hdr), 1, f) == 1 &&
             hdr[0] == SAVE_MAGIC && hdr[1] == SAVE_VERSION &&
             hdr[2] == sizeof(Tower) && hdr[3] == sizeof(GameSim) &&
             hdr[4] == sizeof(Tuning);
    if (ok)
        ok = fread(tower, sizeof(*tower), 1, f) == 1 &&
             fread(sim, sizeof(*sim), 1, f) == 1 &&
             fread(&TUNING, sizeof(TUNING), 1, f) == 1;
    fclose(f);
    return ok ? 0 : -1;
}
