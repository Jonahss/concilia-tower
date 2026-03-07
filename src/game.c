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
        if (t->type == ITEM_STAIRS || t->type == ITEM_ESCALATOR || 
            t->type == ITEM_ELEVATOR_SHAFT) continue;
        
        /* Check if this tenant type is active at current time of day */
        int type_idx = (int)t->type;
        if (type_idx < ITEM_TYPE_COUNT && TENANT_ACTIVE_TIMES[type_idx][sim->time_of_day]) {
            /* Active — contribute population based on state */
            if (t->state >= 1) {  /* At least MOVING_IN */
                int base_pop = (type_idx < ITEM_TYPE_COUNT) ? TENANT_POPULATION[type_idx] : 0;
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
    if (pop < STAR_POP_THRESHOLD[current]) return current;
    
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
        /* Need recycling + metro + hotel suites ≥ 4 + VIP */
        return sim->promo.has_recycling &&
               sim->promo.has_metro &&
               sim->promo.hotel_quarters >= 4;
        /* Note: VIP check omitted for now (not implemented) */
    case 5:
        /* Need medical + metro + hotel suites ≥ 4 */
        return sim->promo.has_medical &&
               sim->promo.has_metro &&
               sim->promo.hotel_quarters >= 4;
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
    
    if (is_day_type) {
        /* Day tenants: fill during morning, peak at afternoon, empty at evening */
        switch (tod) {
        case TOD_DAWN:
        case TOD_MORNING:
            if (t->capacity < CAP_MAX) t->capacity += CAP_STEP;
            break;
        case TOD_AFTERNOON:
            /* Peak — hold at current level */
            break;
        case TOD_EVENING:
            if (t->capacity > CAP_MIN) t->capacity -= CAP_STEP;
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
            if (t->capacity < CAP_MAX) t->capacity += CAP_STEP;
            break;
        case TOD_NIGHT:
            /* Peak — hold */
            break;
        case TOD_DAWN:
            if (t->capacity > CAP_MIN) t->capacity -= CAP_STEP;
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
        if (t->type == ITEM_STAIRS || t->type == ITEM_ESCALATOR || 
            t->type == ITEM_ELEVATOR_SHAFT) continue;
        
        int type_idx = (int)t->type;
        int is_active = (type_idx < ITEM_TYPE_COUNT) ? 
                        TENANT_ACTIVE_TIMES[type_idx][sim->time_of_day] : 0;
        
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
            }
            break;
            
        case TENANT_CONSTRUCTION:
            /* Under construction — decrement timer */
            t->construction--;
            if (t->construction <= 0) {
                t->state = TENANT_MOVING_IN;
                t->capacity = CAP_MIN;
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
                if (base_income > 0 && sim->ticks_per_quarter > 0) {
                    if (sim->tick % 60 == 0) {
                        int pay = base_income / (sim->ticks_per_quarter / 60);
                        income += pay;
                    }
                }
                
                /* Stress management — from MainteT */
                if (t->stress > 0) t->stress--;
            } else if (t->type != ITEM_CONDO) {
                /* Close for the inactive period */
                t->state = TENANT_VACANT;
                /* Don't zero capacity here — let update_capacity handle the fade */
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
                    t->state = TENANT_ABANDONED;
                    t->capacity = CAP_EMPTY;
                    printf("⚠ %s on F%d abandoned! (stress=%d, %d complaints)\n",
                           tower_item_name(t->type), t->floor, t->stress, t->complaints);
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

/* --- Main simulation update --- */

void game_update(GameSim *sim, Tower *tower)
{
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
    
    /* Update tenants every few ticks (not every frame) */
    if (sim->tick % 4 == 0) {
        long tick_income = 0, tick_expenses = 0;
        update_tenants(sim, tower, &tick_income, &tick_expenses);
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
        
        printf("📊 End of %s: Income $%ld, Expenses $%ld, Balance $%ld, Pop %d\n",
               game_quarter_name((Quarter)((sim->quarter - 1 + QUARTER_COUNT) % QUARTER_COUNT)),
               sim->income_this_quarter, sim->expenses_this_quarter,
               tower->money, tower->population);
        
        sim->income_this_quarter = 0;
        sim->expenses_this_quarter = 0;
        
        /* Day transition */
        if (sim->quarter >= QUARTER_COUNT) {
            sim->quarter = 0;
            tower->day++;
            
            /* VIP visit check (from VipT seg_1240: day % 9 == 3) */
            if (tower->day % 9 == 3 && tower->star_rating >= 3) {
                sim->vip_visiting = 1;
                sim->vip_last_day = tower->day;
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
                        printf("👔 VIP was satisfied! ⭐ (Helps with star promotion)\n");
                    } else {
                        printf("👔 VIP was NOT satisfied. Hotels need improvement.\n");
                    }
                    sim->vip_visiting = 0;
                }
            }
            
            /* Daily lobby maintenance (from MoneyT) */
            int lobby_cost = calc_lobby_maintenance(tower);
            if (lobby_cost > 0) {
                tower->money -= lobby_cost;
                sim->expenses_this_quarter += lobby_cost;
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
 * Lobby maintenance (from MoneyT seg_1178)
 * ================================================================
 * Lobby maintenance scales with star level:
 *   Star < 3: $100/segment
 *   Star < 4: $300/segment  
 *   Star >= 4: $500/segment */

static int calc_lobby_maintenance(Tower *tower)
{
    int cost_per_segment;
    if (tower->star_rating < 3)      cost_per_segment = 100;
    else if (tower->star_rating < 4) cost_per_segment = 300;
    else                             cost_per_segment = 500;
    
    int lobby_segments = 0;
    for (int i = 0; i < tower->tenant_count; i++) {
        if (tower->tenants[i].type == ITEM_LOBBY) lobby_segments++;
    }
    
    return lobby_segments * cost_per_segment;
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
