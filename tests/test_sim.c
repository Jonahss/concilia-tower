/* test_sim.c — checks for transport reachability, housekeeping, and the
 * people/elevator pipeline.
 * Build: gcc -o /tmp/test_sim tests/test_sim.c src/tower.c src/game.c \
 *            src/people.c src/twr.c -Isrc -lm
 * No SDL needed — pure simulation. */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "twr.h"

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

/* Metro pumps outside visitors up to commercial venues (UniPeple traffic
 * source); they enter/leave via the metro floor, not the street. */
static void test_metro_visitors(void)
{
    printf("metro visitors (UniPeple traffic source):\n");
    fresh();
    for (int f = 1; f <= 4; f++) place(ITEM_FLOOR, f, BX);
    uint16_t shop = place(ITEM_SHOP, 5, BX + 6);
    for (int f = 0; f <= 6; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);
    CHECK(shop != 0, "shop placed");
    force_occupied(shop);

    /* Construct the metro directly on f2 — placement is basement-restricted,
     * but the sim only needs floor/type/state to feed visitors. */
    Tenant *m = &tw.tenants[tw.tenant_count++];
    *m = (Tenant){0};
    m->id = 0xF00; m->type = ITEM_METRO; m->floor = 2; m->x = BX;
    m->width = 6; m->state = TENANT_OCCUPIED;
    int mf = floor_to_index(2);

    people_rebuild_transport(&sim.people, &tw);
    game_update_reachability(&sim, &tw);   /* metro/venue spawns gate on reach */

    int visitors = 0;
    for (int frame = 0; frame < 3000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_AFTERNOON,
                      sim.reach_public, sim.reach_service);
        visitors = 0;
        for (int i = 0; i < sim.people.people_high; i++) {
            Person *p = &sim.people.people[i];
            if (p->home_tenant == shop && p->entry_floor == mf) visitors++;
        }
        if (visitors > 0) break;
    }
    CHECK(visitors > 0, "metro pumps visitors to the shop (entry via metro floor)");

    /* night: the metro stops feeding the tower */
    memset(&sim.people, 0, sizeof(sim.people));
    people_rebuild_transport(&sim.people, &tw);
    int night = 0;
    for (int frame = 0; frame < 600; frame++)
        people_update(&sim.people, &tw, frame, TOD_NIGHT,
                      sim.reach_public, sim.reach_service);
    for (int i = 0; i < sim.people.people_high; i++) {
        Person *p = &sim.people.people[i];
        if (p->home_tenant == shop && p->entry_floor == mf) night++;
    }
    CHECK(night == 0, "no metro visitors at night");
}

/* Parking is an elevator bypass: a tower's own commuters drive in via the
 * parking level instead of all funnelling through the ground lobby. */
static void test_parking_bypass(void)
{
    printf("parking bypass (commuters drive in via the car park):\n");
    fresh();
    for (int f = 1; f <= 4; f++) place(ITEM_FLOOR, f, BX);
    uint16_t office = place(ITEM_OFFICE, 5, BX + 6);
    for (int f = 0; f <= 6; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);
    CHECK(office != 0, "office placed");
    force_occupied(office);

    /* parking on f2 (constructed directly — placement is basement-only) */
    Tenant *pk = &tw.tenants[tw.tenant_count++];
    *pk = (Tenant){0};
    pk->id = 0xF01; pk->type = ITEM_PARKING; pk->floor = 2; pk->x = BX;
    pk->width = 6; pk->state = TENANT_OCCUPIED;
    int pf = floor_to_index(2);

    people_rebuild_transport(&sim.people, &tw);
    game_update_reachability(&sim, &tw);

    int via_park = 0;
    for (int frame = 0; frame < 3000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING,
                      sim.reach_public, sim.reach_service);
        via_park = 0;
        for (int i = 0; i < sim.people.people_high; i++) {
            Person *p = &sim.people.people[i];
            if (p->home_tenant == office && p->entry_floor == pf) via_park++;
        }
        if (via_park > 0) break;
    }
    CHECK(via_park > 0, "some office commuters arrive via the parking level");
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

static void test_twr_import(void)
{
    printf("original .TDT import (real 1995 save):\n");
    const char *fx = "tests/fixtures/BARKLE4D.TDT";
    FILE *probe = fopen(fx, "rb");
    if (!probe) { printf("  (fixture %s missing — skipped)\n", fx); return; }
    fclose(probe);

    char err[256];
    int rc = twr_import(fx, &tw, &sim, err, sizeof(err));
    CHECK(rc == 0, "BARKLE4D.TDT parses end to end");
    if (rc != 0) { printf("  (%s)\n", err); return; }

    /* Ground truth read straight from the file bytes */
    CHECK(tw.star_rating == 4, "star rating 4 imported");
    CHECK(tw.money == 278104600L, "balance $278,104,600 (file value x100)");
    CHECK(tw.day == 2675, "day counter 2675");

    int stairs = 0, escalators = 0, lobbies = 0, offices = 0;
    for (int i = 0; i < tw.tenant_count; i++) {
        switch (tw.tenants[i].type) {
        case ITEM_STAIRS: stairs++; break;
        case ITEM_ESCALATOR: escalators++; break;
        case ITEM_LOBBY: lobbies++; break;
        case ITEM_OFFICE: offices++; break;
        default: break;
        }
    }
    CHECK(stairs == 30 && escalators == 34,
          "all 64 stair/escalator records imported (30 + 34)");
    CHECK(lobbies >= 2, "ground + sky lobbies present");
    CHECK(offices > 100, "office tower imported (>100 offices)");
    CHECK(sim.people.shaft_count == 24, "all 24 elevator groups imported");

    /* Settings made it onto the rebuilt shafts: this tower's 6th shaft
     * carries a tuned response/wait schedule (threshold 2, patience 1). */
    int tuned = 0;
    for (int i = 0; i < sim.people.shaft_count; i++) {
        ElevatorShaft *s = &sim.people.shafts[i];
        if (s->sched_threshold[0][0] == 2 && s->sched_patience[0][0] == 1)
            tuned = 1;
    }
    CHECK(tuned, "per-group schedule tables imported from the file");

    /* The imported tower must actually RUN */
    run_days(1);
    CHECK(tw.population > 0, "imported tower simulates (population > 0)");
}

/* Snapshot of everything .TDT export is supposed to carry */
typedef struct {
    int star, day, tenants, shafts, stairs, escalators;
    long money;
    int type_counts[ITEM_TYPE_COUNT];
    int rent_hist[4];
} TowerDigest;

static TowerDigest digest(const Tower *t, const GameSim *s)
{
    TowerDigest d = {0};
    d.star = t->star_rating;
    d.day = t->day;
    d.money = t->money;
    d.tenants = t->tenant_count;
    d.shafts = s->people.shaft_count;
    for (int i = 0; i < t->tenant_count; i++) {
        const Tenant *ten = &t->tenants[i];
        d.type_counts[ten->type]++;
        if (ten->type == ITEM_STAIRS) d.stairs++;
        if (ten->type == ITEM_ESCALATOR) d.escalators++;
        if (ten->rent_class <= 3 && ten->type != ITEM_LOBBY &&
            ten->type != ITEM_FLOOR)
            d.rent_hist[ten->rent_class]++;
    }
    return d;
}

static void test_twr_export(void)
{
    printf("original .TDT export (round-trip through the file format):\n");
    const char *fx = "tests/fixtures/SCHMITT.TDT";
    const char *tmp = "/tmp/ct_export.tdt";
    FILE *probe = fopen(fx, "rb");
    if (!probe) { printf("  (fixture %s missing — skipped)\n", fx); return; }
    fclose(probe);

    char err[256];
    if (twr_import(fx, &tw, &sim, err, sizeof(err)) != 0) {
        printf("  FAIL import of fixture (%s)\n", err); fails++; return;
    }
    TowerDigest before = digest(&tw, &sim);
    /* remember one shaft's tuning to verify it survives the file */
    int sx = sim.people.shafts[0].x;
    ItemType sty = sim.people.shafts[0].type;
    uint8_t scars = sim.people.shafts[0].num_cars;
    uint8_t sthr = sim.people.shafts[0].sched_threshold[1][3];
    uint8_t spat = sim.people.shafts[0].sched_patience[0][6];
    uint8_t shome = sim.people.shafts[0].home[5];

    int rc = twr_export(tmp, &tw, &sim, err, sizeof(err));
    CHECK(rc == 0, "SCHMITT exports without error");
    if (rc != 0) { printf("  (%s)\n", err); return; }

    rc = twr_import(tmp, &tw, &sim, err, sizeof(err));
    CHECK(rc == 0, "exported file re-imports cleanly");
    if (rc != 0) { printf("  (%s)\n", err); return; }

    TowerDigest after = digest(&tw, &sim);
    CHECK(after.star == before.star && after.day == before.day &&
          after.money == before.money, "star/day/money round-trip");
    CHECK(after.tenants == before.tenants,
          "tenant count round-trips exactly");
    int types_ok = 1;
    for (int i = 0; i < ITEM_TYPE_COUNT; i++)
        if (after.type_counts[i] != before.type_counts[i]) {
            printf("  (type %d: %d -> %d)\n", i,
                   before.type_counts[i], after.type_counts[i]);
            types_ok = 0;
        }
    CHECK(types_ok, "per-type tenant counts round-trip");
    CHECK(after.shafts == before.shafts, "all elevator groups round-trip");
    CHECK(after.stairs == before.stairs &&
          after.escalators == before.escalators,
          "stairs/escalators round-trip");
    CHECK(memcmp(after.rent_hist, before.rent_hist,
                 sizeof after.rent_hist) == 0,
          "rent classes round-trip");

    int found = 0;
    for (int i = 0; i < sim.people.shaft_count; i++) {
        ElevatorShaft *s = &sim.people.shafts[i];
        if (s->x != sx || s->type != sty) continue;
        found = s->num_cars == scars &&
                s->sched_threshold[1][3] == sthr &&
                s->sched_patience[0][6] == spat &&
                s->home[5] == shome;
        break;
    }
    CHECK(found, "car count, schedules and home floors survive the file");

    run_days(1);
    CHECK(tw.population > 0, "re-imported tower simulates");

    /* A tower born in the port (never imported) must also export */
    fresh();
    place(ITEM_OFFICE, 1, BX);
    place(ITEM_FAST_FOOD, 1, BX + 9);   /* needs support: lobby is below */
    place(ITEM_STAIRS, 0, BX);
    for (int f = 0; f <= 1; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);
    game_update_reachability(&sim, &tw);
    people_rebuild_transport(&sim.people, &tw);
    CHECK(twr_export(tmp, &tw, &sim, err, sizeof(err)) == 0,
          "fresh port tower exports");
    rc = twr_import(tmp, &tw, &sim, err, sizeof(err));
    CHECK(rc == 0, "fresh export re-imports");
    if (rc == 0) {
        TowerDigest d = digest(&tw, &sim);
        CHECK(d.type_counts[ITEM_OFFICE] == 1 &&
              d.type_counts[ITEM_FAST_FOOD] == 1 &&
              d.stairs == 1 && d.shafts == 1,
              "fresh tower contents survive the format");
    }
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

static void test_wedding(void)
{
    printf("TOWER wedding (5-star -> TOWER special event):\n");
    fresh();
    tw.star_rating = 5;
    tw.population = 15000;
    sim.promo.has_cathedral = 1;
    sim.promo.vip_visited = 1;

    game_wedding_daily(&sim, &tw);
    CHECK(sim.wedding.active == 1, "eligible tower holds its wedding");
    CHECK(tw.star_rating == 5, "still 5 stars during the ceremony");

    game_wedding_daily(&sim, &tw);
    CHECK(sim.wedding.active == 0 && sim.wedding.done == 1,
          "ceremony ends the next dawn");
    CHECK(tw.star_rating == 6, "tower crowned TOWER (star 6)");

    game_wedding_daily(&sim, &tw);
    CHECK(tw.star_rating == 6 && !sim.wedding.active,
          "wedding never repeats");

    /* requirements are real requirements */
    fresh();
    tw.star_rating = 5;
    tw.population = 15000;
    sim.promo.has_cathedral = 0;           /* no venue */
    sim.promo.vip_visited = 1;
    game_wedding_daily(&sim, &tw);
    CHECK(!sim.wedding.active, "no cathedral, no wedding");
    sim.promo.has_cathedral = 1;
    tw.population = 14999;
    game_wedding_daily(&sim, &tw);
    CHECK(!sim.wedding.active, "below 15,000 population, no wedding");
}

/* Retail customer competition (JudgeT zone model): same-type retail clustered
 * in a 15-floor zone split a fixed customer pool, so each earns less; a
 * well-spread tower is unaffected; fast food is immune. */
static void test_retail_competition(void)
{
    printf("retail zone competition (JudgeT seg_11a8):\n");
    fresh();

    Tenant rt = { 0 }; rt.type = ITEM_RESTAURANT; rt.floor = 5;  /* zone 0 */
    int z0 = floor_to_zone(5);

    sim.zones[z0].restaurant_count = ZONE_MAX_RESTAURANTS;   /* at the comfortable cap */
    CHECK(game_retail_income(&sim, &rt, 1000) == 1000,
          "restaurants at/under the zone cap earn full income");

    sim.zones[z0].restaurant_count = 2 * ZONE_MAX_RESTAURANTS; /* double the cap */
    CHECK(game_retail_income(&sim, &rt, 1000) == 500,
          "doubling restaurants in a zone halves each one's income");

    /* A lone restaurant in another zone is unaffected by the cluster. */
    Tenant lone = { 0 }; lone.type = ITEM_RESTAURANT; lone.floor = 20; /* zone 1 */
    sim.zones[floor_to_zone(20)].restaurant_count = 1;
    CHECK(game_retail_income(&sim, &lone, 1000) == 1000,
          "spreading restaurants across zones keeps full income");

    /* Fast food never competes (JudgeT: fast food never goes unsatisfied). */
    Tenant ff = { 0 }; ff.type = ITEM_FAST_FOOD; ff.floor = 5;
    sim.zones[z0].fastfood_count = 9;
    CHECK(game_retail_income(&sim, &ff, 1000) == 1000,
          "fast food is immune to zone competition");

    /* Shops compete too, on their own (higher) threshold. */
    Tenant sh = { 0 }; sh.type = ITEM_SHOP; sh.floor = 5;
    sim.zones[z0].shop_count = 2 * ZONE_MAX_SHOPS;
    CHECK(game_retail_income(&sim, &sh, 1000) == 1000 * ZONE_MAX_SHOPS / (2 * ZONE_MAX_SHOPS),
          "over-clustered shops earn a diluted share");
}

/* Tenant pairing: a content tenant eases a stressed same-type floor-mate. */
static void test_tenant_pairing(void)
{
    printf("tenant pairing (MainteT):\n");
    /* Build a controlled tower state directly — pairing only reads
     * type/floor/state/stress, so we skip placement geometry. */
    fresh();
    tw.tenant_count = 0;
    Tenant *ta = &tw.tenants[tw.tenant_count++];
    *ta = (Tenant){0};
    ta->type = ITEM_OFFICE; ta->floor = 1; ta->x = 179; ta->width = 6;
    ta->state = TENANT_OCCUPIED; ta->stress = 0;               /* content */
    Tenant *tb = &tw.tenants[tw.tenant_count++];
    *tb = (Tenant){0};
    tb->type = ITEM_OFFICE; tb->floor = 1; tb->x = 185; tb->width = 6;
    tb->state = TENANT_STRESSED; tb->stress = 90; tb->complaints = 2;
    game_tenant_pairing(&sim, &tw);
    CHECK(tb->state == TENANT_OCCUPIED && tb->stress <= 50 && tb->complaints == 0,
          "a content office stabilises a stressed same-floor office");

    /* A stressed office on a DIFFERENT floor from the content one is not rescued. */
    tb->state = TENANT_STRESSED; tb->stress = 90; tb->floor = 2;
    game_tenant_pairing(&sim, &tw);
    CHECK(tb->state == TENANT_STRESSED, "no content same-floor neighbour -> no rescue");
}

/* VIP visit gates star-4 / star-5 promotion (LevelUp 0xB92D). */
static void test_vip_gate(void)
{
    printf("VIP star gate (LevelUp 0xB92D):\n");
    fresh();
    sim.promo.has_recycling = 1;
    sim.promo.has_metro = 1;
    sim.promo.hotel_quarters = 4;
    sim.promo.vip_visited = 0;
    CHECK(!game_check_promotion(&sim, &tw, 4), "no star-4 without a satisfied VIP");
    sim.promo.vip_visited = 1;
    CHECK(game_check_promotion(&sim, &tw, 4), "star-4 allowed once the VIP is satisfied");
}

/* Persistent occupancy: cap_peak growth, gentrification, hotel upgrade. */
static void test_office_dynamics(void)
{
    printf("office dynamics (MainteT persistent occupancy):\n");

    /* init peaks by type/star */
    CHECK(game_init_cap_peak(ITEM_OFFICE, 1) == CAP_PEAK_LOW,
          "a new office starts at the bottom tier (0x20)");
    CHECK(game_init_cap_peak(ITEM_HOTEL_SINGLE, 1) == 0x10 &&
          game_init_cap_peak(ITEM_HOTEL_SUITE, 5) == 0x20,
          "hotel/suite peaks scale with star (TenantMake)");
    CHECK(game_init_cap_peak(ITEM_SHOP, 5) == 0,
          "retail is not peak-managed");

    /* income/occupancy scaling denominators (cap_base_peak) */
    CHECK(cap_base_peak(ITEM_OFFICE) == CAP_PEAK_LOW &&
          cap_base_peak(ITEM_HOTEL_SINGLE) == 0x10 &&
          cap_base_peak(ITEM_HOTEL_SUITE) == 0x18 &&
          cap_base_peak(ITEM_SHOP) == 0,
          "scaling baselines: office 0x20, hotel 0x10, suite 0x18, retail none");

    /* Growth: a content office climbs LOW -> MID -> HIGH over passes. */
    fresh();
    tw.star_rating = 5;
    tw.tenant_count = 0;
    Tenant *o = &tw.tenants[tw.tenant_count++];
    *o = (Tenant){0};
    o->type = ITEM_OFFICE; o->floor = 1; o->state = TENANT_OCCUPIED;
    o->stress = 0; o->cap_peak = CAP_PEAK_LOW;
    game_office_dynamics(&sim, &tw);
    CHECK(o->cap_peak == CAP_PEAK_MID, "content office grows LOW -> MID");
    game_office_dynamics(&sim, &tw);
    CHECK(o->cap_peak == CAP_PEAK_HIGH, "content office grows MID -> HIGH (5 star)");

    /* A stressed office does not grow. */
    Tenant *s = &tw.tenants[tw.tenant_count++];
    *s = (Tenant){0};
    s->type = ITEM_OFFICE; s->floor = 2; s->state = TENANT_OCCUPIED;
    s->stress = 90; s->cap_peak = CAP_PEAK_LOW;
    game_office_dynamics(&sim, &tw);
    CHECK(s->cap_peak == CAP_PEAK_LOW, "a stressed office does not grow");

    /* Gentrification: a top-tier office lifts a same-floor neighbour. */
    fresh();
    tw.star_rating = 5;
    tw.tenant_count = 0;
    Tenant *hi = &tw.tenants[tw.tenant_count++];
    *hi = (Tenant){0};
    hi->type = ITEM_OFFICE; hi->floor = 3; hi->state = TENANT_OCCUPIED;
    hi->stress = 0; hi->cap_peak = CAP_PEAK_HIGH;
    Tenant *lo = &tw.tenants[tw.tenant_count++];
    *lo = (Tenant){0};
    lo->type = ITEM_OFFICE; lo->floor = 3; lo->state = TENANT_OCCUPIED;
    lo->stress = 50; lo->cap_peak = CAP_PEAK_LOW;   /* stressed-ish, won't grow on its own */
    game_office_dynamics(&sim, &tw);
    CHECK(lo->cap_peak == CAP_PEAK_HIGH,
          "a thriving office gentrifies a same-floor neighbour");

    /* An office on a DIFFERENT floor is not gentrified. */
    fresh();
    tw.star_rating = 5;
    tw.tenant_count = 0;
    Tenant *h2 = &tw.tenants[tw.tenant_count++];
    *h2 = (Tenant){0};
    h2->type = ITEM_OFFICE; h2->floor = 4; h2->state = TENANT_OCCUPIED;
    h2->cap_peak = CAP_PEAK_HIGH;
    Tenant *far = &tw.tenants[tw.tenant_count++];
    *far = (Tenant){0};
    far->type = ITEM_OFFICE; far->floor = 9; far->state = TENANT_OCCUPIED;
    far->stress = 50; far->cap_peak = CAP_PEAK_LOW;
    game_office_dynamics(&sim, &tw);
    CHECK(far->cap_peak == CAP_PEAK_LOW, "no same-floor benefactor -> no gentrification");

    /* Hotel room upgrade: happy, clean room raises occupancy a step. */
    fresh();
    tw.tenant_count = 0;
    Tenant *ht = &tw.tenants[tw.tenant_count++];
    *ht = (Tenant){0};
    ht->type = ITEM_HOTEL_SINGLE; ht->floor = 5; ht->state = TENANT_OCCUPIED;
    ht->stress = 0; ht->dirty = 0; ht->cap_peak = 0x10;
    game_office_dynamics(&sim, &tw);
    CHECK(ht->cap_peak == 0x18, "a happy clean hotel room upgrades 0x10 -> 0x18");
    game_office_dynamics(&sim, &tw);
    CHECK(ht->cap_peak == 0x18, "hotel single caps at 0x18");

    /* A dirty room does not upgrade. */
    Tenant *dr = &tw.tenants[tw.tenant_count++];
    *dr = (Tenant){0};
    dr->type = ITEM_HOTEL_SINGLE; dr->floor = 6; dr->state = TENANT_OCCUPIED;
    dr->stress = 0; dr->dirty = 1; dr->cap_peak = 0x10;
    game_office_dynamics(&sim, &tw);
    CHECK(dr->cap_peak == 0x10, "a dirty hotel room does not upgrade");
}

int main(void)
{
    test_stairs();
    test_elevators();
    test_housekeeping();
    test_commute_elevator();
    test_metro_visitors();
    test_parking_bypass();
    test_walk_rules();
    test_queue_and_stress();
    test_elevator_dialog();
    test_patrons_and_staff();
    test_money();
    test_retail_competition();
    test_tenant_pairing();
    test_office_dynamics();
    test_vip_gate();
    test_twr_import();
    test_twr_export();
    test_wedding();
    test_save_load();
    test_schedules();
    printf(fails ? "\n%d FAILURE(S)\n" : "\nall tests passed\n", fails);
    return fails ? 1 : 0;
}
