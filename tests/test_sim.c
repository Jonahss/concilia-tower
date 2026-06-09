/* test_sim.c — checks for transport reachability + housekeeping.
 * Build: gcc -o /tmp/test_sim tests/test_sim.c src/tower.c src/game.c -Isrc -lm
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

int main(void)
{
    test_stairs();
    test_elevators();
    test_housekeeping();
    printf(fails ? "\n%d FAILURE(S)\n" : "\nall tests passed\n", fails);
    return fails ? 1 : 0;
}
