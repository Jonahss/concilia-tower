/* test_sim.c — checks for transport reachability, housekeeping, and the
 * people/elevator pipeline.
 * Build: gcc -o /tmp/test_sim tests/test_sim.c src/tower.c src/game.c \
 *            src/people.c -Isrc -lm
 * No SDL needed — pure simulation. */
#include <stdio.h>
#include <string.h>
#include "game.h"

static Tower tw;
static GameSim sim;
static int fails = 0;

#define CHECK(cond, msg) do { \
    if (cond) printf("  ok   %s\n", msg); \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

static uint16_t place(ItemType ty, int floor, int x)
{
    uint16_t id = tower_place(&tw, ty, floor, x);
    if (!id) printf("  (placement failed: %s f%d x%d)\n",
                    tower_item_name(ty), floor, x);
    return id;
}

static Tenant *tenant(uint16_t id) { return tower_tenant(&tw, id); }

static void fresh(void)
{
    tower_init(&tw);
    game_init(&sim);
    tw.money = 100000000L;  /* don't let costs interfere */
}

/* Lobby sits at x=179..194; build above/beside it. */
#define BX 179

static void test_stairs(void)
{
    printf("stairs connectivity:\n");
    fresh();
    place(ITEM_FLOOR, 1, BX);
    place(ITEM_FLOOR, 2, BX);
    uint16_t office = place(ITEM_OFFICE, 3, BX + 6);  /* on top of the slab stack */
    game_update_reachability(&sim, &tw);
    CHECK(sim.reach_public[floor_to_index(0)], "ground reachable");
    CHECK(!sim.reach_public[floor_to_index(3)], "floor 3 cut off without stairs");
    CHECK(sim.unreachable_tenants == 1, "office counted unreachable");

    for (int f = 0; f <= 2; f++) place(ITEM_STAIRS, f, BX + 20);
    game_update_reachability(&sim, &tw);
    CHECK(sim.reach_public[floor_to_index(3)], "floor 3 reachable via stair chain");
    CHECK(sim.unreachable_tenants == 0, "office connected");
    (void)office;
}

static void test_elevators(void)
{
    printf("elevator types:\n");
    fresh();

    /* Shafts self-support, starting beside the lobby on the ground floor */
    for (int f = 0; f <= 10; f++) place(ITEM_ELEVATOR_SHAFT, f, 196);    /* standard */
    for (int f = 0; f <= 14; f++) place(ITEM_ELEVATOR_SERVICE, f, 202);  /* service  */
    for (int f = 0; f <= 20; f++) place(ITEM_ELEVATOR_EXPRESS, f, 208);  /* express  */
    game_update_reachability(&sim, &tw);

    CHECK(sim.reach_public[floor_to_index(10)], "standard reaches f10 (public)");
    CHECK(sim.reach_service[floor_to_index(10)], "standard reaches f10 (service)");
    CHECK(!sim.reach_public[floor_to_index(11)], "f11: standard tops out, express passes through");
    CHECK(!sim.reach_public[floor_to_index(14)], "service elevator does NOT extend public net");
    CHECK(sim.reach_service[floor_to_index(14)], "service elevator extends service net to f14");
    CHECK(sim.reach_public[floor_to_index(15)], "express stops at sky lobby f15");
    CHECK(!sim.reach_public[floor_to_index(20)], "express does NOT stop at f20");

    /* Express anchor rule (MakeElevator 11f8:0ff9): a NEW express shaft
     * must start at ground/basement/sky-lobby; extending an existing
     * column is free. f7 atop the service column: supported, mid-tower,
     * not an extension -> refused. f15 same spot: sky lobby -> allowed.
     * f21 on the express column: extension -> allowed. */
    CHECK(!tower_can_place(&tw, ITEM_ELEVATOR_EXPRESS, 7, 196),
          "new express shaft can't start mid-tower (f7)");
    CHECK(tower_can_place(&tw, ITEM_ELEVATOR_EXPRESS, 15, 202),
          "new express shaft CAN start at sky-lobby f15");
    CHECK(tower_can_place(&tw, ITEM_ELEVATOR_EXPRESS, 21, 208),
          "extending the existing express column past f20 is allowed");
}

static void run_days(int days)
{
    sim.speed = SPEED_TURBO;   /* 120 ticks/quarter -> 480/day */
    for (int i = 0; i < 480 * days; i++)
        game_update(&sim, &tw);
}

static void test_housekeeping(void)
{
    printf("housekeeping cycle:\n");
    fresh();
    uint16_t hotel = place(ITEM_HOTEL_SINGLE, 1, BX + 2);   /* above the lobby */
    uint16_t hk    = place(ITEM_HOUSEKEEPING, 1, BX + 7);
    place(ITEM_STAIRS, 0, BX + 12);
    Tenant *room = tenant(hotel);
    if (!room) { printf("  FAIL no hotel tenant\n"); fails++; return; }

    /* Let construction finish and several day cycles run; the room should
     * cycle dirty (after checkout) -> cleaned (housekeeping) each day.
     * Cleaning can land in the same tick batch as checkout, so observe it
     * via the housekeeper's quota counter rather than the transient flag. */
    Tenant *hku = tenant(hk);
    int saw_cleaned = 0, saw_hosted_again = 0;
    run_days(1);
    for (int i = 0; i < 480 * 4; i++) {
        game_update(&sim, &tw);
        if (hku->cleaned_today > 0) saw_cleaned = 1;
        if (saw_cleaned && room->state == TENANT_OCCUPIED &&
            sim.time_of_day == TOD_NIGHT) saw_hosted_again = 1;
    }
    CHECK(saw_cleaned, "housekeeping cleaned a checked-out room");
    CHECK(saw_hosted_again, "cleaned room hosted guests again");

    /* Remove housekeeping: after the next checkout the room sticks dirty */
    tower_remove(&tw, hk);
    for (int i = 0; i < 480 * 3; i++) game_update(&sim, &tw);
    CHECK(room->dirty, "without housekeeping the room stays dirty");
    CHECK(sim.dirty_rooms == 1, "dirty room counted in stats");
    CHECK(room->state != TENANT_OCCUPIED || sim.time_of_day != TOD_NIGHT,
          "dirty room takes no guests");
}

/* --- people/elevator pipeline (people.c) --- */

/* Count live people whose home is a given tenant and who are at a floor */
static int people_at(uint16_t home, int fidx, int state)
{
    int n = 0;
    for (int i = 0; i < sim.people.people_high; i++) {
        Person *p = &sim.people.people[i];
        if (p->home_tenant == home && p->state == state &&
            p->cur_floor == fidx) n++;
    }
    return n;
}

static void force_occupied(uint16_t id)
{
    Tenant *t = tenant(id);
    if (t) { t->state = TENANT_OCCUPIED; t->construction = 0; }
}

static void test_commute_elevator(void)
{
    printf("commute by elevator:\n");
    fresh();
    /* office tower: slabs first (transports may overlap slabs, not vice
     * versa), office on f5, then the shaft */
    for (int f = 1; f <= 4; f++) place(ITEM_FLOOR, f, BX);
    uint16_t office = place(ITEM_OFFICE, 5, BX + 6);
    /* shafts need a clear column (slab cells reject them) */
    for (int f = 0; f <= 6; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);
    CHECK(office != 0, "office placed");
    force_occupied(office);

    sim.time_of_day = TOD_MORNING;
    people_rebuild_transport(&sim.people, &tw);
    CHECK(sim.people.shaft_count == 1, "one shaft detected");
    CHECK(sim.people.shafts[0].capacity == 21,
          "standard car capacity 21 (the 42-person car is the EXPRESS)");

    int f5 = floor_to_index(5);
    int arrived = 0;
    for (int frame = 0; frame < 2000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING,
                      sim.reach_public, sim.reach_service);
        arrived = people_at(office, f5, PERSON_AT_DEST);
        if (arrived == 6) break;
    }
    CHECK(arrived == 6, "all 6 office workers rode to floor 5");
    CHECK(sim.people.trips_done >= 6, "trips recorded");
    CHECK(sim.people.wait_samples >= 6, "wait times banked");

    /* evening: everyone goes home and despawns */
    int gone = 0;
    for (int frame = 2000; frame < 6000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_EVENING,
                      sim.reach_public, sim.reach_service);
        if (sim.people.population_now == 0) { gone = 1; break; }
    }
    CHECK(gone, "workers went home in the evening and despawned");
}

static void test_walk_rules(void)
{
    printf("walk budget (6 escalator / 3 with stairs):\n");
    fresh();
    /* escalators up 5 floors, no elevator: walkable (all-escalator <= 6) */
    for (int f = 1; f <= 4; f++) place(ITEM_FLOOR, f, BX);
    uint16_t office = place(ITEM_OFFICE, 5, BX + 6);
    for (int f = 0; f <= 4; f++) place(ITEM_ESCALATOR, f, BX + 20);
    force_occupied(office);
    people_rebuild_transport(&sim.people, &tw);

    int f5 = floor_to_index(5);
    int arrived = 0;
    for (int frame = 0; frame < 3000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING,
                      sim.reach_public, sim.reach_service);
        arrived = people_at(office, f5, PERSON_AT_DEST);
        if (arrived == 6) break;
    }
    CHECK(arrived == 6, "5 escalator flights are walkable");

    /* 5 floors of STAIRS only: beyond the 3-floor stair budget -> no route */
    fresh();
    for (int f = 1; f <= 4; f++) place(ITEM_FLOOR, f, BX);
    office = place(ITEM_OFFICE, 5, BX + 6);
    for (int f = 0; f <= 4; f++) place(ITEM_STAIRS, f, BX + 20);
    force_occupied(office);
    people_rebuild_transport(&sim.people, &tw);
    Tenant *t = tenant(office);
    int before = t->stress;
    for (int frame = 0; frame < 600; frame++)
        people_update(&sim.people, &tw, frame, TOD_MORNING,
                      sim.reach_public, sim.reach_service);
    CHECK(people_at(office, f5, PERSON_AT_DEST) == 0,
          "5 stair flights exceed the walk budget");
    CHECK(sim.people.trips_failed > 0, "failed trips recorded");
    CHECK(t->stress > before, "no-route frustration reached the tenant");
}

static void test_queue_and_stress(void)
{
    printf("queues, wait banking, stress:\n");
    fresh();
    for (int f = 0; f <= 6; f++) place(ITEM_ELEVATOR_SHAFT, f, 196);
    people_rebuild_transport(&sim.people, &tw);
    ElevatorShaft *s = &sim.people.shafts[0];
    CHECK(sim.people.shaft_count == 1, "shaft detected");

    /* fill the up queue at ground to the 40-person cap */
    int g = floor_to_index(0);
    sim.people.people_high = 200;
    for (int i = 0; i < 60; i++) {
        sim.people.people[i].home_tenant = 1;  /* fake but nonzero */
        sim.people.people[i].state = PERSON_QUEUED;
    }
    int joined = 0;
    for (int i = 0; i < 60; i++)
        joined += people_join_queue(&sim.people, 0, g, 1, i);
    CHECK(joined == 40, "queue caps at 40 per direction");
    CHECK(s->up_call_car[g] != 0, "first joiner pressed the call button");
}

/* The elevator dialog's controls: per-floor stop toggles + car count */
static void test_elevator_dialog(void)
{
    printf("elevator dialog (ElvDlogT):\n");
    fresh();
    for (int f = 0; f <= 6; f++) place(ITEM_ELEVATOR_SHAFT, f, 196);
    people_rebuild_transport(&sim.people, &tw);
    ElevatorShaft *s = &sim.people.shafts[0];
    int g = floor_to_index(0), f3 = floor_to_index(3);

    /* queue someone on floor 3, then toggle the stop off */
    sim.people.people_high = 8;
    sim.people.people[0].home_tenant = 1;
    sim.people.people[0].state = PERSON_QUEUED;
    sim.people.people[0].cur_floor = (uint8_t)f3;
    people_join_queue(&sim.people, 0, f3, 1, 0);
    CHECK(s->up_call_car[f3] != 0, "queued person called a car");
    people_set_serviced(&sim.people, 0, f3, 0);
    CHECK(!s->serviced[f3], "stop toggled off");
    CHECK(sim.people.people[0].state == PERSON_PLANNING,
          "queued person flushed to replan");
    CHECK(s->up_call_car[f3] == 0 && s->stop[f3].up_count == 0,
          "call and queue cleared");
    people_set_serviced(&sim.people, 0, f3, 1);
    CHECK(s->serviced[f3], "stop toggled back on");

    /* car count up: new car parks at the bottom */
    people_set_num_cars(&sim.people, 0, 3);
    CHECK(s->num_cars == 3, "car count raised to 3");
    CHECK(s->car[2].active && s->car[2].floor == s->lo,
          "new car parked at the pit");

    /* car count down: retired car dumps its rider to replan */
    s->car[2].passengers = 1;
    s->car[2].pax[0] = 1;            /* person 0 aboard car 2 */
    s->car[2].floor = (uint8_t)f3;
    sim.people.people[0].state = PERSON_RIDING;
    people_set_num_cars(&sim.people, 0, 2);
    CHECK(s->num_cars == 2, "car count lowered to 2");
    CHECK(!s->car[2].active, "retired car deactivated");
    CHECK(sim.people.people[0].state == PERSON_PLANNING &&
          sim.people.people[0].cur_floor == f3,
          "rider dumped at the car's floor to replan");

    /* home floors: idle car with no work returns home (red diamond) */
    people_set_home(&sim.people, 0, 0, f3);
    CHECK(s->home[0] == f3, "car 0 homed on floor 3");
    s->car[0].floor = (uint8_t)g;
    s->car[0].target = (uint8_t)g;
    s->car[0].door_timer = 1;          /* doors closing, no work anywhere */
    for (int t = 0; t < 200; t++) people_update(&sim.people, &tw, t, 1,
                                               sim.reach_public,
                                               sim.reach_service);
    CHECK(s->car[0].floor == f3, "idle car returned to its home floor");

    /* settings survive a layout rebuild (shaft extended one floor) */
    people_set_serviced(&sim.people, 0, f3, 0);
    place(ITEM_ELEVATOR_SHAFT, 7, 196);
    people_rebuild_transport(&sim.people, &tw);
    s = &sim.people.shafts[0];
    CHECK(s->hi == floor_to_index(7), "shaft extended");
    CHECK(s->num_cars == 2, "car count survived the rebuild");
    CHECK(!s->serviced[f3] && s->serviced[g] && s->serviced[s->hi],
          "serviced flags survived; new floor defaults on");
}

/* Patrons visit venues and leave; housekeepers ride the service net */
static void test_patrons_and_staff(void)
{
    printf("patrons & staff trips:\n");
    fresh();
    for (int f = 1; f <= 1; f++) place(ITEM_FLOOR, f, BX);
    uint16_t ff = place(ITEM_FAST_FOOD, 2, BX + 6);
    for (int f = 0; f <= 1; f++) place(ITEM_STAIRS, f, BX + 30);
    CHECK(ff != 0, "fast food placed");
    force_occupied(ff);
    people_rebuild_transport(&sim.people, &tw);

    int f2 = floor_to_index(2);
    int seen = 0;
    for (int frame = 0; frame < 1200 && seen < 5; frame++) {
        people_update(&sim.people, &tw, frame, TOD_AFTERNOON,
                      sim.reach_public, sim.reach_service);
        if (people_at(ff, f2, PERSON_AT_DEST) > seen)
            seen = people_at(ff, f2, PERSON_AT_DEST);
    }
    CHECK(seen == 5, "5 lunch patrons walked up to the fast food");
    int gone = 0;
    for (int frame = 1200; frame < 6000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_AFTERNOON,
                      sim.reach_public, sim.reach_service);
        if (sim.people.population_now == 0) { gone = 1; break; }
    }
    CHECK(gone, "patrons finished their visit and left");

    /* staff: housekeeping on f1, dirty room on f2, service elevator only —
     * the housekeeper must ride the service net (staff don't use stairs
     * they don't have, and there are none) */
    fresh();
    uint16_t base = place(ITEM_HOTEL_SINGLE, 1, BX);
    uint16_t room = place(ITEM_HOTEL_SINGLE, 2, BX);
    uint16_t hk = place(ITEM_HOUSEKEEPING, 1, BX + 6);
    uint16_t svc = place(ITEM_ELEVATOR_SERVICE, 0, 250);
    place(ITEM_ELEVATOR_SERVICE, 1, 250);
    place(ITEM_ELEVATOR_SERVICE, 2, 250);
    CHECK(base && room && hk && svc, "hotel stack + housekeeping + service shaft placed");
    force_occupied(base); force_occupied(room); force_occupied(hk);
    tenant(room)->dirty = 1;
    people_rebuild_transport(&sim.people, &tw);

    int dispatched = 0, rode = 0;
    int f2s = floor_to_index(2);
    for (int frame = 0; frame < 3000 && !rode; frame++) {
        people_update(&sim.people, &tw, frame, TOD_DAWN,
                      sim.reach_public, sim.reach_service);
        for (int i = 0; i < sim.people.people_high; i++) {
            Person *p = &sim.people.people[i];
            if (p->home_tenant != hk || !p->service) continue;
            dispatched = 1;
            if (p->state == PERSON_AT_DEST && p->cur_floor == f2s) rode = 1;
        }
    }
    CHECK(dispatched, "housekeeper dispatched");
    CHECK(rode, "housekeeper rode the service elevator to the dirty room");
}

static void test_money(void)
{
    printf("money mechanics (globals.md #54):\n");

    CHECK(TUNING.car_cost_std == 80000 && TUNING.car_cost_express == 150000 &&
          TUNING.car_cost_service == 50000,
          "add-a-car prices $80k/$150k/$50k (tuning res +0x90)");

    /* Upkeep sweep at the day boundary: cars x per-type fee + escalators.
     * No occupiable tenants -> no rent flows to muddy the delta. */
    fresh();
    for (int f = 0; f <= 2; f++) place(ITEM_ELEVATOR_SHAFT, f, 196);
    place(ITEM_ESCALATOR, 0, BX + 20);
    game_update_reachability(&sim, &tw);
    people_rebuild_transport(&sim.people, &tw);
    if (sim.people.shaft_count >= 1)
        people_set_num_cars(&sim.people, 0, 3);
    long before = tw.money;
    run_days(1);
    long upkeep = before - tw.money;
    CHECK(upkeep == 3 * TUNING.maint_car_std + TUNING.maint_escalator,
          "day sweep charges 3 cars x $10k + escalator $5k");

    /* Lobbies are free below 3 stars (FUN_1178_0a6a: star fee table 0/30/100) */
    tw.star_rating = 2;
    before = tw.money;
    run_days(1);
    CHECK(before - tw.money == upkeep,
          "2-star lobby adds no upkeep (folklore $100/segment retired)");
}

static void test_save_load(void)
{
    printf("save/load round-trip:\n");
    fresh();
    place(ITEM_OFFICE, 1, BX);
    for (int f = 0; f <= 1; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);
    game_update_reachability(&sim, &tw);
    for (int i = 0; i < 600; i++) game_update(&sim, &tw);
    TUNING.capacity_express = 7;   /* a mod, to prove tuning persists */

    long money = tw.money;
    int shafts = sim.people.shaft_count;
    uint32_t tick = sim.tick;
    CHECK(game_save(&sim, &tw, "/tmp/ct_test.sav") == 0, "save succeeds");

    /* scribble over everything, then restore */
    tower_init(&tw); game_init(&sim); tuning_reset();
    CHECK(game_load(&sim, &tw, "/tmp/ct_test.sav") == 0, "load succeeds");
    CHECK(tw.money == money, "money round-trips");
    CHECK(sim.people.shaft_count == shafts, "shafts round-trip");
    CHECK(sim.tick == tick, "sim clock round-trips");
    CHECK(TUNING.capacity_express == 7, "tuning mods round-trip");
    CHECK(game_load(&sim, &tw, "/no/such/file.sav") != 0,
          "missing file fails cleanly");
    remove("/tmp/ct_test.sav");
}

static void test_schedules(void)
{
    printf("car schedules:\n");
    fresh();
    /* office at f5, shaft f0..f6 — the proven commute fixture */
    for (int f = 1; f <= 4; f++) place(ITEM_FLOOR, f, BX);
    uint16_t office = place(ITEM_OFFICE, 5, BX + 6);
    for (int f = 0; f <= 6; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);
    force_occupied(office);
    people_rebuild_transport(&sim.people, &tw);
    ElevatorShaft *s = &sim.people.shafts[0];
    CHECK(sim.people.shaft_count == 1, "one shaft");
    CHECK(s->sched_threshold[0][0] == 5 && s->sched_mode[1][6] == 0 &&
          s->sched_patience[0][3] == 0,
          "EXE defaults: threshold 5, mode 0, patience 0");

    /* threshold: idle car wins unless a working car is threshold-better.
     * car0 idle at home (lo); car1 at lo+1 already working upward. */
    people_set_num_cars(&sim.people, 0, 2);
    s->car[1].floor = (uint8_t)(s->lo + 1);
    s->car[1].target = (uint8_t)(s->lo + 5);
    s->car[1].dir = 1;
    s->car[1].assigned_calls = 1;       /* mark it busy */
    people_join_queue(&sim.people, 0, s->lo + 3, 1, 0);
    CHECK(s->up_call_car[s->lo + 3] == 1,
          "threshold 5: the idle car answers (working car only 1 closer)");
    s->up_call_car[s->lo + 3] = 0; s->car[0].assigned_calls = 0;
    s->stop[s->lo + 3].up_count = 0;
    s->sched_threshold[0][0] = 1;
    people_join_queue(&sim.people, 0, s->lo + 3, 1, 0);
    CHECK(s->up_call_car[s->lo + 3] == 2,
          "threshold 1: the approaching car answers");
    s->stop[s->lo + 3].up_count = 0;
    s->down_call_car[s->lo + 3] = 0;
    people_set_num_cars(&sim.people, 0, 1);

    /* shuttle mode: an idle car with no work runs to the shaft's far end
     * instead of its home floor */
    s->sched_mode[0][0] = 1;
    s->car[0].floor = s->car[0].target = s->lo;
    s->car[0].dir = 1;
    for (int i = 0; i < 400; i++)
        people_update(&sim.people, &tw, i, TOD_MORNING,
                      sim.reach_public, sim.reach_service);
    CHECK(s->car[0].floor == s->hi,
          "shuttle mode: idle car ran to the top of the shaft");
    s->sched_mode[0][0] = 0;

    /* patience: workers still arrive, but cars dwell (hold_timer engages) */
    s->sched_patience[0][0] = 2;
    sim.time_of_day = TOD_MORNING;
    int held = 0, arrived = 0;
    int f5 = floor_to_index(5);
    for (int frame = 0; frame < 4000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING,
                      sim.reach_public, sim.reach_service);
        if (s->car[0].hold_timer) held = 1;
        arrived = people_at(office, f5, PERSON_AT_DEST);
        if (arrived == 6) break;
    }
    CHECK(held, "patience 2: car dwells at floors (hold timer engaged)");
    CHECK(arrived == 6, "all 6 workers still arrive with patient cars");
}

int main(void)
{
    test_stairs();
    test_elevators();
    test_housekeeping();
    test_commute_elevator();
    test_walk_rules();
    test_queue_and_stress();
    test_elevator_dialog();
    test_patrons_and_staff();
    test_money();
    test_save_load();
    test_schedules();
    printf(fails ? "\n%d FAILURE(S)\n" : "\nall tests passed\n", fails);
    return fails ? 1 : 0;
}
