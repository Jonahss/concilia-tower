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
#include "sound_hook.h"
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
            if (item_is_hotel_room(t->type) && !t->demand_armed &&
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
 * gate; early promotions come any time. is_weekend is the EXE's real
 * [0xB3A0] day flag (every 3rd day) — reconciled 2026-07-12 from the
 * June model that treated the nightly 11pm-5am slice as "the weekend". */
static int promotion_window_open(const GameSim *sim, const Tower *tower)
{
    int evening_or_later = (sim->hour >= 17 || sim->hour < 7);
    return evening_or_later && !game_is_weekend(tower);
}

int game_check_promotion(GameSim *sim, Tower *tower, int target_star)
{
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
               promotion_window_open(sim, tower);
    case 5:
        /* 4->5 branch: metro ([0xB3E8] >= 0) + recycling adequate +
         * medical adequate + the weekday-evening window. NO VIP re-check
         * and no suite re-check — the binary reads neither 0xB923 nor
         * 0xB92B here. */
        return sim->promo.has_metro &&
               sim->promo.recycling_adequate &&
               sim->promo.medical_adequate &&
               promotion_window_open(sim, tower);
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
            if (is_hotel && !t->demand_armed && !t->hosted) is_active = 0;
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
                /* Build-complete jingle, random of two (referee row 21:
                 * (rand%2)+0x2714). Fires as a unit finishes construction. */
                play_snd((rand() & 1) ? SND_BUILD_DONE1 : SND_BUILD_DONE0);
            }
            break;
            
        case TENANT_MOVING_IN:
            /* Condos are a one-time SALE, banked when the buyer moves in
             * (res 0x3E9 condo row: $200k/$150k/$100k/$40k by rate class).
             * A re-occupied abandoned condo is a resale to a new buyer. */
            if (t->type == ITEM_CONDO) {
                int sale = tenant_rent(ITEM_CONDO, t->rent_class);
                income += sale;
                if (sim->cash_pending < 30) sim->cash_pending++;   /* queued cha-ching */
                printf("\xf0\x9f\x8f\xa0 Condo on F%d sold for $%d\n",
                       t->floor, sale);
            }
            t->state = TENANT_OCCUPIED;
            break;
            
        case TENANT_OCCUPIED:
            if (is_active) {
                /* Advance capacity animation (3-phase daily cycle) */
                update_capacity(t, sim->time_of_day, reachable);
                
                /* Per-tick income is now ONLY the parking approximation —
                 * every other income is event-driven: offices/shops bank
                 * quarterly lumps, hotels at checkout, condos at move-in,
                 * cinemas/party halls at show close, restaurants/fast
                 * food at their nightly settle (all byte-verified). */
                int base_income = (type_idx < ITEM_TYPE_COUNT)
                                    ? TENANT_INCOME[type_idx] : 0;

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
                    t->demand_armed = 0;
                    t->hosted = 0;
                    /* the stay is paid at checkout (MoneyT 0eac -> 0854,
                     * same 0x3E9 rows, one lump per guest stay) */
                    income += tenant_rent(t->type, t->rent_class);
                    /* Cash "ka-ching" is the EXE's checkout sound, throttled
                     * to ~every 2nd checkout (MoneyT counter 0x31b8). Queued so
                     * a nightful of checkouts rings a spaced run, not a blob. */
                    static int checkout_ctr = 0;
                    if ((++checkout_ctr & 1) == 0 && sim->cash_pending < 30)
                        sim->cash_pending++;
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
                         * growth timer (the old office-growth mechanic won't grow a
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
            room->condition = ROOM_CLEAN;   /* tenure deliberately kept */
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
    v->demand_armed = 0;
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
static int has_noisy_neighbor(const Tower *tower, const Tenant *room)
{
    for (int i = 0; i < tower->tenant_count; i++) {
        const Tenant *n = &tower->tenants[i];
        if (n == room || n->floor != room->floor) continue;
        if (n->state == TENANT_ABANDONED) continue;
        switch (n->type) {
        case ITEM_RESTAURANT: case ITEM_SHOP: case ITEM_FAST_FOOD:
        case ITEM_OFFICE: case ITEM_CINEMA: case ITEM_PARTY_HALL:
            break;
        case ITEM_HOTEL_SINGLE: case ITEM_HOTEL_TWIN: case ITEM_HOTEL_SUITE:
            /* NoiseT's one asymmetry: rooms only bother CONDOS */
            if (room->type == ITEM_CONDO) break;
            continue;
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
            if (t->demand_armed) {
                t->demand_armed = 0;
                t->demand_category = 0;
                t->tenure = 0;
            } else if (t->condition == ROOM_DIRTY) {
                t->tenure++;
                if (t->tenure == 3) {
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
        int avg = t->pool_stress_trips
                    ? t->pool_stress_total / t->pool_stress_trips : 0;
        if (t->rent_class == 0)      avg += 30;   /* High rate: pickier guests */
        else if (t->rent_class == 2) avg -= 30;   /* Low rate: forgiving */
        else if (t->rent_class == 3) avg = 0;     /* Very low: always fills */
        if (has_noisy_neighbor(tower, t))
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
                t->demand_armed = 0;
                continue;
            }
            pair->demand_category = 1;
            t->demand_category = 1;
        }
        t->demand_armed = 1;
        t->pool_stress_total = 0;   /* fresh sample window (EXE @105a) */
        t->pool_stress_trips = 0;
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

/* End-of-quarter bookkeeping: tally finances, take an analytics sample,
 * roll the quarter into the daily totals. Runs when the tick counter
 * wraps — and from game_clock_jump for any boundary the jump skips over,
 * so no income ever falls out of the books. Call AFTER sim->quarter has
 * been advanced past the closing quarter. */
static void quarter_closeout(GameSim *sim, Tower *tower)
{
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

    printf("📊 %s day %d, %s close: Income $%ld, Expenses $%ld, "
           "Balance $%ld, Pop %d, Commuters %d (avg wait %d)\n",
           game_is_weekend(tower) ? "WE" : "WD", tower->day,
           game_quarter_name((Quarter)((sim->quarter - 1 + QUARTER_COUNT) % QUARTER_COUNT)),
           sim->income_this_quarter, sim->expenses_this_quarter,
           tower->money, tower->population,
           sim->people.population_now, people_avg_wait(&sim->people));

    /* Roll this quarter into the running daily totals before clearing. */
    sim->day_income   += sim->income_this_quarter;
    sim->day_expenses += sim->expenses_this_quarter;
    sim->income_this_quarter = 0;
    sim->expenses_this_quarter = 0;
}

/* Jump the clock forward to an hour later the same day (the EXE's
 * post-catch jump: EventCleanup writes frame_time 0x5DC = 4:00 PM, so
 * the tower skips the dead hours the frozen hunt consumed). Quarter
 * boundaries crossed by the jump still get their close-out; hourly
 * rows in between simply don't fire — same as the EXE, where TimeT
 * rows between the old and new frame_time are skipped. */
void game_clock_jump(GameSim *sim, Tower *tower, int hour)
{
    int day_ticks = TICKS_PER_DAY(sim->speed);
    if (day_ticks <= 0 || hour <= 5) return;
    /* The day runs 5:00 -> 5:00 (tick_to_time above). */
    int target = (hour - 5) * 60 * day_ticks / (24 * 60);
    int now = sim->quarter * sim->ticks_per_quarter + sim->tick;
    if (target <= now) return;              /* never jump backward */
    int tq = target / sim->ticks_per_quarter;
    while ((int)sim->quarter < tq) {
        sim->quarter++;
        quarter_closeout(sim, tower);
    }
    sim->tick = target % sim->ticks_per_quarter;
    tick_to_time(target, day_ticks, &sim->hour, &sim->minute);
    sim->time_of_day = hour_to_tod(sim->hour);
}

void game_update(GameSim *sim, Tower *tower)
{
    /* Reachability is layout-driven, so refresh it even while paused (build
     * mode needs current data); throttled — it only changes on placement. */
    static int reach_throttle = 0;
    if (reach_throttle-- <= 0) {
        game_update_reachability(sim, tower);
        people_rebuild_transport(&sim->people, tower);
        /* Usability is layout-derived too (the EXE also recomputes on
         * build/demolish, not just the daily 7AM sweep). */
        game_parking_recompute(sim, tower);
        reach_throttle = 15;
    }

    if (sim->speed == SPEED_PAUSED) return;
    
    sim->ticks_per_quarter = TICKS_PER_QUARTER[sim->speed];
    sim->frame++;
    sim->tick++;

    /* Drain the settlement cha-ching queue: one ka-ching every ~26 ticks so a
     * rich morning rings a clear run of them (each cash WAV is ~0.5s). */
    if (sim->cash_pending > 0) {
        if (sim->cash_snd_timer <= 0) {
            play_snd(SND_CASH);
            sim->cash_pending--;
            sim->cash_snd_timer = 26;
        } else {
            sim->cash_snd_timer--;
        }
    }

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
        game_venue_hourly(sim, tower);
        game_retail_hourly(sim, tower);

        /* 7AM: MedicalDailyTick re-arms adequacy at star>=3 and the
         * patient-per-day counters start fresh (cap 40/center/day). */
        if (sim->hour == 7) {
            if (tower->star_rating >= 3)
                sim->medical_adequate = 1;
            for (int i = 0; i < tower->tenant_count; i++)
                if (tower->tenants[i].type == ITEM_MEDICAL)
                    tower->tenants[i].patients_today = 0;
            /* CheckAllParking (TimeT ft-0 row = 7:00AM) */
            game_parking_recompute(sim, tower);
        }
        evaluate_star_rating(sim, tower);
    }

    /* Schedule clock for the elevator tables (EXE 0xB3A0/0xB3A1: the
     * weekend day-type + the 7 periods that slice the day) */
    sim->people.sched_day = (uint8_t)game_is_weekend(tower);
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

    /* People + elevators run every tick — cars and queues are the game.
     * Exception: an armed bomb freezes the normal simulation (the EXE
     * swaps the per-frame person dispatch for the guard loop, 1090:0140). */
    if (!(sim->event.active && sim->event.type == EVENT_BOMB)) {
        people_update(&sim->people, tower, sim->frame, sim->time_of_day,
                      sim->hour, sim->reach_public, sim->reach_service);
        game_animate_occupants(sim, tower);   /* frozen with the people */
    }

    /* Sick-worker rolls: 1-in-10 of office arrivals at star>=3 seek the
     * medical center (UniPeple medical path; below star 3 nobody rolls). */
    if (tower->star_rating >= 3) {
        for (int i = 0; i < sim->people.office_arrivals; i++)
            if (rand() % 10 == 0)
                game_medical_seek(sim, tower,
                                  sim->people.office_arrival_floor[i]);
    }
    sim->people.office_arrivals = 0;
    game_venue_arrivals(sim, tower);
    game_retail_arrivals(sim, tower);
    game_relet_arrivals(sim, tower);

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
    }

    /* The 10AM disaster dispatch: a fire every 84th day, a bomb threat
     * every 60th (TimeT ft 0xF0 block; latched once per day). */
    if (sim->hour >= 10 && sim->disaster_sched_day != tower->day) {
        sim->disaster_sched_day = tower->day;
        game_schedule_disasters(sim, tower);
    }

    /* Update active events (fire spread, bomb countdown) */
    game_update_event(sim, tower);

    /* Update Santa position */
    game_update_santa(sim);
    
    /* Quarter transition */
    if (sim->tick >= sim->ticks_per_quarter) {
        sim->tick = 0;
        sim->quarter++;
        quarter_closeout(sim, tower);

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

            /* THE DAILY JUDGE (JudgeTenant tail, 4:59AM): categorize
             * offices/condos/shops and re-arm any vacant unit whose
             * verdict climbed off stressed — this is the whole rental
             * market (2026-07-11 vacancy referee). Runs BEFORE the
             * settlement, as in the EXE (eval then move-outs). */
            game_judge_daily(sim, tower);

            /* THE QUARTERLY SETTLEMENT (every 3rd day = the EXE quarter;
             * JudgeT MainLoop's day%3 gate + the TimeT maintenance row,
             * byte-verified 2026-07-11). EXE order on the one tick:
             * move-outs first (leavers pay nothing), then rent lumps,
             * then the maintenance sweep below. */
            int settlement_day = tower->day % 3 == 0;
            if (settlement_day) {
                game_stressed_moveout(sim, tower);

                /* Rent lumps: each occupied office and shop banks its
                 * full 0x3E9 value as ONE payment (never amortized).
                 * Hotels are absent from this loop — they bank per guest
                 * stay at checkout (MoneyT 0eac); condos are the one-time
                 * sale at move-in. */
                long rent = 0;
                int payers = 0;
                for (int i = 0; i < tower->tenant_count; i++) {
                    Tenant *t = &tower->tenants[i];
                    if (t->type != ITEM_OFFICE && t->type != ITEM_SHOP)
                        continue;
                    if (t->state != TENANT_OCCUPIED &&
                        t->state != TENANT_VACANT) continue;
                    rent += tenant_rent(t->type, t->rent_class);
                    if (t->state == TENANT_OCCUPIED) payers++;
                    /* the shop new-let immunity window closes after its
                     * first full quarter (tenure = EXE +0x17) */
                    if (t->type == ITEM_SHOP && t->tenure < 0xFF)
                        t->tenure++;
                }
                if (rent > 0) {
                    tower->money += rent;
                    sim->income_this_quarter += rent;
                    printf("\xf0\x9f\x92\xb0 Quarterly rent collected: $%ld\n", rent);
                    /* Queue a run of cha-chings that drains over the next
                     * several seconds — the morning "ka-ching...ka-ching" the
                     * EXE plays as its income counter animates up. Capped so a
                     * mega-tower doesn't ring for minutes. */
                    sim->cash_pending += payers;
                    if (sim->cash_pending > 30) sim->cash_pending = 30;
                }
            }

            /* VIP visit check (from VipT seg_1240: day % 9 == 3). The
             * VIP is a suite guest who arrives BY CAR (1240:008f picks
             * the person via their parked car) — no usable parking, no
             * VIP visit (the EXE's VoidVipVisit on a failed park;
             * binding is MEDIUM-HIGH per the 2026-07-11 referee). */
            int vip_can_park = tower->usable_spaces > 0;
            if (tower->day % 9 == 3 && tower->star_rating >= 3 &&
                !vip_can_park) {
                printf("👔 VIP visit canceled — nowhere to park the car.\n");
            }
            if (tower->day % 9 == 3 && tower->star_rating >= 3 &&
                vip_can_park) {
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
            
            /* Upkeep sweep (MoneyT 1178:0b44) — fires on the SAME
             * settlement tick as the rent (TimeT gates both on day%3,
             * byte-verified 2026-07-11; the old daily billing was 3x the
             * EXE's rate). */
            int upkeep = settlement_day ? calc_lobby_maintenance(tower) : 0;
            if (settlement_day) {
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
            /* Metro + parking upkeep (res 0x3EA, byte-verified 2026-07-11
             * referee): $100,000 per station, $10,000 per RAMP — spaces
             * and cars cost nothing (the old "per-car upkeep" note was a
             * misread of elevator cars). */
            for (int i = 0; i < tower->tenant_count; i++) {
                Tenant *mt = &tower->tenants[i];
                if (mt->state == TENANT_EMPTY ||
                    mt->state == TENANT_CONSTRUCTION) continue;
                if (mt->type == ITEM_METRO) upkeep += 100000;
                if (mt->type == ITEM_RAMP)  upkeep += 10000;
            }
            }
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
    /* Intra-day bookkeeping slices — NOT the EXE's WD/WE days (those
     * are day%3; see game_is_weekend). */
    static const char *names[] = {
        "Q1 (5-11a)", "Q2 (11a-5p)", "Q3 (5-11p)", "Q4 (11p-5a)"
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

/* ================================================================
 * The occupancy lifecycle (JudgeTenant daily pass + StressedTenantMoveOut
 * + the re-let path — byte-verified 2026-07-11 vacancy referee)
 * ================================================================
 * Offices, condos and shops live on the SAME +0x14/+0x15 bytes as hotel
 * rooms, on a daily schedule instead of the 5PM one: every day at the
 * 4:59AM tick each unit is judged into a category (2 content / 1 middle
 * / 0 stressed), and a vacant unit re-ARMS the moment its category
 * climbs off 0 — nothing else ever re-arms it. The trap is deliberate:
 * a stress-vacated office is judged forever on its FROZEN banked
 * elevator stress (nothing resets it while vacant), so it stays dead
 * until the player intervenes — cut the rent (class 3 forces content),
 * remove the noisy neighbor, earn a star (higher bar), or let a content
 * same-floor twin vouch for it at the move-out pairing. */

/* One unit's daily verdict (JudgeTenant 1130:0630/069e). */
static void judge_one_unit(GameSim *sim, Tower *tower, Tenant *t)
{
    int metric;
    if (t->type == ITEM_RESTAURANT || t->type == ITEM_FAST_FOOD) {
        /* The rest/FF arm (JudgeT @07ac/@0760, shop-judge referee
         * 2026-07-11): yesterday's CUSTOMERS vs the 50/35/25 income
         * ladder — no stress, no rate class. They can never move out;
         * the category feeds the eval map and the info popup. (The
         * 4th category at >=50 is real in the bytes; its consumer is
         * still undecoded.) */
        int c = t->customers_today;
        t->demand_category = c >= 50 ? 3 : c >= 35 ? 2 : c >= 25 ? 1 : 0;
        (void)sim; (void)tower;
        return;
    }
    if (t->type == ITEM_SHOP) {
        /* Shops judge on yesterday's DEMAND: leftover walk-in quota +
         * customers vs thresholds (20, 25), the bar shifted by rate
         * class {+5, 0, -5, -12} — the class-3 discount is the shop's
         * rescue lever (JudgeT 069e). */
        static const int adj[4] = { 5, 0, -5, -12 };
        int demand = t->retail_quota + t->customers_today;
        int a = adj[t->rent_class <= 3 ? t->rent_class : 1];
        t->demand_category = demand >= 25 + a ? 2
                           : demand >= 20 + a ? 1 : 0;
        return;
    }
    /* Office/condo: mean banked felt-wait of the unit's people — FROZEN
     * while vacant — with the same adjustments the hotel verdict uses
     * (JudgeT 0630: class 0 +30 / class 2 -30 / class 3 forces 0;
     * noise +60; bars 80 and the star-scaled 150/200). */
    metric = t->pool_stress_trips
                ? t->pool_stress_total / t->pool_stress_trips : 0;
    if (t->rent_class == 0)      metric += 30;
    else if (t->rent_class == 2) metric -= 30;
    else if (t->rent_class == 3) metric = 0;
    if (has_noisy_neighbor(tower, t)) metric += 60;
    if (metric < 0) metric = 0;
    int bar = (tower->star_rating >= 4) ? TUNING.judge_stressed
                                        : TUNING.judge_moderate;
    t->demand_category = metric >= bar ? 0
                       : metric >= DEMAND_HAPPY_BAR ? 1 : 2;
    (void)sim;
}

/* ================================================================
 * Parking usability (ParkingT CheckAllParking, TimeT ft-0 row —
 * byte-verified 2026-07-11 referee)
 * ================================================================
 * A space is USABLE iff its floor's ramp is on the chain anchored at
 * B1 — ramps must stack vertically at the same x to extend the chain
 * downward — and no >=4-cell bare gap on the floor cuts the drive
 * path between the ramp and the space. Recomputed daily at 7AM and on
 * build/demolish. Disconnected floors keep their spaces, dark. */
void game_parking_recompute(GameSim *sim, Tower *tower)
{
    /* One ramp per basement floor (build-gated); chain from B1 down. */
    int ramp_x[TOWER_FLOOR_COUNT];
    uint8_t chained[TOWER_FLOOR_COUNT];
    for (int i = 0; i < TOWER_FLOOR_COUNT; i++) {
        ramp_x[i] = -1;
        chained[i] = 0;
    }
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_RAMP || t->state == TENANT_EMPTY ||
            t->state == TENANT_CONSTRUCTION) continue;
        int f = floor_to_index(t->floor);
        if (f >= 0 && f < TOWER_FLOOR_COUNT) ramp_x[f] = t->x;
    }
    for (int floor = -1; floor >= TOWER_MIN_FLOOR; floor--) {
        int f = floor_to_index(floor);
        if (ramp_x[f] < 0) break;                    /* chain ends */
        if (floor < -1) {
            int above = floor_to_index(floor + 1);
            if (!chained[above] || ramp_x[f] != ramp_x[above]) break;
        }
        chained[f] = 1;
    }

    tower->usable_spaces = 0;
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_PARKING) continue;
        int f = floor_to_index(t->floor);
        int ok = f >= 0 && f < TOWER_FLOOR_COUNT && chained[f];
        if (ok) {
            /* Walk from the ramp toward the space; a run of >= 4 bare
             * cells severs the floor past it (referee verdict 6). */
            int rx = ramp_x[f], step = (t->x > rx) ? 1 : -1;
            int from = (step > 0) ? rx + ITEM_WIDTH[ITEM_RAMP] : rx - 1;
            int gap = 0;
            for (int cx = from; ok && cx != t->x && cx >= 0 &&
                                cx < TOWER_WIDTH; cx += step) {
                if (tower->grid[f][cx].type == ITEM_NONE) {
                    if (++gap >= 4) ok = 0;
                } else gap = 0;
            }
        }
        t->space_usable = (uint8_t)ok;
        t->space_ordinal = (uint16_t)(ok ? tower->usable_spaces : 0xFFFF);
        if (ok) tower->usable_spaces++;
    }

    /* Cars never outlive their space's usability window in a way the
     * quota math can see — clamp the counters so a demolished garage
     * doesn't pin the categories full forever. */
    if (tower->cars_office > 2 * tower->usable_spaces)
        tower->cars_office = 2 * tower->usable_spaces;
    if (tower->cars_suite > 2 * tower->usable_spaces)
        tower->cars_suite = 2 * tower->usable_spaces;
    (void)sim;
}

/* The info-dialog price control (InfoDlgT 1100:0b93-0c5e) — the game's
 * ONLY sim-time rate-class writer. An unchanged class returns with no
 * side effects (@0bdd); otherwise write +0x16 (@0bfe) and IMMEDIATELY
 * re-judge the unit (@0c2b lcall JudgeTenant). Because the judge's tail
 * re-arms a vacant unit whenever its fresh category climbs off 0,
 * cutting the rent revives a condemned unit the moment the player
 * clicks — not at the next dawn. Hotel rooms take the new class too
 * (checkout income + guest pickiness) but keep their own 5PM judge.
 * Returns 1 when the change put a dark unit back on the market. */
int game_set_rent_class(GameSim *sim, Tower *tower, Tenant *t, int cls)
{
    if (!t || cls < 0 || cls > 3 || t->rent_class == cls) return 0;
    t->rent_class = (uint8_t)cls;
    if (t->type != ITEM_OFFICE && t->type != ITEM_CONDO &&
        t->type != ITEM_SHOP) return 0;
    judge_one_unit(sim, tower, t);
    if (t->state == TENANT_ABANDONED && !t->demand_armed &&
        t->demand_category != 0) {
        t->demand_armed = 1;
        printf("🔑 Vacant %s on F%d is back on the market (rent change)\n",
               tower_item_name(t->type), t->floor);
        return 1;
    }
    return 0;
}

/* The 4:59AM judge (JudgeTenant tail 1130:09bb): categorize every
 * office/condo/shop — EXCEPT vacant-and-armed units (their movers are
 * already on the way) — then re-arm any vacant unit whose category
 * climbed off 0. Runs daily, BEFORE the 3rd-day move-out. */
void game_judge_daily(GameSim *sim, Tower *tower)
{
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_OFFICE && t->type != ITEM_CONDO &&
            t->type != ITEM_SHOP && t->type != ITEM_RESTAURANT &&
            t->type != ITEM_FAST_FOOD) continue;
        int vacant = t->state == TENANT_ABANDONED;
        /* TENANT_VACANT is the port's "leased, closed overnight" state —
         * at 4:59AM every healthy office is in it, so the judge must
         * treat it as occupied or it would never judge anything. */
        if (!vacant && t->state != TENANT_OCCUPIED &&
            t->state != TENANT_STRESSED &&
            t->state != TENANT_VACANT) continue;
        if (vacant && t->demand_armed && t->type != ITEM_SHOP) continue;
        judge_one_unit(sim, tower, t);
        if (vacant && !t->demand_armed && t->demand_category != 0) {
            t->demand_armed = 1;
            printf("🔑 Vacant %s on F%d is back on the market\n",
                   tower_item_name(t->type), t->floor);
        }
    }
}

/* --- The 3rd-day stressed move-out (StressedTenantMoveOut 1130:09e5,
 * byte-verified 2026-07-11) ---
 * Every 3rd day, OFFICES, CONDOS and SHOPS at category 0 vacate:
 * offices and shops leave free; a condo departure makes the PLAYER buy
 * the unit back at its rate-class price. Hotel rooms are exempt — their
 * whole lifecycle is the 5PM demand pass. Brand-new shops (tenure 0)
 * are immune for their first quarter. After the move-out, the pairing:
 * a content (category 2) same-type floor-mate drags to the middle band
 * with the leaver — and vouches for the vacated unit, which re-arms
 * with fresh banked stress. Without a pair, the unit goes dark until
 * the player rescues it (see game_judge_daily).
 *
 * HISTORY: June 2026 shipped this offset's mechanics inverted from a
 * stale annotation ("hotel upgrades", "office gentrification" — both
 * fabrications, deleted). PORT DIVERGENCE, deliberate: the EXE never
 * decrements population on a shop vacate (+10 leaks per re-let cycle);
 * the port's population recompute stays leak-free. */
void game_stressed_moveout(GameSim *sim, Tower *tower)
{
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_OFFICE && t->type != ITEM_CONDO &&
            t->type != ITEM_SHOP) continue;
        if (t->state != TENANT_OCCUPIED && t->state != TENANT_STRESSED &&
            t->state != TENANT_VACANT)   /* leased-but-closed-overnight */
            continue;
        if (t->demand_category != 0) continue;
        /* Tenure 0 = the first-settlement immunity — and ShopVacate
         * RESETS tenure to 0 (seg48 @123b), so a freshly re-let shop
         * gets the same grace. This is half of why the EXE's every-24th
         * -day rainy/settlement eviction wave is a blink, not a purge
         * (shop-judge referee 2026-07-11): evicted shops keep their
         * scores, keep trading vacant, and re-let within ~1.5 days. */
        if (t->type == ITEM_SHOP && t->tenure == 0) continue;

        if (t->type == ITEM_CONDO) {
            int buyback = tenant_rent(ITEM_CONDO, t->rent_class);
            tower->money -= buyback;
            sim->expenses_this_quarter += buyback;
            printf("\xf0\x9f\x8f\x9a Condo on F%d moved out - bought back for $%d\n",
                   t->floor, buyback);
        } else if (t->type == ITEM_SHOP) {
            printf("\xf0\x9f\x93\xa6 Shop on F%d moved out "
                   "(demand %d+%d, class %d)\n", t->floor,
                   t->retail_quota, t->customers_today, t->rent_class);
        } else {
            printf("\xf0\x9f\x93\xa6 %s on F%d moved out (stressed)\n",
                   tower_item_name(t->type), t->floor);
        }
        t->state = TENANT_ABANDONED;   /* vacant, re-lettable; no rubble */
        t->capacity = CAP_EMPTY;
        t->population = 0;
        t->demand_armed = 0;
        t->tenure = 0;
        if (t->type == ITEM_SHOP) t->retail_open = 0;
        /* rate_class and the banked pool stress survive the vacancy —
         * the frozen stress is what keeps the unit condemned */

        /* The pairing (1130:0a5f): a content same-type floor-mate drags
         * to category 1 WITH the leaver — and its vouching re-arms the
         * vacated unit with a fresh stress window. */
        for (int j = 0; j < tower->tenant_count; j++) {
            Tenant *n = &tower->tenants[j];
            if (j == i || n->type != t->type || n->floor != t->floor) continue;
            if ((n->state == TENANT_OCCUPIED ||
                 n->state == TENANT_VACANT) && n->demand_category == 2) {
                n->demand_category = 1;
                t->demand_category = 1;
                t->demand_armed = 1;
                t->pool_stress_total = 0;
                t->pool_stress_trips = 0;
                break;
            }
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

        /* Retail zone-competition stress REMOVED (2026-07-11 referee):
         * it was an invention. Real competition is emergent — customers
         * pick one random same-kind venue per zone, so clustering thins
         * each venue's customers and the nightly income ladder does the
         * punishing. (Also: it's SHOPS whose satisfaction is pinned 0 in
         * the EXE — they earn rent, not patron sales — not fast food;
         * the old comment repeated a type-ID swap.) */
        switch (t->type) {
        case ITEM_OFFICE:
            /* Offices don't compete by zone, but overcrowding matters */
            if (zd->office_count > ZONE_MAX_OFFICES)
                stress_add += (zd->office_count - ZONE_MAX_OFFICES) * 2;
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

/* ================================================================
 * Venues: cinema + party hall (VenueT seg_1180, fully decoded 2026-07-02)
 * ================================================================
 * Daily cycle, on the TimeT clock (wall-clock rows from the dispatcher
 * map; the port runs them on its hourly hook, so 12:45 rounds to noon):
 *   10AM  reset: film ages a day, patron quotas set, attendance cleared
 *   noon  movies open + matinee crowd summoned    (EXE t=1000, 12:45)
 *   1PM   matinee show starts; party hall opens + summons its 50 guests
 *   3PM   movies summon the evening crowd
 *   4PM   matinee crowd leaves
 *   5PM   evening show starts; party guests leave -> PARTY INCOME + close
 *   8PM   evening crowd leaves -> MOVIE INCOME + close
 * Movie attendance per showing decays with the film's age (tier = age/3):
 * hit films (id 7-13) seat 60/60/40/20, ordinary (0-6) 40/40/40/20; the
 * party hall admits a flat 50 (tuning 0xDDFA/0xDE02 + DailyVenueReset).
 * Income at close is tiered on the DAY's total patrons (GetVenueIncomeTier:
 * <40 -> $0, <80 -> $2,000, <100 -> $10,000, else $15,000). Patrons are
 * real Person entities — a venue nobody can reach earns nothing. */

/* Patrons per showing (GetMoviePatronQuota 1180:0b3c) */
static int movie_patron_quota(const Tenant *t)
{
    static const int hits[4]     = { 60, 60, 40, 20 };   /* 0xDDFA.. */
    static const int ordinary[4] = { 40, 40, 40, 20 };   /* 0xDE02.. */
    int tier = t->venue_age_days / 3;
    if (tier > 3) tier = 3;
    return (t->movie_id >= 7 && t->movie_id != 0xFF) ? hits[tier]
                                                     : ordinary[tier];
}

/* The tiered payout from today's attendance (GetVenueIncomeTier 1180:0bcb) */
static int venue_income(int patrons_today)
{
    if (patrons_today < 40)  return 0;        /* 0xDDEC / 0xDDF2 */
    if (patrons_today < 80)  return 2000;     /* 0xDDEE / 0xDDF4 */
    if (patrons_today < 100) return 10000;    /* 0xDDF0 / 0xDDF6 */
    return 15000;                             /* 0xDDF8 */
}

/* Attendance-per-showing for the info dialog (InfoComment 1108:0285
 * feeds the same quota into the "sales" lines). */
int game_movie_quota(const Tenant *t)
{
    return movie_patron_quota(t);
}

/* The change-movie actions from the theater's info dialog (InfoDlgT
 * 1100:432f hit / 1100:4377 ordinary). The rotation is deterministic —
 * only the film a theater OPENS with is random: each button steps to
 * the next film of its tier ((id+1) mod 7, hits offset by 7), so a hit
 * can follow an ordinary and vice versa. The new film is fresh (age 0);
 * today's quotas were already set at 10AM, so the bigger draw starts at
 * tomorrow's reset — matching the EXE, where the dialog only writes
 * movie_id/age and the quota is read at DailyVenueReset. Costs are
 * tuning words 0xDE10/0xDE12 (3000/1500 hundreds); charged
 * unconditionally — debt is allowed, as with every purchase. */
void game_change_movie(GameSim *sim, Tower *tower, Tenant *t, int hit)
{
    if (!t || t->type != ITEM_CINEMA) return;
    t->movie_id = hit ? (uint8_t)(7 + (t->movie_id + 1) % 7)
                      : (uint8_t)((t->movie_id + 1) % 7);
    t->venue_age_days = 0;
    int cost = hit ? 300000 : 150000;
    tower->money -= cost;
    sim->expenses_this_quarter += cost;
}

/* One hourly step of every venue's day (the TimeT rows above). */
void game_venue_hourly(GameSim *sim, Tower *tower)
{
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_CINEMA && t->type != ITEM_PARTY_HALL) continue;
        if (t->state == TENANT_EMPTY || t->state == TENANT_CONSTRUCTION ||
            t->state == TENANT_ABANDONED) continue;
        /* .TDT cinemas import as TWO strips (24-cell hall + 7-cell
         * entrance, both ITEM_CINEMA) — only the hall runs the show,
         * or an imported theater would sell every ticket twice. */
        if (t->type == ITEM_CINEMA && t->width < 20) continue;
        int is_movie = t->type == ITEM_CINEMA;

        switch (sim->hour) {
        case 10:   /* DailyVenueReset (EXE t=240) */
            if (t->venue_age_days < 0x7F) t->venue_age_days++;
            t->patrons_today = 0;
            if (is_movie) {
                t->quota_matinee = (uint8_t)movie_patron_quota(t);
                t->quota_evening = t->quota_matinee;
            } else {
                t->quota_matinee = 0;
                t->quota_evening = 50;        /* the flat party guest list */
            }
            t->venue_state = 0;
            break;
        case 12:   /* movies open for the matinee */
            if (is_movie && t->venue_state == 0) t->venue_state = 1;
            break;
        case 13:   /* matinee show; party hall opens */
            if (is_movie) { if (t->venue_state >= 1) t->venue_state = 3; }
            else if (t->venue_state == 0) t->venue_state = 1;
            break;
        case 16:   /* matinee crowd out */
            if (is_movie) t->venue_state = 1;
            break;
        case 17:   /* evening show; party hall closes + banks its income */
            if (is_movie) {
                if (t->venue_state >= 1) t->venue_state = 3;
            } else {
                int pay = venue_income(t->patrons_today);
                if (pay > 0) {
                    tower->money += pay;
                    sim->income_this_quarter += pay;
                }
                t->venue_state = 0;
            }
            break;
        case 20:   /* evening crowd out; movie income + close */
            if (is_movie) {
                int pay = venue_income(t->patrons_today);
                if (pay > 0) {
                    tower->money += pay;
                    sim->income_this_quarter += pay;
                }
                t->venue_state = 0;
            }
            break;
        default: break;
        }
    }
}

/* Count the patrons the people sim just delivered (PatronArrived
 * 1180:0c29): attendance is capped by the day's quotas, so an aging film
 * fills fewer of its seats no matter how many people the elevators bring. */
void game_venue_arrivals(GameSim *sim, Tower *tower)
{
    for (int a = 0; a < sim->people.venue_arrivals; a++) {
        Tenant *t = tower_tenant(tower, sim->people.venue_arrival_tenant[a]);
        if (!t) continue;
        if (t->type != ITEM_CINEMA && t->type != ITEM_PARTY_HALL) continue;
        int cap = t->quota_matinee + t->quota_evening;
        if (t->venue_state >= 1 && t->patrons_today < cap) {
            t->patrons_today++;
            if (t->venue_state == 1) t->venue_state = 2;
        }
    }
    sim->people.venue_arrivals = 0;
}

/* ================================================================
 * Retail patron economy (Restaurant.c seg_11a8 + MoneyT 1178:126c,
 * byte-verified 2026-07-11 referee)
 * ================================================================
 * The daily cycle: fast food and shops open at 10AM, restaurants at
 * 5PM. Opening sets the day's walk-in quota from yesterday's service
 * score — clamp(score, 10, cap), where the cap depends on the period
 * class (rainy > weekend > weekday; weekend reads max(wd, we) score).
 * Fast food and shops close at 9PM, restaurants at 11PM. Closing banks
 * the ONE daily income event for restaurants and fast food, a 4-tier
 * ladder on the day's customer count where the bottom tier is a LOSS
 * — a venue that never fills bleeds cash until demolished (they can
 * never move out; JudgeT's move-out handles only office/condo/shop).
 * Shops book nothing at close (quarterly rent instead). All venue
 * income is suppressed on bomb (day%60==59) and fire (day%84==83)
 * offer days. */

/* Walk-in quota caps (tuning res 0x7F05: 0xDDA6../0xDD98../0xDDB2..) */
static int retail_quota_cap(ItemType type, int period)
{
    static const int food[3] = { 35, 50, 25 };   /* wd / we / rainy */
    static const int shop[3] = { 25, 30, 18 };
    return (type == ITEM_SHOP) ? shop[period] : food[period];
}

/* Period class (GetTimePeriod 11a8:17eb): 2 rainy > 1 weekend > 0 weekday.
 * The rainy day is the every-8th-day flag (day%8==4) armed only below
 * 5 stars ([0xB3E4]; its "rain" identity is MEDIUM confidence — the
 * period selection itself is byte-verified). Weekend is the EXE's
 * DAY-based flag 0xB3A0 = ((day%12)%3) >= 2, i.e. every 3rd day — NOT
 * the port's quarter-of-day slice, which never overlaps business hours
 * (that quarter/day-type tangle is a queued time-model question). */
int game_retail_period(const GameSim *sim, const Tower *tower)
{
    (void)sim;
    if (tower->day % 8 == 4 && tower->star_rating < 5) return 2;
    if (game_is_weekend(tower)) return 1;
    return 0;
}

/* The daily income ladder (MoneyT 1178:126c; values are tuning words
 * 0xDDDC-0xDDEA, x$100). Signed: the bottom tier subtracts. */
static int retail_income_tier(ItemType type, int customers)
{
    if (type == ITEM_RESTAURANT)
        return customers >= 50 ? 10000 : customers >= 35 ? 6000
             : customers >= 25 ?  4000 : -6000;
    if (type == ITEM_FAST_FOOD)
        return customers >= 50 ? 5000 : customers >= 35 ? 3000
             : customers >= 25 ? 2000 : -3000;
    return 0;                                    /* shops: quarterly rent */
}

static int is_retail(ItemType ty)
{
    return ty == ITEM_RESTAURANT || ty == ITEM_FAST_FOOD || ty == ITEM_SHOP;
}

static void retail_open_one(GameSim *sim, Tower *tower, Tenant *t)
{
    int period = game_retail_period(sim, tower);
    int score = t->retail_score[period];
    /* Weekends read the better of the weekday/weekend scores. */
    if (period == 1 && t->retail_score[0] > score) score = t->retail_score[0];
    int cap = retail_quota_cap(t->type, period);
    int quota = score < 10 ? 10 : score > cap ? cap : score;

    t->retail_open = 1;
    t->retail_quota = (uint8_t)quota;
    t->retail_score[period] = 0;      /* today's grades start fresh */
    t->walkins_today = 0;
    t->patrons_now = 0;
    t->customers_today = 0;
}

static void retail_close_one(GameSim *sim, Tower *tower, Tenant *t)
{
    t->retail_open = 0;
    t->patrons_now = 0;
    if (t->type == ITEM_SHOP) return;
    /* Disaster-offer days suppress ALL venue income (1178:1283-12af —
     * the only uses of day%60 / day%84 outside the event scheduler). */
    if (tower->day % 60 == 59 || tower->day % 84 == 83) return;
    int amount = retail_income_tier(t->type, t->customers_today);
    tower->money += amount;
    if (amount >= 0) sim->income_this_quarter += amount;
    else             sim->expenses_this_quarter += -amount;
    if (amount < 0)
        printf("💸 %s on F%d lost $%d today (%d customers)\n",
               tower_item_name(t->type), t->floor, -amount,
               t->customers_today);
}

/* The hourly rows (TimeT: ft 240 / 1600 / 2000 / 2200). */
void game_retail_hourly(GameSim *sim, Tower *tower)
{
    int h = sim->hour;
    if (h != 10 && h != 17 && h != 21 && h != 23) return;
    long banked = 0;
    int closed_n = 0, cust = 0;
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (!is_retail(t->type)) continue;
        if (t->state == TENANT_EMPTY || t->state == TENANT_CONSTRUCTION)
            continue;
        /* A VACANT shop's record still runs its day (shop-judge referee:
         * the vacant record opens on its residual score — that quota is
         * what re-arms and re-lets it). Vacant restaurants/FF can't
         * exist (they never move out). */
        if (t->state == TENANT_ABANDONED && t->type != ITEM_SHOP) continue;
        int is_rest = t->type == ITEM_RESTAURANT;
        if ((h == 10 && !is_rest) || (h == 17 && is_rest))
            retail_open_one(sim, tower, t);
        else if ((h == 21 && !is_rest) || (h == 23 && is_rest)) {
            long before = tower->money;
            cust += t->customers_today;
            retail_close_one(sim, tower, t);
            banked += tower->money - before;
            closed_n++;
        }
    }
    if (closed_n)
        printf("🍽️ %dPM close: %d venue%s, %d customers, net $%ld\n",
               h - 12, closed_n, closed_n == 1 ? "" : "s", cust, banked);
}

/* A customer at the door (InRestPeple 11a8:0cc2). Returns 0 admitted,
 * 1 bounced (closed), 2 refused (full house, 40 inside). The EXE counts
 * walk-ins at dispatch and rolls back failed trips; the port counts
 * everyone at the door — same net, minus people stuck in a queue at
 * close. `walkin` marks the venue's own pool for the +7 counter. */
int game_retail_customer_in(GameSim *sim, Tower *tower, Tenant *t,
                            int walkin, int felt_wait)
{
    if (!t || !is_retail(t->type)) return 1;
    /* Service grading FIRST (11a8:1197 runs in the trip-completion
     * finalizer, BEFORE InRestPeple's door check): every arrival's
     * banked elevator wait feeds the current period's score — even a
     * customer who finds the doors already shut still teaches the venue
     * about its elevators. Without this, evening walk-ins arriving
     * after close grade nothing and basement shops spiral: score
     * starves -> tomorrow's quota shrinks -> fewer graders -> the
     * quota floor of 10 -> the 3rd-day judge reaps them. The second
     * bar is the star-scaled word the hotel demand pass also reads. */
    int period = game_retail_period(sim, tower);
    int bar2 = tower->star_rating >= 4 ? 200 : 150;   /* [0xDD78] */
    int add = felt_wait < 80 ? 2 : felt_wait < bar2 ? 1 : 0;
    int cap = retail_quota_cap(t->type, period);
    int ns = t->retail_score[period] + add;
    t->retail_score[period] = (uint8_t)(ns > cap ? cap : ns);

    if (!t->retail_open) return 1;
    if (t->patrons_now >= 40) return 2;
    t->patrons_now++;
    /* walk-ins were counted at dispatch (EXE 11a8:1148); only external
     * customers count at the door (InRestPeple 0e46) */
    if (!walkin && t->customers_today < 0xFFFF) t->customers_today++;
    return 0;
}

/* A patron heading home (OutRestPeple). */
void game_retail_customer_out(Tenant *t)
{
    if (t && is_retail(t->type) && t->patrons_now > 0) t->patrons_now--;
}

/* ================================================================
 * In-tenant occupant sprites (AnimPeple seg_1028, verified 2026-07-02)
 * ================================================================
 * The EXE ties these to real person records and their trip status
 * (+5 <= 5 = "present"); the port keeps the same poses and cadences
 * but decides presence from the unit's own state — a PORT PROXY,
 * flagged per type below. All frame codes and fixed positions are the
 * EXE's own randomizer constants. */

/* Roaming x: rand % (width-1), so a 2-cell sprite never hangs off the
 * right edge (the EXE reads the type table width; we use the strip's). */
static int occ_roam(const Tenant *t)
{
    int w = t->width;
    if (w < 3) w = 3;
    return rand() % (w - 1);
}

static void occ_clamp(const Tenant *t, TenantOccupants *o)
{
    int maxx = t->width >= 2 ? t->width - 2 : 0;
    for (int k = 0; k < o->count; k++)
        if (o->x[k] > maxx) o->x[k] = (uint8_t)maxx;
}

/* AnimateOfficePeople 1028:0902 — 6 workers; layout fork on the decor
 * variant (the same id%6 the renderer picks furniture with). */
static void occ_roll_office(const Tenant *t, TenantOccupants *o)
{
    int variant = t->id % 6;
    o->count = 6;
    if (variant < 2) {
        /* three desk-sitters at fixed positions (frames 0x0C-0x11) */
        o->x[0] = (uint8_t)(variant + 1); o->frame[0] = (uint8_t)(0x0E + rand() % 2);
        o->x[1] = (uint8_t)(variant + 2); o->frame[1] = (uint8_t)(0x10 + rand() % 2);
        o->x[2] = (uint8_t)(variant + 3); o->frame[2] = (uint8_t)(0x0C + rand() % 2);
    } else {
        o->x[0] = 7;                      o->frame[0] = (uint8_t)(0x0A + rand() % 2);
        int f1 = 6 + rand() % 4;
        o->x[1] = (uint8_t)(f1 >= 8 ? 4 : occ_roam(t)); o->frame[1] = (uint8_t)f1;
        int f2 = 2 + rand() % 4;
        o->x[2] = (uint8_t)(f2 >= 4 ? 2 : occ_roam(t)); o->frame[2] = (uint8_t)f2;
    }
    o->x[3] = (uint8_t)occ_roam(t); o->frame[3] = (uint8_t)(rand() % 2);
    o->x[4] = (uint8_t)occ_roam(t); o->frame[4] = (uint8_t)(0x14 + rand() % 4);
    o->x[5] = (uint8_t)occ_roam(t); o->frame[5] = (uint8_t)(0x12 + rand() % 2);
}

/* AnimateCondoPeople 1028:0FEB — 3 residents. */
static void occ_roll_condo(const GameSim *sim, const Tower *tower,
                           const Tenant *t, TenantOccupants *o)
{
    int weekend = game_is_weekend(tower);      /* 0xB3A0 */
    int daytime = sim->hour < 17;              /* period < 4 */
    int night = sim->time_of_day == TOD_NIGHT; /* [0x3218] lights-out */
    o->count = 3;
    o->x[0] = (uint8_t)occ_roam(t);
    o->frame[0] = weekend ? 0x22 : daytime ? 0x1E
                                           : (uint8_t)(0x20 + rand() % 2);
    int f1 = 0x29 + rand() % 9;
    o->x[1] = (uint8_t)(f1 == 0x31 ? 1 : occ_roam(t));  /* pose tied to
                                             the condo's left end */
    o->frame[1] = (uint8_t)f1;
    o->x[2] = (uint8_t)occ_roam(t);
    o->frame[2] = (uint8_t)(night ? 0x36 + rand() % 3   /* the dog band */
                                  : 0x32 + rand() % 4);
}

/* AnimateHotelGuests 1028:12C5 — 2 sprites in singles, 3 in twin/suite. */
static void occ_roll_hotel(const GameSim *sim, const Tenant *t,
                           TenantOccupants *o)
{
    int daytime = sim->hour < 17;
    int night = sim->time_of_day == TOD_NIGHT;
    o->count = (t->type == ITEM_HOTEL_SINGLE) ? 2 : 3;
    o->x[0] = (uint8_t)occ_roam(t);
    o->frame[0] = (uint8_t)(0x60 + rand() % 3);
    o->x[1] = (uint8_t)occ_roam(t);
    o->frame[1] = (uint8_t)(daytime ? 0x50 + rand() % 2
                                    : 0x52 + rand() % 9);
    if (o->count == 3) {
        o->x[2] = (uint8_t)occ_roam(t);
        o->frame[2] = (uint8_t)((!daytime && night) ? 0x5F  /* asleep */
                                                    : 0x5B + rand() % 4);
    }
}

void game_animate_occupants(GameSim *sim, Tower *tower)
{
    for (int i = 0; i < tower->tenant_count; i++) {
        if (((sim->frame + i) & 15) != 0) continue;   /* the EXE stagger */
        Tenant *t = &tower->tenants[i];
        TenantOccupants *o = &sim->occupants[i];
        o->count = 0;

        if (t->state == TENANT_CONSTRUCTION) {
            /* every unit being built shows one worker (no presence check
             * in the EXE either) */
            o->count = 1;
            o->x[0] = (uint8_t)occ_roam(t);
            o->frame[0] = (uint8_t)(0x39 + rand() % 6);
            occ_clamp(t, o);
            continue;
        }
        /* Metro train (DoRandomSubwayInOut 11e8:0273, byte-verified
         * 2026-07-11): pure cosmetics — a 1%/tick present/absent toggle
         * while 10AM-5PM, parked overnight empty. The port piggybacks
         * on this 16-frame pass (1 - 0.99^16 ~ a 1-in-7 flip) and
         * borrows venue_state (unused for metro) as "train in".
         * Runs BEFORE the occupancy gate: metro is infrastructure with no
         * tenant occupancy — the daily judge vacates it, but the EXE's
         * subway toggle never checks occupancy, so gating it on OCCUPIED
         * (as we did) silently killed both the train animation and its
         * sound on every real save. */
        if (t->type == ITEM_METRO) {
            if (sim->hour >= 10 && sim->hour < 17) {
                if (rand() % 7 == 0) {
                    t->venue_state = !t->venue_state;
                    play_snd(SND_METRO);   /* train in/out (referee row 11) */
                }
            } else {
                t->venue_state = 0;
            }
            continue;
        }

        if (t->state != TENANT_OCCUPIED && t->state != TENANT_STRESSED)
            continue;

        switch (t->type) {
        case ITEM_OFFICE:
            /* PROXY: workers shown during office hours (EXE: per-person
             * trip status) */
            if (TENANT_ACTIVE_TIMES[ITEM_OFFICE][sim->time_of_day])
                occ_roll_office(t, o);
            break;
        case ITEM_CONDO:
            occ_roll_condo(sim, tower, t, o);
            break;
        case ITEM_HOTEL_SINGLE:
        case ITEM_HOTEL_TWIN:
        case ITEM_HOTEL_SUITE:
            /* PROXY: guests shown while the room is hosted */
            if (t->hosted) occ_roll_hotel(sim, t, o);
            break;
        case ITEM_FAST_FOOD:
            /* AnimateFastFoodStaff: both behind the counter (left 3 cells);
             * PROXY: staffed while the doors are open */
            if (t->retail_open) {
                o->count = 2;
                o->x[0] = (uint8_t)(rand() % 3);
                o->frame[0] = (uint8_t)(0x3F + rand() % 2);
                o->x[1] = (uint8_t)(rand() % 3);
                o->frame[1] = (uint8_t)(0x41 + rand() % 2);
            }
            break;
        case ITEM_RESTAURANT:
            if (t->retail_open) {
                o->count = 2;
                o->x[0] = (uint8_t)(rand() % 3);
                o->frame[0] = (uint8_t)(0x49 + rand() % 3);
                o->x[1] = (uint8_t)(rand() % 3);
                o->frame[1] = (uint8_t)(0x4C + rand() % 4);
            }
            break;
        default:
            break;
        }
        occ_clamp(t, o);
    }
}

/* Consume the mover-arrival feed: the first arrival at a vacant, armed
 * unit IS the re-let (same code as the first let — 2026-07-11 vacancy
 * referee): the full table value banks at arrival (offices/shops their
 * quarterly lump, the authentic double-dip; condos the full resale),
 * the unit flips occupied, tenure restarts, and the pool's banked
 * stress finally resets. */
void game_relet_arrivals(GameSim *sim, Tower *tower)
{
    for (int a = 0; a < sim->people.relet_arrivals; a++) {
        Tenant *t = tower_tenant(tower, sim->people.relet_arrival_tenant[a]);
        if (!t || t->state != TENANT_ABANDONED || !t->demand_armed) continue;
        int lump = tenant_rent(t->type, t->rent_class);
        tower->money += lump;
        sim->income_this_quarter += lump;
        t->state = TENANT_OCCUPIED;
        t->tenure = 0;
        t->demand_category = 0xFF;         /* judged fresh next dawn */
        t->pool_stress_total = 0;
        t->pool_stress_trips = 0;
        t->capacity = CAP_MIN;
        t->cap_peak = game_init_cap_peak(t->type, tower->star_rating);
        if (t->type == ITEM_SHOP) t->retail_open = 0;  /* opens at 10AM */
        printf("🔑 %s on F%d re-let for $%d\n",
               tower_item_name(t->type), t->floor, lump);
    }
    sim->people.relet_arrivals = 0;
}

/* Consume the people sim's retail-arrival feed (like game_venue_arrivals):
 * every customer who just reached a retail door goes through the
 * InRestPeple check and grades the venue's service. */
void game_retail_arrivals(GameSim *sim, Tower *tower)
{
    for (int a = 0; a < sim->people.retail_arrivals; a++) {
        Tenant *t = tower_tenant(tower, sim->people.retail_arrival_tenant[a]);
        game_retail_customer_in(sim, tower, t,
                                sim->people.retail_arrival_walkin[a],
                                sim->people.retail_arrival_wait[a]);
    }
    sim->people.retail_arrivals = 0;
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

/* NOTE: there is no "medical emergency" event in SimTower. A prior port had a
 * game_try_medical/game_update_medical pair citing "CheckMedicalEmergency,
 * seg_11e8", but the reconciliation referee (referee_medical_reconcile_
 * 2026-07-13) proved that segment holds only MakeLobby + DoRandomSubwayInOut —
 * the "emergency" was a fabrication re-skinned from the metro-train routine.
 * Deleted. The real medical mechanic is game_medical_seek (above). */

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

static void guard_hunt_deploy(GameSim *sim, Tower *tower);

/* Player takes the free path: let the fire burn / send guards bomb-hunting.
 * Refusing the bomb is what ARMS it (TryStartEvent's refuse branch sets the
 * terror flag and the 1:00 PM trigger). */
void game_event_proceed(GameSim *sim, Tower *tower)
{
    (void)tower;
    EventState *ev = &sim->event;
    if (!ev->pending) return;
    ev->pending = 0;
    if (ev->type == EVENT_BOMB) {
        ev->active = 1;                       /* detonates at 1PM unless caught */
        guard_hunt_deploy(sim, tower);        /* GuardT 033d(1) */
    }
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

/* Deploy the guard fleet (FUN_10f8_033d mode 1). Guards 0-2 take the
 * office's floor, 3-5 the floor below; every office expands its own
 * search bubble outward from there. */
static void guard_hunt_deploy(GameSim *sim, Tower *tower)
{
    GuardHunt *h = &sim->event.hunt;
    memset(h, 0, sizeof(*h));

    int16_t left[TOWER_FLOOR_COUNT], right[TOWER_FLOOR_COUNT];
    tower_floor_extents(tower, left, right);

    for (int i = 0; i < tower->tenant_count && h->noffices < GUARD_OFFICES_MAX; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_SECURITY || t->state == TENANT_ABANDONED) continue;
        GuardOffice *o = &h->o[h->noffices++];
        o->office = (uint16_t)(i + 1);
        o->office_floor = (int8_t)t->floor;
        o->claimed_up = (int8_t)t->floor;         /* guards 0-2 hold it */
        o->claimed_down = (int8_t)(t->floor - 1); /* guards 3-5 hold it */
        for (int gi = 0; gi < GUARDS_PER_OFFICE; gi++) {
            GuardState *g = &o->g[gi];
            g->below = gi >= 3;
            g->floor = (int8_t)(g->below ? t->floor - 1 : t->floor);
            int fi = floor_to_index(g->floor);
            if (fi >= 0 && fi < TOWER_FLOOR_COUNT && right[fi] > left[fi])
                g->x = (int16_t)(right[fi] - 2);
            else
                g->x = -1;    /* unbuilt start floor: pull the frontier now */
        }
    }
    h->active = h->noffices > 0;
}

/* The next unsearched floor for a guard: preferred direction first (below
 * guards expand down, the rest up), skipping unbuilt floors and a tall
 * ground lobby's upper stories; both directions exhausted = retire.
 * Returns the floor, or TOWER_MIN_FLOOR - 1 when done. */
static int guard_next_floor(GuardOffice *o, const GuardState *g,
                            const int16_t *left, const int16_t *right,
                            int lobby_h)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        int down = (g->below != 0) ^ (attempt != 0);
        for (;;) {
            int f = down ? o->claimed_down - 1 : o->claimed_up + 1;
            if (f < TOWER_MIN_FLOOR || f > TOWER_TOP_FLOOR) break;
            if (down) o->claimed_down--; else o->claimed_up++;
            if (f >= 2 && f <= lobby_h) continue;    /* lobby upper story */
            int fi = floor_to_index(f);
            if (right[fi] <= left[fi]) continue;     /* unbuilt */
            return f;
        }
    }
    return TOWER_MIN_FLOOR - 1;
}

/* One EXE frame of the hunt (EmergencyGuardLoop 1220:6764/67cf +
 * GuardT sweep 10f8:0701, travel 10f8:104a). Returns 1 on a catch. */
static int guard_hunt_step_frame(GameSim *sim, Tower *tower,
                                 const int16_t *left, const int16_t *right)
{
    GuardHunt *h = &sim->event.hunt;
    int lobby_h = game_lobby_height(tower);

    for (int oi = 0; oi < h->noffices; oi++) {
        GuardOffice *o = &h->o[oi];
        for (int gi = 0; gi < GUARDS_PER_OFFICE; gi++) {
            GuardState *g = &o->g[gi];
            if (g->retired) continue;
            if (g->transit > 0) { g->transit--; continue; }
            if (g->pause > 0)   { g->pause--;   continue; }

            int fi = floor_to_index(g->floor);
            if (g->x >= 0 && fi >= 0 && fi < TOWER_FLOOR_COUNT && g->x > left[fi]) {
                g->x--;
                if (g->floor == sim->event.target_floor &&
                    g->x == sim->event.target_slot)
                    return 1;                          /* CAUGHT */
                g->pause = GUARD_SWEEP_FRAMES - 1;
            } else {
                /* left edge (or unbuilt start): pull the frontier */
                int f = guard_next_floor(o, g, left, right, lobby_h);
                if (f < TOWER_MIN_FLOOR) { g->retired = 1; continue; }
                int d = f - g->floor; if (d < 0) d = -d;
                g->transit = (int16_t)(GUARD_FLOOR_FRAMES * d +
                             (g->floor < o->office_floor ? GUARD_FLOOR_FRAMES : 0));
                g->floor = (int8_t)f;
                g->x = (int16_t)(right[floor_to_index(f)] - 2);
            }
        }
    }
    return 0;
}

void game_update_event(GameSim *sim, Tower *tower)
{
    EventState *ev = &sim->event;
    if (ev->pending) return;                  /* the EXE dialogs are modal */
    if (!ev->active) return;

    if (ev->type == EVENT_BOMB) {
        /* Detonation at 1:00 PM sharp (EXE frame 0x4B0), checked BEFORE
         * guard movement — a guard can't catch it on the deadline frame. */
        if (sim->hour >= 13) {
            game_resolve_event(sim, tower);
            return;
        }
        /* The deterministic sweep, paced in EXE frames like the fire
         * (320 frames/game-hour before 1PM). */
        int16_t left[TOWER_FLOOR_COUNT], right[TOWER_FLOOR_COUNT];
        tower_floor_extents(tower, left, right);
        int ticks_per_hour = sim->ticks_per_quarter / 6;
        if (ticks_per_hour < 1) ticks_per_hour = 1;
        ev->hunt.frame_accum += 320;
        while (ev->hunt.frame_accum >= ticks_per_hour) {
            ev->hunt.frame_accum -= ticks_per_hour;
            if (guard_hunt_step_frame(sim, tower, left, right)) {
                ev->caught = 1;
                ev->active = 0;
                ev->type = EVENT_NONE;
                ev->hunt.active = 0;
                printf("🛡️ Security caught the bomb on floor %d! "
                       "Crisis averted.\n", ev->target_floor);
                /* EXE EventCleanup resets frame_time to 0x5DC = 4:00 PM —
                 * the world was frozen for the whole hunt, and the jump
                 * hands those dead hours back. */
                game_clock_jump(sim, tower, 16);
                return;
            }
        }
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
    ev->hunt.active = 0;
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
#define SAVE_VERSION 11u  /* v11: parking (space_usable, car counters,
                           * Person.parked_cat) */
                          /* v10: the occupancy lifecycle (shared demand
                           * bytes for office/condo/shop; tenure).
                           * v9: occupant sprites in GameSim (AnimPeple).
                           * v8: retail patron economy in Tenant (service
                           * scores / quota / customer counters).
                           * v7: venue fields in Tenant (movie/show cycle).
                           * v6: GuardHunt in EventState (deterministic
                           * bomb sweep).
                           * v5: medical adequacy (0xB92D lifecycle) +
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
