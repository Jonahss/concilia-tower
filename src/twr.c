/* twr.c - Import original SimTower saves (.TDT / .TWR)
 *
 * The layout below is the EXE's own serializer, FileT FUN_10d0_0b3a,
 * read instruction-by-instruction (every io_readwrite call with its
 * address and size — simtower-decomp, 2026-06-11). Only version 0x24xx
 * is accepted; that's what SimTower 1.1 for Windows writes, and all
 * known real saves carry it.
 *
 * Coordinate notes:
 *   - file floors are 0..119 with 10 = ground (F1); EXE internal floor
 *     index = file floor. Port floor = file_floor - 10. File floor 0
 *     (B10) is below the port's range and is skipped.
 *   - x positions are in 8px cells, same space as the port grid.
 *   - money is stored /100 ("the UI lies to you" — TDT_format.txt);
 *     the port stores display dollars, so values scale by 100.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "twr.h"

/* ---------- little-endian cursor over the whole file ---------- */
typedef struct {
    const uint8_t *p;
    long len, pos;
    const char *what;     /* current section, for error messages */
    int fail;
} Cur;

static const uint8_t *take(Cur *c, long n)
{
    if (c->fail || c->pos + n > c->len) { c->fail = 1; return NULL; }
    const uint8_t *r = c->p + c->pos;
    c->pos += n;
    return r;
}
static unsigned u16(Cur *c) { const uint8_t *r = take(c, 2); return r ? r[0] | (r[1] << 8) : 0; }
static long     i32(Cur *c) { const uint8_t *r = take(c, 4); return r ? (long)(int32_t)(r[0] | (r[1] << 8) | ((uint32_t)r[2] << 16) | ((uint32_t)r[3] << 24)) : 0; }
static void     skip(Cur *c, long n) { take(c, n); }

/* ---------- .TDT tenant type -> port item ----------
 * Upper floors of multi-floor items get their own type ids in the file
 * (cinema 18+19, recycling 20+21, party hall 29+30, metro 31..33,
 * cathedral 36..40); we place the base id and skip continuations.
 * Type 42 "structures" marks stair/elevator footprints — transports are
 * serialized in their own blocks, so it's skipped here too. */
static ItemType map_type(int t, int *is_continuation)
{
    *is_continuation = 0;
    switch (t) {
    case 0:  return ITEM_FLOOR;
    case 3:  return ITEM_HOTEL_SINGLE;
    case 4:  return ITEM_HOTEL_TWIN;
    case 5:  return ITEM_HOTEL_SUITE;
    case 6:  return ITEM_RESTAURANT;
    case 7:  return ITEM_OFFICE;
    case 9:  return ITEM_CONDO;
    case 10: return ITEM_SHOP;
    case 11: return ITEM_PARKING;
    case 12: return ITEM_FAST_FOOD;
    case 13: return ITEM_MEDICAL;
    case 14: return ITEM_SECURITY;
    case 15: return ITEM_HOUSEKEEPING;
    case 18: case 34: return ITEM_CINEMA;  /* a cinema is stored as TWO
                            strips: type 18 hall (24 cells) + type 34
                            entrance (7 cells) — real saves confirm */
    case 20: return ITEM_RECYCLING;
    case 24: return ITEM_LOBBY;
    case 29: return ITEM_PARTY_HALL;
    case 31: return ITEM_METRO;
    case 36: return ITEM_CATHEDRAL;
    case 19: case 21: case 30: case 32: case 33: case 35:
    case 37: case 38: case 39: case 40: case 44:
        *is_continuation = 1;
        return ITEM_NONE;
    default: /* 17 SECOM, 42 structures, 45 ramp, 48 burned, unknowns */
        return ITEM_NONE;
    }
}

/* Mirror of the EXE's FloorToStopIndex (ElevatorUI 10a0:17ee): which
 * floors of a group own a 0x144 stop record in the save. */
static int stop_slot(int type, int file_floor, int bottom, int top)
{
    if (type == 0) {                          /* express */
        if (file_floor <= 10) return file_floor - 1;   /* B9..ground; B10 = -1 */
        if ((file_floor - 10) % 15 == 14)               /* sky lobbies */
            return (file_floor - 10) / 15 + 10;
        return -1;
    }
    if (file_floor > top) return -1;
    (void)bottom;
    return file_floor - bottom;
}

int twr_import(const char *path, Tower *tower, GameSim *sim,
               char *err, int errlen)
{
#define FAILF(...) do { if (err) snprintf(err, errlen, __VA_ARGS__); \
                        free(buf); return -1; } while (0)
    uint8_t *buf = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) { if (err) snprintf(err, errlen, "cannot open %s", path); return -1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(len > 0 ? len : 1);
    if (!buf || fread(buf, 1, len, f) != (size_t)len) {
        fclose(f);
        FAILF("cannot read %s", path);
    }
    fclose(f);

    Cur c = { buf, len, 0, "header", 0 };

    /* === header === */
    unsigned version = u16(&c);
    if ((version & 0xff00) != 0x2400)
        FAILF("unsupported save version 0x%04x (want 0x24xx)", version);

    int star            = (int)u16(&c);
    long money          = i32(&c);
    long other_income   = i32(&c);   (void)other_income;
    long constr_costs   = i32(&c);
    long last_q_money   = i32(&c);   (void)last_q_money;
    int frame_time      = (int)u16(&c);  (void)frame_time;
    long day            = i32(&c);
    skip(&c, 2);                     /* 0xb3e4 (v>=0x2000) */
    int lobby_height    = (int)u16(&c); (void)lobby_height;
    skip(&c, 2 * 4 + 4);             /* b3e8..b3ee, b3f0 */
    skip(&c, 2 + 2);                 /* scroll x/y */
    skip(&c, 2 * 6);                 /* b3f8..b402 */
    skip(&c, 2);                     /* b404 (v>=0x2300) */
    skip(&c, 2 + 4 + 4);             /* b406, b408, b40c */
    skip(&c, 0x1ea);                 /* header remainder */
    if (c.fail) FAILF("truncated in header");

    /* Fresh tower: the importer rebuilds everything including lobbies,
     * so start from zeroed state, not tower_init's default ground lobby. */
    memset(tower, 0, sizeof(*tower));
    tower->next_tenant_id = 1;
    tower->money = money * 100;
    tower->built_value = constr_costs * 100;
    tower->star_rating = star < 1 ? 1 : star;
    tower->day = (int)(day < 0 ? 0 : day);

    /* === floor map: 120 floors, 0 = B10 === */
    int placed = 0, skipped = 0, width_mismatches = 0;
    for (int ff = 0; ff < 120; ff++) {
        c.what = "floor map";
        unsigned n = u16(&c);
        skip(&c, 4);                 /* floor left/right extents */
        if (c.fail) FAILF("truncated in floor map (floor %d)", ff);
        if (n > 94) FAILF("floor %d claims %u tenants (max 94) — not a .TDT?", ff, n);
        for (unsigned i = 0; i < n; i++) {
            const uint8_t *t = take(&c, 18);
            if (!t) FAILF("truncated tenant on floor %d", ff);
            int left  = t[0] | (t[1] << 8);
            int right = t[2] | (t[3] << 8);
            int ttype = (int8_t)t[4];
            int under_construction = ttype < 0;
            if (under_construction) ttype = -ttype;

            int cont = 0;
            ItemType it = map_type(ttype, &cont);
            int pfloor = ff - 10;
            if (it == ITEM_NONE || pfloor < TOWER_MIN_FLOOR ||
                pfloor > TOWER_MAX_FLOOR) {
                if (!cont && it == ITEM_NONE) skipped++;
                continue;
            }
            int w = right - left;
            if (w <= 0) continue;
            if (it != ITEM_FLOOR && it != ITEM_LOBBY && it != ITEM_CINEMA &&
                w != ITEM_WIDTH[it])
                width_mismatches++;
            uint16_t id = tower_import_item(tower, it, pfloor, left, w);
            if (id) {
                placed++;
                Tenant *ten = &tower->tenants[tower->tenant_count - 1];
                if (under_construction) {
                    ten->state = TENANT_CONSTRUCTION;
                    ten->construction = 8;
                }
            }
        }
        skip(&c, 0xbc);              /* indexMap[94] (v>=0x2400 raw) */
    }

    /* === people (counts only; the sim respawns from tenants) === */
    c.what = "people";
    long npeople = i32(&c);
    if (c.fail || npeople < 0 || npeople > 200000)
        FAILF("bad people count %ld", npeople);
    skip(&c, npeople * 0x10);

    /* === retail subtype table === */
    skip(&c, 0x2400);
    if (c.fail) FAILF("truncated in retail block");

    /* === elevator groups (24) === */
    struct {
        int active, type, cars, x, bottom, top;
        const uint8_t *hdr;          /* full 0xC2 header in the file */
    } grp[24];
    int ngroups = 0;
    for (int g = 0; g < 24; g++) {
        c.what = "elevators";
        const uint8_t *h = take(&c, 0xc2);
        if (!h) FAILF("truncated elevator group %d", g);
        grp[g].active = h[0];
        grp[g].type   = h[1];
        grp[g].cars   = h[3];
        grp[g].x      = h[0x3e] | (h[0x3f] << 8);
        grp[g].top    = h[0x40];
        grp[g].bottom = h[0x41];
        grp[g].hdr    = h;
        if (!h[0]) continue;
        ngroups++;
        skip(&c, 0x1e0);             /* +0xC2..+0x2A2 */
        skip(&c, 0x78 + 0x78);       /* up/down call owners */
        for (int ff = grp[g].bottom; ff <= grp[g].top; ff++)
            if (stop_slot(grp[g].type, ff, grp[g].bottom, grp[g].top) >= 0)
                skip(&c, 0x144);     /* stop record (queues reset on import) */
        skip(&c, 8 * 0x15a);         /* car records (cars reset to home) */
        if (c.fail) FAILF("truncated in elevator group %d", g);
    }

    /* Stamp shaft cells so the transport rebuild sees them */
    for (int g = 0; g < 24; g++) {
        if (!grp[g].active) continue;
        ItemType it = grp[g].type == 0 ? ITEM_ELEVATOR_EXPRESS
                    : grp[g].type == 2 ? ITEM_ELEVATOR_SERVICE
                    : ITEM_ELEVATOR_SHAFT;
        for (int ff = grp[g].bottom; ff <= grp[g].top; ff++) {
            int pf = ff - 10;
            if (pf < TOWER_MIN_FLOOR || pf > TOWER_MAX_FLOOR) continue;
            tower_import_item(tower, it, pf, grp[g].x, 0);
        }
    }

    /* === post-elevator fixed blocks === */
    skip(&c, 0x58);                  /* 0xb846 */
    skip(&c, 0x84);                  /* finance window: pop/income/maint */
    skip(&c, 0xc + 0x2a + 0x402 + 0x16);
    if (c.fail) FAILF("truncated before stairs");

    /* === stairs/escalators: 64 x 10 bytes === */
    c.what = "stairs";
    for (int i = 0; i < 64; i++) {
        const uint8_t *s = take(&c, 10);
        if (!s) FAILF("truncated stair %d", i);
        if (!s[0]) continue;
        int x  = s[2] | (s[3] << 8);
        int ff = s[4] | (s[5] << 8);
        int pf = ff - 10;
        if (pf < TOWER_MIN_FLOOR || pf + 1 > TOWER_MAX_FLOOR) continue;
        tower_import_item(tower, s[1] ? ITEM_STAIRS : ITEM_ESCALATOR,
                          pf, x, 0);
    }

    /* === tail (walk chains, gap map, routing slots, judge state...) ===
     * All reconstructable; consumed only to verify the file length. */
    c.what = "tail";
    skip(&c, 8 * 0x1e4);             /* walk chains */
    skip(&c, 0x78);                  /* per-gap walk map (0xcf10) */
    skip(&c, 10 * 2);                /* 0xcf88..0xcf9c */
    skip(&c, 0x200 * 6);             /* per-tenant transfer table */
    skip(&c, 0x10 * 6);              /* routing slots (0xdb9c) */
    skip(&c, 10 * 4);                /* 0xdbfc..0xdc24 */
    skip(&c, 0x10 * 0xc);
    skip(&c, 0x50 + 0x28);           /* v>=0x2300 split block */
    skip(&c, 0x1102 + 0x842 + 0xca2);/* far-pointer blocks (judge/people) */
    skip(&c, 8);                     /* v>=0x1800 trailer */
    long leftover = c.fail ? -1 : c.len - c.pos;

    /* Anything left is the named-tenant list the save-as path appends
     * after the serializer: 16-byte C strings ("Office Girl", ...). */
    int names = 0;
    if (leftover > 0 && leftover % 16 == 0) {
        names = (int)(leftover / 16);
        for (int i = 0; i < names && i < 8; i++) {
            char nm[17];
            memcpy(nm, c.p + c.pos + i * 16, 16);
            nm[16] = 0;
            for (int k = 0; k < 16; k++)
                if (nm[k] && (nm[k] < 0x20 || (unsigned char)nm[k] > 0x7e))
                    nm[k] = '?';
            printf("TWR import: named tenant \"%s\"\n", nm);
        }
        leftover = 0;
    }

    /* === wire the simulation === */
    game_init(sim);
    sim->quarter = (day >= 0 && day % 3 == 2) ? QUARTER_WEEKEND
                                              : (int)(day % 3);
    game_update_reachability(sim, tower);
    people_rebuild_transport(&sim->people, tower);

    /* Apply group settings to the rebuilt shafts, matched by (x, type) */
    for (int g = 0; g < 24; g++) {
        if (!grp[g].active) continue;
        ItemType it = grp[g].type == 0 ? ITEM_ELEVATOR_EXPRESS
                    : grp[g].type == 2 ? ITEM_ELEVATOR_SERVICE
                    : ITEM_ELEVATOR_SHAFT;
        for (int si = 0; si < sim->people.shaft_count; si++) {
            ElevatorShaft *sh = &sim->people.shafts[si];
            if (!sh->active || sh->x != grp[g].x || sh->type != it) continue;
            const uint8_t *h = grp[g].hdr;
            int cars = grp[g].cars;
            people_set_num_cars(&sim->people, si,
                                cars < 1 ? 1 : cars > 8 ? 8 : cars);
            for (int ff = 0; ff < 120; ff++) {
                int fidx = ff - 10 + 9;          /* port floor index */
                if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
                if (fidx >= sh->lo && fidx <= sh->hi)
                    sh->serviced[fidx] = h[0x42 + ff] ? 1 : 0;
            }
            for (int car = 0; car < CARS_PER_SHAFT; car++) {
                int hf = h[0xba + car] - 10 + 9;
                if (hf >= sh->lo && hf <= sh->hi) sh->home[car] = hf;
            }
            for (int d = 0; d < 2; d++)
                for (int p = 0; p < 7; p++) {
                    sh->sched_threshold[d][p] = h[0x12 + d * 7 + p];
                    sh->sched_mode[d][p]      = h[0x20 + d * 7 + p];
                    sh->sched_patience[d][p]  = h[0x2e + d * 7 + p];
                }
            break;
        }
    }

    printf("TWR import: %s — v0x%04x, %d star(s), $%ld, day %ld; "
           "%d tenants placed (%d unmapped), %d shafts, %d named, "
           "%d width mismatches, %ld bytes unparsed\n",
           path, version, star, tower->money, day,
           placed, skipped, ngroups, names, width_mismatches, leftover);

    free(buf);
    return 0;
#undef FAILF
}
