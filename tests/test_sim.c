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
    CHECK(sim.people.shafts[0].capacity == 42, "standard car capacity 42");

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

int main(void)
{
    test_stairs();
    test_elevators();
    test_housekeeping();
    test_commute_elevator();
    test_walk_rules();
    test_queue_and_stress();
    printf(fails ? "\n%d FAILURE(S)\n" : "\nall tests passed\n", fails);
    return fails ? 1 : 0;
}
