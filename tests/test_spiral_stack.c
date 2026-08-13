/* Spiral-stack regression: stairs continuing BELOW and ABOVE a tall
 * grand-lobby spiral must be placeable; crossing its body must not. */
#include <stdio.h>
#include "game.h"

static GameSim sim;

static Tower tw;
static int fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("  ok   %s\n", msg); \
    else { printf("  FAIL %s\n", msg); fails++; } \
} while (0)

int main(void)
{
    tower_init(&tw);
    tw.money = 50000000L;
    tower_choose_lobby_height(&tw, 2);
    for (int x = 12; x <= 40; x += 4) tower_place(&tw, ITEM_LOBBY, 0, x);
    tower_extend_deck(&tw, 2, 12, 44);
    tower_extend_deck(&tw, -1, 12, 44);
    tower_extend_deck(&tw, 3, 12, 44);
    for (int f = -1; f <= 3; f++)
        printf("  grid f%d x24: type=%d\n",
               f, tw.grid[floor_to_index(f)][24].type);
    /* stairs click in the lobby band -> promoted spiral (base 0, rise 2) */
    uint16_t sp = tower_place(&tw, ITEM_STAIRS, 0, 24);
    Tenant *t = tower_tenant(&tw, sp);
    CHECK(sp != 0, "spiral placed");
    if (t) { printf("  (spiral floor=%d height=%d)\n", t->floor, t->height);
             CHECK(t->floor == 0 && t->height == 3, "spiral spans 0..2"); }
    CHECK(tower_can_place(&tw, ITEM_STAIRS, 2, 24),
          "stairs above spiral landing OK");
    CHECK(tower_can_place(&tw, ITEM_STAIRS, -1, 24),
          "stairs below spiral entry OK (Jonah 2026-08-12)");
    CHECK(!tower_can_place(&tw, ITEM_STAIRS, 1, 28),
          "stairs crossing spiral body rejected");
    /* --- tall-lobby walk connectivity (Jonah's floor-3/4 complaint) --- */
    tower_extend_deck(&tw, 4, 12, 44);
    tower_place(&tw, ITEM_STAIRS, 2, 32);        /* stair 2 -> 3 */
    uint16_t off3 = tower_place(&tw, ITEM_OFFICE, 3, 14);
    CHECK(off3 != 0, "office on floor 3 placed");
    game_init(&sim);
    game_update_reachability(&sim, &tw);
    CHECK(sim.reach_public[floor_to_index(3)],
          "floor 3 reachable: spiral + stair (Jonah case)");
    CHECK(!sim.reach_public[floor_to_index(4)],
          "floor 4 NOT reachable without its stair");
    tower_place(&tw, ITEM_STAIRS, 3, 24);        /* stair 3 -> 4 */
    game_update_reachability(&sim, &tw);
    CHECK(sim.reach_public[floor_to_index(4)],
          "floor 4 reachable after stair 3->4");

    printf(fails ? "FAILURES: %d\n" : "all pass\n", fails);
    return fails;
}
