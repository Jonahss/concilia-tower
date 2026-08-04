/* test_sim.c — checks for transport reachability, housekeeping, and the
 * people/elevator pipeline.
 * Build: gcc -o /tmp/test_sim tests/test_sim.c src/tower.c src/game.c \
 *            src/people.c src/twr.c src/sound_hook.c -Isrc -lm
 * No SDL needed — pure simulation (sound_hook.c is a no-op stub here). */
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
    /* Widen the ground lobby: floors can't overhang the level below (the
     * deck-extent rule), so a broad ground span keeps placement geometry
     * out of the way of what each test is actually checking. */
    tower_place(&tw, ITEM_LOBBY, 0, 100);
    tower_place(&tw, ITEM_LOBBY, 0, 280);
}

/* Lobby sits at x=179..194; build above/beside it. */
#define BX 179

static void test_stairs(void)
{
    printf("stairs connectivity:\n");
    fresh();
    place(ITEM_FLOOR, 1, BX);
    place(ITEM_FLOOR, 2, BX);
    place(ITEM_FLOOR, 3, BX);   /* stairs land on deck, not thin air */
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
    /* Columns spaced >= 8 cells apart — the EXE's shaft clearance rule
     * (CheckElevatorClearance 10a0:10e8, seg44 drag trace 2026-07-28). */
    for (int f = 0; f <= 10; f++) place(ITEM_ELEVATOR_SHAFT, f, 150);    /* standard */
    for (int f = 0; f <= 14; f++) place(ITEM_ELEVATOR_SERVICE, f, 170);  /* service  */
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
    CHECK(!tower_can_place(&tw, ITEM_ELEVATOR_EXPRESS, 7, 182),
          "new express shaft can't start mid-tower (f7)");
    CHECK(tower_can_place(&tw, ITEM_ELEVATOR_EXPRESS, 15, 182),
          "new express shaft CAN start at sky-lobby f15");
    CHECK(tower_can_place(&tw, ITEM_ELEVATOR_EXPRESS, 21, 208),
          "extending the existing express column past f20 is allowed");
}

static void run_days(int days)
{
    /* The day is always GAME_DAY_TICKS (2600) — speed no longer changes
     * day length (authentic TimeT model, 2026-08-02 re-pace). */
    for (int i = 0; i < GAME_DAY_TICKS * days; i++)
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
    for (int i = 0; i < GAME_DAY_TICKS * 4; i++) {
        game_update(&sim, &tw);
        if (hku->cleaned_today > 0) saw_cleaned = 1;
        if (saw_cleaned && room->state == TENANT_OCCUPIED &&
            sim.time_of_day == TOD_NIGHT) saw_hosted_again = 1;
    }
    CHECK(saw_cleaned, "housekeeping cleaned a checked-out room");
    CHECK(saw_hosted_again, "cleaned room hosted guests again");

    /* Lose housekeeping: after the next checkout the room sticks dirty —
     * and after 3 daily passes spent dirty-and-unrented the roaches move
     * in (JudgeT HotelNeglectCheck: the neglect fuse trips at exactly 3).
     * The bulldozer refuses housekeeping (indestructible set), so wipe
     * the record directly to simulate a tower without it. */
    CHECK(tower_remove(&tw, hk) == 0, "bulldozer refuses housekeeping");
    {
        Tenant *hkt = tenant(hk);
        for (int f = hkt->floor; f < hkt->floor + hkt->height; f++)
            for (int cx = hkt->x; cx < hkt->x + hkt->width; cx++) {
                TowerCell *c = tower_cell(&tw, f, cx);
                if (c && c->tenant_id == hk) { c->type = ITEM_FLOOR; c->tenant_id = 0; }
            }
        hkt->type = ITEM_NONE;
    }
    for (int i = 0; i < GAME_DAY_TICKS * 2; i++) game_update(&sim, &tw);
    CHECK(room->condition != ROOM_CLEAN,
          "without housekeeping the room stays dirty");
    CHECK(!room->demand_armed, "dirty room is closed for booking");
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

    /* Rooms are born armed (creation default +0x14=1), so guests have
     * already cycled through and left checkout dirt — maid the rooms
     * by hand, then one pass must re-arm the clean rooms. */
    a->condition = b->condition = c->condition = d->condition = ROOM_CLEAN;
    game_hotel_demand_pass(&sim, &tw);
    CHECK(a->demand_armed && d->demand_armed,
          "fresh clean rooms are armed by the 5PM pass");

    /* Plant roaches in the middle room and run one pass directly */
    b->condition = ROOM_INFESTED;
    b->demand_armed = 0;
    game_hotel_demand_pass(&sim, &tw);
    CHECK(a->condition == ROOM_INFESTED && c->condition == ROOM_INFESTED,
          "roaches spread to both abutting rooms in one pass");
    CHECK(d->condition == ROOM_CLEAN,
          "roaches do not jump the gap to a detached room");
    CHECK(!a->demand_armed && !c->demand_armed,
          "spread victims are closed for booking");

    /* Maids never fix infestation */
    place(ITEM_HOUSEKEEPING, 1, BX + 24);
    run_days(3);
    CHECK(b->condition == ROOM_INFESTED,
          "housekeeping never cleans an infested room");
    CHECK(!b->demand_armed && b->population == 0,
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
    a->pool_stress_total = 600; a->pool_stress_trips = 2;  /* avg 300 */
    b->pool_stress_total = 0;   b->pool_stress_trips = 4;  /* avg 0 */
    b->demand_category = 0;      /* stale: the pass must recompute it */
    game_hotel_demand_pass(&sim, &tw);
    CHECK(b->demand_armed, "happy room re-arms at the pass");
    CHECK(a->demand_armed,
          "stressed room is rescued by a happy same-floor pairing");
    CHECK(a->demand_category == 1 && b->demand_category == 1,
          "pairing settles both rooms at category 1");

    /* Without a happy floor-mate, the stressed room is disarmed */
    a->pool_stress_total = 600; a->pool_stress_trips = 2;
    b->pool_stress_total = 400; b->pool_stress_trips = 2;  /* avg 200: bad */
    game_hotel_demand_pass(&sim, &tw);
    CHECK(!a->demand_armed && !b->demand_armed,
          "stressed rooms with no happy pair are closed for booking");

    /* Very-low room rate always fills (the EXE zeroes its demand score) */
    a->pool_stress_total = 600; a->pool_stress_trips = 2;
    a->rent_class = 3;
    game_hotel_demand_pass(&sim, &tw);
    CHECK(a->demand_armed, "very-low room rate always books");

    /* Noisy neighbor (NoiseT seg_1138 + JudgeT 1130:0686, byte-verified
     * 2026-07-10): a commercial unit within 20 cells adds +60 to the
     * metric. avg 100 is fine quiet (bar 150 at 1 star) but 160 noisy. */
    a->rent_class = 1;
    a->pool_stress_total = 400; a->pool_stress_trips = 4;   /* avg 100 */
    b->pool_stress_total = 400; b->pool_stress_trips = 4;   /* no rescuer */
    game_hotel_demand_pass(&sim, &tw);
    CHECK(a->demand_armed, "avg-100 room books fine in the quiet");
    uint16_t shop = fplace(ITEM_SHOP, 1, BX + 22);            /* gap 18 <= 20 */
    a->pool_stress_total = 400; a->pool_stress_trips = 4;   /* arming reset them */
    b->pool_stress_total = 400; b->pool_stress_trips = 4;
    game_hotel_demand_pass(&sim, &tw);
    CHECK(!a->demand_armed,
          "the same room next to a shop is disarmed (+60 noise penalty)");
    /* happy guests shrug the noise off: 0 + 60 = 60 < 80 = content */
    a->pool_stress_total = 0; a->pool_stress_trips = 4;
    b->pool_stress_total = 0; b->pool_stress_trips = 4;
    game_hotel_demand_pass(&sim, &tw);
    CHECK(a->demand_armed && a->demand_category == 2,
          "noise alone never closes a room with happy guests");
    /* out of earshot: gap > 20 cells is quiet (for room a) */
    tenant(shop)->x = BX + 30;   /* edge gap from a: 209-183 = 26 */
    a->pool_stress_total = 400; a->pool_stress_trips = 4;   /* avg 100 */
    b->pool_stress_total = 0;   b->pool_stress_trips = 4;   /* rescuer */
    game_hotel_demand_pass(&sim, &tw);
    CHECK(a->demand_armed, "a shop farther than 20 cells is out of earshot");
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
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
        /* a salesman may already be off on his lobby errand — he still
         * counts as having commuted in */
        arrived = people_at(office, f5, PERSON_AT_DEST);
        for (int i = 0; i < sim.people.people_high; i++)
            if (sim.people.people[i].home_tenant == office &&
                sim.people.people[i].errand &&
                !(sim.people.people[i].state == PERSON_AT_DEST &&
                  sim.people.people[i].cur_floor == (uint8_t)f5))
                arrived++;
        if (arrived == 6) break;
    }
    CHECK(arrived == 6, "all 6 office workers rode to floor 5");
    CHECK(sim.people.trips_done >= 6, "trips recorded");
    CHECK(sim.people.wait_samples >= 6, "wait times banked");

    /* evening: everyone goes home and despawns */
    int gone = 0;
    for (int frame = 2000; frame < 6000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_EVENING, 18,
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
    tenant(shop)->retail_open = 1;    /* doors open (the 10AM row's job) */
    tenant(shop)->retail_quota = 25;

    /* Construct the metro directly on f2 — placement is basement-restricted,
     * but the sim only needs floor/type/state to feed visitors. */
    Tenant *m = &tw.tenants[tw.tenant_count++];
    *m = (Tenant){0};
    m->id = 0xF00; m->type = ITEM_METRO; m->floor = 2; m->x = BX;
    m->width = 6; m->state = TENANT_OCCUPIED;
    /* riders enter at the station TOP floor = platform + 2 (11f8:2181) */
    int mf = floor_to_index(2 + 2);

    people_rebuild_transport(&sim.people, &tw);
    game_update_reachability(&sim, &tw);   /* metro/venue spawns gate on reach */

    int visitors = 0;
    for (int frame = 0; frame < 3000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_AFTERNOON, 14,
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
        people_update(&sim.people, &tw, frame, TOD_NIGHT, 23,
                      sim.reach_public, sim.reach_service);
    for (int i = 0; i < sim.people.people_high; i++) {
        Person *p = &sim.people.people[i];
        if (p->home_tenant == shop && p->entry_floor == mf) night++;
    }
    CHECK(night == 0, "no metro visitors at night");
}

/* Cars (ParkingT UseCarPerson, byte-verified 2026-07-11): at star>=3,
 * office worker #2 of qualifying offices ((floor+id)%4==1) and real suite
 * guests drive in via their parked car's floor; a suite guest who can't
 * park cancels the visit. */
static void test_parking_cars(void)
{
    printf("parking cars (worker #2 drives, suites are parking-bound):\n");
    fresh();
    tw.star_rating = 3;
    for (int f = 1; f <= 4; f++) place(ITEM_FLOOR, f, BX);
    for (int f = 0; f <= 6; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);

    /* office constructed at a floor satisfying (floor + index) % 4 == 1 */
    int oidx = tw.tenant_count;
    int ofl = 2; while ((ofl + oidx) % 4 != 1) ofl++;
    Tenant *of = &tw.tenants[tw.tenant_count++];
    *of = (Tenant){0};
    of->id = 0xF02; of->type = ITEM_OFFICE; of->floor = (int8_t)ofl;
    of->x = BX; of->width = 9; of->state = TENANT_OCCUPIED;

    /* a real garage now: B1 ramp + space, on the chain */
    uint16_t rmp = fplace(ITEM_RAMP, -1, 120);
    uint16_t spc = fplace(ITEM_PARKING, -1, 100);
    tenant(rmp)->state = TENANT_OCCUPIED;
    tenant(spc)->state = TENANT_OCCUPIED;
    place(ITEM_ELEVATOR_SHAFT, -1, 250);
    int pf = floor_to_index(-1);

    people_rebuild_transport(&sim.people, &tw);
    game_update_reachability(&sim, &tw);
    game_parking_recompute(&sim, &tw);

    int via_park = 0, via_lobby = 0;
    for (int frame = 0; frame < 3000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
        via_park = via_lobby = 0;
        for (int i = 0; i < sim.people.people_high; i++) {
            Person *p = &sim.people.people[i];
            if (p->home_tenant != of->id) continue;
            if (p->entry_floor == pf) via_park++; else via_lobby++;
        }
        if (via_park > 0 && via_lobby >= 2) break;
    }
    CHECK(via_park == 1 && via_lobby >= 2,
          "exactly worker #2 drives in; the rest use the lobby");

    /* suite guests: with parking, guest #1+ enters at the car floor */
    fresh();
    tw.star_rating = 3;
    uint16_t ste = fplace(ITEM_HOTEL_SUITE, 3, 100);
    for (int f = 0; f <= 4; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);
    Tenant *st = tenant(ste);
    st->state = TENANT_OCCUPIED; st->demand_armed = 1;
    uint16_t rmp2 = fplace(ITEM_RAMP, -1, 120);
    uint16_t spc2 = fplace(ITEM_PARKING, -1, 100);
    tenant(rmp2)->state = TENANT_OCCUPIED;
    tenant(spc2)->state = TENANT_OCCUPIED;
    place(ITEM_ELEVATOR_SHAFT, -1, 250);
    int pf2 = floor_to_index(-1);
    people_rebuild_transport(&sim.people, &tw);
    game_update_reachability(&sim, &tw);
    game_parking_recompute(&sim, &tw);
    int drove = 0;
    for (int frame = 0; frame < 4000 && !drove; frame++) {
        people_update(&sim.people, &tw, frame, TOD_EVENING, 18,
                      sim.reach_public, sim.reach_service);
        for (int i = 0; i < sim.people.people_high; i++) {
            Person *p = &sim.people.people[i];
            if (p->home_tenant == ste && p->entry_floor == pf2) drove = 1;
        }
    }
    CHECK(drove, "a suite guest parks and enters at the car's floor");

    /* no parking: the suite hosts only its carless first guest
     * (demolish the ramp — the chain dies, the space goes dark) */
    tower_remove(&tw, rmp2);
    memset(&sim.people, 0, sizeof(sim.people));
    people_rebuild_transport(&sim.people, &tw);
    game_update_reachability(&sim, &tw);
    game_parking_recompute(&sim, &tw);
    for (int frame = 0; frame < 4000; frame++)
        people_update(&sim.people, &tw, frame, TOD_EVENING, 18,
                      sim.reach_public, sim.reach_service);
    int guests = 0;
    for (int i = 0; i < sim.people.people_high; i++)
        if (sim.people.people[i].home_tenant == ste) guests++;
    CHECK(guests <= 1, "carless suite guests cancel: one guest at most");
}

/* Sick worker's physical clinic round-trip (statuses 2/0x42/0x23/0x63) */
static void test_medical_trip(void)
{
    printf("medical patient trip:\n");
    fresh();
    for (int f = 1; f <= 5; f++) place(ITEM_FLOOR, f, BX);
    uint16_t office = place(ITEM_OFFICE, 5, BX + 6);
    uint16_t med = place(ITEM_MEDICAL, 2, BX + 6);
    for (int f = 0; f <= 5; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);
    force_occupied(office);
    tenant(med)->state = TENANT_OCCUPIED;
    people_rebuild_transport(&sim.people, &tw);
    int f5 = floor_to_index(5), f2 = floor_to_index(2);

    int frame = 0;
    for (; frame < 3000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
        if (people_at(office, f5, PERSON_AT_DEST) >= 4) break;
    }
    people_medical_dispatch(&sim.people, &tw, 5, 2);
    int at_clinic = -1;
    for (; frame < 6000 && at_clinic < 0; frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
        for (int i = 0; i < sim.people.people_high; i++)
            if (sim.people.people[i].home_tenant == office &&
                sim.people.people[i].errand == 6 &&
                sim.people.people[i].cur_floor == (uint8_t)f2)
                at_clinic = i;
    }
    CHECK(at_clinic >= 0, "sick worker traveled to the clinic (member >= 2)");
    CHECK(sim.people.people[at_clinic].member >= 2,
          "salesmen are not the sick-roll pool");
    int back = 0;
    for (; frame < 12000 && !back; frame++) {
        people_update(&sim.people, &tw, frame, TOD_AFTERNOON, 14,
                      sim.reach_public, sim.reach_service);
        const Person *p = &sim.people.people[at_clinic];
        back = p->errand == 0 && p->state == PERSON_AT_DEST &&
               p->cur_floor == (uint8_t)f5;
    }
    CHECK(back, "patient returned to the desk (status 0x63)");
}

static int condo_member_home(uint16_t tid, int fidx, int member)
{
    for (int i = 0; i < sim.people.people_high; i++) {
        const Person *p = &sim.people.people[i];
        if (p->home_tenant == tid && p->member == member &&
            p->state == PERSON_AT_DEST && p->cur_floor == (uint8_t)fidx)
            return 1;
    }
    return 0;
}

/* Condo daily commute (UniPeple 1220:3b0c/3e10): kid home 13-17h, an
 * adult in the evening; overnight at home; morning ride-down and out. */
static void test_condo_cycle(void)
{
    printf("condo daily commute cycle:\n");
    fresh();
    for (int f = 1; f <= 5; f++) place(ITEM_FLOOR, f, BX);
    uint16_t condo = place(ITEM_CONDO, 5, BX + 6);
    for (int f = 0; f <= 5; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);
    force_occupied(condo);
    people_rebuild_transport(&sim.people, &tw);
    int f5 = floor_to_index(5);

    int frame = 0, kid = 0, adult = 0;
    for (; frame < 4000 && !kid; frame++) {
        people_update(&sim.people, &tw, frame, TOD_AFTERNOON, 14,
                      sim.reach_public, sim.reach_service);
        kid = condo_member_home(condo, f5, 2);
    }
    CHECK(kid, "the kid rode home in the afternoon (member 2)");
    for (; frame < 9000 && !adult; frame++) {
        people_update(&sim.people, &tw, frame, TOD_EVENING, 18,
                      sim.reach_public, sim.reach_service);
        adult = condo_member_home(condo, f5, 0);
    }
    CHECK(adult, "an adult came home in the evening (member 0)");
    for (int k = 0; k < 600; k++, frame++)
        people_update(&sim.people, &tw, frame, TOD_NIGHT, 23,
                      sim.reach_public, sim.reach_service);
    CHECK(condo_member_home(condo, f5, 2) && condo_member_home(condo, f5, 0),
          "both residents stay home overnight");
    {
        int asleep = 0;
        for (int i = 0; i < sim.people.people_high; i++)
            if (sim.people.people[i].home_tenant == condo &&
                sim.people.people[i].errand == 8) asleep++;
        CHECK(asleep >= 1, "bedtime staggering: someone has turned in by 23:00");
    }
    int gone = 0;
    for (int k = 0; k < 6000 && !gone; k++, frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
        gone = 1;
        for (int i = 0; i < sim.people.people_high; i++)
            if (sim.people.people[i].home_tenant == condo) gone = 0;
    }
    CHECK(gone, "residents rode down and left the tower in the morning");
}

/* VIP visit (VipT seg_1240): armed sim tags tonight's suite guest
 * (member 1, the driver); a calm stay judges favorable at checkout. */
static void test_vip_visit(void)
{
    printf("VIP visit (a real suite guest, judged on his own stress):\n");
    fresh();
    tw.star_rating = 3;
    uint16_t ste = fplace(ITEM_HOTEL_SUITE, 3, 100);
    for (int f = 0; f <= 4; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);
    Tenant *st = tenant(ste);
    st->state = TENANT_OCCUPIED; st->demand_armed = 1;
    uint16_t rmp = fplace(ITEM_RAMP, -1, 120);
    uint16_t spc = fplace(ITEM_PARKING, -1, 100);
    tenant(rmp)->state = TENANT_OCCUPIED;
    tenant(spc)->state = TENANT_OCCUPIED;
    place(ITEM_ELEVATOR_SHAFT, -1, 250);
    people_rebuild_transport(&sim.people, &tw);
    game_update_reachability(&sim, &tw);
    game_parking_recompute(&sim, &tw);

    people_vip_arm(1);
    int tagged = -1;
    for (int frame = 0; frame < 4000 && tagged < 0; frame++) {
        people_update(&sim.people, &tw, frame, TOD_EVENING, 18,
                      sim.reach_public, sim.reach_service);
        int vt = people_vip_take_tagged();
        if (vt >= 0) tagged = vt;
    }
    CHECK(tagged >= 0, "VIP tagged: suite member 1 checked in");
    CHECK(sim.people.people[tagged].home_tenant == ste &&
          sim.people.people[tagged].member == 1,
          "the tagged guest is the suite's driving guest");
    CHECK(people_vip_take_result() == 0, "no verdict while he's staying");

    /* dawn: guests check out; a calm stay judges favorable */
    for (int frame = 4000; frame < 12000 &&
                           sim.people.people[tagged].home_tenant; frame++)
        people_update(&sim.people, &tw, frame, TOD_DAWN, 6,
                      sim.reach_public, sim.reach_service);
    CHECK(sim.people.people[tagged].home_tenant == 0, "the VIP checked out");
    CHECK(people_vip_take_result() == 1, "calm stay: verdict favorable");
    people_vip_arm(0);
}

static void test_walk_rules(void)
{
    printf("walk budget (6 escalator / 3 with stairs):\n");
    fresh();
    /* escalators up 5 floors, no elevator: walkable (all-escalator <= 6).
     * Deck on every floor: landings must sit on built deck (StairsT). */
    for (int f = 1; f <= 5; f++) place(ITEM_FLOOR, f, BX);
    uint16_t office = place(ITEM_OFFICE, 5, BX + 6);
    for (int f = 0; f <= 4; f++) place(ITEM_ESCALATOR, f, BX + 20);
    force_occupied(office);
    people_rebuild_transport(&sim.people, &tw);

    int f5 = floor_to_index(5);
    int arrived = 0, tagged = 0, stray = 0;
    for (int frame = 0; frame < 3000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
        /* rider attribution: WALKING people carry the id of the
         * escalator they're on (feeds the inspect rider list) */
        for (int i = 0; i < sim.people.people_high; i++) {
            const Person *p = &sim.people.people[i];
            if (p->state != PERSON_WALKING) continue;
            const Tenant *st = tower_tenant(&tw, p->walk_stair);
            if (st && st->type == ITEM_ESCALATOR) tagged++;
            else stray++;
        }
        arrived = people_at(office, f5, PERSON_AT_DEST);
        for (int i = 0; i < sim.people.people_high; i++)
            if (sim.people.people[i].home_tenant == office &&
                sim.people.people[i].errand &&
                !(sim.people.people[i].state == PERSON_AT_DEST &&
                  sim.people.people[i].cur_floor == (uint8_t)f5))
                arrived++;   /* off on the sales errand = commuted in */
        if (arrived == 6) break;
    }
    CHECK(arrived == 6, "5 escalator flights are walkable");
    CHECK(tagged > 0 && stray == 0,
          "every walking leg is attributed to a real escalator");

    /* 5 floors of STAIRS only: beyond the 3-floor stair budget -> no route */
    fresh();
    for (int f = 1; f <= 5; f++) place(ITEM_FLOOR, f, BX);
    office = place(ITEM_OFFICE, 5, BX + 6);
    for (int f = 0; f <= 4; f++) place(ITEM_STAIRS, f, BX + 20);
    force_occupied(office);
    people_rebuild_transport(&sim.people, &tw);
    Tenant *t = tenant(office);
    for (int frame = 0; frame < 600; frame++)
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
    CHECK(people_at(office, f5, PERSON_AT_DEST) == 0,
          "5 stair flights exceed the walk budget");
    CHECK(sim.people.trips_failed > 0, "failed trips recorded");
    CHECK(t->pool_stress_total >= 300 && t->pool_stress_trips > 0,
          "no-route banks a full closed 300-stress period (1210:0090+00a9)");
}

/* Sales errand (UniPeple 0/0x40/0x21/0x61), route warning (STRL 0x2CD),
 * 300-frame queue watchdog (1220:1637). */
static void test_errand_warning_watchdog(void)
{
    printf("sales errand, route warning, queue watchdog:\n");

    /* Route warning: office with no transport at all -> first failed
     * trip reports the floor pair, latched per origin floor. */
    fresh();
    for (int f = 1; f <= 5; f++) place(ITEM_FLOOR, f, BX);
    uint16_t office = place(ITEM_OFFICE, 5, BX + 6);
    force_occupied(office);
    people_rebuild_transport(&sim.people, &tw);
    (void)people_take_noroute_msg();               /* drain */
    for (int frame = 0; frame < 400; frame++)
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
    const char *msg = people_take_noroute_msg();
    CHECK(msg && strstr(msg, "need a path to Floor"),
          "no-route trip produced the floor-pair warning");
    CHECK(people_take_noroute_msg() == NULL,
          "warning latched: one per origin floor");

    /* Sales errand: walkable office tower (escalators), then walk the
     * clock: noon sends member 0 to the lobby, afternoon brings him
     * back to his desk. */
    fresh();
    for (int f = 1; f <= 5; f++) place(ITEM_FLOOR, f, BX);
    office = place(ITEM_OFFICE, 5, BX + 6);
    for (int f = 0; f <= 4; f++) place(ITEM_ESCALATOR, f, BX + 20);
    force_occupied(office);
    people_rebuild_transport(&sim.people, &tw);
    int f5 = floor_to_index(5), g = floor_to_index(0);
    int frame = 0, present = 0;
    for (; frame < 3000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
        present = people_at(office, f5, PERSON_AT_DEST);
        for (int i = 0; i < sim.people.people_high; i++)
            if (sim.people.people[i].home_tenant == office &&
                sim.people.people[i].errand &&
                !(sim.people.people[i].state == PERSON_AT_DEST &&
                  sim.people.people[i].cur_floor == (uint8_t)f5))
                present++;
        if (present == 6) break;
    }
    CHECK(present == 6, "workers commuted in (desk or errand)");
    int at_lobby = 0;
    for (; frame < 6000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_AFTERNOON, 12,
                      sim.reach_public, sim.reach_service);
        at_lobby = 0;
        for (int i = 0; i < sim.people.people_high; i++) {
            const Person *p = &sim.people.people[i];
            if (p->home_tenant == office && p->errand == 2 &&
                p->cur_floor == (uint8_t)g && p->state == PERSON_AT_DEST)
                at_lobby++;
        }
        if (at_lobby) break;
    }
    CHECK(at_lobby >= 1, "salesman reached the lobby for sales calls");
    int back = 0;
    for (; frame < 12000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_AFTERNOON, 14,
                      sim.reach_public, sim.reach_service);
        back = 0;
        for (int i = 0; i < sim.people.people_high; i++) {
            const Person *p = &sim.people.people[i];
            if (p->home_tenant == office && p->errand == 4 &&
                p->cur_floor == (uint8_t)f5 && p->state == PERSON_AT_DEST)
                back++;
        }
        if (back) break;
    }
    CHECK(back >= 1, "salesman returned to his desk (errand done)");

    /* Watchdog: a queued person whose car never comes gives up at 300
     * frames — leaves the ring, banks the wait, trips_failed bumps. */
    fresh();
    for (int f = 0; f <= 6; f++) place(ITEM_ELEVATOR_SHAFT, f, 196);
    people_rebuild_transport(&sim.people, &tw);
    ElevatorShaft *s = &sim.people.shafts[0];
    sim.people.people_high = 4;
    Person *p = &sim.people.people[0];
    memset(p, 0, sizeof *p);
    p->home_tenant = office;      /* real tenant: stress lands somewhere */
    p->state = PERSON_QUEUED;
    p->cur_floor = (uint8_t)g;
    p->dir = 1; p->shaft = 0; p->wait_start = 0;
    people_join_queue(&sim.people, 0, g, 1, 0);
    int failed_before = sim.people.trips_failed;
    people_update(&sim.people, &tw, 301, TOD_MORNING, 9,
                  sim.reach_public, sim.reach_service);
    CHECK(p->state == PERSON_AT_DEST, "watchdog: gave up after 300 frames");
    CHECK(s->stop[g].up_count == 0, "watchdog: pulled out of the ring");
    CHECK(sim.people.trips_failed == failed_before + 1,
          "watchdog: failure recorded");
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
        sim.people.people[i].cur_floor = (uint8_t)g;
        /* a real served destination: boarding re-resolves the in-shaft
         * target via ResolveViaSlot (11b0:092f) and refuses riders whose
         * dest the shaft can't reach (the old code trusted leg_floor) */
        sim.people.people[i].dest_floor = (uint8_t)floor_to_index(3);
    }
    int joined = 0;
    for (int i = 0; i < 60; i++)
        joined += people_join_queue(&sim.people, 0, g, 1, i);
    CHECK(joined == 40, "queue caps at 40 per direction");
    /* SelectElevator's no-assignment path (raw 1002-1012, referee L3):
     * the car is parked right here facing up, so the button assigns
     * NOTHING — the parked car's boarding pass takes the queue. */
    CHECK(s->up_call_car[g] == 0,
          "parked car at the call floor: no call assigned");
    people_update(&sim.people, &tw, 1, 1, 9, sim.reach_public,
                  sim.reach_service);
    people_update(&sim.people, &tw, 2, 1, 9, sim.reach_public,
                  sim.reach_service);
    CHECK(s->car[0].passengers > 0,
          "parked car boarded walk-ups without a call");
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
    for (int t = 0; t < 200; t++) people_update(&sim.people, &tw, t, 1, 9,
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

/* Bulldozing a single car (RemoveOneCar 10a0:036e): riders evicted where
 * the car stands, its calls released, surviving owners remapped down. */
/* Simulate edit mode (ElvEditT seg_10f0; referee 2026-08-01): entering
 * snapshots + isolates the group and fast-forwards its cars 2x150 ticks to
 * a settled arrangement, with ZERO effect on people, queues, stress, money
 * or time; exiting rolls everything back to the enter instant except the
 * schedule/SHOW settings, which survive. */
static void test_simulate_editmode(void)
{
    printf("simulate edit mode (ElvEditT):\n");
    fresh();
    for (int f = 0; f <= 8; f++) place(ITEM_ELEVATOR_SHAFT, f, 196);
    for (int f = 0; f <= 6; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);
    people_rebuild_transport(&sim.people, &tw);
    CHECK(sim.people.shaft_count == 2, "two shafts registered");
    ElevatorShaft *s = &sim.people.shafts[0];
    int g = floor_to_index(0), f3 = floor_to_index(3),
        f5 = floor_to_index(5), f6 = floor_to_index(6);

    /* car 0 homed at f5 but parked at ground; a passenger queued at f3
     * bound for f6, so the settle has a real trip to run */
    people_set_home(&sim.people, 0, 0, f5);
    s->car[0].floor = s->car[0].target = (uint8_t)g;
    sim.people.people_high = 8;
    Person *p0 = &sim.people.people[0];
    memset(p0, 0, sizeof(*p0));
    p0->home_tenant = 1;
    p0->state = PERSON_QUEUED;
    p0->cur_floor = (uint8_t)f3;
    p0->dest_floor = (uint8_t)f6;
    p0->dir = 1;
    people_join_queue(&sim.people, 0, f3, 1, 0);
    CHECK(s->up_call_car[f3] == 1, "queued person's call assigned to car 1");

    /* pre-enter reference state */
    Person pre_p0 = *p0;
    ElevatorStop pre_stop = s->stop[f3];
    uint8_t pre_floor = s->car[0].floor;
    long pre_money = tw.money;
    long pre_wait_total = sim.people.wait_total;
    long pre_trips = sim.people.trips_done;

    people_edit_enter(&sim.people, &tw, 0, tw.star_rating, 0);
    CHECK(people_edit_shaft() == 0, "edit mode active on shaft 0");
    CHECK(s->active && !sim.people.shafts[1].active,
          "other group isolated (active flag cleared)");
    /* the settle: car served f3 -> f6, then went home to f5 — only the
     * physics fields keep the fast-forwarded arrangement */
    CHECK(s->car[0].floor == f5, "settle parked the idle car at its home");
    CHECK(memcmp(p0, &pre_p0, sizeof(Person)) == 0,
          "person record byte-identical after the settle");
    CHECK(memcmp(&s->stop[f3], &pre_stop, sizeof(ElevatorStop)) == 0,
          "stop queue restored to the enter instant");
    CHECK(s->up_call_car[f3] == 1 && s->car[0].assigned_calls == 1,
          "call assignment restored");
    CHECK(s->car[0].passengers == 0 && s->car[0].dest_count[f6] == 0,
          "no pre-sim passengers or dests leak into the still view");
    CHECK(tw.money == pre_money && sim.people.wait_total == pre_wait_total &&
          sim.people.trips_done == pre_trips,
          "zero effect on money / stress bank / trip stats");

    /* re-entering while in the mode is refused (EXE: [0xB3AE] toggle) */
    people_edit_enter(&sim.people, &tw, 1, tw.star_rating, 0);
    CHECK(people_edit_shaft() == 0, "nested enter refused");

    /* in-mode schedule edits (the ALLOWED widgets) survive the rollback */
    s->sched_mode[0][2] = 1;
    s->sched_threshold[0][2] = 42;
    s->sched_patience[0][2] = 3;
    s->hidden = 1;
    people_edit_exit(&sim.people);
    CHECK(people_edit_shaft() == -1, "edit mode exited");
    CHECK(s->car[0].floor == pre_floor,
          "car physics rolled back to the enter instant");
    CHECK(s->sched_mode[0][2] == 1 && s->sched_threshold[0][2] == 42 &&
          s->sched_patience[0][2] == 3 && s->hidden == 1,
          "schedule + SHOW edits survive the rollback");
    CHECK(sim.people.shafts[1].active, "other group re-activated");
    CHECK(s->stop[f3].up_count == 1 &&
          memcmp(p0, &pre_p0, sizeof(Person)) == 0,
          "queue and person untouched across the whole mode");
    CHECK(s->home[0] == f5, "car home (set before entering) kept");
}

static void test_remove_car(void)
{
    printf("car removal (demolish referee 2026-08-02):\n");
    fresh();
    for (int f = 0; f <= 6; f++) place(ITEM_ELEVATOR_SHAFT, f, 196);
    people_rebuild_transport(&sim.people, &tw);
    ElevatorShaft *s = &sim.people.shafts[0];
    people_set_num_cars(&sim.people, 0, 3);
    int f2 = floor_to_index(2), f5 = floor_to_index(5);

    sim.people.people_high = 4;
    sim.people.people[0].home_tenant = 1;
    sim.people.people[0].state = PERSON_RIDING;
    s->car[1].floor = (uint8_t)f2;
    s->car[1].passengers = 1;
    s->car[1].pax[0] = 1;                     /* person 0 rides car 1 */
    s->car[1].assigned_calls = 1;
    s->up_call_car[f5] = 2;                   /* car 1 owns f5's call */
    s->car[2].assigned_calls = 1;
    s->up_call_car[f2] = 3;                   /* car 2 owns f2's call */

    people_remove_car(&sim.people, 0, 1);
    CHECK(s->num_cars == 2, "car count decremented");
    CHECK(s->up_call_car[f5] == 0, "removed car's call released");
    CHECK(s->up_call_car[f2] == 2, "surviving owner remapped down");
    CHECK(s->car[1].assigned_calls == 1, "surviving car compacted in place");
    CHECK(sim.people.people[0].state == PERSON_PLANNING &&
          sim.people.people[0].cur_floor == f2,
          "rider evicted where the car stood");

    people_remove_car(&sim.people, 0, 0);
    people_remove_car(&sim.people, 0, 0);
    CHECK(s->num_cars == 1, "last car can never be removed here");
}

/* Modes 1/2 = one-way shuttles (FindTargetFloor raw 1322-1363/1490-1526):
 * "Express Up" runs NONSTOP to the top, serves on the way down. And the
 * patience dwell holds doors OPEN, only at the home floor or a lobby. */
#define DOOR_TICKS_TEST 5   /* people.c DOOR_OPEN_TICKS */
static void test_shuttle_and_patience(void)
{
    printf("shuttle modes & patience dwell:\n");
    fresh();
    for (int f = 0; f <= 6; f++) place(ITEM_ELEVATOR_SHAFT, f, 196);
    people_rebuild_transport(&sim.people, &tw);
    ElevatorShaft *s = &sim.people.shafts[0];
    int g = floor_to_index(0), f3 = floor_to_index(3), f6 = floor_to_index(6);

    /* Express Up all periods; rider aboard at ground wants f3 */
    memset(s->sched_mode, 1, sizeof s->sched_mode);
    ElevatorCar *c = &s->car[0];
    c->floor = c->target = (uint8_t)g;
    c->dir = 1;
    c->passengers = 1; c->pax[0] = 1; c->pax_dest[0] = (uint8_t)f3;
    c->dest_count[f3] = 1; c->distinct_dests = 1;
    sim.people.people_high = 4;
    sim.people.people[0].home_tenant = 1;
    sim.people.people[0].state = PERSON_RIDING;
    sim.people.people[0].cur_floor = (uint8_t)g;
    sim.people.people[0].dest_floor = (uint8_t)f3;
    sim.people.people[0].leg_floor = (uint8_t)f3;
    c->door_timer = 1;               /* closing: next tick picks a target */
    int doors_at_f3_going_up = 0, reached_top = 0, served_f3_after = 0;
    for (int t = 0; t < 600; t++) {
        people_update(&sim.people, &tw, t, 1, 9,
                      sim.reach_public, sim.reach_service);
        if (!reached_top && c->floor == f3 && c->door_timer)
            doors_at_f3_going_up = 1;
        if (c->floor == f6) reached_top = 1;
        if (reached_top && c->floor == f3 && c->passengers == 0)
            { served_f3_after = 1; break; }
    }
    CHECK(reached_top, "Express Up car ran to the shaft top");
    CHECK(!doors_at_f3_going_up, "nonstop leg skipped the rider's floor");
    CHECK(served_f3_after, "rider delivered on the DOWN serve leg");

    /* Patience dwell: doors held open at a lobby/home floor... */
    memset(s->sched_mode, 0, sizeof s->sched_mode);
    memset(s->sched_patience, 2, sizeof s->sched_patience); /* 60 ticks */
    c->floor = c->target = (uint8_t)g;   /* ground = lobby (and home) */
    c->dir = 1;
    c->door_timer = DOOR_TICKS_TEST;     /* just opened */
    int held = 0;
    for (int t = 0; t < 30; t++) {
        people_update(&sim.people, &tw, 1000 + t, 1, 9,
                      sim.reach_public, sim.reach_service);
        if (c->door_timer == 1) held++;
    }
    CHECK(held > 20, "patience holds the doors open at the lobby");
    /* ...but NOT at an ordinary floor (ShouldTimeout: home or lobby only).
     * Count door-open ticks only while the car is still AT f3 — once it
     * departs it heads home, where doors legitimately reopen. */
    c->floor = c->target = (uint8_t)f3;
    c->door_timer = DOOR_TICKS_TEST;
    c->hold_timer = 0;
    int open_ticks = 0;
    for (int t = 0; t < 30; t++) {
        people_update(&sim.people, &tw, 2000 + t, 1, 9,
                      sim.reach_public, sim.reach_service);
        if (c->floor == f3 && c->door_timer) open_ticks++;
    }
    CHECK(open_ticks <= DOOR_TICKS_TEST,
          "no dwell at a non-lobby, non-home floor");
}

/* Patrons visit venues and leave; housekeepers ride the service net */
static void test_patrons_and_staff(void)
{
    printf("patrons & staff trips:\n");
    fresh();
    for (int f = 1; f <= 2; f++) place(ITEM_FLOOR, f, BX);
    uint16_t ff = place(ITEM_FAST_FOOD, 2, BX + 6);
    for (int f = 0; f <= 1; f++) place(ITEM_STAIRS, f, BX + 30);
    CHECK(ff != 0, "fast food placed");
    force_occupied(ff);
    tenant(ff)->retail_open = 1;      /* doors open (the 10AM row's job) */
    tenant(ff)->retail_quota = 35;    /* plenty of walk-ins to draw from */
    people_rebuild_transport(&sim.people, &tw);

    int f2 = floor_to_index(2);
    int seen = 0;
    for (int frame = 0; frame < 3000 && seen < 5; frame++) {
        people_update(&sim.people, &tw, frame, TOD_AFTERNOON, 14,
                      sim.reach_public, sim.reach_service);
        if (people_at(ff, f2, PERSON_AT_DEST) > seen)
            seen = people_at(ff, f2, PERSON_AT_DEST);
    }
    CHECK(seen == 5, "5 lunch patrons walked up to the fast food");
    int gone = 0;
    for (int frame = 1200; frame < 6000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_AFTERNOON, 14,
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
        people_update(&sim.people, &tw, frame, TOD_DAWN, 6,
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
    place(ITEM_FLOOR, 1, BX);          /* upper landing needs deck */
    place(ITEM_ESCALATOR, 0, BX + 30); /* clear of the shaft's blocked columns */
    game_update_reachability(&sim, &tw);
    people_rebuild_transport(&sim.people, &tw);
    if (sim.people.shaft_count >= 1)
        people_set_num_cars(&sim.people, 0, 3);
    /* The sweep fires on the quarterly settlement (every 3rd day, with
     * the rent lumps — byte-verified 2026-07-11; daily billing was 3x
     * the EXE's rate), so three days = exactly one sweep. */
    long before = tw.money;
    run_days(3);
    long upkeep = before - tw.money;
    CHECK(upkeep == 3 * TUNING.maint_car_std + TUNING.maint_escalator,
          "settlement sweep charges 3 cars x $10k + escalator $5k, once per quarter");

    /* Lobbies are free below 3 stars (FUN_1178_0a6a: star fee table 0/30/100) */
    tw.star_rating = 2;
    before = tw.money;
    run_days(3);
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

/* The occupancy lifecycle (2026-07-11 vacancy referee): daily category
 * judge, category-0 move-outs, frozen-stress condemnation, the price-cut
 * rescue, pairing, and the people-driven re-let. */
static void test_occupancy_lifecycle(void)
{
    printf("occupancy lifecycle (judge/vacate/rescue/re-let):\n");
    fresh();
    tw.star_rating = 1;
    tw.day = 1;
    uint16_t off = fplace(ITEM_OFFICE, 3, 100);
    Tenant *o = tenant(off);
    o->state = TENANT_OCCUPIED;

    /* the daily judge's bars and adjustments */
    o->pool_stress_total = 400; o->pool_stress_trips = 2;  /* avg 200 */
    game_judge_daily(&sim, &tw);
    CHECK(o->demand_category == 0, "avg wait 200 vs bar 150 = stressed");
    o->pool_stress_total = 200;                            /* avg 100 */
    game_judge_daily(&sim, &tw);
    CHECK(o->demand_category == 1, "avg 100 = the middle band");
    o->rent_class = 2;                                     /* Low: -30 */
    game_judge_daily(&sim, &tw);
    CHECK(o->demand_category == 2, "a rent cut buys forgiveness (-30)");
    o->rent_class = 0;                                     /* High: +30 */
    o->pool_stress_total = 260;                            /* 130+30=160 */
    game_judge_daily(&sim, &tw);
    CHECK(o->demand_category == 0, "high rent makes pickier tenants (+30)");

    /* category-0 move-out at the settlement; armed clears; stress FREEZES */
    game_stressed_moveout(&sim, &tw);
    CHECK(o->state == TENANT_ABANDONED && !o->demand_armed,
          "a stressed office vacates and leaves the market");
    game_judge_daily(&sim, &tw);
    CHECK(!o->demand_armed,
          "frozen banked stress keeps the vacancy condemned");
    o->rent_class = 3;                                     /* the rescue */
    game_judge_daily(&sim, &tw);
    CHECK(o->demand_armed && o->demand_category == 2,
          "bottom price class forces content: back on the market");

    /* pairing: a content twin vouches for the leaver */
    uint16_t o2 = fplace(ITEM_OFFICE, 4, 100);
    uint16_t o3 = fplace(ITEM_OFFICE, 4, 120);
    Tenant *a = tenant(o2), *b = tenant(o3);
    a->state = b->state = TENANT_OCCUPIED;
    a->demand_category = 0; a->tenure = 1;
    b->demand_category = 2;
    game_stressed_moveout(&sim, &tw);
    CHECK(a->state == TENANT_ABANDONED && a->demand_armed &&
          a->demand_category == 1 && b->demand_category == 1,
          "the pairing: vacated unit re-arms, the voucher drops to middle");

    /* condo buy-back on vacate */
    uint16_t cn = fplace(ITEM_CONDO, 5, 100);
    Tenant *c = tenant(cn);
    c->state = TENANT_OCCUPIED; c->demand_category = 0; c->rent_class = 1;
    long money0 = tw.money;
    game_stressed_moveout(&sim, &tw);
    CHECK(c->state == TENANT_ABANDONED && tw.money == money0 - 150000,
          "condo departure charges the class-1 buy-back ($150k)");

    /* Shops: the eviction blink (shop-judge referee 2026-07-11) — a
     * tenured cat-0 shop vacates, keeps its scores, and its tenure
     * reset buys the one-settlement immunity that makes the every-24th
     * -day rainy collision a blink instead of a purge. */
    uint16_t sh = fplace(ITEM_SHOP, 6, 100);
    Tenant *s = tenant(sh);
    s->state = TENANT_OCCUPIED; s->demand_category = 0; s->tenure = 0;
    game_stressed_moveout(&sim, &tw);
    CHECK(s->state == TENANT_OCCUPIED, "a first-quarter shop is immune");
    s->tenure = 2;
    s->retail_score[0] = 23;
    game_stressed_moveout(&sim, &tw);
    CHECK(s->state == TENANT_ABANDONED && s->tenure == 0 &&
          s->retail_score[0] == 23,
          "eviction keeps the scores and resets tenure (the blink)");
    /* the vacant shop trades on: opens on its residual score, and a
     * fresh (weekday) demand day re-arms it without a price cut */
    tw.day = 1; sim.hour = 10;
    game_retail_hourly(&sim, &tw);
    CHECK(s->retail_open && s->retail_quota == 23,
          "a vacant shop still opens on its residual score");
    s->customers_today = 23; s->retail_quota = 0;
    game_judge_daily(&sim, &tw);
    CHECK(s->demand_armed,
          "a good trading day re-arms the vacancy - no price cut needed");

    /* shop demand judge: quota_left + customers vs (20,25) + class adj */
    s->state = TENANT_OCCUPIED;
    s->rent_class = 1; s->retail_quota = 10; s->customers_today = 5;
    game_judge_daily(&sim, &tw);
    CHECK(s->demand_category == 0, "demand 15 under bar 20 = stressed");
    s->customers_today = 12;
    game_judge_daily(&sim, &tw);
    CHECK(s->demand_category == 1, "demand 22 = middle");
    s->rent_class = 3;                                     /* -12 adj */
    s->customers_today = 5;
    game_judge_daily(&sim, &tw);
    CHECK(s->demand_category == 2, "the class-3 discount rescues a shop");

    /* the re-let: an armed vacancy's mover arrives and banks the lump */
    fresh();
    tw.star_rating = 1; tw.day = 1;
    for (int f = 1; f <= 2; f++) place(ITEM_FLOOR, f, BX);
    uint16_t rl = place(ITEM_OFFICE, 3, BX + 6);
    for (int f = 0; f <= 3; f++) place(ITEM_ELEVATOR_SHAFT, f, 250);
    Tenant *r = tenant(rl);
    r->state = TENANT_ABANDONED;
    r->demand_armed = 1;
    r->rent_class = 1;
    people_rebuild_transport(&sim.people, &tw);
    game_update_reachability(&sim, &tw);
    money0 = tw.money;
    int relet = 0;
    for (int frame = 0; frame < 6000 && !relet; frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
        game_relet_arrivals(&sim, &tw);
        relet = r->state == TENANT_OCCUPIED;
    }
    CHECK(relet && tw.money == money0 + 10000,
          "the mover arrives: unit re-lets and banks the class-1 lump");
    CHECK(r->pool_stress_trips == 0 && r->tenure == 0,
          "a re-let resets the stress window and tenure");

    /* rest/FF arm categories (JudgeT @07ac/@0760): yesterday's
     * customers vs the 50/35/25 ladder — eval-map food, no move-out */
    uint16_t rf = fplace(ITEM_RESTAURANT, 7, 100);
    Tenant *rt = tenant(rf);
    rt->state = TENANT_OCCUPIED; rt->customers_today = 55;
    game_judge_daily(&sim, &tw);
    CHECK(rt->demand_category == 3, "55 customers = the packed 4th tier");
    rt->customers_today = 20;
    game_judge_daily(&sim, &tw);
    CHECK(rt->demand_category == 0, "under 25 customers = the loss tier");
    game_stressed_moveout(&sim, &tw);
    CHECK(rt->state == TENANT_OCCUPIED,
          "a loss-tier restaurant still never moves out");
}

/* The info-dialog price control (InfoDlgT 1100:0b93-0c5e): the sole
 * sim-time rate-class writer, with the EXE's IMMEDIATE re-judge — a
 * rent cut revives a condemned unit on the click, not at dawn. */
static void test_rent_control(void)
{
    printf("rent control (price selector + instant re-judge):\n");
    fresh();
    tw.star_rating = 1;
    tw.day = 1;
    uint16_t off = fplace(ITEM_OFFICE, 3, 100);
    Tenant *o = tenant(off);
    o->state = TENANT_ABANDONED;          /* stress-vacated, frozen memories */
    o->demand_armed = 0;
    o->demand_category = 0;
    o->pool_stress_total = 400; o->pool_stress_trips = 2;  /* avg 200 */
    o->rent_class = 1;

    /* unchanged class = no side effects (@0bdd early return) */
    CHECK(game_set_rent_class(&sim, &tw, o, 1) == 0 &&
          o->demand_category == 0 && !o->demand_armed,
          "re-selecting the current class is a no-op");
    CHECK(game_set_rent_class(&sim, &tw, o, 4) == 0 && o->rent_class == 1,
          "out-of-range class is rejected");
    /* a modest cut isn't enough against avg 200 (200-30 >= bar 150) */
    CHECK(game_set_rent_class(&sim, &tw, o, 2) == 0 &&
          o->rent_class == 2 && !o->demand_armed,
          "a modest cut can fail to clear the bar (170 vs 150)");
    /* the bottom class forces content and re-arms ON THE CLICK */
    CHECK(game_set_rent_class(&sim, &tw, o, 3) == 1 &&
          o->demand_armed && o->demand_category == 2,
          "Very Low revives the condemned unit instantly");

    /* raising the rent on an occupied unit re-judges instantly too */
    uint16_t of2 = fplace(ITEM_OFFICE, 4, 100);
    Tenant *p = tenant(of2);
    p->state = TENANT_OCCUPIED;
    p->pool_stress_total = 260; p->pool_stress_trips = 2;  /* avg 130 */
    p->rent_class = 1; p->demand_category = 1;
    game_set_rent_class(&sim, &tw, p, 0);                  /* +30 = 160 */
    CHECK(p->demand_category == 0,
          "a rent hike re-judges the unit stressed on the click");

    /* hotel rooms take the class (income/pickiness) but keep the 5PM judge */
    uint16_t hr = fplace(ITEM_HOTEL_SINGLE, 5, 100);
    Tenant *h = tenant(hr);
    h->state = TENANT_OCCUPIED; h->rent_class = 1; h->demand_category = 2;
    CHECK(game_set_rent_class(&sim, &tw, h, 0) == 0 &&
          h->rent_class == 0 && h->demand_category == 2,
          "a hotel room takes the class without an office-style judge");
}

/* Buildable parking (ParkingT, byte-verified 2026-07-11 referee):
 * build gates (same-floor ramp, one ramp/floor, 512 cap), the
 * B1-anchored same-x ramp chain, the >=4-cell gap rule, and the
 * per-category car quotas with double-parking. */
static void test_parking_model(void)
{
    printf("parking (ramps/chain/gap/quota):\n");
    fresh();
    /* widen the ground floor so basement items have support */
    for (int cx = 100; cx < 179; cx += 4) place(ITEM_LOBBY, 0, cx);

    /* build gates (through the real placement path) */
    CHECK(place(ITEM_PARKING, -1, 100) == 0,
          "a space needs a ramp on its floor first");
    uint16_t r1 = place(ITEM_RAMP, -1, 120);
    CHECK(r1 != 0, "a B1 ramp places");
    CHECK(place(ITEM_RAMP, -1, 140) == 0, "one ramp per floor");
    uint16_t s1 = place(ITEM_PARKING, -1, 100);
    CHECK(s1 != 0, "a space places once the ramp exists");

    /* chain: B2 ramp at a DIFFERENT x doesn't chain; same x does
     * (fixtures constructed directly — the chain logic is the subject) */
    tenant(r1)->state = TENANT_OCCUPIED;
    tenant(s1)->state = TENANT_OCCUPIED;
    uint16_t r2 = fplace(ITEM_RAMP, -2, 160);
    uint16_t s2 = fplace(ITEM_PARKING, -2, 200);
    tenant(r2)->state = TENANT_OCCUPIED;
    tenant(s2)->state = TENANT_OCCUPIED;
    game_parking_recompute(&sim, &tw);
    CHECK(tenant(s1)->space_usable, "B1 space on the chain is usable");
    CHECK(!tenant(s2)->space_usable,
          "a B2 ramp at a different x does NOT chain");
    CHECK(tw.usable_spaces == 1, "usable count sees only the chain");
    /* re-anchor B2's ramp at the same x as B1's — it chains, but the
     * 64 bare cells between ramp and space still sever the drive path
     * (>=4-cell gap rule) */
    tower_remove(&tw, r2);
    r2 = fplace(ITEM_RAMP, -2, 120);
    tenant(r2)->state = TENANT_OCCUPIED;
    game_parking_recompute(&sim, &tw);
    CHECK(!tenant(s2)->space_usable && tw.usable_spaces == 1,
          "a same-x ramp chains, but bare floor severs the far space");

    /* pave the drive path except a 4-cell hole at 196..199 — still
     * severed; close the hole to 3 cells and the space comes back */
    for (int cx = 136; cx < 196; cx++)
        tw.grid[floor_to_index(-2)][cx].type = ITEM_FLOOR;
    game_parking_recompute(&sim, &tw);
    CHECK(!tenant(s2)->space_usable,
          "a 4-cell bare gap severs the floor past it");
    tw.grid[floor_to_index(-2)][196].type = ITEM_FLOOR;
    game_parking_recompute(&sim, &tw);
    CHECK(tenant(s2)->space_usable && tw.usable_spaces == 2,
          "shrinking the gap to 3 cells restores the space");

    /* quotas: 2 usable spaces -> 4 cars per category (2N each,
     * double-parking is real — the quota is the only limiter) */
    tw.cars_office = 0; tw.cars_suite = 0;
    uint8_t reach[TOWER_FLOOR_COUNT];
    memset(reach, 1, sizeof reach);
    int admitted = 0;
    for (int k = 0; k < 10; k++)
        if (people_parking_assign(&tw, reach, 0, k) >= 0) admitted++;
    CHECK(admitted == 4 && tw.cars_office == 4,
          "office cars admit to 2N then the lot is full");
    CHECK(people_parking_assign(&tw, reach, 1, 0) >= 0 &&
          tw.cars_suite == 1,
          "the suite category has its own independent 2N quota");
    tw.cars_office = 99;
    game_parking_recompute(&sim, &tw);
    CHECK(tw.cars_office == 2 * tw.usable_spaces,
          "car counters clamp to the 2N quota on recompute");

    /* demolishing the B1 ramp darkens the whole garage */
    tower_remove(&tw, r1);
    game_parking_recompute(&sim, &tw);
    CHECK(tw.usable_spaces == 0 && !tenant(s1)->space_usable,
          "no B1 ramp, no usable garage");
}

/* Metro/parking money (res 0x3EA, byte-verified 2026-07-11): $100k per
 * station + $10k per ramp at the quarterly settlement; parking earns and
 * costs nothing per space or car. */
static void test_infra_upkeep(void)
{
    printf("metro/ramp quarterly upkeep:\n");
    fresh();
    Tenant *m = &tw.tenants[tw.tenant_count++];
    *m = (Tenant){0};
    m->id = 0xF10; m->type = ITEM_METRO; m->floor = -10; m->x = 100;
    m->width = 30; m->state = TENANT_OCCUPIED;
    Tenant *rp = &tw.tenants[tw.tenant_count++];
    *rp = (Tenant){0};
    rp->id = 0xF11; rp->type = ITEM_RAMP; rp->floor = -1; rp->x = 100;
    rp->width = 16; rp->state = TENANT_OCCUPIED;

    sim.speed = SPEED_NORMAL;
    /* Park one tick before the 4:59AM settle row (ft 0x9E5 = 2533):
     * the next update lands exactly on it. The day was already bumped
     * at the ft-2300 midnight row, so set a settlement day directly. */
    sim.quarter = 3;
    sim.tick = FT_DAILY_SETTLE % GAME_TICKS_PER_QUARTER - 1;
    tw.day = 3;                          /* day%3==0: a settlement day */
    long money0 = tw.money;
    game_update(&sim, &tw);
    CHECK(tw.money == money0 - 110000,
          "settlement charges $100k metro + $10k ramp, nothing else");
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
    /* EXE SelectElevator (1090:1053-10cb): the APPROACHING car takes the
     * call unless it scores >= threshold floors worse than the idle car
     * — calls piggy-back onto moving cars. (The old assertion here
     * encoded the inverted pick; elevator-truths referee H1.) */
    CHECK(s->up_call_car[s->lo + 3] == 2,
          "threshold 5: the approaching car answers (piggy-back)");
    s->up_call_car[s->lo + 3] = 0;
    s->stop[s->lo + 3].up_count = 0;
    /* an idle-at-home car only wakes when the working car is clearly
     * worse: send car1 reversing from a far dest. The call goes to lo+1,
     * NOT the idle car's own floor — a call at a parked car's floor now
     * assigns nothing at all (referee L3). */
    s->sched_threshold[0][0] = 1;
    s->car[1].floor = (uint8_t)(s->lo + 4);
    s->car[1].dest_count[s->hi] = 1;    /* sweep runs to the top first */
    people_join_queue(&sim.people, 0, s->lo + 1, 1, 0);
    CHECK(s->up_call_car[s->lo + 1] == 1,
          "reversing car far worse than idle-at-home: idle answers");
    s->up_call_car[s->lo + 1] = 0;
    s->stop[s->lo + 1].up_count = 0;
    s->car[1].dest_count[s->hi] = 0;
    people_set_num_cars(&sim.people, 0, 1);

    /* shuttle modes: a WORKLESS shuttle car parks at HOME like any other
     * car — the EXE's no-work->home check (raw 1313-1319) precedes the
     * mode branches. (The one-way express behavior when it HAS work is
     * covered in test_shuttle_and_patience.) */
    s->sched_mode[0][0] = 1;
    s->car[0].floor = s->car[0].target = s->hi;
    s->car[0].dir = 0;
    int homed = 0;
    for (int i = 0; i < 400; i++) {
        people_update(&sim.people, &tw, i, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
        if (s->car[0].floor == s->home[0]) homed = 1;
    }
    CHECK(homed, "idle shuttle car returns HOME, not to the shaft's end");
    s->sched_mode[0][0] = 0;

    /* patience: workers still arrive, but cars dwell (hold_timer engages) */
    s->sched_patience[0][0] = 2;
    sim.time_of_day = TOD_MORNING;
    int held = 0, arrived = 0;
    int f5 = floor_to_index(5);
    for (int frame = 0; frame < 4000; frame++) {
        /* afternoon: the salesman's return leg supplies boarding traffic
         * (morning would leave him parked at the lobby until 13:00) */
        people_update(&sim.people, &tw, frame, TOD_AFTERNOON, 14,
                      sim.reach_public, sim.reach_service);
        if (s->car[0].hold_timer) held = 1;
        arrived = people_at(office, f5, PERSON_AT_DEST);
        for (int i = 0; i < sim.people.people_high; i++)
            if (sim.people.people[i].home_tenant == office &&
                sim.people.people[i].errand &&
                !(sim.people.people[i].state == PERSON_AT_DEST &&
                  sim.people.people[i].cur_floor == (uint8_t)f5))
                arrived++;   /* salesman on his errand still counts */
        /* workers may all be settled before the loop starts now — run
         * until the errand traffic makes the patient car dwell too */
        if (held && arrived == 6) break;
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

/* The retail patron economy (Restaurant.c seg_11a8 + MoneyT 1178:126c,
 * byte-verified 2026-07-11): open sets quota = clamp(score, 10, cap);
 * customers grade the service score; close banks the tiered daily income
 * (bottom tier a LOSS); disaster-offer days suppress it all. */
static void test_retail_economy(void)
{
    printf("retail patron economy (Restaurant.c + MoneyT 126c):\n");
    fresh();
    tw.day = 1;                                /* weekday, no disasters */
    sim.quarter = 0;
    uint16_t rid = fplace(ITEM_RESTAURANT, 3, 100);
    uint16_t fid = fplace(ITEM_FAST_FOOD, 4, 100);
    uint16_t hid = fplace(ITEM_SHOP, 5, 100);
    Tenant *r = tenant(rid), *f = tenant(fid), *s = tenant(hid);
    r->state = f->state = s->state = TENANT_OCCUPIED;

    CHECK(game_retail_period(&sim, &tw) == 0, "plain day = weekday class");
    tw.day = 12;                               /* 12%8==4 */
    CHECK(game_retail_period(&sim, &tw) == 2, "day%%8==4 = the rainy class");
    tw.star_rating = 5;
    CHECK(game_retail_period(&sim, &tw) == 0, "5-star towers retire rain");
    tw.star_rating = 1; tw.day = 1;

    /* 10AM opens FF+shops (not restaurants); quota floors at 10 */
    sim.hour = 10; game_retail_hourly(&sim, &tw);
    CHECK(!r->retail_open && f->retail_open && s->retail_open,
          "10AM opens fast food and shops; restaurants wait for 5PM");
    CHECK(f->retail_quota == 10, "a scoreless venue still gets quota 10");

    /* customers at the door: admission, grading, the 40 cap */
    game_retail_customer_in(&sim, &tw, f, 0, 40);   /* fast elevator: +2 */
    game_retail_customer_in(&sim, &tw, f, 0, 100);  /* slow: +1 */
    game_retail_customer_in(&sim, &tw, f, 0, 400);  /* awful: +0 */
    CHECK(f->customers_today == 3 && f->patrons_now == 3,
          "arrivals count as customers and patrons");
    CHECK(f->retail_score[0] == 3, "service grades bank +2/+1/+0");
    CHECK(game_retail_customer_in(&sim, &tw, r, 0, 0) == 1,
          "a closed restaurant bounces its customer");
    f->patrons_now = 40;
    CHECK(game_retail_customer_in(&sim, &tw, f, 0, 0) == 2,
          "the 41st patron is refused (house cap 40)");
    f->patrons_now = 3;

    /* 5PM opens restaurants; yesterday's score becomes today's quota */
    r->retail_score[0] = 44;
    sim.hour = 17; game_retail_hourly(&sim, &tw);
    CHECK(r->retail_open && r->retail_quota == 35,
          "restaurant quota = clamp(score, 10, weekday cap 35)");
    CHECK(r->retail_score[0] == 0, "opening resets the period's score");

    /* 9PM closes FF+shops and banks the FF ladder; shops book nothing */
    long money0 = tw.money;
    f->customers_today = 50;
    s->customers_today = 60;
    sim.hour = 21; game_retail_hourly(&sim, &tw);
    CHECK(!f->retail_open && tw.money == money0 + 5000,
          "50 customers bank fast food's $5,000 top tier at 9PM (shop $0)");
    money0 = tw.money;

    /* 11PM closes restaurants — and an empty one takes the LOSS tier */
    r->customers_today = 0;
    sim.hour = 23; game_retail_hourly(&sim, &tw);
    CHECK(tw.money == money0 - 6000,
          "an empty restaurant LOSES $6,000 at its 11PM close");

    /* the disaster-day suppression (MoneyT 126c day%60/%84 gates) */
    tw.day = 59;                                /* bomb-offer day */
    r->customers_today = 200; r->retail_open = 1;
    money0 = tw.money;
    sim.hour = 23; game_retail_hourly(&sim, &tw);
    CHECK(tw.money == money0, "bomb-offer days suppress venue income");

    /* weekend quota cap + max(wd,we) score read */
    tw.day = 2;   /* day%3==2 = the weekend day (game_is_weekend) */
    f->retail_score[0] = 60; f->retail_score[1] = 20;
    sim.hour = 10; game_retail_hourly(&sim, &tw);
    CHECK(f->retail_quota == 50,
          "weekend reads max(wd,we) score against the 50 cap");
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
    tw.day = 3;   /* a weekday — the window needs !game_is_weekend */
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
    tw.day = 5;   /* day%3==2: the weekend day */
    CHECK(!game_check_promotion(&sim, &tw, 4) && !game_check_promotion(&sim, &tw, 5),
          "no big promotions on the weekend");
    sim.promo.has_security = 1;
    CHECK(game_check_promotion(&sim, &tw, 3),
          "2->3 has no clock gate (binary checks only security)");
    tw.day = 3;
}

/* Construction-time occupancy tiers (TenantMake MakeTenant) — these are
 * set once at build (and reconstructed from .TDT bytes on import); no
 * tier-raising mechanic exists in the binary (2026-07-11 referee). */
static void test_cap_peaks(void)
{
    printf("construction occupancy tiers:\n");
    CHECK(game_init_cap_peak(ITEM_OFFICE, 1) == CAP_PEAK_LOW,
          "fresh office starts at the low tier");
    CHECK(game_init_cap_peak(ITEM_HOTEL_SINGLE, 1) == 0x10 &&
          game_init_cap_peak(ITEM_HOTEL_SUITE, 1) == 0x18,
          "hotel room tiers by type");
    CHECK(game_init_cap_peak(ITEM_SHOP, 5) == 0,
          "retail is not tier-managed");
    CHECK(cap_base_peak(ITEM_OFFICE) == CAP_PEAK_LOW &&
          cap_base_peak(ITEM_CONDO) == 0,
          "income-scaling baselines");
}

/* The 3rd-day stressed move-out (JudgeT 1130:09e5, byte-verified
 * 2026-07-11 — the mechanic June shipped inverted as "upgrades"). */
static void test_stressed_moveout(void)
{
    printf("3rd-day stressed move-out (offices/condos/shops):\n");
    fresh();
    tw.tenant_count = 0;

    Tenant *off = &tw.tenants[tw.tenant_count++];
    *off = (Tenant){0};
    off->type = ITEM_OFFICE; off->floor = 3; off->x = 100; off->width = 9;
    off->state = TENANT_OCCUPIED;
    off->demand_category = 0;              /* the judge's verdict */

    Tenant *neigh = &tw.tenants[tw.tenant_count++];
    *neigh = (Tenant){0};
    neigh->type = ITEM_OFFICE; neigh->floor = 3; neigh->x = 110; neigh->width = 9;
    neigh->state = TENANT_OCCUPIED;
    neigh->demand_category = 2;                          /* content */

    Tenant *condo = &tw.tenants[tw.tenant_count++];
    *condo = (Tenant){0};
    condo->type = ITEM_CONDO; condo->floor = 4; condo->x = 100; condo->width = 16;
    condo->state = TENANT_OCCUPIED; condo->rent_class = 1;
    condo->demand_category = 0;

    Tenant *hotel = &tw.tenants[tw.tenant_count++];
    *hotel = (Tenant){0};
    hotel->type = ITEM_HOTEL_TWIN; hotel->floor = 5; hotel->x = 100; hotel->width = 6;
    hotel->state = TENANT_OCCUPIED; hotel->condition = ROOM_CLEAN;
    hotel->demand_category = 0;            /* even a damned verdict is exempt */

    long money0 = tw.money;
    game_stressed_moveout(&sim, &tw);

    CHECK(off->state == TENANT_ABANDONED, "stressed office moves out");
    CHECK(!off->burned, "a move-out leaves a vacant unit, not rubble");
    CHECK(condo->state == TENANT_ABANDONED, "stressed condo moves out");
    CHECK(tw.money == money0 - tenant_rent(ITEM_CONDO, 1),
          "the condo departure charges the rate-class buy-back ($150k avg)");
    CHECK(hotel->state == TENANT_OCCUPIED,
          "hotel rooms are exempt (their lifecycle is the 5PM pass)");
    CHECK(neigh->state == TENANT_OCCUPIED && neigh->demand_category == 1,
          "decline drags a content floor-mate to the middle band");

    /* A content office is untouched (the June inversion would have grown it) */
    fresh();
    tw.tenant_count = 0;
    Tenant *ok = &tw.tenants[tw.tenant_count++];
    *ok = (Tenant){0};
    ok->type = ITEM_OFFICE; ok->floor = 3; ok->x = 100; ok->width = 9;
    ok->state = TENANT_OCCUPIED; ok->cap_peak = CAP_PEAK_LOW;
    ok->demand_category = 2;
    game_stressed_moveout(&sim, &tw);
    CHECK(ok->state == TENANT_OCCUPIED && ok->cap_peak == CAP_PEAK_LOW,
          "content tenants: no move-out, and no invented tier growth");
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

    game_update_event(&sim, &tw);         /* 1 tick = 1 EXE frame, raw */
    CHECK(tenant(off5a) != NULL, "sanity: origin-floor office exists");
    CHECK(sim.event.fire_left[fi6] < 0 && sim.event.fire_right[fi6] < 0,
          "floor above not yet ignited");

    for (int i = 0; i < 85; i++) game_update_event(&sim, &tw);   /* past frame 80 */
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

/* Venues (VenueT seg_1180): show cycle, aging quotas, tiered income. */
static void test_venues(void)
{
    printf("venues (show cycle, film aging, tiered income):\n");
    fresh();
    uint16_t cin = fplace(ITEM_CINEMA, 3, 100);
    Tenant *c = tenant(cin);
    c->state = TENANT_OCCUPIED;
    c->movie_id = 9;                       /* a hit film */
    c->venue_age_days = 0;

    sim.hour = 10; game_venue_hourly(&sim, &tw);
    CHECK(c->quota_matinee == 60 && c->quota_evening == 60,
          "fresh hit film seats 60 per showing");
    CHECK(c->venue_age_days == 1 && c->patrons_today == 0,
          "10AM reset ages the film and clears attendance");

    sim.hour = 12; game_venue_hourly(&sim, &tw);
    CHECK(c->venue_state == 1, "noon: doors open for the matinee");

    /* patrons walk in (via the people sim's arrival feed) */
    for (int k = 0; k < 70; k++) {
        sim.people.venue_arrival_tenant[0] = cin;
        sim.people.venue_arrivals = 1;
        game_venue_arrivals(&sim, &tw);
    }
    CHECK(c->patrons_today == 70 && c->venue_state == 2,
          "arrivals count (70 of 120 seats) and the house shows patrons");

    sim.hour = 13; game_venue_hourly(&sim, &tw);
    CHECK(c->venue_state == 3, "1PM: the matinee runs");

    for (int k = 0; k < 80; k++) {         /* evening crowd, hits the cap */
        sim.people.venue_arrival_tenant[0] = cin;
        sim.people.venue_arrivals = 1;
        game_venue_arrivals(&sim, &tw);
    }
    CHECK(c->patrons_today == 120, "attendance caps at the two quotas");

    long money0 = tw.money;
    sim.hour = 20; game_venue_hourly(&sim, &tw);
    CHECK(tw.money == money0 + 15000,
          "a 120-patron day banks the $15,000 tier at 8PM");
    CHECK(c->venue_state == 0, "the theater closes");

    /* An old film seats 20 per showing -> 40 patrons max -> the $2k tier */
    c->venue_age_days = 9;
    sim.hour = 10; game_venue_hourly(&sim, &tw);
    CHECK(c->quota_matinee == 20, "a 9-day-old film seats only 20");
    c->patrons_today = 39;
    money0 = tw.money;
    sim.hour = 20; game_venue_hourly(&sim, &tw);
    CHECK(tw.money == money0, "under 40 patrons pays nothing");
    c->patrons_today = 40;
    sim.hour = 20; game_venue_hourly(&sim, &tw);
    CHECK(tw.money == money0 + 2000, "40 patrons reach the $2,000 tier");

    /* Party hall: flat 50 guests, income + close at 5PM */
    fresh();
    uint16_t ph = fplace(ITEM_PARTY_HALL, 3, 100);
    Tenant *p = tenant(ph);
    p->state = TENANT_OCCUPIED; p->movie_id = 0xFF;
    sim.hour = 10; game_venue_hourly(&sim, &tw);
    CHECK(p->quota_matinee == 0 && p->quota_evening == 50,
          "party hall summons a flat 50 guests, evening only");
    sim.hour = 13; game_venue_hourly(&sim, &tw);
    CHECK(p->venue_state == 1, "party hall opens at 1PM");
    p->patrons_today = 50;
    money0 = tw.money;
    sim.hour = 17; game_venue_hourly(&sim, &tw);
    CHECK(tw.money == money0 + 2000 && p->venue_state == 0,
          "the party banks its tier and closes at 5PM");
}

/* In-tenant occupants (AnimPeple seg_1028): counts, frame bands and the
 * fixed desk positions per the EXE's randomizers. */
static int tindex(uint16_t id)
{
    for (int i = 0; i < tw.tenant_count; i++)
        if (tw.tenants[i].id == id) return i;
    return 0;
}

static void test_occupants(void)
{
    printf("in-tenant occupant sprites (AnimPeple):\n");
    fresh();
    uint16_t off = fplace(ITEM_OFFICE, 3, 100);
    uint16_t con = fplace(ITEM_CONDO, 4, 100);
    uint16_t twn = fplace(ITEM_HOTEL_TWIN, 5, 100);
    uint16_t sgl = fplace(ITEM_HOTEL_SINGLE, 5, 130);
    uint16_t rst = fplace(ITEM_RESTAURANT, 6, 100);
    Tenant *o = tenant(off), *c = tenant(con), *tw2 = tenant(twn),
           *sg = tenant(sgl), *r = tenant(rst);
    o->state = c->state = tw2->state = sg->state = r->state = TENANT_OCCUPIED;
    tw2->hosted = 1; sg->hosted = 1; r->retail_open = 1;
    sim.time_of_day = TOD_MORNING; sim.hour = 10;

    /* roll every tenant once (the stagger passes each index within 16) */
    for (int f = 0; f < 64; f++) { sim.frame = f; game_animate_occupants(&sim, &tw); }  /* one full re-roll period (the 64-frame wall-clock stagger) */

    TenantOccupants *oo = &sim.occupants[tindex(off)];
    CHECK(oo->count == 6, "an office at work shows 6 workers");
    int inband = 1;
    for (int k = 0; k < 6; k++) if (oo->frame[k] > 0x1D) inband = 0;
    CHECK(inband, "office frames stay in the office band (<= 0x1D)");
    if (o->id % 6 < 2)
        CHECK(oo->x[0] == o->id % 6 + 1, "desk-sitter 0 at its fixed desk");
    else
        CHECK(oo->x[0] == 7, "layout-B sitter at x=7");

    CHECK(sim.occupants[tindex(con)].count == 3, "a condo houses 3 residents");
    CHECK(sim.occupants[tindex(twn)].count == 3 && sim.occupants[tindex(sgl)].count == 2,
          "twin rooms sleep 3 sprites, singles 2");
    TenantOccupants *ro = &sim.occupants[tindex(rst)];
    CHECK(ro->count == 2 && ro->frame[0] >= 0x49 && ro->frame[0] <= 0x4B &&
          ro->x[0] <= 2, "restaurant staff behind the counter, waiter band");

    /* presence gates: night office, unhosted room, closed restaurant */
    sim.time_of_day = TOD_NIGHT; sim.hour = 23;
    tw2->hosted = 0; r->retail_open = 0;
    for (int f = 0; f < 64; f++) { sim.frame = f; game_animate_occupants(&sim, &tw); }  /* one full re-roll period (the 64-frame wall-clock stagger) */
    CHECK(sim.occupants[tindex(off)].count == 0, "offices empty out at night");
    CHECK(sim.occupants[tindex(twn)].count == 0, "an unhosted room shows nobody");
    CHECK(sim.occupants[tindex(rst)].count == 0, "closed restaurants are unstaffed");
    CHECK(sim.occupants[tindex(con)].count == 3, "condo residents are home at night");

    /* construction worker band = exactly the 6 frames of 0x85EA */
    uint16_t bld = fplace(ITEM_OFFICE, 7, 100);
    tenant(bld)->state = TENANT_CONSTRUCTION;
    for (int f = 0; f < 64; f++) { sim.frame = f; game_animate_occupants(&sim, &tw); }  /* one full re-roll period (the 64-frame wall-clock stagger) */
    TenantOccupants *bo = &sim.occupants[tindex(bld)];
    CHECK(bo->count == 1 && bo->frame[0] >= 0x39 && bo->frame[0] <= 0x3E,
          "every construction site gets one worker (frames 0x39-0x3E)");
}

/* Change-movie (InfoDlgT 1100:432f/4377): deterministic rotation within
 * the chosen tier, film fresh at age 0, $300k hit / $150k ordinary. */
static void test_change_movie(void)
{
    printf("change-movie dialog:\n");
    fresh();
    uint16_t cin = fplace(ITEM_CINEMA, 3, 100);
    Tenant *c = tenant(cin);
    c->state = TENANT_OCCUPIED;
    c->movie_id = 3; c->venue_age_days = 6;

    long money0 = tw.money;
    game_change_movie(&sim, &tw, c, 1);
    CHECK(c->movie_id == 11 && c->venue_age_days == 0,
          "hit button steps to the next hit (7 + (id+1) mod 7), fresh");
    CHECK(tw.money == money0 - 300000, "a hit film costs $300,000");

    c->movie_id = 13;                       /* last hit wraps */
    game_change_movie(&sim, &tw, c, 1);
    CHECK(c->movie_id == 7, "hit rotation wraps 13 -> 7");
    money0 = tw.money;
    game_change_movie(&sim, &tw, c, 0);
    CHECK(c->movie_id == 1 && tw.money == money0 - 150000,
          "ordinary button: (id+1) mod 7 for $150,000");

    /* The fresh film's draw shows up at the next 10AM reset. */
    c->movie_id = 9; c->venue_age_days = 9;   /* stale hit: 20/showing */
    sim.hour = 10; game_venue_hourly(&sim, &tw);
    CHECK(c->quota_matinee == 20, "stale film seats 20");
    game_change_movie(&sim, &tw, c, 1);
    sim.hour = 10; game_venue_hourly(&sim, &tw);
    CHECK(c->quota_matinee == 60, "the new hit seats 60 from tomorrow");

    /* Guard: not a theater -> no-op, no charge */
    uint16_t off = fplace(ITEM_OFFICE, 4, 100);
    money0 = tw.money;
    game_change_movie(&sim, &tw, tenant(off), 1);
    CHECK(tw.money == money0, "non-theater tenants are a no-op");
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
    /* Put the sim clock at 10AM for real (ft 240) — fresh() seeds the
     * EXE new-game start 4:59AM (ft 2533), from where a 4PM jump would
     * be backward and refused. */
    sim.hour = 10;
    sim.quarter = 0; sim.tick = game_clock_to_tick(10, 0);
    sim.income_this_quarter = 777;    /* proves the jump closes the books */
    int caught_at = -1;
    for (int t = 0; t < 150 && caught_at < 0; t++) {
        game_update_event(&sim, &tw);
        if (sim.event.caught) caught_at = t;
    }
    CHECK(caught_at >= 0, "sweep reaches the bomb cell - caught, no dice");
    CHECK(!sim.event.active && !sim.event.hunt.active, "everyone stands down");
    /* EXE EventCleanup: frame_time -> 0x5DC = 4:00 PM after a catch. */
    CHECK(sim.hour == 16 && sim.minute == 0,
          "the clock jumps to 4:00 PM on a catch");
    CHECK(sim.income_this_quarter == 0 && sim.day_income == 777,
          "quarter boundaries crossed by the jump still close out");

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
    /* Height is the LOCKED first-click choice ([0xB3E6]), not a row scan.
     * Reopen it (fresh()'s placements locked it at 1) and pick 2. */
    tw.lobby_height = 0;
    tower_choose_lobby_height(&tw, 2);
    CHECK(game_lobby_height(&tw) == 2, "chosen 2-story lobby -> height 2");
    tower_choose_lobby_height(&tw, 3);
    CHECK(game_lobby_height(&tw) == 2, "the choice is locked — 3 refused");

    /* A ground drag mirrors the upper rows of a grand lobby, free. */
    fresh();
    tw.lobby_height = 0;
    tower_choose_lobby_height(&tw, 3);
    long before = tw.money;
    tower_place(&tw, ITEM_LOBBY, 0, 300);   /* extend past fresh()'s span */
    long charged3 = before - tw.money;
    int rows = 0, span_ok = 1;
    for (int i = 0; i < tw.tenant_count; i++) {
        Tenant *l = &tw.tenants[i];
        if (l->type != ITEM_LOBBY || l->floor < 1 || l->floor > 2) continue;
        rows++;
        if (l->x != 100 || l->width != 204) span_ok = 0;
    }
    CHECK(rows == 2, "3-story lobby drag builds the two upper rows");
    CHECK(span_ok, "upper rows span the full ground lobby");
    CHECK(game_lobby_height(&tw) == 3, "grand lobby height reads 3");
    /* Band pricing: ground cells charge the lobby row x height ([0xB3E6]
     * via TerrainCost), so the same extension costs 3x under a 3-story
     * lobby — the upper rows themselves add nothing on top. */
    fresh();
    before = tw.money;
    tower_place(&tw, ITEM_LOBBY, 0, 300);
    CHECK(charged3 == 3 * (before - tw.money),
          "3-story lobby extension costs exactly 3x the 1-story one");

    /* Grand-lobby tall stairs (kinds 2-5, StairsT 11f8:1461/10c0): a
     * stair placed with its span inside a 3-story lobby snaps to ONE
     * tall unit, ground..H, and reachability crosses it end to end. */
    fresh();
    tw.lobby_height = 0;
    tower_choose_lobby_height(&tw, 3);
    tower_place(&tw, ITEM_LOBBY, 0, 300);        /* mirrors rows 1-2 */
    for (int cx = 100; cx < 120; cx += 4) place(ITEM_FLOOR, 3, cx);
    uint16_t ts = tower_place(&tw, ITEM_STAIRS, 1, 104);
    Tenant *tst = tower_tenant(&tw, ts);
    CHECK(tst && tst->floor == 0 && tst->height == 4,
          "stairs in the lobby span promote to one ground..3 tall unit");
    CHECK(!tower_can_place(&tw, ITEM_ESCALATOR, 2, 104),
          "a second unit on the same footprint is refused (dedupe)");
    uint16_t off3 = place(ITEM_OFFICE, 3, 110);
    game_update_reachability(&sim, &tw);
    CHECK(sim.reach_public[floor_to_index(3)],
          "floor 3 is publicly reachable across the tall stairs");
    (void)off3;
    /* kind round-trips through the .TDT block: 3-story stairs = kind 5 */
    {
        const char *tp = "/tmp/test_tallstair.tdt";
        static Tower tw2; static GameSim sim2;
        char err[128] = "";
        CHECK(twr_export(tp, &tw, &sim, err, sizeof err) == 0,
              "tall-stair export ok");
        CHECK(twr_import(tp, &tw2, &sim2, err, sizeof err) == 0,
              "tall-stair import ok");
        int found = 0;
        for (int i = 0; i < tw2.tenant_count; i++)
            if (tw2.tenants[i].type == ITEM_STAIRS &&
                tw2.tenants[i].height == 4 && tw2.tenants[i].floor == 0)
                found = 1;
        CHECK(found, "tall stairs survive the .TDT round-trip (kind 5)");
        CHECK(tw2.lobby_height == 3, "lobby height survives the round-trip");
    }

    /* The REAL medical mechanic (MoreMedicalPlease): a sick worker seeks a
     * center. There is no "medical emergency" event — that was a fabrication,
     * deleted (referee_medical_reconcile_2026-07-13). */
    fresh();
    sim.medical_adequate = 1;
    Tenant *o = &tw.tenants[tw.tenant_count++];
    *o = (Tenant){0}; o->type = ITEM_OFFICE; o->floor = 6; o->state = TENANT_OCCUPIED;
    int r = game_medical_seek(&sim, &tw, 6);
    CHECK(r == 0, "sick worker, no medical center -> not found");
    CHECK(!sim.medical_adequate && sim.medical_nag,
          "no center clears medical adequacy and raises the shortage nag");

    /* With a reachable medical center, the worker is admitted and adequacy holds. */
    fresh();
    sim.medical_adequate = 1;
    Tenant *med = &tw.tenants[tw.tenant_count++];
    *med = (Tenant){0}; med->type = ITEM_MEDICAL; med->floor = 6; med->state = TENANT_OCCUPIED;
    Tenant *o2 = &tw.tenants[tw.tenant_count++];
    *o2 = (Tenant){0}; o2->type = ITEM_OFFICE; o2->floor = 6; o2->state = TENANT_OCCUPIED;
    int r2 = game_medical_seek(&sim, &tw, 6);
    CHECK(r2 == 2, "sick worker with a reachable medical center -> admitted");
    CHECK(sim.medical_adequate, "adequacy stays set when a center is found");
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

    /* The bulldozer refuses units still under construction ("Cannot destroy
     * items under construction"). */
    Tenant *offt = tower_tenant(&tw, off);
    CHECK(offt->state == TENANT_CONSTRUCTION, "fresh office is under construction");
    CHECK(tower_remove(&tw, off) == 0, "bulldozer refuses mid-construction unit");

    /* Finish construction, then bulldozing works and leaves the build
     * floor behind, not bare dirt. */
    offt->construction = 0;
    offt->state = TENANT_OCCUPIED;
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

    /* Security is in the EXE's indestructible set (CanModifyTenant table)
     * along with housekeeping, metro, cathedral and every lobby. */
    uint16_t sec = place(ITEM_SECURITY, 1, 120);
    Tenant *sect = tower_tenant(&tw, sec);
    sect->construction = 0;
    sect->state = TENANT_OCCUPIED;
    CHECK(tower_remove(&tw, sec) == 0, "bulldozer refuses security");
    CHECK(strcmp(tower_reject_reason(), "Cannot destroy this item") == 0,
          "indestructible reason");
}

static void test_build_caps(void)
{
    printf("\n-- build caps and singletons (res 0x3eb) --\n");
    fresh();

    /* 24 elevator groups max: the 25th NEW shaft is rejected at build
     * (seg_11f8 slot scan), while extending an existing shaft still works. */
    int made = 0;
    for (int i = 0; i < 24; i++)
        if (place(ITEM_ELEVATOR_SHAFT, 1, 4 + i * 13)) made++;
    CHECK(made == 24, "24 standard shafts placeable");
    CHECK(tower_shaft_group_count(&tw) == 24, "collector sees 24 groups");
    CHECK(tower_can_place(&tw, ITEM_ELEVATOR_SHAFT, 1, 4 + 24 * 13) == 0,
          "25th shaft rejected");
    CHECK(strcmp(tower_reject_reason(),
                 "No more elevator shafts available") == 0,
          "player-facing shaft-cap reason");
    CHECK(tower_can_place(&tw, ITEM_ELEVATOR_SHAFT, 2, 4) == 1,
          "extending shaft 1 upward is not a new group");

    /* Metro: singleton, virgin-ground platform, nothing at or below the
     * platform level afterwards. */
    fresh();
    place(ITEM_FLOOR, 0, 100);
    uint16_t m1 = place(ITEM_METRO, -3, 105);
    CHECK(m1 != 0, "first metro placeable");
    CHECK(tower_can_place(&tw, ITEM_METRO, -3, 140) == 0,
          "second metro rejected");
    CHECK(strcmp(tower_reject_reason(), "Only one Metro Station allowed") == 0,
          "metro singleton reason");
    CHECK(tower_can_place(&tw, ITEM_RAMP, -3, 240) == 0,
          "nothing may sit at the platform level");
    CHECK(strcmp(tower_reject_reason(), "Cannot place items under Metro") == 0,
          "under-metro reason");

    fresh();
    place(ITEM_FLOOR, 0, 100);
    fplace(ITEM_FLOOR, -3, 105);   /* pre-excavated ground where the platform wants to go */
    CHECK(tower_can_place(&tw, ITEM_METRO, -3, 105) == 0,
          "platform must sit in virgin ground");
    CHECK(strcmp(tower_reject_reason(), "Place Metro station on bottom floor") == 0,
          "metro bottom-floor reason");

    /* Cathedral: pinned to a base at exactly F100, $3M, singleton. */
    CHECK(tower_can_place(&tw, ITEM_CATHEDRAL, 1, 135) == 0,
          "cathedral off the 100th floor rejected");
    CHECK(strcmp(tower_reject_reason(),
                 "Cathedral is available only on 100th floor") == 0,
          "cathedral floor reason");
    CHECK(ITEM_COST[ITEM_CATHEDRAL] == 3000000 &&
          ITEM_COST[ITEM_HOUSEKEEPING] == 50000,
          "cathedral $3M / housekeeping $50k (cost res 0x3e8)");
    fplace(ITEM_CATHEDRAL, 100, 105);
    CHECK(tower_can_place(&tw, ITEM_CATHEDRAL, 100, 140) == 0,
          "second cathedral rejected");
    CHECK(strcmp(tower_reject_reason(), "Only one Cathedral allowed") == 0,
          "cathedral singleton reason");

    /* Fixed table: 64 stairs + escalators COMBINED (StairsT trace: one
     * shared record array). Inject counter fodder directly — the cap
     * check only counts tenant types. */
    fresh();
    for (int i = 0; i < 64; i++) {
        Tenant *t = &tw.tenants[tw.tenant_count++];
        memset(t, 0, sizeof *t);
        t->id = tw.next_tenant_id++;
        t->type = (i < 40) ? ITEM_STAIRS : ITEM_ESCALATOR;
    }
    CHECK(tower_can_place(&tw, ITEM_STAIRS, 1, BX) == 0,
          "65th walk transport rejected (stairs)");
    CHECK(strcmp(tower_reject_reason(), "No more stairs available") == 0,
          "stairs cap reason");
    CHECK(tower_can_place(&tw, ITEM_ESCALATOR, 1, BX) == 0,
          "escalators share the same 64-record table");
    CHECK(strcmp(tower_reject_reason(), "No more escalators available") == 0,
          "escalator cap reason");

    fresh();
    for (int i = 0; i < 16; i++) {
        Tenant *t = &tw.tenants[tw.tenant_count++];
        memset(t, 0, sizeof *t);
        t->id = tw.next_tenant_id++;
        t->type = (i & 1) ? ITEM_CINEMA : ITEM_PARTY_HALL;
    }
    place(ITEM_FLOOR, 0, 100);
    CHECK(tower_can_place(&tw, ITEM_CINEMA, 1, 105) == 0,
          "17th venue rejected");
    CHECK(tower_can_place(&tw, ITEM_PARTY_HALL, 1, 105) == 0,
          "party hall counts against the same 16-record table");

    /* Escalator landing whitelist (StairsT validators 10c0:0775/087d):
     * bare deck is legal ("commercial floors" was never floor zoning);
     * a landing inside a condo is not; inside a shop it is. */
    fresh();
    place(ITEM_FLOOR, 1, BX);
    place(ITEM_FLOOR, 2, BX);
    CHECK(tower_can_place(&tw, ITEM_ESCALATOR, 1, BX + 20) == 1,
          "escalator between two bare built floors is legal");
    place(ITEM_CONDO, 2, BX + 20);
    CHECK(tower_can_place(&tw, ITEM_ESCALATOR, 1, BX + 20) == 0,
          "escalator exit inside a condo rejected");
    CHECK(strcmp(tower_reject_reason(),
                 "Escalators available only at commercial spaces") == 0,
          "escalator whitelist reason");
    uint16_t shop = place(ITEM_SHOP, 2, BX + 40);
    force_occupied(shop);
    CHECK(tower_can_place(&tw, ITEM_ESCALATOR, 1, BX + 40) == 1,
          "escalator exit inside a shop is legal");

    /* Half-tile overlap: same floor pair 4 cells apart is legal; 2 cells
     * apart collides (10c0:0983). */
    place(ITEM_STAIRS, 1, BX);
    CHECK(tower_can_place(&tw, ITEM_STAIRS, 1, BX + 4) == 1,
          "second stair 4 cells over on the same floors is legal");
    CHECK(tower_can_place(&tw, ITEM_STAIRS, 1, BX + 2) == 0,
          "stair 2 cells over collides half-tile");

    /* Parking ramps: one vertical stack, rooted on B1 (pass-3 trace
     * 11f8:0aa0 — [0xB3EE] column, errors 0x1F/0x20). */
    fresh();
    CHECK(tower_can_place(&tw, ITEM_RAMP, -2, 120) == 0 &&
          strcmp(tower_reject_reason(),
                 "Parking Ramps must connect to the 1st floor") == 0,
          "first ramp must sit on B1");
    uint16_t rb1 = place(ITEM_RAMP, -1, 120);
    CHECK(rb1 != 0, "B1 ramp placed");
    CHECK(tower_can_place(&tw, ITEM_RAMP, -2, 140) == 0 &&
          strcmp(tower_reject_reason(),
                 "Parking Ramps must be connected vertically") == 0,
          "later ramps must share the first ramp's column");
    CHECK(tower_can_place(&tw, ITEM_RAMP, -2, 120) == 1,
          "same-column B2 ramp is legal");

    /* Fixed caps: security and medical stop at 10 (dispatch handlers
     * 0xc0d/0xc46); commercial types share a 512-record table. */
    fresh();
    for (int i = 0; i < 10; i++) {
        Tenant *t = &tw.tenants[tw.tenant_count++];
        memset(t, 0, sizeof *t);
        t->id = tw.next_tenant_id++;
        t->type = ITEM_SECURITY;
    }
    place(ITEM_FLOOR, 1, 100);
    CHECK(tower_can_place(&tw, ITEM_SECURITY, 1, 105) == 0 &&
          strcmp(tower_reject_reason(), "Item no longer available") == 0,
          "11th security office rejected");
    CHECK(tower_can_place(&tw, ITEM_OFFICE, 1, 105) == 1,
          "the security cap doesn't leak onto other types");
    for (int i = 0; i < 512; i++) {
        Tenant *t = &tw.tenants[tw.tenant_count++];
        memset(t, 0, sizeof *t);
        t->id = tw.next_tenant_id++;
        t->type = (i % 3 == 0) ? ITEM_RESTAURANT
                : (i % 3 == 1) ? ITEM_SHOP : ITEM_FAST_FOOD;
    }
    CHECK(tower_can_place(&tw, ITEM_SHOP, 1, 105) == 0 &&
          strcmp(tower_reject_reason(), "Item no longer available") == 0,
          "513th commercial unit rejected (shared 512 table)");
}

static void test_route_loss(void)
{
    printf("\n-- route-loss confirmations (res 0x3ed detectors) --\n");
    fresh();
    for (int f = 1; f <= 3; f++) place(ITEM_FLOOR, f, BX);
    place(ITEM_OFFICE, 3, BX + 6);
    for (int f = 0; f <= 3; f++) place(ITEM_ELEVATOR_SHAFT, f, 196);
    people_rebuild_transport(&sim.people, &tw);
    int f3 = floor_to_index(3);
    CHECK(sim.people.shaft_count == 1, "one shaft collected");
    CHECK(game_stop_route_loss(&sim, &tw, 0, f3) == 1,
          "only stop on floor 3 -> key-route warning (#1)");
    CHECK(game_remove_route_loss(&sim, &tw, 0, f3) == 1,
          "removing the segment strands floor 3 (#5)");

    /* Stairs touching the floor silence both (the EXE walkmap exemption —
     * it never asks where the stairs lead). Clear of the shaft's columns. */
    place(ITEM_STAIRS, 2, BX + 30);
    CHECK(game_stop_route_loss(&sim, &tw, 0, f3) == 0,
          "adjacent stairs silence the stop warning");
    CHECK(game_remove_route_loss(&sim, &tw, 0, f3) == 0,
          "adjacent stairs silence the removal warning");

    /* A second same-type shaft: redundant stop on a plain floor is silent;
     * on a lobby floor it's the shared-transfer-slot warning (#3). */
    for (int f = 0; f <= 3; f++) place(ITEM_ELEVATOR_SHAFT, f, 220);
    people_rebuild_transport(&sim.people, &tw);
    CHECK(sim.people.shaft_count == 2, "two shafts collected");
    int f1 = floor_to_index(1), f0 = floor_to_index(0);
    CHECK(game_stop_route_loss(&sim, &tw, 0, f1) == 0,
          "another same-type stop makes the toggle silent");
    CHECK(game_stop_route_loss(&sim, &tw, 0, f0) == 3,
          "shared lobby stop warns about the Lobby route (#3)");

    /* Service elevators: stairs never excuse them, and the two networks
     * don't cover for each other. */
    for (int f = 0; f <= 3; f++) place(ITEM_ELEVATOR_SERVICE, f, 244);
    people_rebuild_transport(&sim.people, &tw);
    CHECK(sim.people.shaft_count == 3, "service shaft collected");
    CHECK(game_stop_route_loss(&sim, &tw, 2, f3) == 2,
          "service stop warns housekeeping (#2) despite stairs");
    CHECK(game_remove_route_loss(&sim, &tw, 2, f3) == 1,
          "public shafts don't cover the service network");
}

static void test_art_styles(void)
{
    printf("art styles (build rotation 0x794C.., record word +6):\n");
    fresh();
    tower_extend_deck(&tw, 1, 100, 170);
    int seq_ok = 1;
    for (int i = 0; i < 7; i++) {
        uint16_t o = place(ITEM_OFFICE, 1, 100 + i * 9);
        if (!o || tenant(o)->style != (uint8_t)(i % 6)) seq_ok = 0;
    }
    CHECK(seq_ok, "offices cycle 6 furniture styles in strict order");

    tower_extend_deck(&tw, 2, 100, 140);
    uint16_t tw0 = place(ITEM_HOTEL_TWIN, 2, 100);
    place(ITEM_HOTEL_TWIN, 2, 106);
    place(ITEM_HOTEL_TWIN, 2, 112);
    place(ITEM_HOTEL_TWIN, 2, 118);
    uint16_t tw4 = place(ITEM_HOTEL_TWIN, 2, 124);
    CHECK(tenant(tw0) && tenant(tw4) &&
          tenant(tw0)->style == 0 && tenant(tw4)->style == 0,
          "twin rotation wraps at 4 styles");

    /* The EXE zeroes the counters on load; stamped styles survive */
    const char *sv = "/tmp/ct_styletest.sav";
    game_save(&sim, &tw, sv);
    tw.style_ctr[3] = 5;               /* dirty the office counter */
    CHECK(game_load(&sim, &tw, sv) == 0, "save/load round-trip");
    CHECK(tw.style_ctr[3] == 0, "rotation counters reset on load");
    int styles_ok = 1, k = 0;
    for (int i = 0; i < tw.tenant_count; i++)
        if (tw.tenants[i].type == ITEM_OFFICE) {
            if (tw.tenants[i].style != (uint8_t)(k % 6)) styles_ok = 0;
            k++;
        }
    CHECK(styles_ok && k == 7, "per-tenant styles survive the load");

    /* .TDT round-trip: style word +6, hotel condition at STATUS +5 (not
     * the money byte), rent class at +0x10 (not the judge byte) */
    Tenant *room = tenant(tw0);
    room->condition = ROOM_INFESTED;
    room->rent_class = 2;
    uint8_t want_style = tenant(tw4)->style;
    char err[128];
    CHECK(twr_export("/tmp/ct_styletest.tdt", &tw, &sim, err, sizeof err) == 0,
          "export with styles");
    fresh();
    CHECK(twr_import("/tmp/ct_styletest.tdt", &tw, &sim, err, sizeof err) == 0,
          "re-import");
    Tenant *back = NULL, *back4 = NULL;
    int twins = 0;
    for (int i = 0; i < tw.tenant_count; i++)
        if (tw.tenants[i].type == ITEM_HOTEL_TWIN) {
            if (twins == 0) back = &tw.tenants[i];
            back4 = &tw.tenants[i];
            twins++;
        }
    CHECK(back && back->condition == ROOM_INFESTED,
          "hotel condition round-trips through status byte +5");
    CHECK(back && back->rent_class == 2,
          "rent class round-trips through +0x10");
    CHECK(back4 && twins == 5 && back4->style == want_style,
          "art styles round-trip through record word +6");
}

static void test_construct_queue(void)
{
    printf("construction queue (ConstructQ 11f0:004b):\n");
    fresh();
    tower_extend_deck(&tw, 1, 110, 174);
    uint16_t h[11];
    for (int i = 0; i < 11; i++)
        h[i] = place(ITEM_HOTEL_SINGLE, 1, 110 + i * 4);
    int building = 0;
    for (int i = 0; i < 11; i++)
        if (tenant(h[i]) && tenant(h[i])->state == TENANT_CONSTRUCTION) building++;
    CHECK(building == 11, "11 jobs start under construction");
    for (int i = 0; i < 8; i++) game_update(&sim, &tw);  /* tenant pass is tick%4 */
    CHECK(tenant(h[0])->state != TENANT_CONSTRUCTION,
          "11th placement force-completes the oldest job instantly");
    building = 0;
    for (int i = 1; i < 11; i++)
        if (tenant(h[i])->state == TENANT_CONSTRUCTION) building++;
    CHECK(building == 10, "the other 10 keep their build timers");
}

static void test_person_names(void)
{
    printf("person naming (NameT seg_1188):\n");
    fresh();
    place(ITEM_FLOOR, 1, BX);
    uint16_t office = place(ITEM_OFFICE, 1, BX);
    uint16_t hotel  = place(ITEM_HOTEL_SINGLE, 1, BX + 20);
    uint16_t shop   = place(ITEM_SHOP, 1, BX + 30);

    CHECK(tower_person_name_set(&tw, office, 0, "Bud Fox") == 0 &&
          strcmp(tower_person_name(&tw, office, 0), "Bud Fox") == 0,
          "name a person and read it back");
    CHECK(tower_person_name_set(&tw, office, 0, "Gekko") == 0 &&
          strcmp(tower_person_name(&tw, office, 0), "Gekko") == 0 &&
          tw.person_name_count == 1,
          "rename reuses the slot in place");

    /* 20-slot cap: the 21st distinct person is refused */
    for (int m = 1; m < 20; m++)
        tower_person_name_set(&tw, office, m, "Filler");
    CHECK(tw.person_name_count == 20, "registry holds 20 names");
    CHECK(tower_person_name_set(&tw, hotel, 0, "One Too Many") < 0,
          "21st name refused (You may only name 20 people.)");
    for (int m = 10; m < 20; m++)
        tower_person_name_clear(&tw, office, m);
    CHECK(tw.person_name_count == 10, "clearing frees slots");

    /* Purges: hotel-guest names at 4PM, visitor names at the day
     * boundary, dead-tenant names always */
    tower_person_name_set(&tw, hotel, 1, "Guest");
    tower_person_name_set(&tw, shop, 1, "Shopper");
    game_purge_person_names(&tw, 1, 0);
    CHECK(tower_person_name(&tw, hotel, 1) == NULL &&
          tower_person_name(&tw, shop, 1) != NULL,
          "4PM purge drops hotel-guest names only");
    game_purge_person_names(&tw, 0, 1);
    CHECK(tower_person_name(&tw, shop, 1) == NULL &&
          tower_person_name(&tw, office, 0) != NULL,
          "day-boundary purge drops visitor names, workers keep theirs");
    force_occupied(office);   /* under-construction units refuse the bulldozer */
    tower_remove(&tw, office);
    game_purge_person_names(&tw, 0, 0);
    CHECK(tower_person_name(&tw, office, 0) == NULL,
          "demolishing the tenant drops its people's names");
}

static void test_deck_economics(void)
{
    printf("floor-deck economics (MoneyT TerrainCost 1178:0583):\n");
    fresh();   /* ground lobby spans x=100..284 */

    /* Floor tool: per-cell, charging only cells outside the extent */
    long before = tw.money;
    CHECK(tower_extend_deck(&tw, 1, 150, 213), "deck tool lays a 63-cell strip");
    CHECK(before - tw.money == 63L * 500, "63 fresh cells cost $31,500");

    before = tw.money;
    CHECK(tower_extend_deck(&tw, 1, 150, 213), "re-covering the span succeeds");
    CHECK(before - tw.money == 0, "cells inside the extent are free");

    before = tw.money;
    CHECK(tower_extend_deck(&tw, 1, 140, 160), "extending the strip leftward");
    CHECK(before - tw.money == 10L * 500, "only the 10 new cells are charged");

    /* No excavation premium: basement deck is the same $500/cell */
    before = tw.money;
    CHECK(tower_extend_deck(&tw, -1, 150, 213), "basement deck under the lobby");
    CHECK(before - tw.money == 63L * 500, "basement cells cost $500 too");

    /* Decks grow only over decks */
    CHECK(!tower_extend_deck(&tw, 2, 100, 120),
          "floor 2 deck can't overhang floor 1");

    /* Items pay deck cost for overhang + gap cells (extent-union model) */
    before = tw.money;
    CHECK(place(ITEM_OFFICE, 1, 220) != 0, "office placed past the deck edge");
    CHECK(before - tw.money == 40000L + (229 - 213) * 500L,
          "office pays item + deck for the 16 gap/footprint cells");

    /* Shafts: new column pays the item; extension segments pay deck only
     * (ExtendUp/Down charge pure TerrainCost — free through built floors,
     * auto-deck stubs charged past the tower). */
    before = tw.money;
    CHECK(place(ITEM_ELEVATOR_SHAFT, 0, 200) != 0, "new shaft on the lobby");
    CHECK(before - tw.money == 200000L, "new shaft = $200k, no deck charge");
    before = tw.money;
    CHECK(place(ITEM_ELEVATOR_SHAFT, 1, 200) != 0, "extend through built floor 1");
    CHECK(before - tw.money == 0, "extension through existing deck is free");
    before = tw.money;
    CHECK(place(ITEM_ELEVATOR_SHAFT, 2, 200) != 0, "extend past the built tower");
    CHECK(before - tw.money == 4L * 500,
          "auto-deck stub = shaft footprint x $500");

    /* The broke message is the floor-specific one */
    tw.money = 1000;
    CHECK(!tower_extend_deck(&tw, 1, 229, 250) &&
          strcmp(tower_reject_reason(), "Not enough money to build floor") == 0,
          "deck refusal says 'Not enough money to build floor'");
    tw.money = 100000000L;

    /* Ground lobby: no item cost — $5,000 x band height per newly decked
     * cell through TerrainCost (converting existing deck stays free) */
    before = tw.money;
    CHECK(tower_place(&tw, ITEM_LOBBY, 0, 90) != 0, "extend the ground lobby left");
    CHECK(before - tw.money == 10L * 5000,
          "10 new lobby cells (gap included) = $50,000");
}

/* Transfer chain/slot tables (TransferT seg_11b0 low half, referee
 * 2026-08-02): slot-resolved transfers at a sky lobby, ride-then-walk
 * chain finishes, the one-transfer depth limit, and the rebuild stamp
 * folding serviced flags + lobby spans. */
static void test_transfer_tables(void)
{
    printf("transfer chain/slot tables (TransferT 11b0):\n");

    /* (a) ride -> transfer at the sky lobby -> ride, via routing slots:
     * express ground..15 and standard 15..20, a sky-lobby tenant
     * overlapping both (width test: 6 express / 4 standard), office on
     * 18. Ground commuters must ride the express to 15, re-plan at the
     * transfer lobby, and finish on the standard shaft. */
    fresh();
    tower_import_item(&tw, ITEM_LOBBY, 15, 195, 30);
    for (int f = 0; f <= 15; f++) place(ITEM_ELEVATOR_EXPRESS, f, 200);
    for (int f = 15; f <= 20; f++) place(ITEM_ELEVATOR_SHAFT, f, 220);
    uint16_t office = fplace(ITEM_OFFICE, 18, 230);
    CHECK(office != 0, "office on 18 placed");
    force_occupied(office);
    game_update_reachability(&sim, &tw);
    people_rebuild_transport(&sim.people, &tw);
    CHECK(sim.people.shaft_count == 2, "express + standard collected");
    int f18 = floor_to_index(18);
    int arrived = 0;
    for (int frame = 0; frame < 8000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
        arrived = people_at(office, f18, PERSON_AT_DEST);
        if (arrived > 0 && sim.people.queued_now == 0 &&
            sim.people.riding_now == 0) break;
    }
    CHECK(arrived > 0, "ground -> express -> sky lobby -> standard -> f18");
    CHECK(sim.people.trips_failed == 0, "no failed trips on the slot route");

    /* Toggling the standard shaft's transfer stop OFF must rebuild the
     * tables (EXE: any serviced change -> 049f+00f2) and sever the
     * route home: the standard shaft leaves the sky-lobby slot, its
     * transfer mask empties, and floor-18 workers are stranded. */
    people_set_serviced(&sim.people, 1, floor_to_index(15), 0);
    long failed0 = sim.people.trips_failed;
    for (int frame = 8000; frame < 10000; frame++)
        people_update(&sim.people, &tw, frame, TOD_EVENING, 18,
                      sim.reach_public, sim.reach_service);
    CHECK(sim.people.trips_failed > failed0,
          "toggled-off transfer stop severs the route home");

    /* (b) ride-then-walk finish through a chain zone: office on 17,
     * express only (NO shaft serves 17), escalators 15->16->17. The
     * chain anchored at the 15-grid covers 15..17 and shares the
     * sky-lobby slot with the express, so the express transfer table
     * carries the chain bit at floor 17: ride to 15, walk the zone up.
     * (The referee's floor-17 example — the old port demanded a second
     * SHAFT serving `to` and could not route this at all.) */
    fresh();
    tower_import_item(&tw, ITEM_LOBBY, 15, 195, 30);
    for (int f = 0; f <= 15; f++) place(ITEM_ELEVATOR_EXPRESS, f, 200);
    fplace(ITEM_ESCALATOR, 15, 226);
    fplace(ITEM_ESCALATOR, 16, 226);
    office = fplace(ITEM_OFFICE, 17, 210);
    force_occupied(office);
    game_update_reachability(&sim, &tw);
    people_rebuild_transport(&sim.people, &tw);
    int f17 = floor_to_index(17);
    arrived = 0;
    for (int frame = 0; frame < 8000; frame++) {
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
        arrived = people_at(office, f17, PERSON_AT_DEST);
        if (arrived > 0) break;
    }
    CHECK(arrived > 0, "ride-then-walk: express to 15, chain walk to 17");
    CHECK(sim.people.trips_failed == 0, "chain finish is a real route");

    /* (c) two transfers = no-route (the tables are one transfer deep by
     * construction; the EXE has no transitive closure). Express 0..15,
     * standard 15..30, standard 30..45, sky lobbies at 15 and 30, office
     * on 40: ground -> 40 needs express -> B -> C, refused with the
     * 300-stress no-route verdict. */
    fresh();
    tower_import_item(&tw, ITEM_LOBBY, 15, 195, 30);
    tower_import_item(&tw, ITEM_LOBBY, 30, 214, 34);
    for (int f = 0; f <= 15; f++) place(ITEM_ELEVATOR_EXPRESS, f, 200);
    for (int f = 15; f <= 30; f++) place(ITEM_ELEVATOR_SHAFT, f, 220);
    for (int f = 30; f <= 45; f++) place(ITEM_ELEVATOR_SHAFT, f, 240);
    office = fplace(ITEM_OFFICE, 40, 250);
    force_occupied(office);
    game_update_reachability(&sim, &tw);
    people_rebuild_transport(&sim.people, &tw);
    CHECK(sim.people.shaft_count == 3, "three shafts collected");
    CHECK(sim.reach_public[floor_to_index(40)],
          "floor 40 is CONNECTED (reachability is transitive)...");
    (void)people_take_noroute_msg();               /* drain the latch */
    for (int frame = 0; frame < 1500; frame++)
        people_update(&sim.people, &tw, frame, TOD_MORNING, 9,
                      sim.reach_public, sim.reach_service);
    CHECK(people_at(office, floor_to_index(40), PERSON_AT_DEST) == 0,
          "...but nobody arrives: two transfers exceed the depth limit");
    CHECK(sim.people.trips_failed > 0, "two-transfer trips fail as no-route");
    const char *msg = people_take_noroute_msg();
    CHECK(msg != NULL, "no-route complaint latched for the floor pair");

    /* (d) rebuild triggers: the layout stamp folds per-floor serviced
     * flags and lobby tenant spans, so a stop toggle or a lobby
     * placement re-stamps (and the tables re-derive from the stamp). */
    uint32_t stamp0 = sim.people.layout_stamp;
    people_set_serviced(&sim.people, 1, floor_to_index(20), 0);
    people_rebuild_transport(&sim.people, &tw);
    CHECK(sim.people.layout_stamp != stamp0,
          "serviced toggle changes the layout stamp");
    stamp0 = sim.people.layout_stamp;
    tower_import_item(&tw, ITEM_LOBBY, 45, 195, 20);
    people_rebuild_transport(&sim.people, &tw);
    CHECK(sim.people.layout_stamp != stamp0,
          "lobby placement changes the layout stamp");
}

/* The authentic non-uniform day clock (TimeT seg 65, byte-verified
 * referee 2026-08-02): 2600-tick day, period = ft/400, boundaries at
 * 7:00AM / 12:00 / 12:30 / 1:00PM / 5:00PM / 9:00PM / 1:00AM. */
static void check_clock(int ft, int want_h, int want_m, const char *msg)
{
    int h, m;
    game_tick_clock(ft, &h, &m);
    if (h == want_h && m == want_m) printf("  ok   %s\n", msg);
    else {
        printf("  FAIL %s (ft %d -> %d:%02d, want %d:%02d)\n",
               msg, ft, h, m, want_h, want_m);
        fails++;
    }
}

static void test_authentic_clock(void)
{
    printf("authentic TimeT clock (2600-tick day, non-uniform table):\n");

    /* Every period boundary (the CS:0x720 jump table rows) + day wrap */
    check_clock(0,    7,  0, "ft 0    -> 7:00AM (period 0 opens the day)");
    check_clock(400,  12, 0, "ft 400  -> 12:00 (lunch, 800 ft/hr begins)");
    check_clock(800,  12, 30, "ft 800  -> 12:30 (second lunch period)");
    check_clock(1200, 13, 0, "ft 1200 -> 1:00PM (100 ft/hr afternoon)");
    check_clock(1600, 17, 0, "ft 1600 -> 5:00PM (business close row)");
    check_clock(2000, 21, 0, "ft 2000 -> 9:00PM (fire hard-stop row)");
    check_clock(2400, 1,  0, "ft 2400 -> 1:00AM (33.3 ft/hr night)");
    check_clock(2600, 7,  0, "ft 2600 wraps to 7:00AM (0xA28 wrap)");
    /* Dispatcher-row anchors re-verified in the referee dis16 */
    check_clock(240,  10, 0, "ft 0xF0  = 240  -> 10:00AM (judging/disasters)");
    check_clock(1500, 16, 0, "ft 0x5DC = 1500 -> 4:00PM (EventCleanup jump)");
    check_clock(2200, 23, 0, "ft 0x898 = 2200 -> 11:00PM");
    check_clock(2300, 0,  0, "ft 0x8FC = 2300 -> 0:00 (midnight day++)");
    check_clock(2500, 4,  0, "ft 0x9C4 = 2500 -> 4:00AM (population reset)");
    check_clock(2533, 4,  59, "ft 0x9E5 = 2533 -> 4:59AM (finance row + new-game start)");
    check_clock(2599, 6,  58, "ft 2599 -> 6:58AM (last tick of the day)");

    /* Inverse mapping is exact at every hour anchor */
    int inv_ok = 1;
    static const int anchors[][2] = {
        {7, 0}, {8, 80}, {9, 160}, {10, 240}, {11, 320}, {12, 400},
        {13, 1200}, {14, 1300}, {15, 1400}, {16, 1500}, {17, 1600},
        {18, 1700}, {19, 1800}, {20, 1900}, {21, 2000}, {22, 2100},
        {23, 2200}, {0, 2300}, {1, 2400}, {4, 2500},
    };
    for (unsigned i = 0; i < sizeof anchors / sizeof anchors[0]; i++)
        if (game_clock_to_tick(anchors[i][0], 0) != anchors[i][1]) inv_ok = 0;
    CHECK(inv_ok, "game_clock_to_tick hits every hour anchor exactly");

    /* Decode->inverse round trip never drifts more than a tick */
    int rt_ok = 1;
    for (int ft = 0; ft < GAME_DAY_TICKS; ft++) {
        int h, m;
        game_tick_clock(ft, &h, &m);
        int back = game_clock_to_tick(h, m);
        int d = back - ft; if (d < 0) d = -d;
        /* period granularity: 80 ft/hr = 1.33 ft/min, lunch 13.3 ft/min */
        int tol = (ft >= 400 && ft < 1200) ? 14 : (ft >= 2400) ? 7 : 3;
        if (d > tol) { rt_ok = 0; break; }
    }
    CHECK(rt_ok, "clock round-trip stays within one displayed minute");

    /* Period-gate spot check: the office departure window (people.c:
     * hour >= 17 && hour < 21) spans EXACTLY ft 1600..1999 — 400 raw
     * ticks, the EXE's 100 ft/hr evening. */
    int lo = -1, hi = -1, count = 0;
    for (int ft = 0; ft < GAME_DAY_TICKS; ft++) {
        int h, m;
        game_tick_clock(ft, &h, &m);
        if (h >= 17 && h < 21) {
            if (lo < 0) lo = ft;
            hi = ft;
            count++;
        }
    }
    CHECK(lo == 1600 && hi == 1999 && count == 400,
          "office departure window (5-9PM) = ft [1600,2000) exactly");

    /* game_update wiring: period crossing, midnight day++, 7AM wrap */
    fresh();
    sim.quarter = 0; sim.tick = 399;             /* one tick before noon */
    game_update(&sim, &tw);
    CHECK(sim.people.sched_period == 1 && sim.hour == 12,
          "crossing ft 400 flips [0xB3A1] period 0 -> 1 at 12:00");

    fresh();
    int day0 = tw.day;
    sim.quarter = FT_MIDNIGHT / GAME_TICKS_PER_QUARTER;
    sim.tick = FT_MIDNIGHT % GAME_TICKS_PER_QUARTER - 1;
    game_update(&sim, &tw);
    CHECK(tw.day == day0 + 1 && sim.hour == 0,
          "day++ fires at ft 2300 = midnight (TimeT 1200:04ab)");

    fresh();
    day0 = tw.day;
    sim.quarter = 3; sim.tick = GAME_TICKS_PER_QUARTER - 1;
    game_update(&sim, &tw);
    CHECK(sim.quarter == 0 && sim.tick == 0 && sim.hour == 7,
          "tick wrap at ft 2600 lands on 7:00AM, quarter resets");
    CHECK(tw.day == day0, "the 7AM wrap does NOT bump the day (midnight did)");
}

/* Load renormalization: a v14 (uniform 5AM-anchored clock) save resumes
 * at the same wall-clock time on the authentic table; a save sitting in
 * the 0:00-5:00AM window gets its day bumped so the judge/settlement
 * cadence matches what the old scheme would have run. */
static void write_v14_save(const char *path, const GameSim *s, const Tower *t)
{
    /* Emit the genuine pre-v16 LAYOUT — dead padding re-inserted
     * (Tenant.stress/complaints, GameSim.zones + last_stress_day) — so
     * loading exercises the real repack path (load_v15_blobs' inverse). */
    FILE *f = fopen(path, "wb");
    if (!f) return;
    static const unsigned char zeros[140];
    uint32_t hdr[5] = { 0x52575443u, 14u,
                        (uint32_t)(sizeof(Tower) + MAX_TENANTS * 8u),
                        (uint32_t)(sizeof(GameSim) + 144u),
                        (uint32_t)sizeof(Tuning) };
    fwrite(hdr, sizeof hdr, 1, f);
    const unsigned char *tb = (const unsigned char *)t;
    size_t base = offsetof(Tower, tenants), cut = offsetof(Tenant, zone);
    fwrite(tb, base, 1, f);
    for (int i = 0; i < MAX_TENANTS; i++) {
        const unsigned char *rec = tb + base + (size_t)i * sizeof(Tenant);
        fwrite(rec, cut, 1, f);
        fwrite(zeros, 8, 1, f);
        fwrite(rec + cut, sizeof(Tenant) - cut, 1, f);
    }
    size_t tail = base + (size_t)MAX_TENANTS * sizeof(Tenant);
    fwrite(tb + tail, sizeof(Tower) - tail, 1, f);
    const unsigned char *sb = (const unsigned char *)s;
    size_t a = offsetof(GameSim, santa), b = offsetof(GameSim, hotel_pass_day);
    fwrite(sb, a, 1, f);
    fwrite(zeros, 140, 1, f);
    fwrite(sb + a, b - a, 1, f);
    fwrite(zeros, 4, 1, f);
    fwrite(sb + b, sizeof(GameSim) - b, 1, f);
    fwrite(&TUNING, sizeof TUNING, 1, f);
    fclose(f);
}

static void test_load_renormalize(void)
{
    printf("v14 save clock renormalization:\n");
    const char *path = "/tmp/ct_test_v14.sav";

    /* Daytime case: old NORMAL clock at half day = 5AM + 12h = 5:00PM */
    fresh();
    tw.day = 7;
    sim.ticks_per_quarter = 720;         /* the old SPEED_NORMAL */
    sim.quarter = 2; sim.tick = 0;       /* old tick-in-day 1440/2880 */
    write_v14_save(path, &sim, &tw);
    tower_init(&tw); game_init(&sim);
    CHECK(game_load(&sim, &tw, path) == 0, "v14 save still loads");
    CHECK(sim.hour == 17 && sim.minute == 0,
          "old 5:00PM resumes at 5:00PM on the authentic table");
    CHECK((int)sim.quarter * GAME_TICKS_PER_QUARTER + sim.tick == 1600,
          "5:00PM lands on ft 1600 exactly");
    CHECK(sim.ticks_per_quarter == GAME_TICKS_PER_QUARTER,
          "ticks_per_quarter renormalized to 650");
    CHECK(tw.day == 7, "daytime resume keeps the day number");

    /* Small-hours case: old TURBO clock at 2AM -> ft 2433, day bumped
     * (the new scheme already incremented at the ft-2300 midnight) */
    fresh();
    tw.day = 7;
    sim.speed = SPEED_TURBO;
    sim.ticks_per_quarter = 120;         /* the old SPEED_TURBO */
    /* 2AM = 21h past 5AM: tick-in-day = 21/24 * 480 = 420 */
    sim.quarter = 3; sim.tick = 60;
    write_v14_save(path, &sim, &tw);
    tower_init(&tw); game_init(&sim);
    CHECK(game_load(&sim, &tw, path) == 0, "v14 TURBO save still loads");
    /* 2AM sits at ft 2433 1/3 on the real table (33 1/3 ft/hr night) —
     * the integer tick decodes to 1:59, one displayed minute shy. */
    CHECK((int)sim.quarter * GAME_TICKS_PER_QUARTER + sim.tick == 2433,
          "old 2AM resumes at ft 2433 (the 2AM anchor, floored)");
    CHECK(sim.hour == 2 || (sim.hour == 1 && sim.minute == 59),
          "resumed clock within one displayed minute of 2AM");
    CHECK(tw.day == 8, "post-midnight resume bumps the day (settlement cadence)");
    CHECK(sim.speed <= SPEED_FAST, "TURBO stays clamped to FAST");

    /* A v15 save (current format) must NOT be renormalized */
    fresh();
    tw.day = 3;
    sim.quarter = 1; sim.tick = 100;     /* ft 750, mid-lunch */
    CHECK(game_save(&sim, &tw, path) == 0, "v15 save writes");
    tower_init(&tw); game_init(&sim);
    CHECK(game_load(&sim, &tw, path) == 0, "v15 save loads");
    CHECK((int)sim.quarter * GAME_TICKS_PER_QUARTER + sim.tick == 750 &&
          tw.day == 3,
          "v15 tick/day pass through untouched");
    remove(path);
    remove("/tmp/ct_test_v14.sav");
}

int main(void)
{
    test_stairs();
    test_flavor();
    test_floor_fill();
    test_bulldozer();
    test_build_caps();
    test_deck_economics();
    test_person_names();
    test_art_styles();
    test_construct_queue();
    test_route_loss();
    test_unreachable_empty();
    test_elevators();
    test_housekeeping();
    test_hotel_infestation();
    test_hotel_demand();
    test_commute_elevator();
    test_metro_visitors();
    test_parking_cars();
    test_vip_visit();
    test_condo_cycle();
    test_medical_trip();
    test_walk_rules();
    test_transfer_tables();
    test_errand_warning_watchdog();
    test_queue_and_stress();
    test_elevator_dialog();
    test_simulate_editmode();
    test_remove_car();
    test_shuttle_and_patience();
    test_patrons_and_staff();
    test_money();
    test_retail_economy();
    test_cap_peaks();
    test_stressed_moveout();
    test_star_requirements();
    test_promotion_cadence();
    test_medical_adequacy();
    test_guard_hunt();
    test_venues();
    test_change_movie();
    test_occupants();
    test_disaster_schedule();
    test_fire_spread();
    test_bomb_blast();
    test_twr_import();
    test_twr_export();
    test_wedding();
    test_save_load();
    test_authentic_clock();
    test_load_renormalize();
    test_schedules();
    test_infra_upkeep();
    test_occupancy_lifecycle();
    test_rent_control();
    test_parking_model();
    printf(fails ? "\n%d FAILURE(S)\n" : "\nall tests passed\n", fails);
    return fails ? 1 : 0;
}
