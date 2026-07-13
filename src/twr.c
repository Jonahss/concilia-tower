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
 *     index = file floor. Port floor = file_floor - 10; file floor 0
 *     is B10, the port's deepest floor.
 *   - multi-floor items keep their BASE record on the TOP floor with
 *     continuation ids on the floors below; the port anchors tenants
 *     on the BOTTOM floor, so both directions convert by height-1.
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
    case 44: return ITEM_RAMP;  /* parking ramp: one strip per floor */
    case 19: case 21: case 30: case 32: case 33: case 35:
    case 37: case 38: case 39: case 40:
        *is_continuation = 1;
        return ITEM_NONE;
    default: /* 17 SECOM, 42 structures, 45 ground strips, 48 burned */
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
    memcpy(tower->twr_header, buf, 0x230);   /* cursor sits at 0x230 here */
    tower->twr_header_valid = 1;
    tower->next_tenant_id = 1;
    tower->money = money * 100;
    /* the EXE's construction tracker (0xb3d6) counts DOWN on spends */
    tower->built_value = (constr_costs < 0 ? -constr_costs : constr_costs) * 100;
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
            /* base record sits on the item's top floor; the port tenant
             * anchors at the bottom */
            int pfloor = ff - 10 - (it == ITEM_NONE ? 0 : ITEM_HEIGHT[it] - 1);
            if (it == ITEM_NONE || pfloor < TOWER_MIN_FLOOR ||
                pfloor > TOWER_TOP_FLOOR) {
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
                ten->rent_class = t[0x0f] <= 3 ? t[0x0f] : 1;
                /* the venue slot table (movie ids) lives outside the floor
                 * map; assign an id-stable film like the retail variants */
                if (ten->type == ITEM_CINEMA)          ten->movie_id = id % 14;
                else if (ten->type == ITEM_PARTY_HALL) ten->movie_id = 0xFF;
                if (it == ITEM_RESTAURANT || it == ITEM_SHOP ||
                    it == ITEM_FAST_FOOD)
                    ten->retail_ref = t[6] + 1;
                ten->twr_unk[0] = t[0x0c];
                ten->twr_unk[1] = t[0x0d];
                ten->twr_unk[2] = t[0x0e];
                ten->twr_unk[3] = t[0x10];
                ten->twr_unk[4] = t[0x11];
                /* Hotel room condition rides in the status byte's band
                 * (+0x0B: >=0x38 infested, >=0x28 dirty — the EXE
                 * round-trips it verbatim, so real saves carry roaches) */
                if (item_is_hotel_room(it)) {
                    if (t[0x0b] >= 0x38)      ten->condition = ROOM_INFESTED;
                    else if (t[0x0b] >= 0x28) ten->condition = ROOM_DIRTY;
                    else                      ten->condition = ROOM_CLEAN;
                }
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

    /* === retail subtype table (preserved verbatim for export) === */
    {
        const uint8_t *rt = take(&c, 0x2400);
        if (!rt) FAILF("truncated in retail block");
        memcpy(tower->twr_retail, rt, 0x2400);
    }
    /* Live retail-economy fields ride in the record (2026-07-11 referee:
     * +2 state, +3/4/5 service scores, +6 quota, +7 walk-ins admitted,
     * +9 patrons inside, +0x10 customers today) — real 1995 saves carry
     * each venue's service history, so imported towers keep it. */
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (!t->retail_ref) continue;
        const uint8_t *e = tower->twr_retail + (t->retail_ref - 1) * 18;
        t->retail_score[0] = e[3];
        t->retail_score[1] = e[4];
        t->retail_score[2] = e[5];
        t->retail_quota    = e[6];
        t->walkins_today   = e[7];
        t->patrons_now     = e[9];
        t->retail_open     = (e[2] != 3 && e[2] != 0xFF);
        t->customers_today = (uint16_t)(e[0x10] | (e[0x11] << 8));
    }

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
        for (int i = 0; i < names; i++) {
            char nm[17];
            memcpy(nm, c.p + c.pos + i * 16, 16);
            nm[16] = 0;
            for (int k = 0; k < 16; k++)
                if (nm[k] && (nm[k] < 0x20 || (unsigned char)nm[k] > 0x7e))
                    nm[k] = '?';
            if (i < 20) {
                memcpy(tower->twr_names[i], c.p + c.pos + i * 16, 16);
                tower->twr_names[i][15] = 0;
                tower->twr_name_count = i + 1;
            }
            if (i < 8) printf("TWR import: named tenant \"%s\"\n", nm);
        }
        leftover = 0;
    }

    /* === wire the simulation === */
    game_init(sim);
    /* Import at the top of the day (5AM slice). Weekday/weekend derives
     * from the imported day number itself now (game_is_weekend) — the
     * old day%3->slice mapping made WE saves import at 11PM, which is
     * also why bomb offers used to detonate instantly on import. */
    sim->quarter = QUARTER_1;
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
                int fidx = floor_to_index(ff - 10);
                if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
                if (fidx >= sh->lo && fidx <= sh->hi)
                    sh->serviced[fidx] = h[0x42 + ff] ? 1 : 0;
            }
            for (int car = 0; car < CARS_PER_SHAFT; car++) {
                int hf = floor_to_index(h[0xba + car] - 10);
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

/* ====================================================================
 * Export — the serializer run forward. Writes a v0x2400 file the
 * importer above (and, structurally, SimTower 1.1 itself) accepts.
 *
 * Fidelity notes, verified against real saves (BARKLE4D/SCHMITT):
 *   - multi-floor items serialize TOP-DOWN: base type on the top floor,
 *     continuation ids on the floors below (cathedral is 5 records).
 *   - type 45 = the B10 ground strips, full width minus the metro span;
 *     regenerated here since import drops them.
 *   - unused group slots = zeroed 0xC2 header, no body (the EXE's own
 *     serializer skips the body when the active byte is 0).
 *   - people are NOT exported (count 0, tenant people refs zeroed) —
 *     the live game repopulates through the daily cycle. Untested in
 *     the real EXE; revisit if a real-game load shows vacancy bugs.
 *   - per-floor indexMap, stop records, car motion state, call owners,
 *     finance window and the tail blocks are rebuildable scratch in the
 *     EXE; written as zeros (cars are written parked at home with empty
 *     passenger lists, matching idle records seen in real saves).
 * ==================================================================== */

typedef struct {
    uint8_t *p;
    long cap, pos;
    int fail;
} Out;

/* Grows as needed; on OOM sets fail and hands back scratch so callers
 * can keep writing blindly — the flag is checked once at the end. */
static uint8_t *emit(Out *o, long n)
{
    static uint8_t scratch[0x2400];
    if (o->pos + n > o->cap) {
        long cap = o->cap * 2;
        while (o->pos + n > cap) cap *= 2;
        uint8_t *p = realloc(o->p, cap);
        if (!p) { o->fail = 1; return scratch; }
        o->p = p;
        o->cap = cap;
    }
    uint8_t *r = o->p + o->pos;
    memset(r, 0, n);
    o->pos += n;
    return r;
}
static void put16(uint8_t *p, unsigned v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }
static void put32(uint8_t *p, long v)
{
    uint32_t u = (uint32_t)(int32_t)v;
    p[0] = u & 0xff; p[1] = (u >> 8) & 0xff;
    p[2] = (u >> 16) & 0xff; p[3] = (u >> 24) & 0xff;
}

/* Port item -> file base type + downward continuation ids */
static int file_type(const Tenant *t, int *cont, int *ncont)
{
    *ncont = 0;
    switch (t->type) {
    case ITEM_FLOOR:        return 0;
    case ITEM_HOTEL_SINGLE: return 3;
    case ITEM_HOTEL_TWIN:   return 4;
    case ITEM_HOTEL_SUITE:  return 5;
    case ITEM_RESTAURANT:   return 6;
    case ITEM_OFFICE:       return 7;
    case ITEM_CONDO:        return 9;
    case ITEM_SHOP:         return 10;
    case ITEM_PARKING:      return 11;
    case ITEM_FAST_FOOD:    return 12;
    case ITEM_MEDICAL:      return 13;
    case ITEM_SECURITY:     return 14;
    case ITEM_HOUSEKEEPING: return 15;
    case ITEM_RAMP:         return 44;  /* one record per floor, no conts */
    case ITEM_CINEMA:                      /* hall + entrance strips */
        if (t->width >= 20) { cont[0] = 19; *ncont = 1; return 18; }
        cont[0] = 35; *ncont = 1; return 34;
    case ITEM_RECYCLING:    cont[0] = 21; *ncont = 1; return 20;
    case ITEM_LOBBY:        return 24;
    case ITEM_PARTY_HALL:   cont[0] = 30; *ncont = 1; return 29;
    case ITEM_METRO:        cont[0] = 32; cont[1] = 33; *ncont = 2; return 31;
    case ITEM_CATHEDRAL:    /* 5 file floors; the port renders the top */
        cont[0] = 37; cont[1] = 38; cont[2] = 39; cont[3] = 40;
        *ncont = 4; return 36;
    default:                return -1;    /* transports, none */
    }
}

/* One emitted floor-map record */
typedef struct {
    int left, right, type;
    const Tenant *ten;       /* NULL for synthesized ground strips */
} Rec;

static int rec_cmp(const void *a, const void *b)
{
    return ((const Rec *)a)->left - ((const Rec *)b)->left;
}

/* How many storefront variants the EXE art carries per retail class:
 * restaurants/fast food are PAIRS of sheets (empty|busy + packed|closed),
 * shops are single sheets with three fill states. */
int twr_variant_count(ItemType it)
{
    return it == ITEM_SHOP ? 11 : 5;
}

/* Which storefront variant a retail tenant shows: the variant byte
 * (+0x0B) of its preserved retail-table entry, or a stable id-derived
 * fallback for tenants that have no slot yet. The exporter writes the
 * same fallback into newly allocated slots, so the look survives a
 * round-trip. */
int twr_tenant_variant(const Tower *tower, const Tenant *t)
{
    int nv = twr_variant_count(t->type);
    if (t->retail_ref) {
        const uint8_t *e = tower->twr_retail + (t->retail_ref - 1) * 18;
        return e[0x0b] % nv;
    }
    return t->id % nv;
}

/* Observed per-class constants in real retail-table entries */
static void retail_template(ItemType it, int file_floor, int seq,
                            int variant, uint8_t *e)
{
    memset(e, 0, 18);
    e[1] = (uint8_t)seq;
    e[0x0b] = (uint8_t)variant;
    switch (it) {
    case ITEM_FAST_FOOD:
        e[0] = (uint8_t)file_floor;
        e[3] = 0x23; e[4] = 0x32; e[5] = 0x19;
        e[7] = 0x23; e[8] = 0x23; e[0x0a] = 0x32;
        e[0x0c] = 0xdc; e[0x0d] = 0xff;
        break;
    case ITEM_SHOP:
        e[0] = (uint8_t)file_floor;
        e[3] = 0x19; e[4] = 0x1e; e[5] = 0x12;
        e[7] = 0x19; e[8] = 0x19;
        e[0x0c] = 0xe6; e[0x0d] = 0xff;
        break;
    default: /* restaurant: +0 is 0x64 in the wild, not the floor */
        e[0] = 0x64;
        e[3] = 0x23; e[4] = 0x32; e[5] = 0x19;
        e[7] = 0x23; e[8] = 0x23; e[0x0a] = 0x64;
        e[0x0c] = 0xdc; e[0x0d] = 0xff;
        break;
    }
}

int twr_export(const char *path, Tower *tower, const GameSim *sim,
               char *err, int errlen)
{
#define EFAIL(...) do { if (err) snprintf(err, errlen, __VA_ARGS__); \
                        free(out.p); return -1; } while (0)
    Out out = { malloc(1 << 20), 1 << 20, 0, 0 };
    if (!out.p) { if (err) snprintf(err, errlen, "out of memory"); return -1; }

    /* === retail table: give port-built retail tenants a slot === */
    int retail_seq[ITEM_TYPE_COUNT] = {0};
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (t->type != ITEM_RESTAURANT && t->type != ITEM_SHOP &&
            t->type != ITEM_FAST_FOOD)
            continue;
        retail_seq[t->type]++;
        if (t->retail_ref) continue;
        for (int s = 0; s < 256; s++) {       /* +6 in the record is u8 */
            uint8_t *e = tower->twr_retail + s * 18;
            /* free slot: negative floor byte (the file's empty marker)
             * or an untouched all-zero entry on a fresh tower */
            int allzero = 1;
            for (int k = 0; k < 18; k++) allzero &= !e[k];
            if (e[0] < 0x80 && !allzero) continue;
            retail_template(t->type, t->floor + 10,
                            retail_seq[t->type] - 1,
                            t->id % twr_variant_count(t->type), e);
            t->retail_ref = s + 1;
            break;
        }
        if (!t->retail_ref) EFAIL("retail table full (256 slots)");
    }
    /* Sync the live retail economy back into the records (the inverse of
     * the import read; state 0xFF = un-let shop slot is preserved). */
    for (int i = 0; i < tower->tenant_count; i++) {
        Tenant *t = &tower->tenants[i];
        if (!t->retail_ref) continue;
        if (t->type != ITEM_RESTAURANT && t->type != ITEM_SHOP &&
            t->type != ITEM_FAST_FOOD) continue;
        uint8_t *e = tower->twr_retail + (t->retail_ref - 1) * 18;
        if (e[2] != 0xFF)
            e[2] = !t->retail_open       ? 3
                 : t->patrons_now >= 10  ? 2
                 : t->patrons_now >= 1   ? 1 : 0;
        e[3] = t->retail_score[0];
        e[4] = t->retail_score[1];
        e[5] = t->retail_score[2];
        e[6] = t->retail_quota;
        e[7] = t->walkins_today;
        e[9] = t->patrons_now;
        e[0x10] = (uint8_t)(t->customers_today & 0xFF);
        e[0x11] = (uint8_t)(t->customers_today >> 8);
    }
    int retail_used = 0;
    for (int s = 0; s < 512; s++) {
        const uint8_t *e = tower->twr_retail + s * 18;
        int allzero = 1;
        for (int k = 0; k < 18; k++) allzero &= !e[k];
        if (e[0] < 0x80 && !allzero) retail_used++;
    }

    /* === header === */
    uint8_t *h = emit(&out, 0x230);
    if (tower->twr_header_valid)
        memcpy(h, tower->twr_header, 0x230);
    else {
        put16(h + 0x1c, 3);          /* lobby height, 3 in all real saves */
        put16(h + 0x20, 0xffff);     /* b3ea/b3ec: -1 sentinels in the wild */
        put16(h + 0x22, 0xffff);
        put16(h + 0x36, 2);          /* b400 = 2 in all real saves */
    }
    put16(h + 0x00, 0x2400);
    put16(h + 0x02, (unsigned)tower->star_rating);
    put32(h + 0x04, tower->money / 100);
    put32(h + 0x0c, -(tower->built_value / 100));  /* counts down on spends */
    if (!tower->twr_header_valid)
        put32(h + 0x10, tower->money / 100);       /* last quarter money */
    put32(h + 0x16, tower->day);
    put16(h + 0x2e, (unsigned)retail_used);        /* b3f8 tracks the table */

    /* === floor map: 120 floors === */
    static Rec recs[256];
    for (int ff = 0; ff < 120; ff++) {
        int n = 0;
        for (int i = 0; i < tower->tenant_count; i++) {
            const Tenant *t = &tower->tenants[i];
            int cont[4], ncont;
            int base = file_type(t, cont, &ncont);
            if (base < 0) continue;
            int tff = t->floor + ncont + 10;    /* base sits on the top */
            int ty = -1;
            if (tff == ff) ty = base;
            else if (ff < tff && tff - ff <= ncont) ty = cont[tff - ff - 1];
            if (ty < 0) continue;
            if (n >= 256) EFAIL("floor %d: too many records", ff);
            recs[n++] = (Rec){ t->x, t->x + t->width, ty, t };
        }
        if (ff == 0) {
            /* B10 ground strips (type 45), split around the metro pit */
            int m0 = -1, m1 = -1;
            for (int i = 0; i < n; i++)
                if (recs[i].type == 33) { m0 = recs[i].left; m1 = recs[i].right; }
            if (m0 > 0)
                recs[n++] = (Rec){ 0, m0, 45, NULL };
            else if (m0 < 0)
                recs[n++] = (Rec){ 0, TOWER_WIDTH, 45, NULL };
            if (m1 > 0 && m1 < TOWER_WIDTH)
                recs[n++] = (Rec){ m1, TOWER_WIDTH, 45, NULL };
        }
        if (n > 94) EFAIL("floor %d has %d records (file max 94)", ff, n);
        qsort(recs, n, sizeof recs[0], rec_cmp);

        int left = 0, right = 0;
        if (n) {
            left = recs[0].left;
            right = recs[0].right;
            for (int i = 1; i < n; i++)
                if (recs[i].right > right) right = recs[i].right;
        }
        uint8_t *fh = emit(&out, 6);
        put16(fh, (unsigned)n);
        put16(fh + 2, (unsigned)left);
        put16(fh + 4, (unsigned)right);
        for (int i = 0; i < n; i++) {
            const Rec *r = &recs[i];
            uint8_t *t = emit(&out, 18);
            put16(t, (unsigned)r->left);
            put16(t + 2, (unsigned)r->right);
            int ty = r->type;
            if (r->ten && r->ten->state == TENANT_CONSTRUCTION) ty = -ty;
            t[4] = (uint8_t)(int8_t)ty;
            /* +5 status and +7 people ref stay 0: no people exported */
            if (r->ten && r->ten->retail_ref)
                t[6] = (uint8_t)(r->ten->retail_ref - 1);
            if (r->ten) {
                /* Hotel rooms: write the condition band back into the
                 * status byte (day variants; no people are exported, so
                 * every room goes out as a vacant band) */
                if (item_is_hotel_room(r->ten->type))
                    t[0x0b] = (r->ten->condition == ROOM_INFESTED) ? 0x38
                            : (r->ten->condition == ROOM_DIRTY)    ? 0x28
                            : 0x18;
                t[0x0c] = r->ten->twr_unk[0];
                t[0x0d] = r->ten->twr_unk[1];
                t[0x0e] = r->ten->twr_unk[2] ? r->ten->twr_unk[2] : 1;
                t[0x0f] = (r->ten->type == ITEM_LOBBY ||
                           r->ten->type == ITEM_FLOOR)
                              ? 0xff : r->ten->rent_class;
                t[0x10] = r->ten->twr_unk[3];
                t[0x11] = r->ten->twr_unk[4];
            } else {
                t[0x0e] = 1;
                t[0x0f] = 0xff;
            }
        }
        emit(&out, 0xbc);            /* indexMap: scratch, zeros */
    }

    /* === people: none exported === */
    put32(emit(&out, 4), 0);

    /* === retail table: untouched slots go out as floor -1 (empty) === */
    {
        uint8_t *rt = emit(&out, 0x2400);
        memcpy(rt, tower->twr_retail, 0x2400);
        for (int s = 0; s < 512; s++) {
            uint8_t *e = rt + s * 18;
            int allzero = 1;
            for (int k = 0; k < 18; k++) allzero &= !e[k];
            if (allzero) e[0] = 0xff;
        }
    }

    /* === elevator groups: live shafts, then zeroed inactive slots === */
    int ngroups = 0;
    for (int si = 0; si < sim->people.shaft_count && ngroups < 24; si++) {
        const ElevatorShaft *sh = &sim->people.shafts[si];
        if (!sh->active) continue;
        ngroups++;
        int ftype = sh->type == ITEM_ELEVATOR_EXPRESS ? 0
                  : sh->type == ITEM_ELEVATOR_SERVICE ? 2 : 1;
        int bottom = index_to_floor(sh->lo) + 10;    /* fidx -> file floor */
        int top    = index_to_floor(sh->hi) + 10;
        uint8_t *g = emit(&out, 0xc2);
        g[0] = 1;
        g[1] = (uint8_t)ftype;
        g[2] = sh->capacity;
        g[3] = sh->num_cars;
        memset(g + 4, 1, 14);        /* +0x04..0x11: 01s in every real save */
        for (int d = 0; d < 2; d++)
            for (int p = 0; p < 7; p++) {
                g[0x12 + d * 7 + p] = sh->sched_threshold[d][p];
                g[0x20 + d * 7 + p] = sh->sched_mode[d][p];
                g[0x2e + d * 7 + p] = sh->sched_patience[d][p];
            }
        put16(g + 0x3e, (unsigned)sh->x);
        g[0x40] = (uint8_t)top;
        g[0x41] = (uint8_t)bottom;
        for (int ff = 0; ff < 120; ff++) {
            int fidx = floor_to_index(ff - 10);
            if (fidx >= sh->lo && fidx <= sh->hi && fidx < TOWER_FLOOR_COUNT)
                g[0x42 + ff] = sh->serviced[fidx] ? 1 : 0;
        }
        for (int c = 0; c < CARS_PER_SHAFT; c++)
            g[0xba + c] = (uint8_t)(index_to_floor(sh->home[c]) + 10);

        emit(&out, 0x1e0);           /* per-stop timing scratch */
        emit(&out, 0x78);            /* up-call owners (zeros in the wild) */
        emit(&out, 0x78);            /* down-call owners */
        for (int ff = bottom; ff <= top; ff++)
            if (stop_slot(ftype, ff, bottom, top) >= 0)
                emit(&out, 0x144);   /* stop record: queues empty */
        for (int c = 0; c < 8; c++) {
            uint8_t *car = emit(&out, 0x15a);
            uint8_t home_ff = (uint8_t)(index_to_floor(sh->home[c]) + 10);
            car[0] = car[5] = car[6] = car[0x0d] = home_ff;
            car[7] = 1;              /* parked-at-destination flag */
            car[0x0f] = 1;
            memset(car + 0x10, 0xff, 0xe2 - 0x10);  /* empty passenger list */
        }
    }
    for (int g = ngroups; g < 24; g++)
        emit(&out, 0xc2);            /* inactive slot: zero header, no body */

    /* === post-elevator fixed blocks === */
    emit(&out, 0x58);
    emit(&out, 0x84);                /* finance window: recomputed in play */
    emit(&out, 0xc + 0x2a + 0x402 + 0x16);

    /* === stairs/escalators === */
    {
        uint8_t *st = emit(&out, 64 * 10);
        int n = 0;
        for (int i = 0; i < tower->tenant_count && n < 64; i++) {
            const Tenant *t = &tower->tenants[i];
            if (t->type != ITEM_STAIRS && t->type != ITEM_ESCALATOR) continue;
            uint8_t *s = st + n++ * 10;
            s[0] = 1;
            s[1] = t->type == ITEM_STAIRS ? 1 : 0;
            put16(s + 2, (unsigned)t->x);
            put16(s + 4, (unsigned)(t->floor + 10));
        }
    }

    /* === tail: rebuildable state, zeros === */
    emit(&out, 8 * 0x1e4);
    emit(&out, 0x78);
    emit(&out, 10 * 2);
    emit(&out, 0x200 * 6);
    emit(&out, 0x10 * 6);
    emit(&out, 10 * 4);
    emit(&out, 0x10 * 0xc);
    emit(&out, 0x50 + 0x28);
    emit(&out, 0x1102 + 0x842 + 0xca2);
    emit(&out, 8);

    /* === named tenants, appended after the serializer === */
    for (int i = 0; i < tower->twr_name_count && i < 20; i++)
        memcpy(emit(&out, 16), tower->twr_names[i], 16);
    if (out.fail) EFAIL("out of memory growing export buffer");

    FILE *f = fopen(path, "wb");
    if (!f) EFAIL("cannot create %s", path);
    size_t wr = fwrite(out.p, 1, out.pos, f);
    fclose(f);
    if (wr != (size_t)out.pos) EFAIL("short write to %s", path);

    printf("TWR export: %s — %ld bytes, %d star(s), $%ld, day %d; "
           "%d groups, %d retail slots\n",
           path, out.pos, tower->star_rating, tower->money, tower->day,
           ngroups, retail_used);
    free(out.p);
    return 0;
#undef EFAIL
}
