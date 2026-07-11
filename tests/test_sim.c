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

/* Direct placement for disaster geometry — tower_import_item skips cost
 * and adjacency validation but fills the grid, which the fire needs. */
static uint16_t fplace(ItemType ty, int floor, int x)
{
    return tower_import_item(&tw, ty, floor, x, ITEM_WIDTH[ty]);
}

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

    /* Into-dirt: a stair whose UPPER floor is empty dirt is rejected (floors 1
     * and 2 are built; floor 5/6 are bare). The old below/above fallback used
     * to let this through. */
    CHECK(!tower_can_place(&tw, ITEM_STAIRS, 5, BX),
          "stair into an empty upper floor rejected (no into-dirt)");
    CHECK(tower_can_place(&tw, ITEM_STAIRS, 1, BX + 30),
          "stair between two built floors still allowed");

    /* Overlap: a second stair fully on top of an existing one (same two
     * floors, same columns) is rejected; sharing one landing (a chain) is ok. */
    CHECK(!tower_can_place(&tw, ITEM_STAIRS, 1, BX + 20),
          "stair fully overlapping another stair rejected");
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

static void test_unreachable_empty(void)
{
    printf("unreachable venue drains to empty:\n");
    fresh();
    tw.money = 100000000L;
    /* A disconnected stack: floors 1..5 above the lobby, no stairs/elevator. */
    for (int f = 1; f <= 5; f++) place(ITEM_FLOOR, f, BX);
    uint16_t r = place(ITEM_RESTAURANT, 6, BX);   /* sits on floor 5's support */
    Tenant *rt = tenant(r);
    rt->state = TENANT_OCCUPIED;
    rt->capacity = CAP_MAX;          /* pretend it filled up */
    game_update_reachability(&sim, &tw);
    CHECK(!sim.reach_public[floor_to_index(6)], "restaurant floor unreachable");
    run_days(1);
    CHECK(rt->capacity == CAP_EMPTY, "unreachable restaurant drains to empty (not 'packed')");
    CHECK(rt->population == 0, "unreachable restaurant has no patrons");
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

    /* Remove housekeeping: after the next checkout the room sticks dirty —
     * and after 3 daily passes spent dirty-and-unrented the roaches move
     * in (JudgeT HotelNeglectCheck: the neglect fuse trips at exactly 3) */
    tower_remove(&tw, hk);
    for (int i = 0; i < 480 * 2; i++) game_update(&sim, &tw);
    CHECK(room->condition != ROOM_CLEAN,
          "without housekeeping the room stays dirty");
    CHECK(!room->open_for_booking, "dirty room is closed for booking");
    run_days(4);
    CHECK(room->condition == ROOM_INFESTED,
          "3 days of neglect -> cockroach infestation");
}

static void test_hotel_infestation(void)
{
    printf("hotel infestation (spread & cure):\n");
    fresh();
    /* Three singles in a row (they abut), then a gap, then a fourth */
    uint16_t r1 = place(ITEM_HOTEL_SINGLE, 1, BX);        /* 4 cells wide */
    uint16_t r2 = place(ITEM_HOTEL_SINGLE, 1, BX + 4);
    uint16_t r3 = place(ITEM_HOTEL_SINGLE, 1, BX + 8);
    uint16_t r4 = place(ITEM_HOTEL_SINGLE, 1, BX + 14);   /* gap at +12/+13 */
    place(ITEM_STAIRS, 0, BX + 20);
    Tenant *a = tenant(r1), *b = tenant(r2), *c = tenant(r3), *d = tenant(r4);
    if (!a || !b || !c || !d) { printf("  FAIL placement\n"); fails++; return; }
    /* Hotel construction (56 ticks, decremented every 4th tick) outlasts
     * day 0's 5PM pass, so the first pass that sees the finished rooms is
     * day 1's — run two days. */
    run_days(2);

    CHECK(a->open_for_booking && d->open_for_booking,
          "fresh clean rooms are armed by the 5PM pass");

    /* Plant roaches in the middle room and run one pass directly */
    b->condition = ROOM_INFESTED;
    b->open_for_booking = 0;
    game_hotel_demand_pass(&sim, &tw);
    CHECK(a->condition == ROOM_INFESTED && c->condition == ROOM_INFESTED,
          "roaches spread to both abutting rooms in one pass");
    CHECK(d->condition == ROOM_CLEAN,
          "roaches do not jump the gap to a detached room");
    CHECK(!a->open_for_booking && !c->open_for_booking,
          "spread victims are closed for booking");

    /* Maids never fix infestation */
    place(ITEM_HOUSEKEEPING, 1, BX + 24);
    run_days(3);
    CHECK(b->condition == ROOM_INFESTED,
          "housekeeping never cleans an infested room");
    CHECK(!b->open_for_booking && b->population == 0,
          "infested room takes no guests, ever");

    /* Demolition is the only cure */
    tower_remove(&tw, r2);
    CHECK(tenant(r2) == NULL, "demolition removes the infested room");
}

static void test_hotel_demand(void)
{
    printf("hotel demand (booking flag):\n");
    fresh();
    uint16_t r1 = place(ITEM_HOTEL_SINGLE, 1, BX);
    uint16_t r2 = place(ITEM_HOTEL_SINGLE, 1, BX + 4);
    place(ITEM_STAIRS, 0, BX + 12);
    Tenant *a = tenant(r1), *b = tenant(r2);
    if (!a || !b) { printf("  FAIL placement\n"); fails++; return; }
    run_days(1);

    /* Stressed guests (avg >= 150 at 1 star) close the room at the pass —
     * unless a happy same-floor room vouches for it (the pairing rescue) */
    a->condition = ROOM_CLEAN;  b->condition = ROOM_CLEAN;
    a->guest_stress_total = 600; a->guest_stress_trips = 2;  /* avg 300 */
    b->guest_stress_total = 0;   b->guest_stress_trips = 4;  /* avg 0 */
    b->demand_category = 0;      /* stale: the pass must recompute it */
    game_hotel_demand_pass(&sim, &tw);
    CHECK(b->open_for_booking, "happy room re-arms at the pass");
    CHECK(a->open_for_booking,
          "stressed room is rescued by a happy same-floor pairing");
    CHECK(a->demand_category == 1 && b->demand_category == 1,
          "pairing settles both rooms at category 1");

    /* Without a happy floor-mate, the stressed room is disarmed */
    a->guest_stress_total = 600; a->guest_stress_trips = 2;
    b->guest_stress_total = 400; b->guest_stress_trips = 2;  /* avg 200: bad */
    game_hotel_demand_pass(&sim, &tw);
    CHECK(!a->open_for_booking && !b->open_for_booking,
          "stressed rooms with no happy pair are closed for booking");

    /* Very-low room rate always fills (the EXE zeroes its demand score) */
    a->guest_stress_total = 600; a->guest_stress_trips = 2;
    a->rent_class = 3;
    game_hotel_demand_pass(&sim, &tw);
    CHECK(a->open_for_booking, "very-low room rate always books");

    /* Noisy neighbor (NoiseT seg_1138 + JudgeT 1130:0686, byte-verified
     * 2026-07-10): a commercial unit within 20 cells adds +60 to the
     * metric. avg 100 is fine quiet (bar 150 at 1 star) but 160 noisy. */
    a->rent_class = 1;
    a->guest_stress_total = 400; a->guest_stress_trips = 4;   /* avg 100 */
    b->guest_stress_total = 400; b->guest_stress_trips = 4;   /* no rescuer */
    game_hotel_demand_pass(&sim, &tw);
    CHECK(a->open_for_booking, "avg-100 room books fine in the quiet");
    uint16_t shop = fplace(ITEM_SHOP, 1, BX + 22);            /* gap 18 <= 20 */
    a->guest_stress_total = 400; a->guest_stress_trips = 4;   /* arming reset them */
    b->guest_stress_total = 400; b->guest_stress_trips = 4;
    game_hotel_demand_pass(&sim, &tw);
    CHECK(!a->open_for_booking,
          "the same room next to a shop is disarmed (+60 noise penalty)");
    /* happy guests shrug the noise off: 0 + 60 = 60 < 80 = content */
    a->guest_stress_total = 0; a->guest_stress_trips = 4;
    b->guest_stress_total = 0; b->guest_stress_trips = 4;
    game_hotel_demand_pass(&sim, &tw);
    CHECK(a->open_for_booking && a->demand_category == 2,
          "noise alone never closes a room with happy guests");
    /* out of earshot: gap > 20 cells is quiet (for room a) */
    tenant(shop)->x = BX + 30;   /* edge gap from a: 209-183 = 26 */
    a->guest_stress_total = 400; a->guest_stress_trips = 4;   /* avg 100 */
    b->guest_stress_total = 0;   b->guest_stress_trips = 4;   /* rescuer */
    game_hotel_demand_pass(&sim, &tw);
    CHECK(a->open_for_booking, "a shop farther than 20 cells is out of earshot");
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
    tenant(room)->condition = ROOM_DIRTY;
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
    tw.population = 15000; sim.standing_population = 15000;
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
    tw.population = 15000; sim.standing_population = 15000;
    sim.promo.has_cathedral = 0;           /* no venue */
    sim.promo.vip_visited = 1;
    game_wedding_daily(&sim, &tw);
    CHECK(!sim.wedding.active, "no cathedral, no wedding");
    sim.promo.has_cathedral = 1;
    tw.population = 14999; sim.standing_population = 14999;
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

/* Star requirements per LevelUp 1148:007e (byte-verified 2026-07-09):
 * 3->4 = suite + recycling adequate + medical + VIP verdict;
 * 4->5 = metro + recycling adequate + medical, NO VIP re-check. */
static void test_star_requirements(void)
{
    printf("star requirements (LevelUp 1148:007e):\n");
    fresh();
    /* 3->4 and 4->5 only evaluate on weekday evenings (time_period >= 4,
     * not weekend) — open the window so the requirement checks can pass. */
    sim.hour = 17;
    sim.quarter = QUARTER_WEEKDAY3;
    sim.promo.has_suite = 1;
    sim.promo.recycling_adequate = 1;
    sim.promo.medical_adequate = 1;
    sim.promo.vip_visited = 0;
    CHECK(!game_check_promotion(&sim, &tw, 4), "no star-4 without a satisfied VIP");
    sim.promo.vip_visited = 1;
    CHECK(game_check_promotion(&sim, &tw, 4), "star-4 allowed once the VIP is satisfied");
    sim.promo.has_suite = 0;
    CHECK(!game_check_promotion(&sim, &tw, 4), "no star-4 without a suite (0xB92B)");
    sim.promo.has_suite = 1;
    sim.promo.has_metro = 1;
    CHECK(game_check_promotion(&sim, &tw, 4) && game_check_promotion(&sim, &tw, 5),
          "metro doesn't gate 3->4; 4->5 passes with metro+recycling+medical");
    sim.promo.vip_visited = 0;
    CHECK(game_check_promotion(&sim, &tw, 5),
          "4->5 has NO VIP re-check (binary reads neither 0xB923 nor 0xB92B)");
    sim.promo.has_metro = 0;
    CHECK(!game_check_promotion(&sim, &tw, 5), "no star-5 without a metro station");
    sim.promo.has_metro = 1;
    sim.promo.recycling_adequate = 0;
    CHECK(!game_check_promotion(&sim, &tw, 5) && !game_check_promotion(&sim, &tw, 4),
          "inadequate recycling blocks both 3->4 and 4->5 (dynamic flag)");
    sim.promo.recycling_adequate = 1;
    sim.promo.vip_visited = 1;

    /* The weekday-evening window (LevelUp @00de/@00e5, @011e/@0125). */
    sim.hour = 12;
    CHECK(!game_check_promotion(&sim, &tw, 4) && !game_check_promotion(&sim, &tw, 5),
          "midday: 3->4 and 4->5 wait for the evening");
    sim.hour = 2;
    CHECK(game_check_promotion(&sim, &tw, 4),
          "the window runs through the night (time_period >= 4 wraps to 7AM)");
    sim.hour = 17;
    sim.quarter = QUARTER_WEEKEND;
    CHECK(!game_check_promotion(&sim, &tw, 4) && !game_check_promotion(&sim, &tw, 5),
          "no big promotions on the weekend");
    sim.promo.has_security = 1;
    CHECK(game_check_promotion(&sim, &tw, 3),
          "2->3 has no clock gate (binary checks only security)");
    sim.quarter = QUARTER_WEEKDAY3;
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
    ht->stress = 0; ht->condition = ROOM_CLEAN; ht->cap_peak = 0x10;
    game_office_dynamics(&sim, &tw);
    CHECK(ht->cap_peak == 0x18, "a happy clean hotel room upgrades 0x10 -> 0x18");
    game_office_dynamics(&sim, &tw);
    CHECK(ht->cap_peak == 0x18, "hotel single caps at 0x18");

    /* A dirty room does not upgrade. */
    Tenant *dr = &tw.tenants[tw.tenant_count++];
    *dr = (Tenant){0};
    dr->type = ITEM_HOTEL_SINGLE; dr->floor = 6; dr->state = TENANT_OCCUPIED;
    dr->stress = 0; dr->condition = ROOM_DIRTY; dr->cap_peak = 0x10;
    game_office_dynamics(&sim, &tw);
    CHECK(dr->cap_peak == 0x10, "a dirty hotel room does not upgrade");
}

/* Scheduled disasters (TimeT 10AM dispatch, byte-verified 2026-07-09/10):
 * a fire every 84th day (star>2 + security + NO cathedral), a bomb offer
 * every 60th (stars 2/3/4 only; ransom $200k/$300k/$1M). */
static void test_disaster_schedule(void)
{
    printf("scheduled disasters (fire day%%84==83, bomb day%%60==59):\n");

    /* Every floor in the pick range [above lobby .. top] valid, so the
     * uniform floor pick can't whiff (the EXE never retries a bad pick). */
    fresh();
    tw.star_rating = 3;
    fplace(ITEM_SECURITY, 2, 140);
    for (int f = 2; f <= 5; f++)
        for (int i = 0; i < 4; i++)
            fplace(ITEM_OFFICE, f, 100 + i * 9);  /* extent [100,136) */

    tw.day = 82;
    game_schedule_disasters(&sim, &tw);
    CHECK(sim.event.type == EVENT_NONE, "day 82: nothing scheduled");

    tw.day = 83;
    game_schedule_disasters(&sim, &tw);
    CHECK(sim.event.type == EVENT_FIRE && sim.event.active,
          "day 83 (the 84th): a fire starts");
    CHECK(sim.event.pending && sim.event.ransom_cost == FIRE_CHOPPER_COST,
          "the $500,000 helicopter offer is pending");
    {
        int fi = floor_to_index(sim.event.target_floor);
        int16_t L[TOWER_FLOOR_COUNT], R[TOWER_FLOOR_COUNT];
        tower_floor_extents(&tw, L, R);
        CHECK(sim.event.target_slot == R[fi] - 32,
              "ignition 32 cells in from the right extent");
        CHECK(sim.event.fire_left[fi] == sim.event.target_slot &&
              sim.event.fire_right[fi] == sim.event.target_slot,
              "both fronts start at the ignition cell");
    }

    /* Building the cathedral retires fires for good. */
    fresh();
    tw.star_rating = 3;
    fplace(ITEM_SECURITY, 2, 140);
    for (int i = 0; i < 4; i++)
        fplace(ITEM_OFFICE, 5, 100 + i * 9);
    fplace(ITEM_CATHEDRAL, 90, 100);
    tw.day = 83;
    game_schedule_disasters(&sim, &tw);
    CHECK(sim.event.type == EVENT_NONE, "a cathedral retires fires");

    /* Star 2: no fires yet — but bomb threats exist, at $200k. */
    fresh();
    tw.star_rating = 2;
    fplace(ITEM_SECURITY, 2, 140);
    for (int f = 2; f <= 5; f++)
        for (int i = 0; i < 4; i++)
            fplace(ITEM_OFFICE, f, 100 + i * 9);
    tw.day = 83;
    game_schedule_disasters(&sim, &tw);
    CHECK(sim.event.type == EVENT_NONE, "star 2: no fires");
    tw.day = 59;
    game_schedule_disasters(&sim, &tw);
    CHECK(sim.event.type == EVENT_BOMB && sim.event.pending && !sim.event.active,
          "day 59 (the 60th): bomb offer pending, not armed");
    CHECK(sim.event.ransom_cost == 200000, "star 2 ransom = $200k");
    {
        int fi = floor_to_index(sim.event.target_floor);
        int16_t L[TOWER_FLOOR_COUNT], R[TOWER_FLOOR_COUNT];
        tower_floor_extents(&tw, L, R);
        CHECK(sim.event.target_slot >= L[fi] && sim.event.target_slot <= R[fi] - 4,
              "bomb cell within [left extent, right extent - 4]");
    }

    /* Star 5: five-star towers never get bomb threats. */
    fresh();
    tw.star_rating = 5;
    fplace(ITEM_SECURITY, 2, 140);
    for (int i = 0; i < 4; i++)
        fplace(ITEM_OFFICE, 5, 100 + i * 9);
    tw.day = 59;
    game_schedule_disasters(&sim, &tw);
    CHECK(sim.event.type == EVENT_NONE, "star 5: no bomb threats");

    /* No security office: no disasters at all. */
    fresh();
    tw.star_rating = 3;
    for (int i = 0; i < 4; i++)
        fplace(ITEM_OFFICE, 5, 100 + i * 9);
    tw.day = 83;
    game_schedule_disasters(&sim, &tw);
    CHECK(sim.event.type == EVENT_NONE, "no security = no disasters");
}

/* Fire mechanics (FireT 0304/0450/0856): fronts destroy and advance every
 * 7th frame, floors above ignite +80 frames, never downward; fronts die at
 * the extent edges; the paid chopper sweeps right-to-left dousing. */
static void test_fire_spread(void)
{
    printf("fire spread (fronts, up-only, edges) + the $500k chopper:\n");

    /* Geometry: floors 4/5/6 each hold offices at 100/109/118/127
     * (extent [100,136)). Fire forced on floor 5 -> ignition cell 104. */
    fresh();
    tw.star_rating = 3;
    fplace(ITEM_SECURITY, 2, 60);
    uint16_t off4  = fplace(ITEM_OFFICE, 4, 100);
    for (int i = 1; i < 4; i++) fplace(ITEM_OFFICE, 4, 100 + i * 9);
    uint16_t off5a = fplace(ITEM_OFFICE, 5, 100);
    for (int i = 1; i < 4; i++) fplace(ITEM_OFFICE, 5, 100 + i * 9);
    for (int i = 0; i < 4; i++) fplace(ITEM_OFFICE, 6, 100 + i * 9);

    game_start_fire(&sim, &tw, 5);
    CHECK(sim.event.type == EVENT_FIRE && sim.event.active, "forced fire starts");
    int fi4 = floor_to_index(4), fi5 = floor_to_index(5), fi6 = floor_to_index(6);
    CHECK(sim.event.target_slot == 104, "floor 5 extent [100,136): ignition at 104");

    game_event_proceed(&sim, &tw);        /* let it burn */
    sim.hour = 11;                        /* pre-1PM pace: 320 frames/hour */

    game_update_event(&sim, &tw);         /* one tick ~ 2.7 frames (320/hr, 120 ticks/hr) */
    CHECK(tenant(off5a) != NULL, "sanity: origin-floor office exists");
    CHECK(sim.event.fire_left[fi6] < 0 && sim.event.fire_right[fi6] < 0,
          "floor above not yet ignited");

    for (int i = 0; i < 35; i++) game_update_event(&sim, &tw);   /* past frame 80 */
    CHECK(sim.event.fire_left[fi6] >= 0 || sim.event.fire_right[fi6] >= 0,
          "floor above ignites on the 80-frame schedule");
    CHECK(sim.event.fire_left[fi4] < 0 && sim.event.fire_right[fi4] < 0,
          "fire never spreads downward");

    for (int i = 0; i < 2000 && sim.event.active; i++)
        game_update_event(&sim, &tw);
    CHECK(!sim.event.active, "fire burns out at the floor edges");
    CHECK(tenant(off5a)->state == TENANT_ABANDONED && tenant(off5a)->burned,
          "burned offices leave rubble");
    CHECK(tenant(off4)->state != TENANT_ABANDONED, "floor below survives untouched");

    /* Same layout, but pay for helicopters this time. */
    fresh();
    tw.star_rating = 3;
    fplace(ITEM_SECURITY, 2, 60);
    for (int i = 0; i < 3; i++) fplace(ITEM_OFFICE, 5, 100 + i * 9);
    uint16_t off5d = fplace(ITEM_OFFICE, 5, 127);

    game_start_fire(&sim, &tw, 5);
    long money0 = tw.money;
    game_event_ransom(&sim, &tw);         /* hire the choppers */
    CHECK(tw.money == money0 - FIRE_CHOPPER_COST, "helicopters cost $500,000");
    CHECK(!sim.event.pending && sim.event.active, "fire still burns while they fly");
    CHECK(sim.event.chopper_x == 136 - 12, "chopper starts at right extent - 12");

    sim.hour = 11;
    for (int i = 0; i < 500 && sim.event.active; i++)
        game_update_event(&sim, &tw);
    CHECK(!sim.event.active && sim.event.chopper_x == 0,
          "chopper sweep + edge burn-out end the fire");
    CHECK(tenant(off5d)->state != TENANT_ABANDONED,
          "the doused right front never reached the rightmost office");
}

/* Bomb resolution (ResolveEvent(0) + DestroyTenants 10c8:02bd): blast box =
 * floors [t-2 .. t+3] x cells [t-20 .. t+20], any overlap destroys; the EXE
 * charges no cash for the damage. */
static void test_bomb_blast(void)
{
    printf("bomb blast box + pay/deploy decisions:\n");

    /* Pay path: money down, recorded, no blast. */
    fresh();
    long money0 = tw.money;
    long exp0 = sim.expenses_this_quarter;
    sim.event = (EventState){0};
    sim.event.type = EVENT_BOMB;
    sim.event.pending = 1;
    sim.event.target_floor = 10;
    sim.event.ransom_cost = 300000;
    game_event_ransom(&sim, &tw);
    CHECK(tw.money == money0 - 300000, "paying the ransom deducts the fee");
    CHECK(sim.expenses_this_quarter == exp0 + 300000, "ransom recorded as an expense");
    CHECK(!sim.event.pending && !sim.event.active, "paying ends the threat");
    CHECK(sim.event.caught == 1, "paid = no detonation");

    /* Deploy path: refusing is what arms the bomb. */
    fresh();
    sim.event = (EventState){0};
    sim.event.type = EVENT_BOMB;
    sim.event.pending = 1;
    money0 = tw.money;
    game_event_proceed(&sim, &tw);
    CHECK(sim.event.active && !sim.event.pending, "refusing arms the bomb");
    CHECK(tw.money == money0, "deploying security is free");

    /* Blast geometry, resolved directly (the guard race is stochastic). */
    fresh();
    uint16_t in_up    = fplace(ITEM_OFFICE, 13, 110); /* t+3: in */
    uint16_t out_up   = fplace(ITEM_OFFICE, 14, 110); /* t+4: out */
    uint16_t in_down  = fplace(ITEM_OFFICE,  8, 110); /* t-2: in */
    uint16_t out_down = fplace(ITEM_OFFICE,  7, 110); /* t-3: out */
    uint16_t in_edge  = fplace(ITEM_OFFICE, 10, 122); /* overlaps cell 130 */
    uint16_t out_side = fplace(ITEM_OFFICE, 10, 131); /* starts at 131: out */
    sim.event = (EventState){0};
    sim.event.type = EVENT_BOMB;
    sim.event.active = 1;
    sim.event.target_floor = 10;
    sim.event.target_slot = 110;          /* blast cells [90 .. 130] */
    money0 = tw.money;
    game_resolve_event(&sim, &tw);
    CHECK(tenant(in_up)->state == TENANT_ABANDONED &&
          tenant(in_down)->state == TENANT_ABANDONED,
          "floors t-2 .. t+3 are inside the blast");
    CHECK(tenant(out_up)->state != TENANT_ABANDONED &&
          tenant(out_down)->state != TENANT_ABANDONED,
          "floors beyond the box survive");
    CHECK(tenant(in_edge)->state == TENANT_ABANDONED && tenant(in_edge)->burned,
          "any overlap with the cell range destroys (and leaves rubble)");
    CHECK(tenant(out_side)->state != TENANT_ABANDONED,
          "a tenant fully right of cell t+20 survives");
    CHECK(tw.money == money0, "detonation charges no cash - the loss is the buildings");
    CHECK(!sim.event.active && sim.event.type == EVENT_NONE, "blast ends the event");
}

/* The bomb hunt (GuardT seg_10f8, byte-verified 2026-07-11): fully
 * deterministic — guards sweep right-to-left, expanding floor by floor
 * from their office; stepping onto the exact bomb cell = caught. */
static void test_guard_hunt(void)
{
    printf("bomb hunt (deterministic guard sweep):\n");
    fresh();
    tw.star_rating = 3;
    fplace(ITEM_SECURITY, 5, 60);
    for (int f = 4; f <= 6; f++)
        for (int i = 0; i < 4; i++)
            fplace(ITEM_OFFICE, f, 100 + i * 9);   /* extents [100,136) */

    game_offer_bomb(&sim, &tw, 5);
    CHECK(sim.event.pending, "bomb offer opens");
    sim.event.target_slot = 110;                   /* pin the target */
    game_event_proceed(&sim, &tw);
    CHECK(sim.event.hunt.active && sim.event.hunt.noffices == 1,
          "refusing the ransom deploys the office's guards");
    {
        GuardOffice *o = &sim.event.hunt.o[0];
        CHECK(o->g[0].floor == 5 && o->g[3].floor == 4,
              "guards 0-2 take the office floor, 3-5 the floor below");
        CHECK(o->g[0].x == 134, "guards materialize at right extent - 2");
    }
    sim.hour = 10;
    int caught_at = -1;
    for (int t = 0; t < 100 && caught_at < 0; t++) {
        game_update_event(&sim, &tw);
        if (sim.event.caught) caught_at = t;
    }
    CHECK(caught_at >= 0, "sweep reaches the bomb cell - caught, no dice");
    CHECK(!sim.event.active && !sim.event.hunt.active, "everyone stands down");

    /* Same setup, but the guards are demolished away before the refusal:
     * nobody hunts, and 1:00 PM detonates the bomb. */
    fresh();
    tw.star_rating = 3;
    uint16_t sec = fplace(ITEM_SECURITY, 5, 60);
    for (int i = 0; i < 4; i++) fplace(ITEM_OFFICE, 5, 100 + i * 9);
    uint16_t vic = fplace(ITEM_OFFICE, 5, 100);   /* dup id guard */
    (void)vic;
    game_offer_bomb(&sim, &tw, 5);
    sim.event.target_slot = 110;
    tenant(sec)->state = TENANT_ABANDONED;         /* office gone */
    game_event_proceed(&sim, &tw);
    CHECK(sim.event.active && sim.event.hunt.noffices == 0,
          "no security office = nobody to hunt");
    sim.hour = 10;
    for (int t = 0; t < 50; t++) game_update_event(&sim, &tw);
    CHECK(sim.event.active, "bomb still live before 1PM");
    sim.hour = 13;
    game_update_event(&sim, &tw);
    CHECK(!sim.event.active && !sim.event.caught,
          "1:00 PM sharp: detonation (checked before guard movement)");
}

/* Medical adequacy (MedicalT seg_1170, byte-verified 2026-07-10 referee):
 * cleared ONLY when a sick worker finds no center (own 15-floor band +
 * band-0 fallback both empty); a full center (40/day) turns patients away
 * silently; re-armed every 7AM at star>=3. */
static void test_medical_adequacy(void)
{
    printf("medical adequacy (band seek, 40/day cap, no-center clear):\n");
    fresh();
    sim.medical_adequate = 1;

    CHECK(game_medical_seek(&sim, &tw, 20) == 0, "no center anywhere -> not found");
    CHECK(!sim.medical_adequate && sim.medical_nag,
          "no center clears adequacy (0xB92D) and nags");

    sim.medical_adequate = 1; sim.medical_nag = 0;
    uint16_t med1 = fplace(ITEM_MEDICAL, 18, 100);   /* band 1 = floors 15-29 */
    CHECK(game_medical_seek(&sim, &tw, 20) == 2, "same-band center admits the patient");
    CHECK(tenant(med1)->patients_today == 1, "admission counts against the daily cap");
    CHECK(sim.medical_adequate, "a found center keeps adequacy");

    CHECK(game_medical_seek(&sim, &tw, 50) == 0,
          "band-3 worker, band-1 center, no band-0 fallback -> not found");
    CHECK(!sim.medical_adequate, "adequacy cleared again");

    sim.medical_adequate = 1;
    uint16_t med0 = fplace(ITEM_MEDICAL, 3, 100);    /* band 0 */
    CHECK(game_medical_seek(&sim, &tw, 50) == 2, "band-0 center is the universal fallback");

    tenant(med1)->patients_today = 40;
    tenant(med0)->patients_today = 40;
    CHECK(game_medical_seek(&sim, &tw, 20) == 1, "a full center turns patients away");
    CHECK(sim.medical_adequate, "overflow NEVER clears adequacy (no recycling-style bar)");
}

/* The hourly star evaluation (game_update wiring): promotions land on the
 * hour, using the STANDING population (workers count while employed, not
 * while present) so the count survives the evening window. */
static void test_promotion_cadence(void)
{
    printf("hourly star evaluation via game_update:\n");
    fresh();
    TUNING.star_pop[0] = 10;                 /* shrink the star-2 threshold */
    tower_import_item(&tw, ITEM_FLOOR, -1, 100, 30);  /* width gate: > 25 */
    fplace(ITEM_STAIRS, 0, 150);             /* entrance floor -> floor 1 */
    for (int i = 0; i < 3; i++) {
        uint16_t id = fplace(ITEM_OFFICE, 1, 100 + i * 9);
        Tenant *t = tenant(id);
        t->state = TENANT_OCCUPIED;
        t->cap_peak = CAP_PEAK_LOW;
    }
    tw.star_rating = 1;
    int promoted_hour = -1;
    for (int i = 0; i < (720 * 4 * 2)  /* two normal-speed days */ && tw.star_rating < 2; i++) {
        game_update(&sim, &tw);
        if (tw.star_rating == 2) promoted_hour = sim.hour;
    }
    CHECK(tw.star_rating == 2, "tower promotes via the hourly evaluation");
    CHECK(promoted_hour >= 0, "promotion observed during the update loop");
    CHECK(sim.standing_population >= 10,
          "standing population counts office workers around the clock");
    tuning_reset();
}

/* Flavor mechanics: grand-lobby height (drives the WaitT wait-forgiveness
 * bonus) and medical emergencies (only with a medical center, no penalty). */
static void test_flavor(void)
{
    printf("flavor: lobby height + medical gating:\n");

    fresh();
    CHECK(game_lobby_height(&tw) == 1, "default ground lobby -> height 1");
    for (int f = 0; f <= 1; f++) {
        Tenant *l = &tw.tenants[tw.tenant_count++];
        *l = (Tenant){0};
        l->type = ITEM_LOBBY; l->floor = f; l->state = TENANT_OCCUPIED;
    }
    CHECK(game_lobby_height(&tw) == 2, "lobby on floors 0-1 -> height 2");
    Tenant *l2 = &tw.tenants[tw.tenant_count++];
    *l2 = (Tenant){0}; l2->type = ITEM_LOBBY; l2->floor = 2; l2->state = TENANT_OCCUPIED;
    CHECK(game_lobby_height(&tw) == 3, "lobby on floors 0-2 -> height 3");

    /* a gap breaks the stack */
    fresh();
    Tenant *g0 = &tw.tenants[tw.tenant_count++];
    *g0 = (Tenant){0}; g0->type = ITEM_LOBBY; g0->floor = 0; g0->state = TENANT_OCCUPIED;
    Tenant *g2 = &tw.tenants[tw.tenant_count++];
    *g2 = (Tenant){0}; g2->type = ITEM_LOBBY; g2->floor = 2; g2->state = TENANT_OCCUPIED;
    CHECK(game_lobby_height(&tw) == 1, "lobby floors 0 and 2 (gap) -> height 1");

    /* medical never fires without a medical center */
    fresh();
    sim.promo.has_medical = 0;
    Tenant *o = &tw.tenants[tw.tenant_count++];
    *o = (Tenant){0}; o->type = ITEM_OFFICE; o->floor = 6; o->state = TENANT_OCCUPIED;
    int fired = 0;
    for (int i = 0; i < 5000 && !fired; i++) {
        game_try_medical(&sim, &tw);
        if (sim.medical.active) fired = 1;
    }
    CHECK(!fired, "no medical center -> no medical emergency");

    /* with a medical center it eventually fires, on an occupied floor */
    fresh();
    sim.promo.has_medical = 1;   /* flavor events gate on existence */
    Tenant *o2 = &tw.tenants[tw.tenant_count++];
    *o2 = (Tenant){0}; o2->type = ITEM_OFFICE; o2->floor = 6; o2->state = TENANT_OCCUPIED;
    int got = 0;
    for (int i = 0; i < 20000 && !got; i++) {
        game_try_medical(&sim, &tw);
        if (sim.medical.active) got = 1;
    }
    CHECK(got, "medical center -> emergency eventually fires");
    CHECK(sim.medical.floor == 6 && sim.medical.notice,
          "emergency lands on the occupied floor and raises a notice");

    sim.medical.timer = 1;
    game_update_medical(&sim);
    CHECK(!sim.medical.active, "medical emergency clears when its timer runs out");
}

/* Auto-fill: gaps between tenants on a row become plain floor (no
 * swiss-cheese); cells beyond the outermost tenants stay open. */
static void test_floor_fill(void)
{
    printf("auto-fill floor between tenants:\n");
    fresh();
    tw.money = 100000000L;
    place(ITEM_FLOOR, 0, 100);     /* 100..162 support, clear of the seeded lobby */
    place(ITEM_OFFICE, 1, 105);    /* 105..113 */
    place(ITEM_OFFICE, 1, 125);    /* 125..133, gap 114..124 */
    int fidx = floor_to_index(1);
    int gap_filled = 1;
    for (int cx = 114; cx <= 124; cx++)
        if (tw.grid[fidx][cx].type != ITEM_FLOOR) gap_filled = 0;
    CHECK(gap_filled, "gap between two offices fills with floor");
    CHECK(tw.grid[fidx][104].type == ITEM_NONE, "cells left of the tenants stay open");
    CHECK(tw.grid[fidx][140].type == ITEM_NONE, "cells right of the tenants stay open");
}

static void test_bulldozer(void)
{
    printf("bulldozer keeps the build floor, spares the lobby:\n");
    fresh();
    tw.money = 100000000L;
    place(ITEM_FLOOR, 0, 100);          /* ground support 100..162 */
    uint16_t off = place(ITEM_OFFICE, 1, 105);   /* 105..113 */
    int fidx = floor_to_index(1);

    /* Bulldozing the office leaves the build floor behind, not bare dirt. */
    CHECK(tower_remove(&tw, off) == 1, "office bulldozed");
    int left_floor = 1;
    for (int cx = 105; cx <= 113; cx++)
        if (tw.grid[fidx][cx].type != ITEM_FLOOR) left_floor = 0;
    CHECK(left_floor, "office cells revert to build floor, not dirt");
    CHECK(tw.grid[fidx][105].tenant_id == 0, "vacated cells have no tenant");

    /* The ground lobby is permanent — bulldozer refuses it. */
    int lidx = floor_to_index(0);
    uint16_t lob = tw.grid[lidx][BX].tenant_id;
    CHECK(lob != 0, "lobby present at ground");
    CHECK(tower_remove(&tw, lob) == 0, "bulldozer refuses to remove the lobby");
    CHECK(tw.grid[lidx][BX].type == ITEM_LOBBY, "lobby still standing after bulldoze attempt");
}

int main(void)
{
    test_stairs();
    test_flavor();
    test_floor_fill();
    test_bulldozer();
    test_unreachable_empty();
    test_elevators();
    test_housekeeping();
    test_hotel_infestation();
    test_hotel_demand();
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
    test_star_requirements();
    test_promotion_cadence();
    test_medical_adequacy();
    test_guard_hunt();
    test_disaster_schedule();
    test_fire_spread();
    test_bomb_blast();
    test_twr_import();
    test_twr_export();
    test_wedding();
    test_save_load();
    test_schedules();
    printf(fails ? "\n%d FAILURE(S)\n" : "\nall tests passed\n", fails);
    return fails ? 1 : 0;
}
