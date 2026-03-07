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
#include <string.h>

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
            /* New tenant — starts moving in after a brief delay */
            if (sim->tick > 30) {  /* Small delay before first occupancy */
                t->state = TENANT_MOVING_IN;
            }
            break;
            
        case TENANT_MOVING_IN:
            /* Brief transition, then occupied */
            t->state = TENANT_OCCUPIED;
            break;
            
        case TENANT_OCCUPIED:
            if (is_active) {
                /* Generate income (per tick, scaled) */
                int base_income = (type_idx < ITEM_TYPE_COUNT) ? TENANT_INCOME[type_idx] : 0;
                if (base_income > 0 && sim->ticks_per_quarter > 0) {
                    /* Distribute income across the quarter's ticks */
                    /* Only add income once per quarter-tick boundary */
                    if (sim->tick % 60 == 0) {
                        int pay = base_income / (sim->ticks_per_quarter / 60);
                        income += pay;
                    }
                }
                
                /* Stress management */
                if (t->stress > 0) t->stress--;
            } else if (t->type != ITEM_CONDO) {
                /* Close for the inactive period */
                t->state = TENANT_VACANT;
            }
            break;
            
        case TENANT_CLOSING:
            t->state = TENANT_VACANT;
            break;
            
        case TENANT_VACANT:
            if (is_active) {
                t->state = TENANT_OCCUPIED;
            }
            break;
            
        case TENANT_STRESSED:
            if (t->stress > 100) {
                t->state = TENANT_ABANDONED;
                printf("⚠ %s on F%d abandoned! (stress=%d)\n",
                       tower_item_name(t->type), t->floor, t->stress);
            } else if (t->stress < 50) {
                t->state = TENANT_OCCUPIED;
            }
            break;
            
        case TENANT_ABANDONED:
            /* Dead tenant — stays until demolished */
            t->population = 0;
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
