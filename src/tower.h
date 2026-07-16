/* tower.h - Tower grid data structure
 *
 * The tower is a 2D grid of cells. Each cell is 8 pixels wide × 36 pixels tall
 * (matching the original game's coordinate system).
 *
 * Floors range from B9 (-9) to roof (+100 at 5 stars).
 * Width is 63 cells (504 pixels).
 * Floor 0 is the ground/lobby level.
 */
#ifndef TOWER_H
#define TOWER_H

#include <stdint.h>

/* Grid dimensions */
#define TOWER_WIDTH       375   /* Cells wide (375 from decompiled 0x177 lobby constant + online sources) */
#define TOWER_MIN_FLOOR   -10   /* B10 (deepest basement — the file has
                                   ten: the metro platform bottoms out
                                   at B10, .TDT file floor 0) */
#define TOWER_MAX_FLOOR   100   /* Build ceiling at 5 stars */
#define TOWER_TOP_FLOOR   105   /* Storage top: the cathedral stands ABOVE
                                   the ceiling (real saves keep it at file
                                   floors 109-113 = port floors 99-103) */
#define TOWER_FLOOR_COUNT (TOWER_TOP_FLOOR - TOWER_MIN_FLOOR + 1)  /* 116 */
#define TOWER_LOBBY_FLOOR 0

/* Pixel dimensions per cell */
#define CELL_W  8
#define CELL_H  36     /* Total floor height: ceiling (12px) + tenant (24px) */
#define CEIL_H  12     /* Ceiling/roof strip height */
#define TENANT_H 24    /* Tenant content height (below ceiling) */

/* Tenant/item types (from the original game) */
typedef enum {
    ITEM_NONE = 0,
    ITEM_LOBBY,         /* Ground floor lobby (always present) */
    ITEM_FLOOR,         /* Empty floor (walkway) */
    ITEM_OFFICE,        /* Office: 9 cells wide, 1 floor */
    ITEM_CONDO,         /* Condo: 16 cells wide, 1 floor */
    ITEM_HOTEL_SINGLE,  /* Hotel single: 4 cells wide, 1 floor */
    ITEM_HOTEL_TWIN,    /* Hotel twin: 6 cells wide, 1 floor */
    ITEM_HOTEL_SUITE,   /* Hotel suite: 8 cells wide, 1 floor */
    ITEM_RESTAURANT,    /* Restaurant: 24 cells wide, 1 floor */
    ITEM_FAST_FOOD,     /* Fast food: 16 cells wide, 1 floor */
    ITEM_SHOP,          /* Shop: 8 cells wide, 1 floor */
    ITEM_CINEMA,        /* Cinema: 31 cells wide, 2 floors */
    ITEM_PARTY_HALL,    /* Party hall: 24 cells wide, 2 floors */
    ITEM_METRO,         /* Metro station: 30 cells wide, 3 floors, underground only */
    ITEM_PARKING,       /* Parking: 8 cells wide, underground only */
    ITEM_CATHEDRAL,     /* Cathedral: 16 cells wide, 2 floors */
    ITEM_MEDICAL,       /* Medical center: 6 cells wide, 1 floor */
    ITEM_SECURITY,      /* Security office: 6 cells wide, 1 floor */
    ITEM_RECYCLING,     /* Recycling center: 6 cells wide, 2 floors, underground */
    ITEM_STAIRS,        /* Stairs: 8 cells wide, connects 2 floors */
    ITEM_ESCALATOR,     /* Escalator: 8 cells wide, connects 2 floors */
    ITEM_ELEVATOR_SHAFT,/* Standard elevator shaft: 4 cells wide, variable height */
    ITEM_ELEVATOR_SERVICE, /* Service elevator: freight/staff transport */
    ITEM_ELEVATOR_EXPRESS, /* Express elevator: stops only on lobby/sky-lobby floors */
    ITEM_HOUSEKEEPING,  /* Housekeeping: services hotel rooms */
    ITEM_RAMP,          /* Parking ramp: 16 cells, one strip per basement
                           floor (file type 44) — kept for .TDT round-trip;
                           not yet buildable in the port */
    ITEM_TYPE_COUNT
} ItemType;

/* Item widths in cells — CANONICAL from SimTower (GameFAQs BStuart guide + sprite analysis)
 * Each cell = 8px. Frame widths verified against sprite bitmap dimensions. */
static const int ITEM_WIDTH[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 4,        /* 4-cell segments (extends existing lobby) */
    [ITEM_FLOOR] = 63,
    [ITEM_OFFICE] = 9,       /* 72px — sprite 288/4 frames = 72px ✓ */
    [ITEM_CONDO] = 16,       /* 128px — sprite 128px ✓ */
    [ITEM_HOTEL_SINGLE] = 4, /* 32px — create(9*32) = 9 frames × 32px ✓ */
    [ITEM_HOTEL_TWIN] = 6,   /* 48px — create(9*48) = 9 frames × 48px ✓ */
    [ITEM_HOTEL_SUITE] = 10, /* 80px — sprite 720/9 = 80px per frame (was 8, FIXED) */
    [ITEM_RESTAURANT] = 24,  /* 192px — sprite 768/4 = 192px per frame ✓ */
    [ITEM_FAST_FOOD] = 16,   /* 128px — sprite 512/4 = 128px per frame ✓ */
    [ITEM_SHOP] = 12,        /* 96px — GameFAQs: "Retail Shop 12" (was 8, FIXED) */
    [ITEM_CINEMA] = 31,      /* 248px — OpenSkyscraper: int2(31,2) ✓ */
    [ITEM_PARTY_HALL] = 24,  /* 192px — sprite 576/3 = 192px per frame ✓ */
    [ITEM_METRO] = 30,       /* 240px — OpenSkyscraper: int2(30,3) ✓ */
    [ITEM_PARKING] = 4,      /* 32px — GameFAQs: "Parking 4" (was 8, FIXED) */
    [ITEM_CATHEDRAL] = 28,   /* 224px — real .TDT saves store width 28
                                (was 16; fixed against SCHMITT/THEECSTA) */
    [ITEM_MEDICAL] = 26,     /* 208px — sprite 208px each, GameFAQs: "Medical 26" (was 6, FIXED) */
    [ITEM_SECURITY] = 16,    /* 128px — sprite 128px, GameFAQs: "Security 16" (was 6, FIXED) */
    [ITEM_RECYCLING] = 25,   /* 200px — sprite 200×60, GameFAQs: "Recycling 25x2" (was 6, FIXED) */
    [ITEM_STAIRS] = 8,       /* 64px — sprite 448/7 = 64px per frame ✓ */
    [ITEM_ESCALATOR] = 8,    /* 64px — sprite 512/8 = 64px per frame ✓ */
    /* EXE group type byte: 0=EXPRESS, 1=standard, 2=service (MakeElevator
     * jump table: item 1->type 1, item 42->type 0 cap 42 lobby-anchored,
     * item 43->type 2; the type-0-on-lobby-floors placement rule is the
     * express anchor). Widths from BuildRoutingSlots 11b0:0538:
     * type==0 (express) = 6 cells, others 4. */
    [ITEM_ELEVATOR_SHAFT] = 4, /* 32px */
    [ITEM_ELEVATOR_SERVICE] = 4,
    [ITEM_ELEVATOR_EXPRESS] = 6, /* 48px — the wide 42-person one */
    [ITEM_HOUSEKEEPING] = 15, /* 120px — real .TDT saves store width 15
                                 consistently (was 4; fixed 2026-06-11) */
    [ITEM_RAMP] = 16,        /* real .TDT saves: 16-cell strips */
};

/* Item heights in floors */
static const int ITEM_HEIGHT[] = {
    [ITEM_NONE] = 1,
    [ITEM_LOBBY] = 1,
    [ITEM_FLOOR] = 1,
    [ITEM_OFFICE] = 1,
    [ITEM_CONDO] = 1,
    [ITEM_HOTEL_SINGLE] = 1,
    [ITEM_HOTEL_TWIN] = 1,
    [ITEM_HOTEL_SUITE] = 1,
    [ITEM_RESTAURANT] = 1,
    [ITEM_FAST_FOOD] = 1,
    [ITEM_SHOP] = 1,
    [ITEM_CINEMA] = 2,       /* OpenSkyscraper: int2(31,2) */
    [ITEM_PARTY_HALL] = 2,   /* OpenSkyscraper: int2(24,2) */
    [ITEM_METRO] = 3,        /* OpenSkyscraper: int2(30,3) */
    [ITEM_PARKING] = 1,
    [ITEM_CATHEDRAL] = 5,    /* Real saves: five records (types 36-40),
                                file floors 109-113 — the dome spans five
                                floors above the build ceiling */
    [ITEM_MEDICAL] = 1,
    [ITEM_SECURITY] = 1,
    [ITEM_RECYCLING] = 2,
    [ITEM_STAIRS] = 2,       /* OpenSkyscraper: int2(8,2) */
    [ITEM_ESCALATOR] = 2,    /* OpenSkyscraper: int2(8,2) */
    [ITEM_ELEVATOR_SHAFT] = 1,
    [ITEM_ELEVATOR_SERVICE] = 1,
    [ITEM_ELEVATOR_EXPRESS] = 1,
    [ITEM_HOUSEKEEPING] = 1,
    [ITEM_RAMP] = 1,
};

/* Item costs — CANONICAL from SimTower (GameFAQs BStuart guide) */
static const int ITEM_COST[] = {
    [ITEM_NONE] = 0,
    [ITEM_LOBBY] = 5000,       /* Per lobby segment */
    [ITEM_FLOOR] = 500,        /* Per floor section */
    [ITEM_OFFICE] = 40000,
    [ITEM_CONDO] = 80000,      /* Was 200000, guide says 80k (income 50-200k once) */
    [ITEM_HOTEL_SINGLE] = 20000,  /* Guide: 20k */
    [ITEM_HOTEL_TWIN] = 50000,    /* Guide: 50k */
    [ITEM_HOTEL_SUITE] = 100000,  /* Guide: 100k */
    [ITEM_RESTAURANT] = 200000,
    [ITEM_FAST_FOOD] = 100000,
    [ITEM_SHOP] = 100000,
    [ITEM_CINEMA] = 500000,
    [ITEM_PARTY_HALL] = 100000,   /* Guide: 100k (was 500k) */
    [ITEM_METRO] = 1000000,
    [ITEM_PARKING] = 3000,        /* Guide: 3k (was 30k) */
    [ITEM_CATHEDRAL] = 0,         /* Special unlock, free */
    [ITEM_MEDICAL] = 500000,
    [ITEM_SECURITY] = 100000,
    [ITEM_RECYCLING] = 500000,
    [ITEM_STAIRS] = 5000,
    [ITEM_ESCALATOR] = 20000,
    /* Elevator prices EXE-verified: cost resource 0x7f0b id 0x3e8,
     * BE u32 per item id, x$100: item 0x01=2000, 0x2a=4000, 0x2b=1000. */
    [ITEM_ELEVATOR_SHAFT] = 200000,   /* Standard elevator */
    [ITEM_ELEVATOR_SERVICE] = 100000, /* Service elevator (was 80k folklore) */
    [ITEM_ELEVATOR_EXPRESS] = 400000, /* Express elevator */
    [ITEM_HOUSEKEEPING] = 100000,
    [ITEM_RAMP] = 50000,
};

/* Star rating at which each item unlocks (Campaign mode). Dual-sourced
 * (kiwizoid GameFAQs cost table + simtower.fandom). Sandbox ignores this. */
static const int ITEM_STAR_REQ[] = {
    [ITEM_NONE] = 1,
    [ITEM_LOBBY] = 1, [ITEM_FLOOR] = 1, [ITEM_OFFICE] = 1, [ITEM_CONDO] = 1,
    [ITEM_FAST_FOOD] = 1, [ITEM_STAIRS] = 1, [ITEM_ELEVATOR_SHAFT] = 1,
    [ITEM_HOTEL_SINGLE] = 2, [ITEM_SECURITY] = 2, [ITEM_HOUSEKEEPING] = 2,
    [ITEM_ELEVATOR_SERVICE] = 2,
    [ITEM_HOTEL_TWIN] = 3, [ITEM_HOTEL_SUITE] = 3, [ITEM_RESTAURANT] = 3,
    [ITEM_SHOP] = 3, [ITEM_CINEMA] = 3, [ITEM_PARTY_HALL] = 3,
    [ITEM_ELEVATOR_EXPRESS] = 3, [ITEM_ESCALATOR] = 3, [ITEM_PARKING] = 3,
    [ITEM_RECYCLING] = 3, [ITEM_MEDICAL] = 3, [ITEM_RAMP] = 3,
    [ITEM_METRO] = 4,
    [ITEM_CATHEDRAL] = 5,
};

/* Which items are underground-only */
static const int ITEM_UNDERGROUND_ONLY[] = {
    [ITEM_NONE] = 0, [ITEM_LOBBY] = 0, [ITEM_FLOOR] = 0,
    [ITEM_OFFICE] = 0, [ITEM_CONDO] = 0,
    [ITEM_HOTEL_SINGLE] = 0, [ITEM_HOTEL_TWIN] = 0, [ITEM_HOTEL_SUITE] = 0,
    [ITEM_RESTAURANT] = 0, [ITEM_FAST_FOOD] = 0, [ITEM_SHOP] = 0,
    [ITEM_CINEMA] = 0, [ITEM_PARTY_HALL] = 0,
    [ITEM_METRO] = 1,       /* Underground only */
    [ITEM_PARKING] = 1,     /* Underground only */
    [ITEM_CATHEDRAL] = 0,
    [ITEM_MEDICAL] = 0, [ITEM_SECURITY] = 0,
    [ITEM_RECYCLING] = 1,   /* Underground only */
    [ITEM_STAIRS] = 0, [ITEM_ESCALATOR] = 0, [ITEM_ELEVATOR_SHAFT] = 0,
    [ITEM_ELEVATOR_SERVICE] = 0, [ITEM_ELEVATOR_EXPRESS] = 0, [ITEM_HOUSEKEEPING] = 0,
    [ITEM_RAMP] = 1,    /* Underground only */
};

/* TowerCell.flags bits. Only two are used today; named so the grid code
 * reads as prose instead of bare 1/2 literals. (flags stays a uint8_t so the
 * grid memory and native save layout are unchanged.) */
#define CELL_OCCUPIED           0x01   /* bit 0: a tenant occupies this cell */
#define CELL_TRANSPORT_OVERLAY  0x02   /* bit 1: a stair/escalator overlay sits here */

/* A single cell in the tower grid */
typedef struct {
    ItemType type;
    uint16_t tenant_id;    /* Which tenant instance owns this cell (0 = none) */
    uint8_t  cell_index;   /* Position within the tenant (0 = leftmost) */
    uint8_t  flags;        /* CELL_* bit flags (occupied, transport overlay) */
} TowerCell;

static inline int cell_has_transport_overlay(const TowerCell *c) {
    return (c->flags & CELL_TRANSPORT_OVERLAY) != 0;
}

/* Hotel room condition — the port's explicit version of the EXE's packed
 * status-byte bands (tenant +0x0B: clean 0x18/0x20, dirty 0x28/0x30,
 * infested 0x38/0x40; the pair is only the day/night sprite variant).
 * Decoded by the 2026-07-09 referees (referee_84day_hotelbit +
 * referee_infested_checkin):
 *   - checkout always makes the room DIRTY (MoneyT 1178:0eac)
 *   - housekeepers clean DIRTY rooms only — they never touch INFESTED
 *     (MainteT 1150:00b4/00c9 match {0x28,0x30} exactly)
 *   - 3 daily passes spent dirty-and-unrented -> INFESTED (JudgeT 1130:0e5c)
 *   - INFESTED spreads to adjacent hotel rooms, one per side per day,
 *     clean and occupied alike (JudgeT ExpandoBadHotel 1130:01e2)
 *   - dirty and infested rooms take NO new guests (the booking flag below
 *     can never arm for them), so demolition is the only infestation cure */
typedef enum {
    ROOM_CLEAN = 0,
    ROOM_DIRTY,
    ROOM_INFESTED
} RoomCondition;

static inline int item_is_hotel_room(ItemType t) {
    return t == ITEM_HOTEL_SINGLE || t == ITEM_HOTEL_TWIN ||
           t == ITEM_HOTEL_SUITE;
}

/* Tenant instance — represents one placed item.
 *
 * NOTE: game_save() (game.c) writes the whole Tower struct, tenants included,
 * as a raw byte image guarded by sizeof(Tower). Do NOT reorder these fields
 * without bumping SAVE_VERSION — a reorder keeps the same size, so old saves
 * would pass the size check and then read every field from the wrong offset.
 * The fields below are grouped by section-comment only; their order is the
 * save layout. */
typedef struct {
    /* --- Identity & placement --- */
    uint16_t id;           /* Unique ID */
    ItemType type;
    int      floor;        /* Which floor */
    int      x;            /* Left cell position */
    int      width;        /* Width in cells */
    int      height;       /* Height in floors */
    uint8_t  state;        /* Lifecycle state (TenantState enum) */

    /* --- Live occupancy (live byte + persistent tier; see the cap_* and
     *     occupancy_tier helpers in game.h) --- */
    uint8_t  capacity;     /* LIVE capacity byte — drives sprite frame selection.
                            * From TenantMake: the byte packs a persistent tier
                            * plus a daily oscillation. This field is the live,
                            * time-of-day-modulated value; cap_peak below is the
                            * persistent tier ceiling it oscillates beneath.
                            * Steps: 0x10→0x18→0x20→0x28→0x30→0x38→0x40
                            * Offices fill during day (ascending), empty at night.
                            * Hotels fill at night (reverse), empty during day. */
    uint8_t  cap_peak;     /* PERSISTENT occupancy ceiling (the capacity byte's
                            * tier top). Offices: 0x20 new → 0x30 → 0x40 thriving,
                            * grown by MainteT when content, forced to top by the
                            * 3-strike check, and spread to neighbours by office
                            * gentrification. Hotels/suites: their max room
                            * occupancy, raised by the happy-tenant upgrade.
                            * 0 = not peak-managed (retail, services, condos).
                            * Reconstructed from the live byte's tier on import. */
    /* --- Construction & simulation state --- */
    int      construction; /* Construction ticks remaining (0 = built).
                            * From TenantMake: office=2, condo=3, restaurant=48,
                            * hotel=56, cathedral=240, etc. */
    int      population;   /* Number of people currently here */
    int      stress;       /* Satisfaction level (lower = happier, 0-100) */
    int      complaints;   /* Strike count (3 = forced action). From MainteT. */
    int      zone;         /* Commercial zone (0-6, floors/15). From JudgeT. */
    int      upgrade_day;  /* Last upgrade day (3-day cadence). From MainteT. */

    /* --- Episodic / daily state (hotel rooms, fire) --- */
    uint8_t  hosted;       /* Hotel room: guests stayed overnight */
    uint8_t  condition;    /* Hotel room: RoomCondition (clean/dirty/infested) */
    uint8_t  burned;       /* Destroyed by fire — renders as rubble until rebuilt */
    uint8_t  cleaned_today;/* Housekeeping unit: rooms cleaned since dawn */
    uint8_t  patients_today; /* Medical center: patients admitted since 7AM
                                (hard cap 40/day — InMedicalPeple 1170:0291;
                                a full center turns patients away SILENTLY) */

    /* --- The demand model (EXE tenant bytes +0x14/+0x15/+0x17) — SHARED
     *     by two systems on the same bytes: hotel rooms on the 5PM pass
     *     (referee_infested_checkin_2026-07-10) and office/condo/shop on
     *     the daily 4:59AM judge (referee_vacancy_relet_2026-07-11). --- */
    uint8_t  demand_armed; /* EXE +0x14 —
                                * HOTEL: "takes guests tonight" — the booking
                                * gate (UniPeple 1220:3032); armed/disarmed by
                                * the 5PM pass; nothing can arm a dirty or
                                * infested room.
                                * OFFICE/CONDO/SHOP: "on the market" — a
                                * VACANT unit's movers only travel while
                                * armed. Cleared at vacate; re-armed only by
                                * the daily judge (category != 0) or the
                                * move-out pairing. */
    uint8_t  demand_category;  /* EXE +0x15 — the judge's verdict:
                                * 2 = content (metric < 80)
                                * 1 = middle  (metric < the star bar 150/200)
                                * 0 = stressed — hotel rooms are disarmed
                                *     for the night (unless paired);
                                *     office/condo/shop move OUT on the
                                *     3rd-day tick and won't re-arm while
                                *     judged 0. 0xFF = not yet judged. */
    uint8_t  tenure;     /* EXE +0x17 — dual-purpose by type:
                                * HOTEL: consecutive 5PM passes spent
                                * dirty-and-unrented; at exactly 3 the room
                                * turns INFESTED (reset by check-in only —
                                * maids never touch it).
                                * SHOP: quarters since (re)let — tenure 0
                                * shops are immune to the stress move-out. */
    /* Pool elevator-stress accumulator feeding the demand verdicts. The
     * EXE keeps this per-person (+0x0E total / +0x09 periods) and averages
     * the unit's people at judge time; the port banks the same "felt"
     * wait-stress (post lobby-forgiveness, people.c deliver_stress) per
     * UNIT and resets when the unit re-arms/re-lets. FROZEN while vacant
     * — the frozen bad memory is what keeps a vacated unit condemned. */
    uint16_t pool_stress_total;
    uint16_t pool_stress_trips;

    /* --- Venues (VenueT seg_1180, byte-verified 2026-07-02 annotation) ---
     * Cinemas and party halls run a daily show cycle: reset at 10AM (film
     * ages, quotas set), matinee + evening showings for movies / one
     * evening event for the party hall, tiered income at close from the
     * day's total attendance. */
    uint8_t  movie_id;         /* cinema: 0..13 (0-6 ordinary, 7-13 hits);
                                  party hall: 0xFF; others: 0 */
    uint8_t  venue_age_days;   /* days since the film changed (caps 0x7F);
                                  attendance tier = age/3 */
    uint8_t  venue_state;      /* 0 closed / 1 open / 2 has patrons / 3 show */
    uint8_t  quota_matinee;    /* patrons admittable per showing today */
    uint8_t  quota_evening;
    uint16_t patrons_today;    /* total admitted today -> the income tier */

    /* --- Retail patron economy (Restaurant.c seg_11a8 venue record,
     * byte-verified 2026-07-11 referee). Restaurants and fast food earn
     * ONE income event per day at close — tiered on customers_today,
     * bottom tier a LOSS; shops earn quarterly rent and book nothing
     * here. The three service scores are quality accumulators: each
     * arriving customer grades their elevator wait into the current
     * period's slot, and that slot becomes tomorrow's walk-in quota. */
    uint8_t  retail_score[3];  /* service score: weekday/weekend/rainy
                                  (file retail record +3/+4/+5) */
    uint8_t  retail_quota;     /* today's walk-in quota, counts DOWN as
                                  walk-ins dispatch (record +6) */
    uint8_t  walkins_today;    /* walk-ins admitted today (+7) */
    uint8_t  patrons_now;      /* inside right now, cap 40 (+9) */
    uint8_t  retail_open;      /* doors open (record +2 state != 3) */
    uint16_t customers_today;  /* the income driver (+0x10) */

    /* --- Rent --- */
    uint8_t  rent_class;   /* 0 High / 1 Average / 2 Low / 3 Very Low —
                              the map's Rent overlay (file tenant +0x0F;
                              JudgeT adjusts it in the EXE) */

    /* --- Info-dialog display state (not persisted to .TDT; resets on load) --- */
    uint8_t  let_quarters;    /* office/condo tenancy length in quarters — the
                                 dialog's "Length" field (EXE tenant +0x17);
                                 bumped each settlement while occupied. */
    int32_t  yesterday_profit;/* restaurant/fast-food: last night's settled
                                 profit (can be negative) — the dialog's
                                 "Yesterday's Profit" field (EXE commercial +0xA). */

    /* --- Parking space (ITEM_PARKING only) --- */
    uint8_t  space_usable; /* on the B1-anchored ramp chain, not cut off by
                              a >=4-cell bare gap (ParkingT CheckAllParking;
                              recomputed daily at 7AM + on build/demolish) */
    uint16_t space_ordinal;/* position among usable spaces (recomputed with
                              space_usable) — spaces with ordinal < parked
                              cars render a car, the rest an empty bay */

    /* --- .TDT round-trip side data (preserved verbatim across import/export) --- */
    /* Slot in the file's 512-entry retail subtype table, stored +1 so
     * 0 = none. Set on import from tenant byte +0x06; allocated at
     * export for retail built in the port. */
    uint16_t retail_ref;
    /* Raw file-tenant bytes +0x0C,+0x0D,+0x0E,+0x10,+0x11 — meaning
     * unknown, preserved verbatim so exports of imported towers keep
     * them. All-zero means "no file origin" and export uses defaults. */
    uint8_t  twr_unk[5];
} Tenant;

/* Big imported towers (THEECSTA.TDT) carry ~4100 tenant strips plus one
 * tenant per shaft-floor segment — 4096 overflowed on real saves. */
#define MAX_TENANTS 8192

/* The tower */
typedef struct {
    TowerCell grid[TOWER_FLOOR_COUNT][TOWER_WIDTH];
    Tenant    tenants[MAX_TENANTS];
    int       tenant_count;
    uint16_t  next_tenant_id;
    
    int       star_rating;     /* 1-5 stars */
    long      money;           /* Current funds */
    int       population;      /* Total people in tower */
    int       day;             /* Game day counter */
    int       quarter;         /* 0-3: Q1, Q2, Q3, Weekend */

    /* Value tracking (for analytics): construction value currently
     * standing, and value destroyed by bulldozing */
    long      built_value;
    long      lost_value;
    
    /* Camera position (pixel coordinates of top-left corner) */
    int       cam_x;
    int       cam_y;

    /* .TDT round-trip side data (set by twr_import, written back by
     * twr_export): raw file blocks the port doesn't model. The header
     * is the full 0x230-byte serialized header (version word through
     * the 0x1EA remainder); modeled fields are overridden at export.
     * The retail table is the 512 x 18-byte subtype/state block. */
    uint8_t   twr_header[0x230];
    uint8_t   twr_header_valid;
    uint8_t   twr_retail[0x2400];
    /* Named tenants ("Office Girl", ...): 16-byte C strings appended
     * after the serializer proper; preserved for round-trip. */
    char      twr_names[20][16];
    int       twr_name_count;

    /* --- Parking (ParkingT, byte-verified 2026-07-11 referee) --- */
    int       usable_spaces;   /* spaces on the B1 ramp chain (recomputed) */
    int       cars_office;     /* cars in per admitting category; quota =
                                  2 x usable_spaces EACH (double-parking is
                                  real — the quota is the limiter, not
                                  per-space occupancy) */
    int       cars_suite;
} Tower;

/* Vertical transport categories */
static inline int item_is_elevator(ItemType t)
{
    return t == ITEM_ELEVATOR_SHAFT || t == ITEM_ELEVATOR_SERVICE ||
           t == ITEM_ELEVATOR_EXPRESS;
}

static inline int item_is_transport(ItemType t)
{
    return t == ITEM_STAIRS || t == ITEM_ESCALATOR || item_is_elevator(t);
}

/* Convert floor number to grid array index */
static inline int floor_to_index(int floor)
{
    return floor - TOWER_MIN_FLOOR;
}

/* Convert grid array index to floor number */
static inline int index_to_floor(int index)
{
    return index + TOWER_MIN_FLOOR;
}

/* Per-floor extents of the floor map — the left/right pair the EXE keeps in
 * its per-floor records: every cell a non-transport tenant covers, including
 * multi-floor continuations. Arrays are indexed by floor index and must hold
 * TOWER_FLOOR_COUNT entries; right is EXCLUSIVE, right == 0 marks an empty
 * floor. Anchors overlay decorations, the crane, and disaster targeting. */
void tower_floor_extents(const Tower *tower, int16_t *left, int16_t *right);

/* Initialize a new tower with lobby on floor 0 */
void tower_init(Tower *tower);

/* Place an item. Returns tenant_id on success, 0 on failure. */
uint16_t tower_place(Tower *tower, ItemType type, int floor, int x);

/* Remove a tenant. Returns 1 on success. */
int tower_remove(Tower *tower, uint16_t tenant_id);

/* Place with explicit width, no cost, no validation beyond bounds — for
 * .TDT import (file strips like floor/lobby have arbitrary widths). */
uint16_t tower_import_item(Tower *tower, ItemType type, int floor, int x,
                           int width);

/* Check if placement is valid (no overlap, valid floor, etc.) */
int tower_can_place(Tower *tower, ItemType type, int floor, int x);

/* Get the cell at grid position. Returns NULL if out of bounds. */
TowerCell *tower_cell(Tower *tower, int floor, int x);

/* Get tenant by ID. Returns NULL if not found. */
Tenant *tower_tenant(Tower *tower, uint16_t id);

/* Get human-readable name for an item type */
const char *tower_item_name(ItemType type);

/* Pre-populate a demo tower with one of each building type */
void tower_build_demo(Tower *tower);

#endif /* TOWER_H */
