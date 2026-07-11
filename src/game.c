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
    sim->hotel_pass_day = -1;   /* so the day-0 5PM pass still fires */
    sim->disaster_sched_day = -1;
    people_init(&sim->people);
}

/* --- Population calculation --- */

int game_calc_population(GameSim *sim, Tower *tower)
{
    int pop = 0;
    int standing = 0;
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
            /* A hotel room that isn't open for booking gets no guests
             * tonight — but already-hosted guests stay their night out
             * (the EXE only gates NEW check-ins, UniPeple 1220:3032). */
            if (item_is_hotel_room(t->type) && !t->open_for_booking &&
                !t->hosted) {
                t->population = 0;
                continue;
            }
        }

        /* Check if this tenant type is active at current time of day */
        int type_idx = (int)t->type;
        int base_pop = (type_idx < ITEM_TYPE_COUNT) ? TENANT_POPULATION[type_idx] : 0;
        /* Office headcount scales with established occupancy (cap_peak):
         * a thriving office (0x40) holds twice a new one's (0x20) staff.
         * Hotels are excluded — a room's guest count is fixed by room
         * type (1/2/3); their cap_peak growth shows up in revenue, not
         * heads (and 1-3 guests can't scale meaningfully in ints). */
        if (t->type == ITEM_OFFICE && t->cap_peak > CAP_PEAK_LOW)
            base_pop = base_pop * t->cap_peak / CAP_PEAK_LOW;
        /* Occupancy ramp: new tenants start at 50% pop, grow to 100% */
        if (t->state == 1) base_pop /= 2;  /* moving in */

        if (t->state >= 1) {
            /* STANDING population: everyone who lives or works here,
             * regardless of the hour — the EXE's population global counts
             * person records tied to tenants, so an office's workers count
             * at midnight too. Hotel guests are the exception: they only
             * exist while hosted. This is the number star promotion reads
             * (otherwise the weekday-EVENING window could never see an
             * office tower's population). */
            if (item_is_hotel_room(t->type)) {
                if (TENANT_ACTIVE_TIMES[type_idx][sim->time_of_day])
                    standing += base_pop;
            } else {
                standing += base_pop;
            }
        }

        if (type_idx < ITEM_TYPE_COUNT && TENANT_ACTIVE_TIMES[type_idx][sim->time_of_day]) {
            /* Active — people are physically present right now */
            if (t->state >= 1) {  /* At least MOVING_IN */
                t->population = base_pop;
                pop += base_pop;
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
    sim->standing_population = standing;
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
    
    int recycling_centers = 0;

    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        switch (t->type) {
        case ITEM_SECURITY:  sim->promo.has_security = 1;  break;
        case ITEM_RECYCLING: recycling_centers++;          break;
        case ITEM_METRO:     sim->promo.has_metro = 1;     break;
        case ITEM_MEDICAL:   sim->promo.has_medical = 1;   break;  /* existence */
        case ITEM_CATHEDRAL: sim->promo.has_cathedral = 1; break;
        case ITEM_HOTEL_SUITE:
            sim->promo.has_suite = 1;
            break;
        default: break;
        }
    }

    /* TrashT (seg_1088): adequate while population stays under 2500 per
     * recycling center; inadequate flips trucks off and blocks star 4/5. */
    sim->promo.recycling_adequate =
        recycling_centers > 0 &&
        sim->standing_population / recycling_centers < 2500;

    /* 0xB92D lives on the sim (armed 7AM, cleared by the no-center path);
     * the scan just mirrors it into the flag bank */
    sim->promo.medical_adequate = sim->medical_adequate;

    sim->tower_width = game_measure_width(tower);
}

/* --- Star rating check ---
 * Ported from seg_1140 FUN_1140_0411
 * Goes up one star at a time per check (original behavior). */

int game_check_star_rating(GameSim *sim, Tower *tower)
{
    int current = tower->star_rating;
    if (current >= 6) return current;  /* Already TOWER */
    
    /* Population threshold check — the STANDING count (workers count
     * while employed, not while present), else the evening-only promotion
     * window would never see an office tower's daytime population. */
    int pop = sim->standing_population;
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
               sim->standing_population >= STAR_POP_THRESHOLD[5] &&
               sim->promo.has_cathedral && sim->promo.vip_visited) {
        sim->wedding.active = 1;
        sim->wedding.day = tower->day;
        printf("\xf0\x9f\x92\x92 A wedding is being held at the "
               "cathedral today! (Day %d)\n", tower->day);
    }
}

/* The 3->4 and 4->5 branches of levelup_check also gate on the CLOCK:
 * time_period >= 4 (5:00 PM onward, running through the night to the 7AM
 * reset) and is_weekend != 1 (LevelUp @00de/@00e5 and @011e/@0125) — big
 * promotions only land on weekday evenings. The 2->3 branch has no clock
 * gate; early promotions come any time. Mapped to this sim's calendar:
 * evening-or-night hours, outside the weekend quarter (the port runs the
 * EXE's WD1/WD2/WE day-cycle as quarters within its 24h day, so the
 * weekend quarter plays the EXE's weekend day). */
static int promotion_window_open(const GameSim *sim)
{
    int evening_or_later = (sim->hour >= 17 || sim->hour < 7);
    return evening_or_later && sim->quarter != QUARTER_WEEKEND;
}

int game_check_promotion(GameSim *sim, Tower *tower, int target_star)
{
    (void)tower;
    switch (target_star) {
    case 1: return 1;  /* Starting star, always OK */
    case 2: return 1;  /* Just need population */
    case 3:
        /* Need security office (no clock gate at 2->3) */
        return sim->promo.has_security;
    case 4:
        /* LevelUp 1148:007e, 3->4 branch (byte-verified 2026-07-09):
         * suite (0xB92B) + recycling adequate (0xB92C) + VIP verdict
         * (0xB923) + medical adequate (0xB92D) + the weekday-evening
         * window. Metro is NOT required here — the old table had
         * metro/recycling swapped and invented a "4 suites" count; one
         * suite is the requirement. */
        return sim->promo.has_suite &&
               sim->promo.recycling_adequate &&
               sim->promo.medical_adequate &&
               sim->promo.vip_visited &&
               promotion_window_open(sim);
    case 5:
        /* 4->5 branch: metro ([0xB3E8] >= 0) + recycling adequate +
         * medical adequate + the weekday-evening window. NO VIP re-check
         * and no suite re-check — the binary reads neither 0xB923 nor
         * 0xB92B here. */
        return sim->promo.has_metro &&
               sim->promo.recycling_adequate &&
               sim->promo.medical_adequate &&
               promotion_window_open(sim);
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
static void update_capacity(Tenant *t, TimeOfDay tod, int reachable)
{
    /* A venue nobody can reach has no patrons — its occupancy byte must drain
     * to empty, not follow the time-of-day fill curve. (Otherwise an isolated
     * restaurant would animate to "packed" at dinnertime despite zero pop —
     * the "full unreachable restaurant" Jonah spotted.) */
    if (!reachable) {
        t->capacity = cap_drain_step(t->capacity);
        return;
    }

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
            t->capacity = cap_step_up_to(t->capacity, ceil);
            break;
        case TOD_AFTERNOON:
            /* Peak — hold at current level */
            break;
        case TOD_EVENING:
            t->capacity = cap_step_down_to(t->capacity, floor);
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
            t->capacity = cap_step_up_to(t->capacity, ceil);
            break;
        case TOD_NIGHT:
            /* Peak — hold */
            break;
        case TOD_DAWN:
            t->capacity = cap_step_down_to(t->capacity, floor);
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
        int reachable;
        {
            int fidx = floor_to_index(t->floor);
            int is_staff = (t->type == ITEM_SECURITY || t->type == ITEM_HOUSEKEEPING);
            reachable = (fidx >= 0 && fidx < TOWER_FLOOR_COUNT &&
                         (is_staff ? sim->reach_service[fidx] : sim->reach_public[fidx]));
            if (!reachable) is_active = 0;
            /* Hotel rooms only open for the night if the demand pass armed
             * them; hosted guests ride out their stay regardless. */
            if (is_hotel && !t->open_for_booking && !t->hosted) is_active = 0;
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
            /* Under construction — decrement on a GAME-TIME cadence, not every
             * frame. The old code stepped construction every update_tenants
             * call (every 4 frames), so an office (CONSTRUCTION_TIME 2) finished
             * in ~0.13s and build time was tied to frame rate, not the sim
             * clock. Gate the decrement to ~12 steps per in-game quarter
             * (cdiv = ticks_per_quarter/12) so it's frame-rate independent and
             * scales with game speed: office ~0.8s, hotel ~22s, at normal. */
            {
                int cdiv = sim->ticks_per_quarter / 24;
                if (cdiv < 4) cdiv = 4;
                if (sim->tick % cdiv < 4) t->construction--;
            }
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
                update_capacity(t, sim->time_of_day, reachable);
                
                /* Generate income (per tick, scaled) */
                int base_income = (type_idx < ITEM_TYPE_COUNT) ? TENANT_INCOME[type_idx] : 0;

                /* Retail customer competition: clustering same-type retail in a
                 * zone dilutes each one's revenue (see game_retail_income). */
                base_income = game_retail_income(sim, t, base_income);

                /* Rent scales with established occupancy (cap_peak): a thriving,
                 * grown office (0x40) pays ~2× a fresh one (0x20); an upgraded
                 * hotel/suite earns more than a new room. This is what makes the
                 * growth / gentrification / room-upgrade mechanics economically
                 * real instead of a sprite-frame change. (Decomp: TenantBehavior
                 * keys hotel revenue off the capacity byte too.) */
                {
                    uint8_t cbase = cap_base_peak(t->type);
                    if (cbase && t->cap_peak > cbase)
                        base_income = base_income * t->cap_peak / cbase;
                }
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

                /* Checkout: the room turns dirty and closes for booking
                 * unconditionally (MoneyT 1178:0ed1 writes the dirty band,
                 * 1178:0f14 clears the booking flag — even a room the
                 * roaches hit while occupied lands at DIRTY here, which is
                 * the only way an infested room ever recovers without
                 * demolition: its pre-roach guests leave, it comes out
                 * dirty, and a maid can reach it again). */
                if (is_hotel && t->hosted) {
                    t->condition = ROOM_DIRTY;
                    t->open_for_booking = 0;
                    t->hosted = 0;
                }
            }
            break;
            
        case TENANT_CLOSING:
            t->state = TENANT_VACANT;
            break;
            
        case TENANT_VACANT:
            /* Even when vacant, capacity can animate (hotels emptying, etc.) */
            update_capacity(t, sim->time_of_day, reachable);
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
                        /* Offices have no abandon path in the original (MainteT
                         * OfficeStressCheck): an unhappy office just rides it
                         * out — the firm stays put. Being stressed RESETS its
                         * growth timer (game_office_dynamics won't grow a
                         * stressed office), so stress only ever *delays* growth;
                         * it is never rewarded for it. So: clear and persist. */
                        t->stress = 0;
                        t->complaints = 0;
                        t->state = TENANT_OCCUPIED;
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
        if (item_is_hotel_room(t->type) && t->condition == ROOM_DIRTY)
            dirty++;
    }
    sim->unreachable_tenants = unreach;
    sim->dirty_rooms = dirty;
}

/* --- Housekeeping ---
 * After checkout, hotel rooms stay dirty (and unrentable) until a
 * housekeeping unit cleans them. Housekeepers work morning/afternoon, travel
 * the service network, and each unit handles a limited number of rooms per
 * day — too few units and rooms sit dirty, losing the night's income.
 * Maids clean DIRTY rooms only: the EXE's room picker matches the dirty
 * band exactly (MainteT 1150:00b4/00c9), so an INFESTED room is never on
 * their list. Cleaning also does NOT reset the room's neglect counter —
 * only a check-in does. */
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
            if (!item_is_hotel_room(room->type) ||
                room->condition != ROOM_DIRTY) continue;
            int rf = floor_to_index(room->floor);
            if (rf < 0 || rf >= TOWER_FLOOR_COUNT || !sim->reach_service[rf])
                continue;
            room->condition = ROOM_CLEAN;   /* neglect_days deliberately kept */
            hk->cleaned_today++;
            break;
        }
    }
}

/* --- The daily 5PM hotel pass ---
 * Ported from the TimeT 17:00 dispatch (ft 0x640): roach spread
 * (ExpandoBadHotel 1130:01e2) runs FIRST, then JudgeAllHotel's neglect
 * fuse (HotelNeglectCheck 1130:0e5c) and demand arm/disarm (1130:0f57),
 * in that order. Byte evidence: referee_84day_hotelbit_2026-07-09.md and
 * referee_infested_checkin_2026-07-10.md in the decomp repo.
 *
 * The EXE also re-arms rooms at the 4:59AM JudgeTenant sweep; the port
 * folds arming into this single daily pass — same net cadence (a room's
 * booking verdict changes at most once per day either way). */

/* The tenant physically abutting a room on one side, or NULL. Roaches only
 * cross to a touching hotel room — any gap, office, or corridor stops that
 * side (the EXE checks the adjacent floor-slot record's type). */
static Tenant *abutting_tenant(Tower *tower, const Tenant *t, int side)
{
    int x = (side < 0) ? t->x - 1 : t->x + t->width;
    TowerCell *c = tower_cell(tower, t->floor, x);
    if (!c || !c->tenant_id) return NULL;
    return tower_tenant(tower, c->tenant_id);
}

/* Stamp one spread victim (ExpandoBadHotel 1130:0283-02a9): the room turns
 * infested, closes for booking, and its satisfaction is marked ruined
 * (the EXE writes eval mark 0xFF — the black room in the eval overlay).
 * Occupancy is NOT touched: guests already inside stay until checkout,
 * which then lands the room at DIRTY (see the checkout comment above). */
static void infest_room(Tenant *v)
{
    if (!v || !item_is_hotel_room(v->type)) return;
    if (v->state == TENANT_EMPTY || v->state == TENANT_CONSTRUCTION) return;
    v->condition = ROOM_INFESTED;
    v->open_for_booking = 0;
    v->demand_category = 0;
    v->stress = 100;
}

/* Guest-stress bar that disarms a room: EXE tuning [0xDD78], star-scaled —
 * 150 at stars 1-3, 200 at 4+ (same two values as the judge thresholds;
 * R4 verified this consumer). [0xDD76] = 80 splits "happy" from "ok". */
#define DEMAND_HAPPY_BAR 80

/* Noisy neighbor for a hotel room (NoiseT seg_1138, byte-verified
 * 2026-07-10 referee): a restaurant, shop, fast food, office, movie
 * theater, or party hall within 20 cells of the room's near edge, on the
 * room's own floor. (Offices are bothered within 10, condos within 30;
 * hotel rooms only bother condos — neither matters to this check.) The
 * EXE walk tests the range BEFORE stepping outward, so one record past
 * the strict cutoff still counts — replicated here as edge distance <= 20
 * against nearest edges. */
static int hotel_has_noisy_neighbor(const Tower *tower, const Tenant *room)
{
    for (int i = 0; i < tower->tenant_count; i++) {
        const Tenant *n = &tower->tenants[i];
        if (n == room || n->floor != room->floor) continue;
        if (n->state == TENANT_ABANDONED) continue;
        switch (n->type) {
        case ITEM_RESTAURANT: case ITEM_SHOP: case ITEM_FAST_FOOD:
        case ITEM_OFFICE: case ITEM_CINEMA: case ITEM_PARTY_HALL:
            break;
        default:
            continue;
        }
        int gap = (n->x >= room->x + room->width)
                    ? n->x - (room->x + room->width)
                    : (room->x >= n->x + n->width)
                        ? room->x - (n->x + n->width)
                        : 0;                       /* overlapping/abutting */
        if (gap <= 20) return 1;
    }
    return 0;
}

void game_hotel_demand_pass(GameSim *sim, Tower *tower)
{
    (void)sim;

    /* Phase 1 — roach spread, one room per side per day. Sources are
     * snapshotted before stamping so a room infested today doesn't spread
     * further today. (The EXE's in-place loop reads as if a fresh stamp
     * could chain rightward, but both referees byte-derived the net
     * cadence as one-per-side-per-day; the port implements the cadence.
     * Flagged as a micro loose end in the decomp notes.) */
    static uint16_t sources[MAX_TENANTS];
    int nsources = 0;
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (item_is_hotel_room(t->type) && t->condition == ROOM_INFESTED)
            sources[nsources++] = (uint16_t)i;
    }
    for (int s = 0; s < nsources; s++) {
        Tenant *src = &tower->tenants[sources[s]];
        infest_room(abutting_tenant(tower, src, -1));
        infest_room(abutting_tenant(tower, src, +1));
    }

    /* Phase 2 — neglect fuse, and TODAY'S demand category for every clean
     * room. The EXE splits this the same way: JudgeAllHotel pass 1 runs
     * the per-room eval (whose verdict lands in +0x15) plus the neglect
     * check, and only THEN does pass 2 read the categories to arm/disarm —
     * so the pairing rescue below always sees today's verdicts, never a
     * mix of today's and yesterday's. */
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (!item_is_hotel_room(t->type)) continue;
        if (t->state == TENANT_EMPTY || t->state == TENANT_CONSTRUCTION)
            continue;

        if (t->condition != ROOM_CLEAN) {
            /* Neglect fuse (HotelNeglectCheck 1130:0e5c): a dirty room
             * still flagged open is closed and forgiven; an already-closed
             * one burns a fuse day, and on exactly the 3rd it turns
             * infested. Cleaning doesn't reset the fuse; check-in does. */
            if (t->open_for_booking) {
                t->open_for_booking = 0;
                t->demand_category = 0;
                t->neglect_days = 0;
            } else if (t->condition == ROOM_DIRTY) {
                t->neglect_days++;
                if (t->neglect_days == 3) {
                    t->condition = ROOM_INFESTED;
                    t->stress = 100;
                    printf("🪳 Cockroaches! Room on floor %d was left dirty "
                           "3 days (day %d)\n", t->floor, tower->day);
                }
            }
            continue;
        }

        /* Demand verdict: average the guests' banked elevator stress,
         * adjust for the room rate, add the noise penalty, compare to the
         * star-scaled bar (JudgeT 1130:0686: noisy neighbor -> metric
         * += 60, hardcoded — noise lowers demand but never closes a room
         * whose guests are otherwise happy). */
        int avg = t->guest_stress_trips
                    ? t->guest_stress_total / t->guest_stress_trips : 0;
        if (t->rent_class == 0)      avg += 30;   /* High rate: pickier guests */
        else if (t->rent_class == 2) avg -= 30;   /* Low rate: forgiving */
        else if (t->rent_class == 3) avg = 0;     /* Very low: always fills */
        if (hotel_has_noisy_neighbor(tower, t))
            avg += 60;
        if (avg < 0) avg = 0;

        int bar = (tower->star_rating >= 4) ? TUNING.judge_stressed
                                            : TUNING.judge_moderate;
        t->demand_category = (avg >= bar) ? 0
                           : (avg >= DEMAND_HAPPY_BAR) ? 1 : 2;
    }

    /* Phase 3 — arm/disarm (JudgeAllHotel pass 2, 1130:0f57): category
     * 1/2 rooms open for tonight; a category-0 room is rescued if a happy
     * (category 2) same-type room on its floor vouches for it — both
     * settle at category 1 — and is closed for booking otherwise. */
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (!item_is_hotel_room(t->type) || t->condition != ROOM_CLEAN)
            continue;
        if (t->state == TENANT_EMPTY || t->state == TENANT_CONSTRUCTION)
            continue;

        if (t->demand_category == 0) {
            Tenant *pair = NULL;
            for (int j = 0; j < tower->tenant_count && !pair; j++) {
                Tenant *r = &tower->tenants[j];
                if (r != t && r->type == t->type && r->floor == t->floor &&
                    item_is_hotel_room(r->type) && r->condition == ROOM_CLEAN &&
                    r->demand_category == 2)
                    pair = r;
            }
            if (!pair) {
                t->open_for_booking = 0;
                continue;
            }
            pair->demand_category = 1;
            t->demand_category = 1;
        }
        t->open_for_booking = 1;
        t->guest_stress_total = 0;   /* fresh sample window (EXE @105a) */
        t->guest_stress_trips = 0;
    }
}

/* Star evaluation — the EXE's LevelT re-checks continuously; the clock
 * gates inside levelup_check (weekday evenings for 3->4/4->5) are what
 * decide when a promotion LANDS. Called hourly from game_update so an
 * eligible tower promotes at the first open-window hour — 5PM on a
 * weekday — instead of at the old once-a-day midnight check. */
static void evaluate_star_rating(GameSim *sim, Tower *tower)
{
    scan_promotion_flags(sim, tower);

    int new_rating = game_check_star_rating(sim, tower);
    if (new_rating > tower->star_rating) {
        tower->star_rating = new_rating;
        sim->pending_star_up = new_rating;
        if (new_rating == 6) {
            printf("\xf0\x9f\x8f\x86 TOWER STATUS ACHIEVED!\n");
        } else {
            printf("\xe2\xad\x90 Promoted to %d star%s! Population: %d\n",
                   new_rating, new_rating > 1 ? "s" : "",
                   sim->standing_population);
        }
        /* Promotion bonus (LevelUp seg42:020f -> MoneyT AwardMoney):
         * the EXE pays $200k/$300k/$500k on reaching star 2/3/4.
         * No bonus decoded for star 5 or TOWER. */
        if (new_rating >= 2 && new_rating <= 4) {
            int bonus = TUNING.star_bonus[new_rating - 2];
            tower->money += bonus;
            sim->income_this_quarter += bonus;
            printf("\xf0\x9f\x92\xb0 Promotion bonus: $%d\n", bonus);
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
    
    /* Star evaluation, hourly — the weekday-evening window inside
     * game_check_promotion picks WHICH hours a 3->4/4->5 can land on. */
    if (day_ticks >= 24 && tick_in_day % (day_ticks / 24) == 0) {
        /* 7AM: MedicalDailyTick re-arms adequacy at star>=3 and the
         * patient-per-day counters start fresh (cap 40/center/day). */
        if (sim->hour == 7) {
            if (tower->star_rating >= 3)
                sim->medical_adequate = 1;
            for (int i = 0; i < tower->tenant_count; i++)
                if (tower->tenants[i].type == ITEM_MEDICAL)
                    tower->tenants[i].patients_today = 0;
        }
        evaluate_star_rating(sim, tower);
    }

    /* Schedule clock for the elevator tables (EXE 0xB3A0/0xB3A1: the
     * weekend day-type + the 7 periods that slice the day) */
    sim->people.sched_day = (sim->quarter == QUARTER_WEEKEND);
    if (day_ticks > 0) {
        int per = (int)((long)tick_in_day * 7 / day_ticks);
        sim->people.sched_period = (uint8_t)(per < 0 ? 0 : per > 6 ? 6 : per);
    }

    /* Grand-lobby prestige: a 2/3-story ground lobby forgives 25/50 ticks of
     * waiting (WaitT LobbyBonusAdjust). Recompute before the people step. */
    {
        int h = game_lobby_height(tower);
        sim->people.lobby_bonus = (h >= 3) ? 50 : (h == 2) ? 25 : 0;
    }

    /* The 5PM hotel pass: roach spread, neglect fuse, demand arm/disarm
     * (TimeT fires it at ft 0x640 = 17:00 sharp; latched once per day) */
    if (sim->hour >= 17 && sim->hotel_pass_day != tower->day) {
        sim->hotel_pass_day = tower->day;
        game_hotel_demand_pass(sim, tower);
    }

    /* People + elevators run every tick — cars and queues are the game */
    people_update(&sim->people, tower, sim->frame, sim->time_of_day,
                  sim->reach_public, sim->reach_service);

    /* Sick-worker rolls: 1-in-10 of office arrivals at star>=3 seek the
     * medical center (UniPeple medical path; below star 3 nobody rolls). */
    if (tower->star_rating >= 3) {
        for (int i = 0; i < sim->people.office_arrivals; i++)
            if (rand() % 10 == 0)
                game_medical_seek(sim, tower,
                                  sim->people.office_arrival_floor[i]);
    }
    sim->people.office_arrivals = 0;

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

        /* A medical emergency, sometimes (needs a medical center, no disaster) */
        game_try_medical(sim, tower);
    }

    /* The 10AM disaster dispatch: a fire every 84th day, a bomb threat
     * every 60th (TimeT ft 0xF0 block; latched once per day). */
    if (sim->hour >= 10 && sim->disaster_sched_day != tower->day) {
        sim->disaster_sched_day = tower->day;
        game_schedule_disasters(sim, tower);
    }

    /* Update active events (fire spread, bomb countdown) */
    game_update_event(sim, tower);
    game_update_medical(sim);

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
            
            /* Santa: a rare holiday flyby. A game "year" is only 4 days, so
             * firing every year (day % 4) put Santa on screen every ~3 minutes
             * at normal speed — far too often. Make it a roughly-every-third-
             * year Easter egg (day % 12) so it stays a treat. */
            if (tower->day > 0 && tower->day % 12 == 0 && !sim->santa.active) {
                game_launch_santa(sim, 960);
            }
            
            printf("🌅 Day %d begins! Pop: %d, Stars: %d, Money: $%ld\n",
                   tower->day, tower->population, tower->star_rating, tower->money);
            
            /* Wedding check at dawn (ChurchT) — wants fresh promo flags */
            scan_promotion_flags(sim, tower);
            game_wedding_daily(sim, tower);
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
    uint8_t office_top = (tower->star_rating >= 4) ? CAP_PEAK_HIGH : CAP_PEAK_HIGH_CAPPED;

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
        if (t->state != TENANT_OCCUPIED || t->stress > 10 ||
            t->condition != ROOM_CLEAN) continue;
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

/* Height of the ground lobby in stories: count consecutive floors from the
 * lobby floor (0) upward that carry a LOBBY tenant. A grand lobby is 2-3
 * stories tall and earns the WaitT wait-forgiveness bonus. Capped at 3. */
int game_lobby_height(Tower *tower)
{
    int h = 0;
    for (int f = TOWER_LOBBY_FLOOR; f <= TOWER_LOBBY_FLOOR + 2; f++) {
        int on_this_floor = 0;
        for (int i = 0; i < tower->tenant_count; i++) {
            if (tower->tenants[i].type == ITEM_LOBBY &&
                tower->tenants[i].floor == f) { on_this_floor = 1; break; }
        }
        if (!on_this_floor) break;
        h++;
    }
    return h;
}

/* --- Medical adequacy (MedicalT seg_1170, byte-verified 2026-07-10) ---
 * A sick office worker (1-in-10 roll on arriving at their desk, star>=3)
 * seeks a medical center in their 15-floor band, falling back to band 0.
 * Finding NONE is the only thing that clears medical adequacy (0xB92D) —
 * and fires the "more medical please" nag (InfoUI msg 6). A center at its
 * 40-patients/day cap turns the patient away SILENTLY; overflow never
 * dings adequacy, and no population-per-center bar exists (unlike
 * recycling). The EXE computes the registration and lookup bands with
 * different offsets — an original off-by-a-few bug — so the port uses ONE
 * band function for both, as the referee recommended. */
static int medical_band(int floor)
{
    return floor <= 0 ? 0 : floor / 15;
}

int game_medical_seek(GameSim *sim, Tower *tower, int from_floor)
{
    int band = medical_band(from_floor);
    Tenant *found = NULL;
    for (int pass = 0; pass < 2 && !found; pass++) {
        int want = pass == 0 ? band : 0;      /* own band, then band 0 */
        if (pass == 1 && band == 0) break;
        /* a RANDOM center in the band (ChoiceOfficeOneUBM picks randomly,
         * which is also what spreads patients across multiple centers) */
        Tenant *in_band[16];
        int n = 0;
        for (int i = 0; i < tower->tenant_count && n < 16; i++) {
            Tenant *t = &tower->tenants[i];
            if (t->type != ITEM_MEDICAL || t->state == TENANT_ABANDONED)
                continue;
            if (medical_band(t->floor) == want) in_band[n++] = t;
        }
        if (n > 0) found = in_band[rand() % n];
    }
    if (!found) {
        /* MoreMedicalPlease 1170:061c: nag + clear, unconditionally */
        if (sim->medical_adequate)
            printf("\xf0\x9f\x8f\xa5 A sick worker found no medical help "
                   "- the tower needs more medical centers!\n");
        sim->medical_adequate = 0;
        sim->medical_nag = 1;
        return 0;
    }
    if (found->patients_today >= 40)
        return 1;                              /* full: turned away, silent */
    found->patients_today++;
    return 2;
}

/* ================================================================
 * Medical emergencies (CheckMedicalEmergency, seg_11e8)
 * ================================================================
 * Faithful to the EXE skeleton: only fires when a medical center exists and
 * no disaster is running, at ~1% per evaluation. In the original it's purely
 * cosmetic (activates an emergency animation + a sound, no penalty). Here it
 * surfaces as a brief alert at a tenant floor — the medical center handles
 * it, so it's flavor, not a threat. */
void game_try_medical(GameSim *sim, Tower *tower)
{
    if (sim->medical.active || sim->event.active || sim->event.pending) return;
    if (!sim->promo.has_medical) return;
    if ((rand() % 100) != 0) return;

    /* Pick a random occupied tenant floor. */
    int occ_floors[128], n = 0;
    for (int i = 0; i < tower->tenant_count && n < 128; i++) {
        if (tower->tenants[i].state == TENANT_OCCUPIED &&
            tower->tenants[i].floor > 0)
            occ_floors[n++] = tower->tenants[i].floor;
    }
    if (n == 0) return;

    sim->medical.active = 1;
    sim->medical.floor  = occ_floors[rand() % n];
    sim->medical.timer  = 240;   /* paramedics on scene for a few seconds */
    sim->medical.notice = 1;     /* one-shot for the UI feed */
}

void game_update_medical(GameSim *sim)
{
    if (!sim->medical.active) return;
    if (--sim->medical.timer <= 0) {
        sim->medical.active = 0;
    }
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
 * Disasters (EventT seg_10c8 + FireT seg_10e8)
 * ================================================================
 * Scheduled, not random — see the EventState comment in game.h for the
 * full byte-verified model. TimeT's 10AM dispatch calls
 * game_schedule_disasters once per day; fires recur every 84th day and
 * bomb threats every 60th. */

static int tower_has_security(const Tower *tower)
{
    for (int i = 0; i < tower->tenant_count; i++)
        if (tower->tenants[i].type == ITEM_SECURITY &&
            tower->tenants[i].state != TENANT_ABANDONED)
            return 1;
    return 0;
}

static int tower_has_cathedral(const Tower *tower)
{
    for (int i = 0; i < tower->tenant_count; i++)
        if (tower->tenants[i].type == ITEM_CATHEDRAL)
            return 1;
    return 0;
}

/* PickRandomFloor (10c8:033e): uniform in [min_floor .. top built floor],
 * where "built" means the floor map has any non-transport tenant.
 * Returns the floor NUMBER, or TOWER_MIN_FLOOR - 1 if the tower doesn't
 * reach min_floor. */
static int pick_disaster_floor(const int16_t *left, const int16_t *right,
                               int min_floor)
{
    int top = TOWER_MIN_FLOOR - 1;
    for (int fi = 0; fi < TOWER_FLOOR_COUNT; fi++)
        if (right[fi] > left[fi])
            top = index_to_floor(fi);
    if (top < min_floor) return TOWER_MIN_FLOOR - 1;
    return min_floor + rand() % (top - min_floor + 1);
}

void game_schedule_disasters(GameSim *sim, Tower *tower)
{
    /* EXE order: the fire check runs first (TimeT @0357 before @0373), and
     * a started fire sets the game-flags bit that gates the bomb offer. */
    if (tower->day % 84 == 83)
        game_start_fire(sim, tower, 0);
    if (tower->day % 60 == 59)
        game_offer_bomb(sim, tower, 0);
}

void game_start_fire(GameSim *sim, Tower *tower, int forced_floor)
{
    EventState *ev = &sim->event;
    if (ev->active || ev->pending) return;
    if (tower->star_rating <= 2) return;
    if (!tower_has_security(tower)) return;
    if (tower_has_cathedral(tower)) return;   /* fires retire for good */

    int16_t left[TOWER_FLOOR_COUNT], right[TOWER_FLOOR_COUNT];
    tower_floor_extents(tower, left, right);

    /* Origin floor: uniform in [first floor above the ground lobby .. top
     * built floor]; its extent must fit the 32-cell right-edge offset. */
    int floor = forced_floor > 0
              ? forced_floor
              : pick_disaster_floor(left, right, game_lobby_height(tower) + 1);
    if (floor < TOWER_MIN_FLOOR) return;
    int fi = floor_to_index(floor);
    if (fi < 0 || fi >= TOWER_FLOOR_COUNT) return;
    if (right[fi] - left[fi] < 32) return;    /* StartFire: width must exceed 0x1F */

    *ev = (EventState){0};
    ev->type = EVENT_FIRE;
    ev->active = 1;
    ev->pending = 1;                          /* the helicopter offer (10e8:0147) */
    ev->target_floor = floor;
    ev->target_slot = right[fi] - 32;         /* 32 cells in from the right edge */
    ev->ransom_cost = FIRE_CHOPPER_COST;
    for (int i = 0; i < TOWER_FLOOR_COUNT; i++)
        ev->fire_left[i] = ev->fire_right[i] = -1;
    ev->fire_left[fi] = ev->fire_right[fi] = (int16_t)ev->target_slot;

    printf("🔥 FIRE breaks out on floor %d at cell %d!\n", floor, ev->target_slot);
}

void game_offer_bomb(GameSim *sim, Tower *tower, int forced_floor)
{
    EventState *ev = &sim->event;
    if (ev->active || ev->pending) return;
    if (!tower_has_security(tower)) return;

    /* The ransom switch (10c8:006e) has cases only for stars 2/3/4 —
     * any other star means no bomb threat at all. Values are tuning
     * words 0xDE1C/1E/20 (2000/3000/10000, x$100). */
    int ransom;
    switch (tower->star_rating) {
    case 2:  ransom = 200000;  break;
    case 3:  ransom = 300000;  break;
    case 4:  ransom = 1000000; break;
    default: return;
    }

    int16_t left[TOWER_FLOOR_COUNT], right[TOWER_FLOOR_COUNT];
    tower_floor_extents(tower, left, right);

    int floor = forced_floor > 0
              ? forced_floor
              : pick_disaster_floor(left, right, game_lobby_height(tower) + 1);
    if (floor < TOWER_MIN_FLOOR) return;
    int fi = floor_to_index(floor);
    if (fi < 0 || fi >= TOWER_FLOOR_COUNT) return;
    if (right[fi] - left[fi] <= 3) return;    /* extent must exceed 3 cells */

    *ev = (EventState){0};
    ev->type = EVENT_BOMB;
    ev->pending = 1;                          /* the pay-or-deploy dialog (0xBCC) */
    ev->target_floor = floor;
    /* RandomRange(left_extent, right_extent - 4) */
    ev->target_slot = left[fi] + rand() % (right[fi] - 4 - left[fi] + 1);
    ev->ransom_cost = ransom;

    printf("💣 BOMB THREAT — they want $%d. Floor %d (hidden from the player).\n",
           ev->ransom_cost, floor);
}

/* Player takes the free path: let the fire burn / send guards bomb-hunting.
 * Refusing the bomb is what ARMS it (TryStartEvent's refuse branch sets the
 * terror flag and the 1:00 PM trigger). */
void game_event_proceed(GameSim *sim, Tower *tower)
{
    (void)tower;
    EventState *ev = &sim->event;
    if (!ev->pending) return;
    ev->pending = 0;
    if (ev->type == EVENT_BOMB)
        ev->active = 1;                       /* detonates at 1PM unless caught */
}

/* Player pays: helicopters for a fire, the ransom for a bomb. */
void game_event_ransom(GameSim *sim, Tower *tower)
{
    EventState *ev = &sim->event;
    if (!ev->pending) return;

    if (ev->type == EVENT_FIRE) {
        /* The chopper starts at the origin floor's right extent - 12 and
         * sweeps left, dousing every front to its right (10e8:0147/0450). */
        int16_t left[TOWER_FLOOR_COUNT], right[TOWER_FLOOR_COUNT];
        tower_floor_extents(tower, left, right);
        int fi = floor_to_index(ev->target_floor);
        ev->pending = 0;
        ev->chopper_x = right[fi] - FIRE_FRONT_CELLS;
        tower->money -= ev->ransom_cost;
        sim->expenses_this_quarter += ev->ransom_cost;
        printf("🚁 Helicopters dispatched ($%d)\n", ev->ransom_cost);
        return;
    }

    /* Bomb: pay off the threat — no blast, threat over. */
    ev->pending = 0;
    ev->active = 0;
    tower->money -= ev->ransom_cost;
    sim->expenses_this_quarter += ev->ransom_cost;
    ev->caught = 1;
    ev->type = EVENT_NONE;
}

/* Destroy the tenant covering (floor index, cell) — burned tenants leave
 * rubble until rebuilt, exactly like the EXE's Burned Area records. */
static void fire_destroy_cell(GameSim *sim, Tower *tower, int fi, int x)
{
    if (x < 0 || x >= TOWER_WIDTH) return;
    uint16_t id = tower->grid[fi][x].tenant_id;
    if (id == 0) return;
    Tenant *t = tower_tenant(tower, id);
    if (!t || t->state == TENANT_ABANDONED) return;
    sim->event.damage_cost += ITEM_COST[(int)t->type];
    printf("🔥 %s on F%d destroyed by fire!\n",
           tower_item_name(t->type), t->floor);
    t->state = TENANT_ABANDONED;
    t->capacity = CAP_EMPTY;
    t->population = 0;
    t->burned = 1;
}

/* One EXE frame of fire simulation (SpreadFire 10e8:0304 + the chopper
 * pass 0450/0856). Fronts destroy where they stand and advance every 7th
 * frame; floors above the origin ignite on the 80-frames-per-floor
 * schedule; the chopper flies 1 cell left per frame dousing everything to
 * its right. */
static void fire_step_frame(GameSim *sim, Tower *tower,
                            const int16_t *left, const int16_t *right)
{
    EventState *ev = &sim->event;
    ev->fire_frame++;

    int origin_fi = floor_to_index(ev->target_floor);

    for (int fi = 0; fi < TOWER_FLOOR_COUNT; fi++) {
        if (right[fi] <= left[fi]) continue;  /* empty floor */

        if (ev->fire_left[fi] < 0) {
            /* Vertical spread: floor origin+k ignites at exactly frame
             * k*80, at the origin cell — never below the origin. A floor
             * whose extent misses the cell at that moment stays unburnt
             * (the EXE's equality check never retries). */
            if (fi > origin_fi &&
                ev->fire_frame == (fi - origin_fi) * FIRE_FLOOR_FRAMES &&
                ev->target_slot >= left[fi])
                ev->fire_left[fi] = (int16_t)ev->target_slot;
        } else {
            fire_destroy_cell(sim, tower, fi, ev->fire_left[fi]);
            if (ev->fire_frame % FIRE_SPREAD_FRAMES == 0)
                ev->fire_left[fi]--;
            if (ev->fire_left[fi] < left[fi])
                ev->fire_left[fi] = -1;       /* burned off the left edge */
        }

        if (ev->fire_right[fi] < 0) {
            if (fi > origin_fi &&
                ev->fire_frame == (fi - origin_fi) * FIRE_FLOOR_FRAMES &&
                ev->target_slot + FIRE_FRONT_CELLS <= right[fi])
                ev->fire_right[fi] = (int16_t)ev->target_slot;
        } else {
            /* The right front's flames span [front .. front+11]; it
             * destroys at front+12, the cell it's advancing into. */
            fire_destroy_cell(sim, tower, fi, ev->fire_right[fi] + FIRE_FRONT_CELLS);
            if (ev->fire_frame % FIRE_SPREAD_FRAMES == 0)
                ev->fire_right[fi]++;
            if (ev->fire_right[fi] + FIRE_FRONT_CELLS > right[fi])
                ev->fire_right[fi] = -1;      /* burned off the right edge */
        }
    }

    if (ev->chopper_x > 0) {
        ev->chopper_x--;
        for (int fi = 0; fi < TOWER_FLOOR_COUNT; fi++) {
            if (ev->fire_left[fi] > ev->chopper_x)  ev->fire_left[fi] = -1;
            if (ev->fire_right[fi] > ev->chopper_x) ev->fire_right[fi] = -1;
        }
        /* Done at the origin floor's left extent. Quirk kept from the EXE:
         * a front that slipped LEFT of that on some wider floor survives
         * the sweep and keeps burning to its own edge. */
        if (origin_fi >= 0 && ev->chopper_x <= left[origin_fi])
            ev->chopper_x = 0;
    }

    /* Fire over? (AnimateFire 10e8:025a's any-front scan + the ft==2000
     * hard stop = 9:00 PM.) */
    int burning = 0;
    for (int fi = 0; fi < TOWER_FLOOR_COUNT; fi++)
        if (ev->fire_left[fi] >= 0 || ev->fire_right[fi] >= 0)
            burning = 1;
    if (!burning || ev->fire_frame >= FIRE_END_FRAME) {
        printf("🧯 Fire out (origin floor %d, $%d damage)\n",
               ev->target_floor, ev->damage_cost);
        ev->active = 0;
        ev->type = EVENT_NONE;
        ev->chopper_x = 0;
    }
}

void game_update_event(GameSim *sim, Tower *tower)
{
    EventState *ev = &sim->event;
    if (ev->pending) return;                  /* the EXE dialogs are modal */
    if (!ev->active) return;

    if (ev->type == EVENT_BOMB) {
        /* Guards race the clock. The real GuardT pathing (guards walking
         * from security offices to the target) isn't decoded yet — this
         * stochastic stand-in keeps the race uncertain until it is. */
        if (rand() % 200 == 0) {
            ev->caught = 1;
            ev->active = 0;
            ev->type = EVENT_NONE;
            printf("🛡️ Security caught the bomb on floor %d! Crisis averted.\n",
                   ev->target_floor);
            return;
        }
        /* Detonation at 1:00 PM sharp (EXE frame 0x4B0). */
        if (sim->hour >= 13)
            game_resolve_event(sim, tower);
        return;
    }

    /* FIRE — advance the EXE frame clock. TimeT's day clock is non-uniform:
     * 320 frames per game-hour from 10AM to 1PM (ft 240->1200), then 100
     * per hour to the 9PM stop (->2000). One frame fires per
     * ticks-per-hour of accumulated frames-per-hour, so this is exact at
     * every game speed. */
    int16_t left[TOWER_FLOOR_COUNT], right[TOWER_FLOOR_COUNT];
    tower_floor_extents(tower, left, right);

    int ticks_per_hour = sim->ticks_per_quarter / 6;
    if (ticks_per_hour < 1) ticks_per_hour = 1;
    ev->fire_accum += (sim->hour < 13) ? 320 : 100;
    while (ev->fire_accum >= ticks_per_hour) {
        ev->fire_accum -= ticks_per_hour;
        fire_step_frame(sim, tower, left, right);
        if (!ev->active) return;
    }
}

void game_resolve_event(GameSim *sim, Tower *tower)
{
    EventState *ev = &sim->event;
    if (ev->type == EVENT_BOMB && !ev->caught) {
        /* DestroyTenants (10c8:02bd): everything touching floors
         * [target-2 .. target+3] x cells [target-20 .. target+20]. */
        int min_f = ev->target_floor - BOMB_BLAST_FLOORS_DOWN;
        int max_f = ev->target_floor + BOMB_BLAST_FLOORS_UP;
        int min_s = ev->target_slot - BOMB_BLAST_HALF_CELLS;
        int max_s = ev->target_slot + BOMB_BLAST_HALF_CELLS;

        int destroyed = 0;
        for (int i = 0; i < tower->tenant_count; i++) {
            Tenant *t = &tower->tenants[i];
            if (t->state == TENANT_ABANDONED) continue;
            if (t->floor + t->height - 1 < min_f || t->floor > max_f) continue;
            if (t->x + t->width - 1 < min_s || t->x > max_s) continue;
            ev->damage_cost += ITEM_COST[(int)t->type];
            t->state = TENANT_ABANDONED;
            t->capacity = CAP_EMPTY;
            t->population = 0;
            t->burned = 1;   /* leaves rubble until rebuilt, like fire */
            destroyed++;
        }

        /* No cash charge — the EXE's ResolveEvent(0) never touches money;
         * the loss is the destroyed buildings. */
        printf("💥 BOMB EXPLODED on floor %d! %d tenants destroyed ($%d of construction)!\n",
               ev->target_floor, destroyed, ev->damage_cost);
    }

    ev->active = 0;
    ev->type = EVENT_NONE;
}

void game_update_santa(GameSim *sim)
{
    if (!sim->santa.active) return;
    
    sim->santa.x -= 3;   /* Fly left (slower than original's 10 for visibility) */
    /* Stay HIGH in the sky with a gentle bob, instead of drifting down low
     * across the flight (the old y += 1 sank Santa to mid-screen by the end).
     * Integer triangle wave — no math.h needed. */
    {
        int phase = (sim->santa.x >> 3) & 15;          /* 0..15 */
        int bob = phase < 8 ? phase : 16 - phase;      /* 0..8..0 */
        sim->santa.y = 14 + bob;                        /* stays high: 14..22 */
    }

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
#define SAVE_VERSION 5u   /* v5: medical adequacy (0xB92D lifecycle) +
                           * Tenant.patients_today.
                           * v4: standing_population (star checks read the
                           * time-independent count).
                           * v3: scheduled-disaster EventState (per-floor
                           * fire fronts, chopper) + disaster_sched_day.
                           * v2: hotel condition/demand fields in Tenant,
                             hotel_pass_day in GameSim */

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
