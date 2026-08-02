/* savprobe — load a .sav via game_load and dump day/money/tenant census
 * by type+state. The deploy protocol content-probe: run on the F5 save
 * before restarting the service, and diff against the boot-autoload line.
 * Build: gcc -o /tmp/savprobe tools/savprobe.c src/game.c src/tower.c \
 *   src/people.c src/twr.c src/sound_hook.c src/strings.c src/ne_resource.c -Isrc -lm */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"

int main(int argc, char **argv)
{
    static Tower tower; static GameSim sim;
    if (game_load(&sim, &tower, argv[1]) != 0) { fprintf(stderr, "load failed\n"); return 1; }
    int by_type_state[64][8]; memset(by_type_state, 0, sizeof by_type_state);
    for (int i = 0; i < tower.tenant_count; i++) {
        Tenant *t = &tower.tenants[i];
        int ty = t->type < 64 ? t->type : 63;
        int st = t->state < 8 ? t->state : 7;
        by_type_state[ty][st]++;
    }
    printf("day=%d money=%ld tenants=%d\n", tower.day, tower.money, tower.tenant_count);
    for (int ty = 0; ty < 64; ty++)
        for (int st = 0; st < 8; st++)
            if (by_type_state[ty][st])
                printf("type=%-2d (%s) state=%d count=%d\n", ty, tower_item_name(ty), st, by_type_state[ty][st]);
    return 0;
}
