/* SimTower for Linux - main.c
 *
 * A native Linux port of SimTower, using game mechanics extracted from
 * decompilation of SIMTOWER.EXE and guided by the YootTower code map.
 * Sprite IDs verified against OpenSkyscraper SimTowerLoader.cpp.
 */
#define _USE_MATH_DEFINES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <SDL.h>
#include <SDL_ttf.h>
#include "ne_resource.h"
#include "sprites.h"
#include "tower.h"
#include "game.h"
#include "twr.h"
#include "audio.h"
#include "sound_hook.h"
#include "strings.h"

/* ---------- Window / display ---------- */
#define WINDOW_W    960
#define WINDOW_H    720
#define HUD_HEIGHT  0     /* No separate HUD — info window handles it */
#define MENU_BAR_H  18    /* Win3.1 menu bar across the top */
#define MENU_ITEM_PAD 12  /* Horizontal padding for menu items (kept for dropdown code) */

/* ---------- Sprite IDs for rendering ---------- */
/* Verified against OpenSkyscraper's SimTowerLoader.cpp */

/* Sky background tiles (32×360 each, palette-swapped for time of day) */
#define SPR_SKY_BASE    0x8351
#define SPR_SKY_COUNT   11

/* Lobby: assembled from raw bitmaps (992×36 each, 3 variants) */
#define SPR_LOBBY_BOT0  0x89e8   /* Lobby ground level, variant 0 (raw) */
#define SPR_LOBBY_BOT1  0x89e9
#define SPR_LOBBY_BOT2  0x89ea
#define SPR_LOBBY_MID0  0x8a28   /* Grand lobby middle row (raw, chunk x=328) */
#define SPR_LOBBY_TOP0  0x8a68   /* Grand lobby top row (raw, chunk x=0) */

/* Floor/ceiling color source — 96×36, extract column at x=16 for floor color */
#define SPR_FLOOR_SRC   0x83e8

/* Office interior sheets (each frame 72px wide, 24px tall):
 *   0x85a8/0x85a9/0x85aa — 288×24 = 4 frames = two furniture variants each,
 *                          every variant a lit/unlit pair (six variants total).
 *   0x85ab              — 144×24 = 2 frames = the VACANT office, lit/unlit.
 * Variant/lit/vacant selection lives in the ITEM_OFFICE render branch. */
#define SPR_OFFICE_BASE 0x85a8

/* Condo: 0x8628+ — 128×24 */
#define SPR_CONDO_BASE  0x8628

/* Restaurant: 0x8568+ — 384×24 */
#define SPR_RESTAURANT_BASE 0x8568

/* Fast food: 0x85e8 — 480×24 */
#define SPR_FASTFOOD_BASE 0x85e8

/* Hotel single: 0x84A8 — door 32×24, room 256×24 */
#define SPR_HOTEL_S_BASE 0x84a8

/* Hotel twin: 0x84E8 — door 48×24, room 384×24 */
#define SPR_HOTEL_T_BASE 0x84e8

/* Hotel suite: 0x8528+0x8529 (row 1), 0x852A+0x852B (row 2) */
#define SPR_HOTEL_SUITE_A 0x8528
#define SPR_HOTEL_SUITE_B 0x8529

/* Shop: 0x8668-0x8672 = 11 shop type variants, each 288×24 (3 frames of 96px)
 *        0x8673 = shared "For RENT" storefront, 0x8674 = shared closed
 *        shutters (single 96×24 frames — the EXE's frames 0x21/0x22 past
 *        the 11×3 variant frames)
 * OpenSkyscraper: loadMergedByID(shops[1], 'y', 0x8668..0x8672) */
#define SPR_SHOP_BASE   0x8668  /* 288×24, 3 frames of 96px */
#define SPR_SHOP_VARIANTS 11    /* 0x8668 through 0x8672 */
#define SPR_SHOP_END0   0x8673  /* shared For-RENT frame (EXE frame 0x21) */
#define SPR_SHOP_END1   0x8674  /* shared closed frame  (EXE frame 0x22) */

/* Security: 0x8768 (animated, 3 frames via palette cycling) */
#define SPR_SECURITY    0x8768
#define SPR_HOUSEKEEPING 0x87A8  /* laundry room: washers + linen carts (120×24) */
#define SPR_SECURITY_F1 0xf768  /* synthetic: palette-cycle steps 1/2 */

/* Medical: 0x8728+0x8729+0x872A (3 bitmaps horizontally) */
#define SPR_MEDICAL_A   0x8728
#define SPR_MEDICAL_B   0x8729
#define SPR_MEDICAL_C   0x872A

/* Cathedral exterior: five 448x36 strips (2 frames of 224px: day|night),
 * top dome 0x8CE8 down to entrance 0x8DE8 (step 0x40). The 224x36
 * singles next to them (0x8CE9..0x8DE9 + 0x8DEA) are the TOWER CEREMONY
 * state: cherubs, "Welcome to Tower" banner, priest and congregation —
 * six strips, one floor taller than the building. (An earlier note here
 * claimed no exterior sprite exists — wrong: it hid as per-floor strips.)
 * 0x8828 (128x36) is the WEDDING PROCESSION ANIMATION — brides in white,
 * grooms in top hats — used during the CheckMarry event.
 * ChurchT.c: OpenChurch, CloseChurch, StartMarry, CheckMarry.
 * Wedding event required for the 5-star -> TOWER transition. */
#define SPR_CATH_STRIP_TOP 0x8CE8  /* +0x40 per floor going down, 5 strips */
#define SPR_WEDDING_ANIM   0x8828  /* event animation, not a building */

/* Recycling: 0x88E8 (empty state, single DIB) */
#define SPR_RECYCLING_EMPTY 0x88e8

/* Parking space: 0x86A8+0x86A9 */
#define SPR_PARKING_A   0x86A8
#define SPR_PARKING_B   0x86A9

/* Party Hall: 0x8B28 (top) + 0x8B68 (bottom) vertically */
#define SPR_PARTYHALL_TOP 0x8B28
#define SPR_PARTYHALL_BOT 0x8B68

/* Cinema hall: 0x8868 (upper, animated marquee) + 0x88A8 (lower) */
#define SPR_CINEMA_UPPER 0x8868
#define SPR_CINEMA_LOWER 0x88A8
#define SPR_CINEMA_UPPER_F1 0xf868  /* synthetic: cycled marquee steps */

/* Cinema screens: 0x8C68+0x8CA8 (screen 0), 0x8C69+0x8CA9 (screen 1) */
#define SPR_CINEMA_SCR0_TOP 0x8C68
#define SPR_CINEMA_SCR0_BOT 0x8CA8

/* Metro: 0x8BA8/0x8BA9 (row 0), +0x40 each row */
#define SPR_METRO_BASE  0x8BA8

/* Elevator: shaft + cars */
/* Car sheets — labels per decomp globals.md #52: type 0 = EXPRESS (the
 * sky-lobby placement anchor at 11f8:0ff9 plus the $400k price in cost
 * resource 0x7f0b:0x3e8 settle it). The wide 6-cell shaft, the 48px
 * dense-crowd 42-person car and the double-drum engine all belong to the
 * EXPRESS; OpenSkyscraper's wide-express labeling was right all along. */
#define SPR_ELEV_STD_EMPTY 0x8428 /* standard car, empty (32×36) */
#define SPR_ELEV_STD_LOADED 0x8429 /* standard frames 1-4 + narrow engine (×32) */
#define SPR_ELEV_SERVICE 0x842a   /* service car frames 0-4 (×32) */
#define SPR_ELEV_EXPRESS 0x842b  /* express frames 0-4 + wide engine (×48) */
#define SPR_ELEV_EXT     0x842c   /* 2 8px shaft side extensions (wide shaft) */
/* Synthetic ids: palette-cycle animation frames of the engine sheets */
#define SPR_ELEV_STD_F1  0xf829
#define SPR_ELEV_STD_F2  0xf929
#define SPR_ELEV_EXP_F1  0xf82b
#define SPR_ELEV_EXP_F2  0xf92b
#define SPR_ELEV_QUEUE   0x8468   /* waiting people silhouettes (40 × 16px) */

/* Person figure sheet (InfoPeple blitter 1100:364a): RT_BITMAP 0x2BC
 * normal row / 0x2BE named row / 0x2BF VIP row — each 96x24, 12 cells of
 * 8px; frames 0-5 are 8px wide, frames 6/8/10 are 16px (two cells).
 * Popup portrait draws 2x, occupant/rider lists draw 1:1. */
#define SPR_FIGURE_NORMAL 0x82BC
#define SPR_FIGURE_NAMED  0x82BE
#define SPR_FIGURE_VIP    0x82BF
#define SPR_ELEV_SHAFT   0x87e8   /* shaft sections: tile 0 plain, 1+ digits */
#define SPR_ELEV_DIGITS  0x87e9   /* floor digits 0-9, 11x17 glyphs at
                                   * (1+16*n, 16); bg 25,25,25 keyed out. */
#define SPR_ELEV_DIGITS_RED 0x87ec /* red twin of 0x87e9: the floor-number plate
                                    * lights red when a car of the group is at
                                    * that floor (IsCarOnFloor gate, decomp
                                    * seg_10a8:367 — the +0x58 highlight bank) */

/* Stairs: 0x8968 (top) + 0x89A8 (bottom) */
#define SPR_STAIRS_TOP    0x8968
#define SPR_STAIRS_BOT    0x89a8

/* Escalator: 0x8AA8 (top) + 0x8AE8 (bottom) */
#define SPR_ESCALATOR_TOP 0x8aa8
#define SPR_ESCALATOR_BOT 0x8ae8

/* Underground dirt: 0x8F28 */
#define SPR_UNDERGROUND  0x8f28

/* Clouds — 4 different cloud shapes (DIB bitmaps) */
#define SPR_CLOUD_0      0x8384   /* 96×41 */
#define SPR_CLOUD_1      0x8385   /* 192×19 */
#define SPR_CLOUD_2      0x8386   /* 292×38 */
#define SPR_CLOUD_3      0x8387   /* 216×43 */
#define SPR_CLOUD_COUNT  4
/* Decorative sprites (from OpenSkyscraper SimTowerLoader.cpp) */
#define SPR_SANTA        0x8388   /* 140×48 — Santa helicopter Easter egg */
#define SPR_SKYLINE      0x8389   /* Background city skyline */
#define SPR_ENTRANCES    0x83E9   /* Entrance awning — the iconic red awning! */
#define SPR_CRANE        0x83EA   /* Construction crane — appears during building */
#define CRANE_NONE       -1000    /* crane_floor sentinel (B-floors are negative) */
#define SPR_FIRELADDER   0x842D   /* Fire escape stairs — zigzag up the side */
/* Disaster event art (IDs confirmed via OpenSkyscraper + decomp FireT/EventT) */
#define SPR_FIRE_0       0x8F68   /* flame frame 0 — 96x36 = 12 cells x 1 floor */
#define SPR_FIRE_1       0x8F69   /* flame frame 1 */
#define SPR_FIRE_2       0x8F6A   /* flame frame 2 */
#define SPR_FIRE_3       0x8F6B   /* flame frame 3 (FireT animates frame = b3de%4) */
#define SPR_FIRE_CHOPPER 0x8F6D   /* firefighting helicopter */
#define SPR_FIRE_DESTROY 0x8FA8   /* burnt-out cell (fire aftermath) */
#define SPR_ALERT_TERROR 0xA710   /* terrorist/bomb alert icon, 76x67 */
#define SPR_ALERT_FIRE   0xA714   /* fire alert icon, 76x60 */
#define SPR_CONST_GRID   0x8E28   /* Construction grid placeholder */
#define SPR_CONST_SOLID  0x8E29   /* Construction solid fill */
#define SPR_CONST_WORKER 0x85EA   /* Construction worker sprite */

/* UI */
#define SPR_TOOLBAR      0x8140

/* ---------- Composite sprite IDs (assembled at init from raw parts) ---------- */
#define SPR_FASTFOOD_COMP     0x0010  /* 0x86E8 + 0x86E9 joined horizontally */
#define SPR_HOTEL_S_COMP      0x0011  /* 0x84A8 + 0x84A9 joined horizontally */
#define SPR_HOTEL_T_COMP      0x0012  /* 0x84E8 + 0x84E9 joined horizontally */
#define SPR_STAIRS_COMP       0x0013  /* 0x8968 + 0x89A8 joined vertically */
#define SPR_ESCALATOR_COMP    0x0014  /* 0x8AA8 + 0x8AE8 joined vertically */
#define SPR_RESTAURANT_COMP   0x0015  /* 0x8568 + 0x8569 joined horizontally */
#define SPR_HOTEL_SUITE_COMP  0x0016  /* 0x8528 + 0x8529 joined horizontally */
#define SPR_MEDICAL_COMP      0x0017  /* 0x8728 + 0x8729 + ... */
#define SPR_PARKING_COMP      0x0018  /* 0x86A8 + 0x86A9 */
#define SPR_PARTYHALL_COMP    0x0019  /* 0x8B28 + 0x8B68 vertically */
#define SPR_CINEMA_COMP       0x001A  /* cinema hall composite */
#define SPR_METRO_COMP        0x001B  /* metro station composite */
#define SPR_CINEMA_COMP_F1    0x001C  /* cinema with cycled marquee */
#define SPR_CATHEDRAL_COMP    0x001D  /* 5 strips stacked: 448x180, day|night */
#define SPR_CATH_CEREMONY     0x001E  /* TOWER wedding: 6 strips, 224x216 —
                                         cherubs + banner row floats one floor
                                         ABOVE the dome (0x8CE9..0x8DEA) */
/* Retail storefront variants (retail table byte +0x0B picks one).
 * Restaurants 0x8568+2v paired with 0x8569+2v (frames: empty, busy,
 * packed, closed), fast food likewise from 0x86E8; shops are single
 * sheets 0x8668+v with three fill frames, used straight from the EXE. */
#define SPR_RESTAURANT_V0     0x0020  /* ..0x0024, 5 variants */
#define SPR_FASTFOOD_V0       0x0025  /* ..0x0029, 5 variants */
#define SPR_RECYCLING_COMP    0x002B  /* 5-frame trash-accumulation cycle: top row
                                       * 0x88E9..0x88ED over bottom 0x8929..0x892D
                                       * (OS loadRecycling). 200px frames. */
/* Style-variant composites (style trace 2026-07-29): each type's extra
 * styles are the next consecutive resource pairs after the style-0 art.
 * Selection = Tenant.style; missing art falls back to style 0. */
#define SPR_HOTEL_S_S1        0x0040  /* 0x84AA + 0x84AB */
#define SPR_HOTEL_T_S1        0x0041  /* 0x84EA + 0x84EB (S2/S3 follow) */
#define SPR_HOTEL_T_S2        0x0042
#define SPR_HOTEL_T_S3        0x0043
#define SPR_HOTEL_SUITE_S1    0x0044  /* 0x852A + 0x852B */
#define SPR_CONDO_S1          0x0045  /* 0x862D..0x8631 joined */
#define SPR_CONDO_S2          0x0046  /* 0x8632..0x8636 joined */

#define SPR_CONDO_COMP        0x002A  /* 0x8628..0x862c joined = 5 frames of 128px:
                                       * 0 occupied-day, 1 occupied-evening,
                                       * 2 occupied-night, 3 for-sale-day,
                                       * 4 for-sale-night (OS loadCondo). */

/* ---------- Sprite mapping for item types ---------- */
static uint16_t item_sprite_id(ItemType type, int *frame_w, int *floors)
{
    *floors = ITEM_HEIGHT[type];
    switch (type) {
    case ITEM_LOBBY:         *frame_w = 0;   return SPR_LOBBY_BOT0;
    case ITEM_OFFICE:        *frame_w = 72;  return SPR_OFFICE_BASE;
    case ITEM_CONDO:         *frame_w = 128; return SPR_CONDO_COMP;
    case ITEM_HOTEL_SINGLE:  *frame_w = 32;  return SPR_HOTEL_S_COMP;
    case ITEM_HOTEL_TWIN:    *frame_w = 48;  return SPR_HOTEL_T_COMP;
    case ITEM_HOTEL_SUITE:   *frame_w = 80;  return SPR_HOTEL_SUITE_COMP;  /* 720/9=80 */
    case ITEM_RESTAURANT:    *frame_w = 192; return SPR_RESTAURANT_COMP;
    case ITEM_FAST_FOOD:     *frame_w = 128; return SPR_FASTFOOD_COMP;
    case ITEM_SHOP:          *frame_w = 96;  return SPR_SHOP_BASE; /* 288×24, 3 frames of 96px */
    case ITEM_CINEMA:        *frame_w = 192; return SPR_CINEMA_COMP;   /* 768/4=192 per frame */
    case ITEM_PARTY_HALL:    *frame_w = 192; return SPR_PARTYHALL_COMP; /* 576/3=192 per frame */
    case ITEM_METRO:         *frame_w = 240; return SPR_METRO_COMP;  /* 720/3=240 per frame */
    case ITEM_PARKING:       *frame_w = 32;  return SPR_PARKING_COMP;
    case ITEM_CATHEDRAL:     *frame_w = 224; return SPR_CATHEDRAL_COMP; /* 448/2:
        * day|night frames, 5 floors tall (composited at init) */
    case ITEM_MEDICAL:       *frame_w = 208; return SPR_MEDICAL_COMP;  /* 3 states × 208px */
    case ITEM_SECURITY:      *frame_w = 128; return SPR_SECURITY;     /* 128px, palette animated */
    case ITEM_RECYCLING:     *frame_w = 200; return SPR_RECYCLING_COMP; /* 5-frame trash cycle */
    case ITEM_STAIRS:        *frame_w = 64;  return SPR_STAIRS_COMP;
    case ITEM_ESCALATOR:     *frame_w = 64;  return SPR_ESCALATOR_COMP;
    case ITEM_ELEVATOR_SHAFT:
    case ITEM_ELEVATOR_SERVICE:
    case ITEM_ELEVATOR_EXPRESS: *frame_w = 32; return SPR_ELEV_SHAFT; /* reuse shaft art for now */
    case ITEM_FLOOR:         *frame_w = 0;   return 0;
    case ITEM_HOUSEKEEPING:  *frame_w = 120; return SPR_HOUSEKEEPING; /* laundry room */
    default:                 *frame_w = 0;   return 0;
    }
}

/* Fallback colors for items without sprites loaded */
static void item_fallback_color(ItemType type, uint8_t *r, uint8_t *g, uint8_t *b)
{
    switch (type) {
    case ITEM_OFFICE:       *r=200; *g=200; *b=150; break;
    case ITEM_CONDO:        *r=180; *g=220; *b=180; break;
    case ITEM_HOTEL_SINGLE: *r=150; *g=150; *b=220; break;
    case ITEM_HOTEL_TWIN:   *r=140; *g=140; *b=230; break;
    case ITEM_HOTEL_SUITE:  *r=120; *g=120; *b=240; break;
    case ITEM_RESTAURANT:   *r=220; *g=180; *b=150; break;
    case ITEM_FAST_FOOD:    *r=220; *g=220; *b=100; break;
    case ITEM_SHOP:         *r=220; *g=160; *b=220; break;
    case ITEM_CINEMA:       *r=80;  *g=60;  *b=120; break;
    case ITEM_PARTY_HALL:   *r=200; *g=100; *b=180; break;
    case ITEM_METRO:        *r=100; *g=100; *b=120; break;
    case ITEM_PARKING:      *r=160; *g=160; *b=160; break;
    case ITEM_CATHEDRAL:    *r=230; *g=220; *b=200; break; /* Warm stone — no exterior sprite exists */
    case ITEM_MEDICAL:      *r=220; *g=240; *b=240; break;
    case ITEM_SECURITY:     *r=180; *g=180; *b=200; break;
    case ITEM_RECYCLING:    *r=100; *g=180; *b=100; break;
    case ITEM_FLOOR:        *r=200; *g=200; *b=200; break;
    case ITEM_HOUSEKEEPING: *r=200; *g=190; *b=160; break;
    default:                *r=100; *g=100; *b=100; break;
    }
}

/* ---------- Game state ---------- */
typedef struct {
    NEResourceTable exe;
    SpriteAtlas     sprites;
    Tower           tower;
    GameSim         sim;
    SDL_Window     *window;
    SDL_Renderer   *renderer;
    TTF_Font       *font;
    TTF_Font       *font_small;
    TTF_Font       *font_info;   /* 13px — time.rml stats/date size */
    int             running;
    int             screen_w, screen_h;
    int             show_debug;   /* Toggle diagnostic labels */
    int             show_stats;   /* Analytics window (F3) */
    int             show_tuning;  /* Tuning/modding window (F4) */
    int             fin_open;     /* Financial report dialog (CountT, bitmap 0x81f4) */
    int             fin_x, fin_y; /* financial report window position (draggable) */
    int             stats_x, stats_y; /* window positions (draggable; */
    int             tune_x, tune_y;   /*  -1,-1 = default placement) */
    int             map_mode;     /* 0 map / 1 eval / 2 rent / 3 hotel
                                     (EXE global 0x7840; legends 0x139..) */
    int             elv_open;     /* Elevator dialog (double-click a shaft) */
    int             elv_sx;       /* shaft column the dialog is bound to */
    int             elv_stype;    /* shaft ItemType (column+type = identity) */
    int             elv_day;      /* schedule editor: 0 weekday / 1 weekend */
    int             elv_period;   /* schedule editor: selected period 0..6 */
    int             elv_scroll;   /* faithful grid: bottom visible floor offset */
    int             elv_x, elv_y; /* dialog window position (draggable) */
    int             elv_edit_mode;   /* Simulate: full-screen shaft-edit surface (seg_10f0) */
    GameSpeed       elv_saved_speed; /* speed to restore when edit mode exits */
    
    /* Build mode */
    ItemType        build_type;
    int             demolish_mode;   /* Bulldozer active: clicks remove facilities */
    int             finger_mode;     /* Finger/pointer tool: clicks interact (open
                                      * elevator dialog, etc.) instead of building */
    int             inspect_mode;    /* Inspector tool: clicks open a unit info popup */
    int             inspect_open;    /* info popup showing */
    uint16_t        inspect_tid;     /* tenant the popup is bound to */
    int             inspect_x, inspect_y;  /* popup window position */
    int             rent_dd_open;    /* rent/price dropdown (item 0xD) expanded */
    int             name_edit_open;  /* name-editor sub-dialog (res 0x2DC) */
    char            name_edit_buf[16]; /* editing buffer (max 15 chars, faithful) */
    int             name_edit_len;
    int             name_edit_person; /* 1 = editor renames a person, not a tenant */

    /* Person-inspection popup (InfoPeple seg_1110): click a queue figure
     * with the inspector to see who they are. */
    int             person_open;
    uint16_t        person_pid;      /* people[] slot + 1 */
    int             person_x, person_y;
    int             movie_dlg_open;  /* movie-chooser sub-dialog (res 0x2DB) */
    int             mouse_x, mouse_y;
    int             mouse_floor, mouse_cell;
    
    /* Drag placement */
    int             dragging;       /* 1 if currently dragging to place */
    int             drag_start_cell;
    int             drag_start_floor;
    int             cap_drag;       /* pointer-drag on a motor room / pit: the
                                     * shaft's type was borrowed into build_type
                                     * for the drag; restore ITEM_NONE on release */
    int             cap_drag_dir;   /* +1 dragging the top cap, -1 the pit */
    int             cap_drag_next;  /* next floor a live drag step will claim */
    int             cap_drag_placed;/* segments placed live during this drag */
    
    /* Camera smoothing */
    float           cam_fx, cam_fy;
    
    /* Zoom */
    float           zoom;
    
    /* Cloud sprites (up to 4 different shapes + Santa Easter egg) */
    Sprite         *clouds[SPR_CLOUD_COUNT];
    Sprite         *santa;       /* 0x8388 — Santa helicopter */
    Sprite         *entrances;   /* 0x83E9 — entrance awning */
    Sprite         *crane;       /* 0x83EA — construction crane */
    int             crane_floor; /* OverlayT state: top floor when last
                                    re-evaluated; CRANE_NONE = no crane */
    int             crane_x;     /* left edge captured at that moment */
    Sprite         *fireladder;  /* 0x842D — fire escape stairs */
    Sprite         *skyline;     /* 0x8389 — city skyline background */
    SDL_Texture    *queue_hot;   /* white clone of the queue silhouettes —
                                  * color-mod can only DARKEN, so tinting the
                                  * black figures pink/red needs a white base */
    int             cloud_count;

    /* Disaster event art (real EXE sprites, replacing colored rectangles) */
    Sprite         *fire_frames[4]; /* 0x8F68-0x8F6B — flame, 96x36 (12 cells), 4-frame anim */
    Sprite         *fire_chopper;   /* 0x8F6D — firefighting helicopter */
    Sprite         *fire_destroyed; /* 0x8FA8 — burnt-out cell (fire aftermath) */
    Sprite         *alert_terror;   /* 0xA710 — terrorist/bomb alert icon (76x67) */
    Sprite         *alert_fire;     /* 0xA714 — fire alert icon (76x60) */
    
    /* UI bitmaps from EXE */
    SDL_Texture    *ui_items;    /* Toolbox item icons: 32 icons × 32px each, 3 rows (normal+pressed) */
    int             ui_items_w, ui_items_h;
    SDL_Texture    *ui_timebar;  /* Info bar background (431×41) */
    int             ui_timebar_w, ui_timebar_h;
    SDL_Texture    *ui_star[2];  /* Star rating: [0]=empty, [1]=filled (24×19 each) */
    int             ui_star_w, ui_star_h;
    SDL_Texture    *ui_speed;    /* Speed buttons (4 states, normal + pressed) */
    int             ui_speed_w, ui_speed_h;
    SDL_Texture    *ui_tools;    /* Tool buttons (bulldozer/finger/inspector) */
    int             ui_tools_w, ui_tools_h;
    SDL_Texture    *ui_map;      /* Map background (200×288) */
    int             ui_map_w, ui_map_h;
    
    /* Win3.1 Menu system */
    int             menu_open;       /* -1 = closed, 0+ = which top-level menu is open */
    int             menu_hover;      /* which item is hovered in the open dropdown */
    int             menu_bar_hover;  /* which top-level menu is hovered */
    
    /* Moveable window positions (Win3.1 style: drag by title bar, not resizable) */
    int             info_x, info_y;  /* Info bar window */
    int             map_x, map_y;    /* Minimap window */
    int             tool_x, tool_y;  /* Toolbox window */
    
    /* Toolbox group pull-down (click-and-hold) state */
    int             tool_popup;      /* index into tool_buttons[] whose sub-menu is open, or -1 */

    /* Windows / Options menu toggles (original menu ids 40009-40016).
     * Windows: show/hide the floating windows ([0x31A8]/[0x31AA]/[0x31AC]).
     * Options: the 1994 performance toggles — Animation gates the crowd
     * and effect passes ([0xDE30]/[0xDE32], read by AnimPeple/AnimeT),
     * Sound gates voice categories at the mixer funnel
     * ([0xDE2A]/[0xDE2C]/[0xDE2E]). All default ON, runtime-only. */
    uint8_t         win_toolbar, win_infobar, win_map;
    uint8_t         anim_people, anim_effects;
    uint8_t         snd_elev, snd_bg, snd_events;

    /* SmoothScroll (CameraT 1080): minimap click-nav animates the camera
     * to the target instead of jumping (pass-3 trace: map click-nav =
     * SmoothScroll). */
    uint8_t         cam_anim;
    float           cam_tx, cam_ty;

    /* Find Person/Find Tenant modal (10d8:0000): 0 closed, 1 person,
     * 2 tenant. The sim halts while open. */
    uint8_t         find_open;
    int             find_sel;

    /* Window dragging state */
    int             win_dragging;    /* 0=none, 1=info, 2=map, 3=toolbox */
    int             win_drag_ox;     /* Mouse offset from window origin at drag start */
    int             win_drag_oy;
    
    /* Camera panning (middle/right-click drag) */
    int             cam_panning;     /* 1 if currently panning camera */
    int             cam_pan_last_x;
    int             cam_pan_last_y;
    
    /* Weather (from OpenSkyscraper Sky.cpp) */
    int             rainy_day;       /* 1 = rain today */

    /* Disaster decision modal — opens when sim->event.pending is set, pausing
     * the sim until the player accepts/declines. (EventT shows a dialog before
     * the event runs.) */
    int             disaster_modal;       /* 1 = modal capturing input */
    GameSpeed       disaster_saved_speed; /* speed to restore on dismiss */

    /* One-button notice dialog — the EXE pops these for event resolutions
     * and VIP visits (dialogs 0xBB9/0xBBA/0xBBB/0xBC5/0xBCF/0xBD0), pausing
     * until dismissed. Text/button come straight from the dialog resource. */
    int             notice_modal;
    char            notice_text[192];
    char            notice_btn[32];
    GameSpeed       notice_saved_speed;

    /* Route-loss confirmation (res 0x3ed): a Yes/No modal shown before a
     * stop-toggle or shaft-segment demolition severs a floor's only route.
     * Pauses the sim while open, like the EXE's system-modal MessageBox. */
    int             route_confirm;        /* 0 = closed, else 1 */
    const char     *route_confirm_text;   /* one of the res-0x3ed strings */
    int             route_confirm_kind;   /* 1 = toggle stop, 2 = remove tenant */
    int             route_confirm_shaft;
    int             route_confirm_fidx;
    uint16_t        route_confirm_tid;
    GameSpeed       route_saved_speed;

    /* Mouse cursors: arrow normally, crosshair while bulldozing. */
    SDL_Cursor     *cursor_arrow;
    SDL_Cursor     *cursor_demolish;
} Game;

static Game game;

/* Route-loss confirmation (defined with the other modals below; the
 * stop-toggle and demolish paths above them need the entry points). */
static void request_stop_toggle(int si, int fidx);
static int  request_remove_tenant(uint16_t tid, ItemType ty);

/* Palette-cycle animation variants (the EXE's AnimeT cycles color-table
 * entries continuously; the security radar and cinema marquee live on
 * those entries, so they animate all day — same scheme as the motors).
 * These two sheets only use the TOGGLE-PAIR entries (197/198, 199/200),
 * not the 3-rotation, so they're 2-frame animations: cycle step 2 is
 * pixel-identical to the base (verified by frame hashing). */
static uint16_t item_sprite_animated(ItemType type, uint16_t base)
{
    if ((game.sim.frame / 6) % 2 == 0) return base;
    if (type == ITEM_SECURITY) return SPR_SECURITY_F1;
    if (type == ITEM_CINEMA)   return SPR_CINEMA_COMP_F1;
    return base;
}

/* ---------- Win 3.1 style Menu system ---------- */
/* Classic Windows 3.1 colors */
#define WIN31_BG         192, 192, 192  /* Silver/gray background */
#define WIN31_SHADOW     128, 128, 128  /* Dark border */
#define WIN31_HIGHLIGHT  255, 255, 255  /* Light border */
#define WIN31_TEXT        0,   0,   0   /* Black text */
#define WIN31_SEL_BG      0,   0, 128   /* Navy selection */
#define WIN31_SEL_TEXT  255, 255, 255   /* White text on selection */
#define WIN31_DISABLED  128, 128, 128   /* Grayed out text */

typedef struct {
    const char *label;       /* NULL = separator */
    ItemType    build_type;  /* ITEM_NONE for non-build actions */
    int         action;      /* custom action code */
} MenuItem;

#define ACT_NONE       0
#define ACT_SPEED_PAUSE  1
#define ACT_SPEED_1      2
#define ACT_SPEED_2      3
#define ACT_SPEED_3      4
#define ACT_DEBUG_TOGGLE 5
#define ACT_SCREENSHOT   6
#define ACT_QUIT         7
#define ACT_SANTA        8
#define ACT_MODE_CAMPAIGN 9
#define ACT_MODE_SANDBOX  10
#define ACT_FINANCE       11
#define ACT_SAVE          12
#define ACT_LOAD          13
#define ACT_EXPORT_TDT    14
#define ACT_STATS         15
#define ACT_TUNING        16
#define ACT_NEW_TOWER     17
#define ACT_WIN_TOOLBAR   18   /* Windows menu (40014-40016) */
#define ACT_WIN_INFOBAR   19
#define ACT_WIN_MAP       20
#define ACT_ANIM_PEOPLE   21   /* Options menu (40009-40013) */
#define ACT_ANIM_EFFECTS  22
#define ACT_SND_ELEV      23
#define ACT_SND_BG        24
#define ACT_SND_EVENTS    25
#define ACT_FIND_PERSON   26   /* Windows menu (40019/40020) */
#define ACT_FIND_TENANT   27

/* Build > Residential submenu */
static const MenuItem menu_build_res[] = {
    { "Office\t1",         ITEM_OFFICE,       ACT_NONE },
    { "Condo\t2",          ITEM_CONDO,        ACT_NONE },
    { NULL, ITEM_NONE, ACT_NONE },  /* separator */
    { "Hotel (Single)\t5", ITEM_HOTEL_SINGLE, ACT_NONE },
    { "Hotel (Twin)\t6",   ITEM_HOTEL_TWIN,   ACT_NONE },
    { "Hotel (Suite)\t7",  ITEM_HOTEL_SUITE,  ACT_NONE },
};
#define MENU_BUILD_RES_COUNT 6

/* Build > Commercial */
static const MenuItem menu_build_com[] = {
    { "Restaurant\t3",    ITEM_RESTAURANT,   ACT_NONE },
    { "Fast Food\t4",     ITEM_FAST_FOOD,    ACT_NONE },
    { "Shop\tO",          ITEM_SHOP,         ACT_NONE },
    { NULL, ITEM_NONE, ACT_NONE },
    { "Cinema\tC",        ITEM_CINEMA,       ACT_NONE },
    { "Party Hall\tP",    ITEM_PARTY_HALL,   ACT_NONE },
};
#define MENU_BUILD_COM_COUNT 6

/* Build > Transport */
static const MenuItem menu_build_trans[] = {
    { "Standard Elevator\tE", ITEM_ELEVATOR_SHAFT,   ACT_NONE },
    { "Service Elevator\tV",  ITEM_ELEVATOR_SERVICE, ACT_NONE },
    { "Express Elevator\tW",  ITEM_ELEVATOR_EXPRESS, ACT_NONE },
    { NULL, ITEM_NONE, ACT_NONE },
    { "Stairs\t8",        ITEM_STAIRS,       ACT_NONE },
    { "Escalator\t9",     ITEM_ESCALATOR,    ACT_NONE },
    { NULL, ITEM_NONE, ACT_NONE },
    { "Lobby\tL",         ITEM_LOBBY,        ACT_NONE },
    { "Parking\tK",       ITEM_PARKING,      ACT_NONE },
    { "Parking Ramp",     ITEM_RAMP,         ACT_NONE },
    { "Metro Station\tM", ITEM_METRO,        ACT_NONE },
};
#define MENU_BUILD_TRANS_COUNT 11

/* Build > Services */
static const MenuItem menu_build_svc[] = {
    { "Security\tG",      ITEM_SECURITY,     ACT_NONE },
    { "Medical Center\tX",ITEM_MEDICAL,      ACT_NONE },
    { "Recycling\tR",     ITEM_RECYCLING,    ACT_NONE },
    { NULL, ITEM_NONE, ACT_NONE },
    { "Cathedral\tH",     ITEM_CATHEDRAL,    ACT_NONE },
};
#define MENU_BUILD_SVC_COUNT 5

/* Speed menu — Paused / Normal / Fast only, matching the original's
 * model (Options -> Fast Mode toggle, menu id 40007 -> [0xDE34] ->
 * TimeT 1200:01a5). "Fast" is capped at 2x normal as a stand-in for
 * "unthrottled on era hardware"; finer/faster control is a mod
 * (MOD-IDEAS.md). Turbo survives in the enum for old saves and debug
 * but has no menu entry. */
static const MenuItem menu_speed[] = {
    { "Paused\tSpace",    ITEM_NONE,  ACT_SPEED_PAUSE },
    { "Normal\t+",        ITEM_NONE,  ACT_SPEED_1 },
    { "Fast Mode\t++",    ITEM_NONE,  ACT_SPEED_2 },
};
#define MENU_SPEED_COUNT 3

/* View menu */
static const MenuItem menu_view[] = {
    { "Financial Statement\tF7", ITEM_NONE, ACT_FINANCE },
    { "Analytics Graphs\tF3",    ITEM_NONE, ACT_STATS },
    { "Tuning / Modding\tF4",    ITEM_NONE, ACT_TUNING },
    { NULL, ITEM_NONE, ACT_NONE },
    { "Debug Labels\t`",   ITEM_NONE,  ACT_DEBUG_TOGGLE },
    { "Screenshot\tF12",   ITEM_NONE,  ACT_SCREENSHOT },
    { NULL, ITEM_NONE, ACT_NONE },
    { "Santa!\tF2",        ITEM_NONE,  ACT_SANTA },
};
#define MENU_VIEW_COUNT 8

/* Game menu — save/load, mode (radio), quit. Every keyboard command has a
 * menu home (VNC clients often can't send function keys). */
static const MenuItem menu_file[] = {
    { "New Tower",         ITEM_NONE,  ACT_NEW_TOWER },
    { NULL, ITEM_NONE, ACT_NONE },     /* separator */
    { "Save\tF5",          ITEM_NONE,  ACT_SAVE },
    { "Load\tF9",          ITEM_NONE,  ACT_LOAD },
    { "Export .TDT\tF6",   ITEM_NONE,  ACT_EXPORT_TDT },
    { NULL, ITEM_NONE, ACT_NONE },     /* separator */
    { "Campaign Mode\tF8", ITEM_NONE,  ACT_MODE_CAMPAIGN },
    { "Sandbox Mode\tF8",  ITEM_NONE,  ACT_MODE_SANDBOX },
    { NULL, ITEM_NONE, ACT_NONE },     /* separator */
    { "Quit\tQ",           ITEM_NONE,  ACT_QUIT },
};
#define MENU_FILE_COUNT 10

/* Options menu — the original's Animation/Sound toggle block (menu ids
 * 40009-40013). Fast Mode (40007) lives in the Speed menu; Call Fire
 * Rescue (40008) awaits the FireT response trace. */
static const MenuItem menu_options[] = {
    { "Anim: People",      ITEM_NONE, ACT_ANIM_PEOPLE },
    { "Anim: Effects",     ITEM_NONE, ACT_ANIM_EFFECTS },
    { NULL, ITEM_NONE, ACT_NONE },
    { "Sound: Elevators",  ITEM_NONE, ACT_SND_ELEV },
    { "Sound: Background", ITEM_NONE, ACT_SND_BG },
    { "Sound: Events",     ITEM_NONE, ACT_SND_EVENTS },
};
#define MENU_OPTIONS_COUNT 6

/* Windows menu — show/hide the floating windows (menu ids 40014-40016).
 * Find Person... / Find Tenant... (40019/40020) land here once traced. */
static const MenuItem menu_windows[] = {
    { "Tool Bar",   ITEM_NONE, ACT_WIN_TOOLBAR },
    { "Info Bar",   ITEM_NONE, ACT_WIN_INFOBAR },
    { "Map Window", ITEM_NONE, ACT_WIN_MAP },
    { NULL, ITEM_NONE, ACT_NONE },
    { "Find Person...", ITEM_NONE, ACT_FIND_PERSON },
    { "Find Tenant...", ITEM_NONE, ACT_FIND_TENANT },
};
#define MENU_WINDOWS_COUNT 6

/* Top-level menus */
typedef struct {
    const char     *label;
    const MenuItem *items;
    int             count;
} TopMenu;

static const TopMenu top_menus[] = {
    { "Game",       menu_file,        MENU_FILE_COUNT },
    { "Build Res.", menu_build_res,   MENU_BUILD_RES_COUNT },
    { "Build Com.", menu_build_com,   MENU_BUILD_COM_COUNT },
    { "Transport",  menu_build_trans, MENU_BUILD_TRANS_COUNT },
    { "Services",   menu_build_svc,   MENU_BUILD_SVC_COUNT },
    { "Speed",      menu_speed,       MENU_SPEED_COUNT },
    { "Options",    menu_options,     MENU_OPTIONS_COUNT },
    { "Windows",    menu_windows,     MENU_WINDOWS_COUNT },
    { "View",       menu_view,        MENU_VIEW_COUNT },
};
#define TOP_MENU_COUNT 9

/* Get pixel position of top menu item */
static void get_top_menu_rect(int idx, int *x, int *y, int *w, int *h)
{
    int cx = 4;
    for (int i = 0; i < TOP_MENU_COUNT; i++) {
        int tw = (int)strlen(top_menus[i].label) * 8 + MENU_ITEM_PAD * 2;
        if (i == idx) {
            *x = cx; *y = HUD_HEIGHT; *w = tw; *h = MENU_BAR_H;
            return;
        }
        cx += tw;
    }
    *x = *y = *w = *h = 0;
}

/* ---------- Cloud positions (scattered, higher up like original) ---------- */
/* OpenSkyscraper: cloudGrid(250, 100), cmin.y starts at 2 (= 200px above ground).
 * We scatter clouds from 150px to 1500px above lobby (floors 6-60 equivalent). */
typedef struct { int x, y; int cloud_idx; } CloudPos;
static const CloudPos CLOUD_POSITIONS[] = {
    { 50,  -180,  0 },
    { 320, -250, 2 },
    { 600, -160, 1 },
    { 150, -420, 3 },
    { 520, -380, 0 },
    { 880, -200, 2 },
    { 280, -550, 1 },
    { 700, -480, 3 },
    { 80,  -650, 0 },
    { 450, -720, 2 },
    { 950, -340, 1 },
    { 200, -850, 3 },
    { 750, -600, 0 },
    { 100, -950, 2 },
    { 550, -1100, 1 },
    { 380, -1300, 3 },
};
#define CLOUD_COUNT (int)(sizeof(CLOUD_POSITIONS)/sizeof(CLOUD_POSITIONS[0]))

/* ---------- Coordinate conversion ---------- */

static void screen_to_grid(int sx, int sy, int *floor, int *cell)
{
    int world_x = sx + (int)game.cam_fx - game.screen_w / 2;
    int world_y = sy + (int)game.cam_fy - game.screen_h / 2;

    /* True floor-division by CELL_H. Plain integer division truncates toward
     * zero, which picks the wrong floor (off by one) above ground where
     * world_y is negative — that made the build ghost sit a cell below the
     * cursor. */
    int fdiv = (world_y >= 0) ? (world_y / CELL_H)
                              : -(((-world_y) + CELL_H - 1) / CELL_H);
    *cell = world_x / CELL_W;
    *floor = -fdiv;

    if (*cell < 0) *cell = 0;
    if (*cell >= TOWER_WIDTH) *cell = TOWER_WIDTH - 1;
}

static void grid_to_screen(int floor, int cell, int *sx, int *sy)
{
    int world_x = cell * CELL_W;
    int world_y = -floor * CELL_H;

    *sx = world_x - (int)game.cam_fx + game.screen_w / 2;
    *sy = world_y - (int)game.cam_fy + game.screen_h / 2;
}

/* Ambient background murmur (referee event #24 + referee_ambient_timing):
 * each tick, 1-in-16, sample one of 6 on-screen probe points and play the
 * ambient tied to the tenant type there — so the bed reflects what's on
 * screen and changes as you scroll. Window 10AM-1AM, muted during a disaster.
 * A running cinema plays one of the 9xxx soundtrack WAVs, keyed to its film. */
static void ambient_tick(void)
{
    int hr = game.sim.hour;
    if (!(hr >= 10 || hr < 1)) return;      /* 10:00 AM .. 1:00 AM window */
    if (game.sim.event.active) return;       /* fire/emergency suppresses ambient */
    if (rand() % 16 != 0) return;            /* 1-in-16 per tick */

    int sel = rand() % 6;                     /* probe: {mid,¾-down} × {¼,½,¾} */
    int frac = sel % 3;                       /* 0,1,2 -> ¼,½,¾ */
    int sx = game.screen_w * (frac + 1) / 4;
    int sy = (sel < 3) ? game.screen_h / 2 : game.screen_h * 3 / 4;

    int floor, cell;
    screen_to_grid(sx, sy, &floor, &cell);
    int fidx = floor_to_index(floor);
    if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT || cell < 0 || cell >= TOWER_WIDTH)
        return;
    if (!game.tower.grid[fidx][cell].tenant_id) {
        /* Empty probe = the EXE's seasonal branch (param_1 == -1): a rare
         * season accent. Conditions/ids proven; the [0xDD6C] fallback (#10002)
         * is undecoded, omitted. Low frequency so it stays an accent. */
        if ((rand() & 3) != 0) return;
        int year_q  = (game.tower.day / 3) % 4;
        int evening = game.sim.hour >= 17 || game.sim.hour < 5;
        if (year_q == 2 && !evening) play_snd(AMB_SEASON_DAY);
        else if (year_q == 3 && evening) play_snd(AMB_SEASON_EVE);
        else if (game.sim.santa.active) play_snd(AMB_SEASON_SANTA);  /* EXE [0xDD6C] */
        return;
    }

    int id = 0;
    switch (game.tower.grid[fidx][cell].type) {
    case ITEM_RESTAURANT:   id = (rand() & 1) ? AMB_RESTAURANT_A : AMB_RESTAURANT_B; break;
    case ITEM_OFFICE:       id = AMB_OFFICE; break;
    case ITEM_HOTEL_SINGLE:
    case ITEM_HOTEL_TWIN:
    case ITEM_HOTEL_SUITE:  id = AMB_HOTEL; break;
    case ITEM_CONDO:        id = (rand() % 10 == 0) ? AMB_CONDO_RARE : AMB_HOTEL; break;
    case ITEM_SHOP:
    case ITEM_FAST_FOOD:    id = (rand() & 1) ? AMB_RESTAURANT_B : AMB_SHOP_FF_B; break;
    case ITEM_PARKING:      id = (rand() & 1) ? AMB_PARKING_A : AMB_PARKING_B; break;
    case ITEM_PARTY_HALL:   id = AMB_PARTY; break;
    case ITEM_CINEMA: {
        /* A running show (venue_state 3, SoundT 11c8:0875) plays that film's
         * OWN theme: sound resource 9001 + movie_id (11c8:0895 `add ax,0x2329`)
         * — a fixed per-film mapping, no pool (referee 2026-07-30, HIGH).
         * Films 3 and 6 point at non-RIFF resources (0xA32C/0xA32F) in our
         * EXE — the original most likely runs those two films silent, and so
         * do we (play_snd no-ops on an unloaded clip). */
        Tenant *ct = tower_tenant(&game.tower, game.tower.grid[fidx][cell].tenant_id);
        if (!ct || ct->venue_state != 3) return;   /* silent unless showing */
        id = (uint16_t)(0xA329 + ct->movie_id);
        break;
    }
    default: return;   /* other/infrastructure types silent */
    }
    play_snd(id);
}

/* Keep the camera within the world. cam_fy is the world-Y at screen center;
 * larger = deeper (basements). Stop the view from running past the bottom of
 * the deepest basement, or absurdly far above the build ceiling. */
static void clamp_camera(void)
{
    float half = game.screen_h / 2.0f;
    /* Bottom: B10's lower edge should sit no higher than the screen bottom. */
    float bottom = (float)((-TOWER_MIN_FLOOR + 1) * CELL_H) - half;
    /* Top: allow seeing up to the build ceiling plus a little sky. */
    float top = (float)(-(TOWER_MAX_FLOOR + 4) * CELL_H) + half;
    if (top > bottom) top = bottom;
    if (game.cam_fy > bottom) game.cam_fy = bottom;
    if (game.cam_fy < top)    game.cam_fy = top;

    /* Horizontal: keep at least part of the tower's width on screen. */
    float halfw = game.screen_w / 2.0f;
    float right = (float)(TOWER_WIDTH * CELL_W) - halfw;
    float left  = halfw;
    if (left > right) left = right;
    if (game.cam_fx > right) game.cam_fx = right;
    if (game.cam_fx < left)  game.cam_fx = left;
}

/* ---------- HUD text rendering helpers ---------- */

/* Format money with commas: 5000000 -> "$5,000,000" */
static void format_money(long amount, char *buf, int bufsize)
{
    char raw[32];
    int neg = amount < 0;
    long abs_amount = neg ? -amount : amount;
    snprintf(raw, sizeof(raw), "%ld", abs_amount);
    int len = (int)strlen(raw);
    int commas = (len - 1) / 3;
    int total = len + commas + 1 + (neg ? 1 : 0); /* +1 for $ */
    if (total >= bufsize) { snprintf(buf, bufsize, "$%ld", amount); return; }
    
    int pos = 0;
    if (neg) buf[pos++] = '-';
    buf[pos++] = '$';
    int digits_before_comma = len % 3;
    if (digits_before_comma == 0) digits_before_comma = 3;
    
    for (int i = 0; i < len; i++) {
        if (i > 0 && ((len - i) % 3 == 0)) buf[pos++] = ',';
        buf[pos++] = raw[i];
    }
    buf[pos] = '\0';
}

/* Render text to a texture, returns texture and sets w/h */
static SDL_Texture *render_text(const char *text, SDL_Color color, int *w, int *h)
{
    if (!game.font || !text || !text[0]) return NULL;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(game.font, text, color);
    if (!surf) return NULL;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(game.renderer, surf);
    *w = surf->w;
    *h = surf->h;
    SDL_FreeSurface(surf);
    return tex;
}

/* Draw text at position, returns width drawn */
static int draw_text(const char *text, int x, int y, SDL_Color color)
{
    int w, h;
    SDL_Texture *tex = render_text(text, color, &w, &h);
    if (!tex) return 0;
    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(game.renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    return w;
}

/* Draw a dialog-style body: split on '\n', word-wrap each paragraph to
 * max_w. Returns the y just below the last line drawn. Used for the EXE
 * dialog texts, whose line breaks were laid out for the original's
 * dialog widths. */
static int draw_text_wrapped(const char *text, int x, int y, int max_w,
                             SDL_Color color)
{
    char line[160];
    const char *p = text;
    while (*p) {
        /* collect words until the line would overflow max_w */
        int len = 0;
        line[0] = '\0';
        while (*p && *p != '\n') {
            const char *we = p;
            while (*we && *we != ' ' && *we != '\n') we++;
            int wl = (int)(we - p);
            char probe[160];
            snprintf(probe, sizeof probe, "%s%s%.*s",
                     line, len ? " " : "", wl, p);
            int tw = 0, th = 0;
            if (game.font) TTF_SizeUTF8(game.font, probe, &tw, &th);
            if (len && tw > max_w) break;         /* wrap before this word */
            snprintf(line, sizeof line, "%s", probe);
            len = 1;
            p = we;
            while (*p == ' ') p++;
        }
        if (line[0]) draw_text(line, x, y, color);
        y += 18;
        if (*p == '\n') p++;
    }
    return y;
}

/* ---------- Rendering ---------- */

static void render_sky(void)
{
    int lobby_sx, lobby_sy;
    grid_to_screen(0, 0, &lobby_sx, &lobby_sy);
    
    /* Sky: gradient background.
     * NOTE: The original sky bitmap (0x8352) contains raindrop patterns
     * baked into palette entries 207/213. OpenSkyscraper fixes this by
     * patching those palette entries to match sky color (see loadSky()),
     * but our sprite loader doesn't do palette patching yet. So we use
     * a clean gradient that matches the original's daytime sky color. */
    {
        int sky_h = lobby_sy;
        if (sky_h > game.screen_h) sky_h = game.screen_h;
        for (int y = 0; y < sky_h; y++) {
            /* SimTower sky blue: gradient from deep blue at top to light at horizon */
            float t = (float)y / (sky_h > 0 ? (float)sky_h : 1.0f);
            int r = (int)(74 + t * 106);   /* 74 → 180 */
            int g = (int)(140 + t * 80);   /* 140 → 220 */
            int b = (int)(220 + t * 35);   /* 220 → 255 */
            SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
            SDL_RenderDrawLine(game.renderer, 0, y, game.screen_w, y);
        }
    }
    
    /* Time-of-day sky tint overlay */
    {
        uint8_t tr, tg, tb, ta;
        game_sky_tint(&game.sim, &tr, &tg, &tb, &ta);
        if (ta > 0) {
            SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(game.renderer, tr, tg, tb, ta);
            int sky_bottom = lobby_sy;
            if (sky_bottom > game.screen_h) sky_bottom = game.screen_h;
            SDL_Rect tint_rect = { 0, 0, game.screen_w, sky_bottom };
            SDL_RenderFillRect(game.renderer, &tint_rect);
            SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
        }
    }
    
    /* Render clouds in the sky.
     * IMPORTANT: Each cloud bitmap has 4 rows stacked vertically
     * (day, twilight, night, overcast — see Sky.cpp: h = size.y / 4).
     * We only render the appropriate 1/4 based on time of day. */
    if (game.cloud_count > 0) {
        /* Select cloud row: 0=day, 1=twilight, 2=night, 3=overcast */
        int cloud_row = 0;
        if (game.sim.time_of_day == TOD_NIGHT) cloud_row = 2;
        else if (game.sim.time_of_day == TOD_EVENING || game.sim.time_of_day == TOD_DAWN) cloud_row = 1;
        else if (game.rainy_day && game.sim.hour >= 8 && game.sim.hour < 16) cloud_row = 3;
        
        for (int i = 0; i < CLOUD_COUNT; i++) {
            int ci = CLOUD_POSITIONS[i].cloud_idx % game.cloud_count;
            Sprite *cs = game.clouds[ci];
            if (!cs) continue;
            
            int row_h = cs->h / 4;  /* Each row = 1/4 of total height */
            if (row_h < 1) row_h = cs->h;
            
            SDL_SetTextureAlphaMod(cs->texture, 180);
            
            int cx = lobby_sx + CLOUD_POSITIONS[i].x;
            int cy = lobby_sy + CLOUD_POSITIONS[i].y;
            
            /* Source rect: only the correct 1/4 row */
            SDL_Rect src = { 0, cloud_row * row_h, cs->w, row_h };
            
            for (int tx = cx - 1200; tx < game.screen_w + 200; tx += 1200) {
                if (tx + cs->w < 0) continue;
                if (tx > game.screen_w) break;
                if (cy + row_h < 0 || cy > game.screen_h) continue;
                
                SDL_Rect dst = { tx, cy, cs->w, row_h };
                SDL_RenderCopy(game.renderer, cs->texture, &src, &dst);
            }
            SDL_SetTextureAlphaMod(cs->texture, 255);
        }
    }
    
    /* Render Santa flying across the sky (SantaT). He flies at a fixed
     * WORLD altitude — LaunchSanta's y = viewport_height - 0x84c puts him
     * ~2124px (59 floors) above the ground — so you only catch him when
     * you're scrolled up near the top of a tall tower on Christmas night.
     * Options -> Anim: Effects ([0xDE32], the AnimeT gate) hides him. */
    if (game.sim.santa.active && game.santa && game.anim_effects) {
        SDL_Rect dst = {
            game.sim.santa.x,
            lobby_sy + CELL_H - 0x84c + game.sim.santa.y,
            game.santa->w, game.santa->h
        };
        SDL_RenderCopy(game.renderer, game.santa->texture, NULL, &dst);
    }
    
    /* City skyline — tiled at ground level (from Sky.cpp).
     * OpenSkyscraper: origin(0, 55), position(x*96, 0) — bottom at ground, extends upward.
     * In our coordinate system, ground level = lobby_sy + CELL_H (bottom of lobby). */
    if (game.skyline) {
        SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
        /* Skyline bottom aligns with lobby bottom (floor 0 bottom edge) */
        int ground_y = lobby_sy + CELL_H;  /* Bottom of lobby = ground level */
        int skyline_y = ground_y - game.skyline->h;  /* Extends upward from ground */
        
        /* Apply time-of-day tinting to skyline via color mod */
        uint8_t tr, tg, tb, ta;
        game_sky_tint(&game.sim, &tr, &tg, &tb, &ta);
        if (ta > 0) {
            /* Blend sky tint into the skyline sprite */
            int mr = 255 - (int)ta * (255 - (int)tr) / 255;
            int mg = 255 - (int)ta * (255 - (int)tg) / 255;
            int mb = 255 - (int)ta * (255 - (int)tb) / 255;
            SDL_SetTextureColorMod(game.skyline->texture, mr, mg, mb);
        } else {
            SDL_SetTextureColorMod(game.skyline->texture, 255, 255, 255);
        }
        
        /* Skyline scrolls horizontally with the camera, wrapped at the 96px
         * tile width — matches decomp seg_1048 (FUN_1048_03a3 offsets the strip
         * by the camera scroll with a % 0x60 wrap) and OpenSkyscraper's
         * world position x*96. Previously this loop used raw screen coords, so
         * the distant city stayed glued to the window while the tower panned
         * underneath it. See annotated/seg_1048_SkyT.c. */
        int tile_w = game.skyline->w;            /* 96px (res 0x8389, 96x55) */
        int shift = (int)game.cam_fx % tile_w;   /* world scroll, may be < 0 */
        for (int sx = -tile_w - shift; sx < game.screen_w + tile_w; sx += tile_w) {
            SDL_Rect dst = { sx, skyline_y, tile_w, game.skyline->h };
            SDL_RenderCopy(game.renderer, game.skyline->texture, NULL, &dst);
        }
        /* Reset color mod */
        SDL_SetTextureColorMod(game.skyline->texture, 255, 255, 255);
        SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
    }
    
    /* Underground: concrete gray background (like original), gets darker with depth. */
    int ug_start = lobby_sy + CELL_H;
    if (ug_start < game.screen_h) {
        for (int y = ug_start; y < game.screen_h; y++) {
            int depth = y - ug_start;
            int r = 160 - depth / 6; if (r < 70) r = 70;
            int g = 150 - depth / 6; if (g < 65) g = 65;
            int b = 140 - depth / 7; if (b < 60) b = 60;
            SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
            SDL_RenderDrawLine(game.renderer, 0, y, game.screen_w, y);
        }
        for (int bf = -1; bf >= TOWER_MIN_FLOOR; bf--) {
            int bsx, bsy;
            grid_to_screen(bf, 0, &bsx, &bsy);
            if (bsy > 0 && bsy < game.screen_h) {
                SDL_SetRenderDrawColor(game.renderer, 100, 95, 90, 255);
                SDL_RenderDrawLine(game.renderer, 0, bsy + CELL_H - 1, game.screen_w, bsy + CELL_H - 1);
            }
        }
    }
    
    /* Rain effect — from AnimeT: palette entries 207/213 alternate.
     * Rain on rainy days (1 in 3 chance, decided at dawn per OpenSkyscraper Sky.cpp). */
    if (game.rainy_day && game.sim.hour >= 8 && game.sim.hour < 16) {
        SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
        int rain_set = (game.sim.frame / 4) % 2;
        int alpha = 50 + rain_set * 25;
        SDL_SetRenderDrawColor(game.renderer, 170, 180, 200, alpha);
        
        int offset = (game.sim.frame * 4) % 16;
        for (int rx = -300 + offset; rx < game.screen_w + 300; rx += 16) {
            int ry_top = HUD_HEIGHT + MENU_BAR_H;
            int ry_bot = lobby_sy > 0 ? lobby_sy : game.screen_h;
            if ((rx / 16 + rain_set) % 2 == 0) {
                SDL_RenderDrawLine(game.renderer, rx, ry_top, rx - 30, ry_bot);
            }
        }
        SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
    }
    
    /* Twinkling window lights at night (from AnimeT palette cycling).
     * At night, scattered small bright pixels over occupied buildings. */
    if (game.sim.time_of_day == TOD_NIGHT || game.sim.time_of_day == TOD_EVENING) {
        int twinkle_alpha = (game.sim.time_of_day == TOD_NIGHT) ? 200 : 80;
        SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
        
        /* Based on AnimeT: 3-phase cycling (counter % 3), amber/white alternating */
        int phase = game.sim.frame % 3;
        
        for (int i = 0; i < game.tower.tenant_count; i++) {
            Tenant *t = &game.tower.tenants[i];
            if (t->type == ITEM_LOBBY || t->type == ITEM_FLOOR || 
                t->type == ITEM_STAIRS || t->type == ITEM_ESCALATOR) continue;
            if (t->state != TENANT_OCCUPIED && t->state != TENANT_VACANT) continue;
            
            int tx, ty;
            grid_to_screen(t->floor, t->x, &tx, &ty);
            int tw = t->width * CELL_W;
            
            /* Scatter light points across the tenant's area */
            /* Use tenant_id as seed for consistent positions */
            unsigned int seed = (unsigned int)(t->x * 7 + t->floor * 31 + i * 13);
            int num_lights = tw / 12;
            if (num_lights < 2) num_lights = 2;
            if (num_lights > 8) num_lights = 8;
            
            for (int li = 0; li < num_lights; li++) {
                /* Pseudo-random but deterministic position per light */
                seed = seed * 1103515245 + 12345;
                int lx = tx + (int)(seed % (unsigned int)tw);
                seed = seed * 1103515245 + 12345;
                int ly = ty + CEIL_H + (int)(seed % (unsigned int)(CELL_H - CEIL_H - 2));
                
                /* 3-phase cycle: 1/3 of lights dim each tick */
                int light_phase = (li + phase) % 3;
                if (light_phase == 0) {
                    /* Warm amber */
                    SDL_SetRenderDrawColor(game.renderer, 255, 220, 100, twinkle_alpha);
                } else if (light_phase == 1) {
                    /* Cool white */
                    SDL_SetRenderDrawColor(game.renderer, 200, 220, 255, twinkle_alpha - 40);
                } else {
                    /* Dim (skip this one) */
                    continue;
                }
                SDL_Rect dot = { lx, ly, 2, 2 };
                SDL_RenderFillRect(game.renderer, &dot);
            }
        }
        SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
    }
}

/* Find the people-sim shaft occupying column x with the given type */
static ElevatorShaft *find_people_shaft(int x, ItemType ty)
{
    PeopleSim *ps = &game.sim.people;
    for (int i = 0; i < ps->shaft_count; i++)
        if (ps->shafts[i].active && ps->shafts[i].x == x &&
            ps->shafts[i].type == ty)
            return &ps->shafts[i];
    return NULL;
}

static int elv_structural_stop(const ElevatorShaft *s, int fidx);

/* Floor number on a shaft section, composed from the 0x87e9 digit glyphs
 * exactly like the original (OS Elevator::render). Serviced floors only.
 * hot = a car of the group is on this floor -> draw the red 0x87ec twin. */
static void draw_shaft_digits(int tx, int ty, int w, int wf, int hot)
{
    if (getenv("ELV_REDTEST")) hot = 1;   /* force red plates for a capture */
    Sprite *d = sprites_find(&game.sprites,
                             hot ? SPR_ELEV_DIGITS_RED : SPR_ELEV_DIGITS);
    if (!d) d = sprites_find(&game.sprites, SPR_ELEV_DIGITS);  /* red missing */
    if (!d || wf < 0) return;     /* basements unlabeled (only 0-9 glyphs) */
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "%d", wf);
    if (n < 1 || n > 3) return;
    int x = tx + (w - (n * 12 - 1)) / 2;
    for (int i = 0; i < n; i++) {
        SDL_Rect src = { 1 + 16 * (buf[i] - '0'), 16, 11, 17 };
        SDL_Rect dst = { x, ty + 10, 11, 17 };
        SDL_RenderCopy(game.renderer, d->texture, &src, &dst);
        x += 12;
    }
}

/* Per-floor extents of the floor map — the OverlayT anchor for every
 * decoration. Mirrors the file's floor records: every floor a
 * non-transport tenant covers, including multi-floor continuations.
 * right is EXCLUSIVE; right == 0 marks an empty floor. */
static int16_t ovl_left[TOWER_FLOOR_COUNT], ovl_right[TOWER_FLOOR_COUNT];

static void render_occupants(void);
static uint8_t condo_lit[MAX_TENANTS];   /* filled by render_tower each frame */

static void floor_map_extents(void)
{
    tower_floor_extents(&game.tower, ovl_left, ovl_right);
}

static void render_tower(void)
{
    /* Determine visible floor range */
    int top_floor, bot_floor, dummy;
    screen_to_grid(0, 0, &top_floor, &dummy);
    screen_to_grid(0, game.screen_h, &bot_floor, &dummy);
    top_floor += 2;
    bot_floor -= 2;
    if (top_floor > TOWER_TOP_FLOOR) top_floor = TOWER_TOP_FLOOR;
    if (bot_floor < TOWER_MIN_FLOOR) bot_floor = TOWER_MIN_FLOOR;
    
    /* Per-condo resident sleep census (the EXE's all-asleep check,
     * UniPeple 7100): a unit shows lit while any resident is awake and
     * goes dark when the last one sleeps — the staggered evening look.
     * File-scope: render_occupants also reads it, so a dark unit shows
     * no midnight pacing residents either. */
    memset(condo_lit, 0, (size_t)game.tower.tenant_count);
    for (int i = 0; i < game.sim.people.people_high; i++) {
        const Person *p = &game.sim.people.people[i];
        if (!p->home_tenant || p->state != PERSON_AT_DEST) continue;
        if (p->errand == 8) continue;                 /* asleep */
        Tenant *ct = tower_tenant(&game.tower, p->home_tenant);
        if (!ct || ct->type != ITEM_CONDO) continue;
        if (p->cur_floor != (uint8_t)floor_to_index(ct->floor)) continue;
        condo_lit[ct - game.tower.tenants] = 1;
    }

    /* Lobby sprite (raw bitmap, 992×36) */
    Sprite *lobby_spr = sprites_find(&game.sprites, SPR_LOBBY_BOT0);
    
    /* ====== PASS 1: Floor backgrounds ======
     * Draw ALL floor backgrounds first, so multi-floor sprites
     * can paint over them without being overwritten. */
    /* First: solid dirt across the whole view below the surface, shaded
     * darker with depth — built floors carve interiors out of it next.
     * (The original fills the underground wholesale; flat brown bands
     * only under buildings was a port artifact.) */
    {
        for (int floor = -1; floor >= bot_floor - 1; floor--) {
            int sx, sy;
            grid_to_screen(floor, 0, &sx, &sy);
            (void)sx;
            if (sy > game.screen_h) break;
            if (sy + CELL_H < 0) continue;
            int depth = -floor;
            int r = 120 - depth * 6; if (r < 60) r = 60;
            int g = 85 - depth * 5;  if (g < 35) g = 35;
            int b = 50 - depth * 4;  if (b < 15) b = 15;
            SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
            int band_h = (floor == bot_floor - 1) ? game.screen_h - sy
                                                  : CELL_H;
            SDL_Rect band = { 0, sy, game.screen_w, band_h };
            SDL_RenderFillRect(game.renderer, &band);
        }
    }

    /* The floor strip behind every CONTIGUOUS run of built cells —
     * above and below ground alike. The strip is the EXE's own art:
     * bitmap 0x83E8, the 2px column at x=16 (horizontally uniform) —
     * a 12px concrete joist band over a 24px DARK GREY empty interior.
     * Tenants paint over the interior, leaving the joist band visible
     * between every pair of floors, as in the original. Gaps between
     * runs show sky/dirt. */
    Sprite *floor_strip = sprites_find(&game.sprites, 0x83e8);
    for (int floor = bot_floor; floor <= top_floor; floor++) {
        int fidx = floor_to_index(floor);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;

        int sx_base, sy_base;
        grid_to_screen(floor, 0, &sx_base, &sy_base);

        for (int x = 0; x < TOWER_WIDTH; ) {
            /* The cathedral brings its own complete art (dome against
             * sky) — no shell/joist backdrop behind it */
            ItemType ct = game.tower.grid[fidx][x].type;
            if (ct == ITEM_NONE || ct == ITEM_CATHEDRAL) { x++; continue; }
            int run = x;
            while (run < TOWER_WIDTH &&
                   game.tower.grid[fidx][run].type != ITEM_NONE &&
                   game.tower.grid[fidx][run].type != ITEM_CATHEDRAL) run++;

            int wall_x = sx_base + x * CELL_W;
            int wall_w = (run - x) * CELL_W;
            if (floor_strip) {
                SDL_Rect src = { 16, 0, 2, 36 };
                SDL_Rect dst = { wall_x, sy_base, wall_w, CELL_H };
                SDL_RenderCopy(game.renderer, floor_strip->texture, &src, &dst);
            } else {
                /* fallback: joist band + dark interior */
                SDL_SetRenderDrawColor(game.renderer, 64, 64, 64, 255);
                SDL_Rect wall_rect = { wall_x, sy_base, wall_w, CELL_H };
                SDL_RenderFillRect(game.renderer, &wall_rect);
                SDL_SetRenderDrawColor(game.renderer, 178, 172, 160, 255);
                SDL_Rect ceil_rect = { wall_x, sy_base, wall_w, CEIL_H };
                SDL_RenderFillRect(game.renderer, &ceil_rect);
            }
            x = run;
        }
    }
    
    /* ====== PASS 2: Tenant sprites ======
     * Iterate the TENANT ARRAY, not the grid: a shaft stamped over a
     * tenant's leftmost cell would hide it from a grid scan (real saves
     * legally overlap tenants and shaft columns — that "sparse tower"
     * bug). Tenants never overlap each other, so array order is fine. */
    for (int ti = 0; ti < game.tower.tenant_count; ti++) {
        Tenant *tenant = &game.tower.tenants[ti];
        if (tenant->type == ITEM_NONE) continue;
        /* Floor strips: drawn by the background pass */
        if (tenant->type == ITEM_FLOOR) continue;
        /* Elevators draw in their own pass, in FRONT of lobbies;
         * stairs/escalators in the overlay pass after that */
        if (item_is_transport(tenant->type)) continue;
        int floor = tenant->floor;
        if (floor > top_floor ||
            floor + tenant->height - 1 < bot_floor) continue;
        {

            int frame_w_hint = 0, item_floors = 1;
            uint16_t spr_id = item_sprite_id(tenant->type, &frame_w_hint, &item_floors);
            spr_id = item_sprite_animated(tenant->type, spr_id);
            /* TOWER wedding: the cathedral wears the ceremony art —
             * six strips, the cherub banner floating above the dome */
            if (tenant->type == ITEM_CATHEDRAL && game.sim.wedding.active) {
                spr_id = SPR_CATH_CEREMONY;
                item_floors = 6;
            }
            /* Retail: swap in this tenant's storefront variant */
            if (tenant->type == ITEM_RESTAURANT || tenant->type == ITEM_SHOP ||
                tenant->type == ITEM_FAST_FOOD) {
                int v = twr_tenant_variant(&game.tower, tenant);
                spr_id = tenant->type == ITEM_RESTAURANT ? SPR_RESTAURANT_V0 + v
                       : tenant->type == ITEM_FAST_FOOD  ? SPR_FASTFOOD_V0 + v
                       : 0x8668 + v;
            }
            Sprite *spr = spr_id ? sprites_find(&game.sprites, spr_id) : NULL;
            
            int tx, ty;
            grid_to_screen(floor, tenant->x, &tx, &ty);
            int tw = tenant->width * CELL_W;
            
            int tenant_y = ty + CEIL_H;
            
            /* Calculate draw rect for this tenant */
            int draw_h = (item_floors > 1) ? item_floors * CELL_H : TENANT_H;
            int draw_y = (item_floors > 1) ? ty - (item_floors - 1) * CELL_H : tenant_y;
            
            if (tenant->type == ITEM_LOBBY && lobby_spr) {
                /* Lobby sheets (layout decoded via OS loadLobbies): each
                 * 984px raw 0x89E8+v (v = star variant 1★/2★/3★+) holds
                 * three 328px chunks: [0] ground lobby, [1] sky lobby,
                 * [2] ground row of the grand multi-story lobby. The
                 * grand lobby's upper rows live in 0x8A28+v (middle row,
                 * chunk at x=328) and 0x8A68+v (top row, chunk at x=0).
                 * Chunk carve: 256px body at +0, 56px end cap at +272. */
                int v = game.tower.star_rating >= 3 ? 2
                      : game.tower.star_rating == 2 ? 1 : 0;
                int fidx = floor_to_index(floor);
                int lob_below = (fidx - 1 >= 0 &&
                    game.tower.grid[fidx - 1][tenant->x].type == ITEM_LOBBY);
                int lob_above = (fidx + 1 < TOWER_FLOOR_COUNT &&
                    game.tower.grid[fidx + 1][tenant->x].type == ITEM_LOBBY);
                uint16_t sheet_id = SPR_LOBBY_BOT0 + v;
                int chunk = 0;
                if (floor == 0)        chunk = lob_above ? 2 : 0;
                else if (!lob_below)   chunk = 1;   /* sky lobby */
                else {                              /* grand lobby upper row */
                    sheet_id = (lob_above ? SPR_LOBBY_MID0 : SPR_LOBBY_TOP0) + v;
                    chunk = lob_above ? 1 : 0;      /* OS: middle reads x=328 */
                }
                Sprite *sheet = sprites_find(&game.sprites, sheet_id);
                if (!sheet) sheet = lobby_spr;
                int base = chunk * 328;
                int interior_w = 256;
                int cap_w = 56;
                int lobby_pw = tenant->width * CELL_W;

                /* Faithful to the original (OpenSkyscraper Lobby::render +
                 * loadLobbies): the lobby is the interior BODY (chunk +0,
                 * 256px) TILED across the whole width, with the facade
                 * overlay (chunk +272, 56px) drawn exactly ONCE at the LEFT
                 * edge — it is a single left-edge facade, NOT a per-segment
                 * separator. (The old code tiled [facade+body] every 312px,
                 * stamping the facade mid-lobby — those were the stray red
                 * "endcap" strips.) The red entrance awnings at the two true
                 * ends come separately from the 0x83E9 overlay pass. */
                for (int sxp = 0; sxp < lobby_pw; sxp += interior_w) {
                    int bw = interior_w;
                    if (sxp + bw > lobby_pw) bw = lobby_pw - sxp;
                    int bsx = tx + sxp;
                    if (bsx + bw < 0 || bsx > game.screen_w) continue;
                    SDL_Rect src_body = { base, 0, bw, sheet->h };
                    SDL_Rect dst_body = { bsx, ty, bw, CELL_H };
                    SDL_RenderCopy(game.renderer, sheet->texture, &src_body, &dst_body);
                }
                /* Facade overlay — once, at the left edge, on top of the body. */
                {
                    int fw = cap_w;
                    if (fw > lobby_pw) fw = lobby_pw;
                    if (fw > 0 && !(tx + fw < 0 || tx > game.screen_w)) {
                        SDL_Rect src_cap = { base + 272, 0, fw, sheet->h };
                        SDL_Rect dst_cap = { tx, ty, fw, CELL_H };
                        SDL_RenderCopy(game.renderer, sheet->texture, &src_cap, &dst_cap);
                    }
                }
                continue;
            } else if (spr && frame_w_hint > 0) {
                /* Frame-based sprite sheet.
                 * Frame selection driven by capacity byte (from TenantMake).
                 * capacity 0x10→frame 0, 0x18→1, 0x20→2, etc.
                 * This makes offices fill with people during the day and
                 * empty at night, hotels the reverse. */
                int nframes = spr->w / frame_w_hint;
                if (nframes < 1) nframes = 1;
                /* Map capacity (0x00-0x40) to available frame range.
                 * capacity_to_frame gives 0-6, but sprites have varying
                 * frame counts (office=4, hotel=9, restaurant=4, etc).
                 * Scale proportionally so cap 0x40 always maps to last frame. */
                /* Restaurants and fast food draw the EXE's venue state
                 * directly (CloudT: frame = variant*4 + state): 0 open-
                 * empty / 1 busy (1-9 inside) / 2 packed (10+) / 3 closed
                 * — driven by the retail cycle's doors and the live patron
                 * count, not by clock guesswork. (UpdateVenueBusyTier
                 * 11a8:0bd5: 10+ patrons = packed, byte-verified.) */
                int retail_closes = (nframes >= 2 &&
                    (tenant->type == ITEM_RESTAURANT ||
                     tenant->type == ITEM_FAST_FOOD));
                int closed = retail_closes && !tenant->retail_open;
                int crowd_frames = retail_closes ? nframes - 1 : nframes;

                int is_hotel = (tenant->type == ITEM_HOTEL_SINGLE ||
                                tenant->type == ITEM_HOTEL_TWIN ||
                                tenant->type == ITEM_HOTEL_SUITE);

                /* Art style: swap in this tenant's style sheet (the
                 * intra-sheet frame math below is style-invariant). */
                if (tenant->style && (is_hotel || tenant->type == ITEM_CONDO)) {
                    uint16_t sid = 0;
                    switch (tenant->type) {
                    case ITEM_HOTEL_SINGLE: sid = SPR_HOTEL_S_S1; break;
                    case ITEM_HOTEL_TWIN:
                        sid = (uint16_t)(SPR_HOTEL_T_S1 + tenant->style - 1);
                        break;
                    case ITEM_HOTEL_SUITE:  sid = SPR_HOTEL_SUITE_S1; break;
                    case ITEM_CONDO:
                        sid = (uint16_t)(SPR_CONDO_S1 + tenant->style - 1);
                        break;
                    default: break;
                    }
                    Sprite *alt = sid ? sprites_find(&game.sprites, sid) : NULL;
                    if (alt) { spr = alt; nframes = spr->w / frame_w_hint; }
                }

                int frame_idx;
                if (is_hotel && nframes >= 9) {
                    /* Hotel room sheet = door (frame 0) + 8 room frames as
                     * state×time-of-day pairs (day, night):
                     *   1/2 occupied · 3/4 clean · 5/6 dirty · 7/8 cockroaches.
                     * This is exactly the EXE's status byte: frame = +0x0B/8,
                     * so the band pairs (0x28/0x30 dirty, 0x38/0x40 infested)
                     * ARE these day/night frames. The old "dirty + 2
                     * complaints -> roaches" rule here was an inference; the
                     * real trigger is the room's condition (R4 referee). */
                    int night = (game.sim.time_of_day == TOD_NIGHT ||
                                 game.sim.time_of_day == TOD_EVENING);
                    if (tenant->condition == ROOM_INFESTED)
                        frame_idx = night ? 8 : 7;   /* cockroaches */
                    else if (tenant->condition == ROOM_DIRTY)
                        frame_idx = night ? 6 : 5;   /* needs housekeeping */
                    else if (tenant->capacity > CAP_EMPTY)
                        frame_idx = night ? 2 : 1;   /* guest in the room */
                    else
                        frame_idx = night ? 4 : 3;   /* clean, vacant */
                } else if (tenant->type == ITEM_OFFICE) {
                    /* Office interior — the FULL variant set, decoded from the
                     * four office sheets and cross-checked against OpenSkyscraper
                     * (SimTowerLoader::loadOffice + Office::updateSprite):
                     *   0x85a8 / 0x85a9 / 0x85aa : 4 frames each = TWO furniture
                     *       variants apiece, every variant a lit/unlit (day/night)
                     *       pair → six occupied variants in all.
                     *   0x85ab : 2 frames = the VACANT office (bare windows, no
                     *       desks), lit/unlit — shown until a tenant leases it.
                     * OS picks index_y = occupied ? variant : 6 and
                     * index_x = lit ? 0 : 1; the port mirrors that with a stable
                     * per-tenant variant (cosmetic, like the retail/parking
                     * variants), the empty sheet before move-in, and day/night by
                     * time-of-day. Supersedes the earlier tier-based guess, which
                     * predated finding sheets 0x85a9..0x85ab (it only had 0x85a8
                     * and mistook its second furniture pair for a tier variant). */
                    int night = (game.sim.time_of_day == TOD_NIGHT ||
                                 game.sim.time_of_day == TOD_EVENING);
                    int leased = (tenant->state >= TENANT_MOVING_IN &&
                                  tenant->state != TENANT_ABANDONED);
                    uint16_t office_sheet;
                    if (!leased) {
                        office_sheet = 0x85ab;             /* bare vacant room */
                        frame_idx = night ? 1 : 0;
                    } else {
                        /* Furniture style from the build rotation (the
                         * EXE's 0x7954 counter mod 6), not id-random. */
                        int variant = tenant->style % 6;
                        office_sheet = 0x85a8 + variant / 2;
                        frame_idx = (variant % 2) * 2 + (night ? 1 : 0);
                    }
                    Sprite *osheet = sprites_find(&game.sprites, office_sheet);
                    if (osheet) { spr = osheet; nframes = spr->w / frame_w_hint; }
                } else if (tenant->type == ITEM_CONDO && nframes >= 5) {
                    /* Condo sheet (0x8628..0x862c) = occupied day/evening/night
                     * (0/1/2) + For-Sale day/night (3/4) (Jonah's decode + OS
                     * loadCondo). Sold once it has residents (reachable ->
                     * population>0); unsold/unreachable shows the For Sale board. */
                    TimeOfDay tod = game.sim.time_of_day;
                    if (tenant->population > 0) {
                        /* evening/night: lit while a resident is awake,
                         * dark once the last one turns in (staggered
                         * per unit — no more uniform blackout) */
                        if (tod == TOD_NIGHT || tod == TOD_EVENING)
                            frame_idx =
                                condo_lit[tenant - game.tower.tenants] ? 1 : 2;
                        else
                            frame_idx = 0;
                    } else
                        frame_idx = (tod == TOD_NIGHT || tod == TOD_EVENING) ? 4 : 3;
                } else if (tenant->type == ITEM_MEDICAL && nframes >= 3) {
                    /* Medical: 1 clean/idle by day, 2 at night. Frame 0 was the
                     * fabricated "emergency" state — no real trigger exists. */
                    frame_idx = (game.sim.time_of_day == TOD_NIGHT) ? 2 : 1;
                } else if (tenant->type == ITEM_CINEMA && nframes >= 4) {
                    /* Cinema: the venue state IS the sprite frame (the EXE
                     * renderer draws slot+6 directly, seg_1038:0396) —
                     * 0 closed, 1 open, 2 has patrons, 3 show running.
                     * Jonah's frame decode matched it exactly. */
                    frame_idx = tenant->venue_state <= 3 ? tenant->venue_state : 0;
                } else if (tenant->type == ITEM_PARTY_HALL && nframes >= 3) {
                    /* Party hall: same state-driven pick, folded onto its
                     * 3-frame sheet (0 dark, 1 lit, 2+ party). */
                    frame_idx = tenant->venue_state == 0 ? 0
                              : tenant->venue_state == 1 ? 1 : 2;
                } else if (tenant->type == ITEM_PARKING && nframes >= 15) {
                    /* Parking: 15-frame composite. Sheets dumped from the
                     * EXE 2026-07-12: 0x86A8 = ONE empty-bay frame, 0x86A9
                     * = red X + 13 car variants. So composite frame 0 =
                     * empty bay, 1 = X, 2-14 = cars. (The June "frame 0 =
                     * X" annotation was wrong — the old variant math both
                     * leaked X's onto good spaces and never showed an
                     * empty bay.) A space draws a car while the garage's
                     * parked-car count covers its ordinal — the garage
                     * fills by day and empties at night. */
                    int parked = game.tower.cars_office +
                                 game.tower.cars_suite;
                    frame_idx = !tenant->space_usable ? 1
                              : (tenant->space_ordinal < parked)
                                    ? 2 + (tenant->id % 13) : 0;
                } else if (tenant->type == ITEM_RECYCLING && nframes >= 5) {
                    /* Recycling (Jonah): the frames are a trash-accumulation
                     * cycle. We don't model a fill level, so loop it slowly —
                     * trash piles up then the truck clears it back to empty. */
                    frame_idx = (game.sim.frame / 96) % nframes;
                } else if (tenant->type == ITEM_METRO && nframes >= 3) {
                    /* Metro: frame 0 = TRAIN at the platform (sheet 0x8C29,
                     * eyeballed 2026-07-11), 1 = empty day, 2 = night. The
                     * sim's 1%/tick toggle (DoRandomSubwayInOut) drives it
                     * via venue_state — trains come and go, 10AM-5PM. */
                    frame_idx = (game.sim.time_of_day == TOD_NIGHT) ? 2
                              : tenant->venue_state ? 0 : 1;
                } else if (closed) {
                    frame_idx = nframes - 1;          /* shuttered storefront */
                } else if (retail_closes) {
                    /* open restaurant/FF: the live patron tiers */
                    frame_idx = tenant->patrons_now >= 10 ? 2
                              : tenant->patrons_now >= 1  ? 1 : 0;
                    if (frame_idx > crowd_frames - 1) frame_idx = crowd_frames - 1;
                } else if (tenant->type == ITEM_SHOP && nframes >= 3) {
                    /* Shops: the variant sheets hold only the 3 fill frames;
                     * the closed and for-rent storefronts are the two SHARED
                     * single-frame sheets appended after the 11 variants —
                     * frame 0x21 = 0x8673 "For RENT", 0x22 = 0x8674 shuttered
                     * (referee 2026-07-18; sheets dumped and confirmed
                     * 2026-07-30 — their old "end-cap" label was wrong).
                     * State pick per the EXE: un-let → 0x21, outside opening
                     * hours → 0x22, else the patron tiers. */
                    uint16_t shared =
                        tenant->state == TENANT_ABANDONED ? SPR_SHOP_END0
                        : !tenant->retail_open            ? SPR_SHOP_END1 : 0;
                    Sprite *sh = shared ? sprites_find(&game.sprites, shared)
                                        : NULL;
                    if (sh) {
                        spr = sh;
                        nframes = 1;
                        frame_idx = 0;
                    } else {
                        frame_idx = tenant->patrons_now >= 10 ? 2
                                  : tenant->patrons_now >= 1  ? 1 : 0;
                    }
                } else if (tenant->capacity <= CAP_EMPTY) {
                    frame_idx = 0;
                } else {
                    /* Proportional mapping: cap [0x10..0x40] → [0..crowd_frames-1] */
                    int cap_range = CAP_MAX - CAP_MIN;  /* 0x30 = 48 */
                    int cap_pos = tenant->capacity - CAP_MIN;
                    if (cap_pos < 0) cap_pos = 0;
                    if (cap_pos > cap_range) cap_pos = cap_range;
                    frame_idx = (cap_pos * (crowd_frames - 1)) / cap_range;
                }
                if (frame_idx >= nframes) frame_idx = nframes - 1;
                if (frame_idx < 0) frame_idx = 0;
                
                /* Cathedral: frame 1 is the lit night version */
                if (tenant->type == ITEM_CATHEDRAL)
                    frame_idx = (game.sim.time_of_day == TOD_NIGHT ||
                                 game.sim.time_of_day == TOD_EVENING) ? 1 : 0;

                /* Under construction: draw the dark construction-grid floor
                 * (0x8E28, one floor tall, tileable) instead of the finished
                 * interior — the workers (below) supply the activity. Falls back
                 * to the normal sprite if the grid sprite isn't loaded. */
                /* Two construction stages: open scaffolding grid (0x8E28) for
                 * the first half, then the solid walls-up fill (0x8E29) as it
                 * nears completion — visible on slow builds like hotels.
                 * (Stage split inferred; both are dedicated construction art.) */
                uint16_t cg_id = SPR_CONST_GRID;
                if (tenant->state == TENANT_CONSTRUCTION &&
                    (int)tenant->type < ITEM_TYPE_COUNT) {
                    int total = CONSTRUCTION_TIME[(int)tenant->type];
                    if (total > 1 && tenant->construction <= total / 2)
                        cg_id = SPR_CONST_SOLID;
                }
                Sprite *cgrid = (tenant->state == TENANT_CONSTRUCTION)
                              ? sprites_find(&game.sprites, cg_id) : NULL;
                if (cgrid && cgrid->w > 0) {
                    /* Anchor to the CELL top, not the tenant strip: the 36px
                     * scaffold art covers the full floor incl. the ceiling
                     * band (drawing it at tenant_y sank it 12px into the
                     * floor below). The 24px walls-up stage (0x8E29) sits in
                     * the tenant strip, with the scaffold sheet's slab band
                     * keeping the ceiling above it. Tiles blit 1:1 — never
                     * stretched. */
                    int cy0 = ty - (item_floors - 1) * CELL_H;
                    Sprite *scaf = sprites_find(&game.sprites, SPR_CONST_GRID);
                    for (int fr = 0; fr < item_floors; fr++) {
                        int fy = cy0 + fr * CELL_H;
                        for (int gx = tx; gx < tx + tw; gx += cgrid->w) {
                            int gw = cgrid->w;
                            if (gx + gw > tx + tw) gw = tx + tw - gx;
                            if (cgrid->h < CELL_H && scaf) {
                                SDL_Rect cs = { 0, 0, gw, CEIL_H };
                                SDL_Rect cd = { gx, fy, gw, CEIL_H };
                                SDL_RenderCopy(game.renderer, scaf->texture,
                                               &cs, &cd);
                            }
                            SDL_Rect gs = { 0, 0, gw, cgrid->h };
                            SDL_Rect gd = { gx, fy + CELL_H - cgrid->h,
                                            gw, cgrid->h };
                            SDL_RenderCopy(game.renderer, cgrid->texture, &gs, &gd);
                        }
                    }
                } else {
                    if (tenant->state == TENANT_CONSTRUCTION) frame_idx = 0;
                    SDL_Rect src = { frame_idx * frame_w_hint, 0, frame_w_hint, spr->h };
                    SDL_Rect dst = { tx, draw_y, tw, draw_h };
                    SDL_RenderCopy(game.renderer, spr->texture, &src, &dst);
                }

                /* Recycling collection truck (0x892E): pulls up to empty the
                 * center when the trash cycle is at its fullest, then it's gone
                 * (collected). Slides in from the left, parks, slides back. */
                if (tenant->type == ITEM_RECYCLING &&
                    ((game.sim.frame / 96) % nframes) == nframes - 1) {
                    Sprite *truck = sprites_find(&game.sprites, 0x892E);
                    if (truck) {
                        int win = game.sim.frame % 96;          /* 0..95 */
                        float p = (win < 32) ? win / 32.0f
                                : (win < 64) ? 1.0f
                                : 1.0f - (win - 64) / 32.0f;    /* arrive/hold/leave */
                        int tk_w = tw / 3;
                        int parked = tx + tw / 2 - tk_w / 2;
                        int off = tx - tk_w;
                        SDL_Rect td = { off + (int)((parked - off) * p),
                                        draw_y + draw_h - truck->h,
                                        tk_w, truck->h };
                        SDL_RenderCopy(game.renderer, truck->texture, NULL, &td);
                    }
                }
            } else if (spr) {
                /* Full sprite, no frame extraction — scale to fit */
                SDL_Rect dst = { tx, draw_y, tw, draw_h };
                SDL_RenderCopy(game.renderer, spr->texture, NULL, &dst);
            } else {
                /* Fallback: colored rectangle with label */
                uint8_t r, g, b;
                item_fallback_color(tenant->type, &r, &g, &b);
                SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
                SDL_Rect rect = { tx, draw_y, tw, draw_h };
                SDL_RenderFillRect(game.renderer, &rect);
                SDL_SetRenderDrawColor(game.renderer, 60, 60, 60, 255);
                SDL_RenderDrawRect(game.renderer, &rect);
                if (item_floors > 1) {
                    SDL_SetRenderDrawColor(game.renderer, r - 20, g - 20, b - 20, 255);
                    int mid_y = draw_y + CELL_H;
                    SDL_RenderDrawLine(game.renderer, tx, mid_y, tx + tw, mid_y);
                }
            }

            /* Burned-out cell: lay the fire/destroyed rubble over the wreckage.
             * Sampled by world position so adjacent burned cells form one
             * continuous rubble field. Persists until the tenant is rebuilt. */
            if (tenant->burned && game.fire_destroyed && game.fire_destroyed->texture) {
                Sprite *rub = game.fire_destroyed;
                int sx = (tenant->x * CELL_W) % rub->w;
                int dx = tx, remaining = tw;
                while (remaining > 0) {
                    int seg = rub->w - sx;
                    if (seg > remaining) seg = remaining;
                    SDL_Rect src = { sx, 0, seg, rub->h };
                    SDL_Rect dst = { dx, tenant_y, seg, rub->h };
                    SDL_RenderCopy(game.renderer, rub->texture, &src, &dst);
                    dx += seg; remaining -= seg; sx = 0;
                }
            }

            /* Tenant state visual overlay (not for transports — a shaft
             * has no vacancy/stress state to tint — nor the cathedral,
             * which is never rented) */
            if (!item_is_transport(tenant->type) &&
                tenant->type != ITEM_CATHEDRAL) {
                SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
                
                if (tenant->state == TENANT_CONSTRUCTION) {
                    /* The scaffold tiles (drawn above) are the whole look; the
                     * occupants pass supplies the EXE's one worker figure.
                     * (The old extra worker loop here sliced the 16px-frame
                     * sheet at 32px and stretched it — the flashing
                     * half-workers bug.) */
                } else if (tenant->state == TENANT_STRESSED) {
                    /* No unit tint: the EXE shows stress on the WAITING
                     * PEOPLE (queue silhouettes shade pink->red — drawn in
                     * render_shaft) and in the eval views, never as a red
                     * wash over the unit art. (The old pulse was invented.) */
                } else if (tenant->state == TENANT_VACANT || tenant->state == TENANT_EMPTY) {
                    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 60);
                    SDL_Rect overlay = { tx, draw_y, tw, draw_h };
                    SDL_RenderFillRect(game.renderer, &overlay);
                } else if (tenant->state == TENANT_ABANDONED) {
                    SDL_SetRenderDrawColor(game.renderer, 180, 0, 0, 50);
                    SDL_Rect overlay = { tx, draw_y, tw, draw_h };
                    SDL_RenderFillRect(game.renderer, &overlay);
                } else if (game.sim.time_of_day == TOD_NIGHT && 
                           tenant->state == TENANT_OCCUPIED) {
                    SDL_SetRenderDrawColor(game.renderer, 255, 220, 100, 30);
                    SDL_Rect overlay = { tx, draw_y, tw, draw_h };
                    SDL_RenderFillRect(game.renderer, &overlay);
                }
                
                SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
            }
        }
    }

    /* In-tenant people draw with their units, BEFORE the shaft pass — in the
     * EXE, AnimPeple composites inside the cell repaint while the elevator
     * pass (ElvPeple, seg_1090 tick-swap) runs after, so a shaft passing
     * through an office covers the workers, never the reverse. */
    render_occupants();

    /* ====== PASS 2.3: Decorative overlays (awning, fire escape) ======
     * All of OverlayT (seg_11c0) anchors to the FLOOR MAP's per-floor
     * extents — transports live in separate file blocks and do not
     * count, so a shaft poking past the wall never drags an overlay
     * with it. Drawn BEFORE the shafts so a shaft standing at the wall
     * covers the fire escape, not the reverse. Extents computed once
     * per frame, shared with the crane (drawn later, after people). */
    floor_map_extents();

    /* Entrance awnings — GROUND FLOOR ONLY (11c0:0374 draws solely on
     * file floor 10): 56x36 halves of 0x83E9 hanging outside the
     * ground extent's edges. Sky lobbies get none. */
    if (game.entrances) {
        int gi = floor_to_index(0);
        if (ovl_right[gi] > 0) {
            int half_w = game.entrances->w / 2;
            int tx, ty;
            grid_to_screen(0, ovl_left[gi], &tx, &ty);
            SDL_Rect src_l = { 0, 0, half_w, game.entrances->h };
            SDL_Rect awning_l = { tx - half_w, ty, half_w, CELL_H };
            SDL_RenderCopy(game.renderer, game.entrances->texture, &src_l, &awning_l);
            grid_to_screen(0, ovl_right[gi], &tx, &ty);
            SDL_Rect src_r = { half_w, 0, half_w, game.entrances->h };
            SDL_Rect awning_r = { tx, ty, half_w, CELL_H };
            SDL_RenderCopy(game.renderer, game.entrances->texture, &src_r, &awning_r);
        }
    }

    /* Fire escapes — both sides of every floor's extent, ABOVE the ground
     * floor up to the build ceiling. Jonah's call: no fire escapes on the
     * ground floor (the lobby/entrance level); basements get none either. */
    if (game.fireladder) {
        int half_w = game.fireladder->w / 2;
        for (int floor = 1; floor <= TOWER_MAX_FLOOR; floor++) {
            int fidx = floor_to_index(floor);
            if (ovl_right[fidx] == 0) continue;     /* empty floor */
            int lx, ly, rx, ry;
            grid_to_screen(floor, ovl_left[fidx], &lx, &ly);
            grid_to_screen(floor, ovl_right[fidx], &rx, &ry);
            SDL_Rect src_left = { 0, 0, half_w, game.fireladder->h };
            SDL_Rect fe_left = { lx - half_w, ly, half_w, CELL_H };
            SDL_RenderCopy(game.renderer, game.fireladder->texture, &src_left, &fe_left);
            SDL_Rect src_right = { half_w, 0, half_w, game.fireladder->h };
            SDL_Rect fe_right = { rx, ry, half_w, CELL_H };
            SDL_RenderCopy(game.renderer, game.fireladder->texture, &src_right, &fe_right);
        }
    }

    /* ====== PASS 2.5: Elevator shafts ======
     * Drawn after tenants so shafts overlay lobbies the same way they
     * overlay the ceiling joists — as in the original. */
    for (int i = 0; i < game.tower.tenant_count; i++) {
        Tenant *t = &game.tower.tenants[i];
        if (!item_is_elevator(t->type)) continue;
        if (t->floor < bot_floor || t->floor > top_floor) continue;

        int frame_w_hint = 0, item_floors = 1;
        uint16_t spr_id = item_sprite_id(t->type, &frame_w_hint, &item_floors);
        Sprite *spr = spr_id ? sprites_find(&game.sprites, spr_id) : NULL;
        if (!spr) continue;

        int tx, ty;
        grid_to_screen(t->floor, t->x, &tx, &ty);
        int tw = t->width * CELL_W;

        /* Dialog Show Off: the shaft renders as just its two guide rails —
         * whatever it passes through stays visible. */
        ElevatorShaft *hs = find_people_shaft(t->x, t->type);
        if (hs && hs->hidden) {
            SDL_SetRenderDrawColor(game.renderer, 70, 70, 78, 255);
            SDL_RenderDrawLine(game.renderer, tx, ty, tx, ty + CELL_H - 1);
            SDL_RenderDrawLine(game.renderer, tx + tw - 1, ty,
                               tx + tw - 1, ty + CELL_H - 1);
            continue;
        }

        /* Shaft section: always tile 0 of 0x87E8. The 36px tile spans
         * the FULL floor height. The wide (express, 6-cell) shaft = 8px
         * extension tiles (0x842c) flanking the 32px shaft. */
        SDL_Rect src = { 0, 0, 32, spr->h };
        if (tw == 48) {
            Sprite *ext = sprites_find(&game.sprites, SPR_ELEV_EXT);
            SDL_Rect mid = { tx + 8, ty, 32, CELL_H };
            SDL_RenderCopy(game.renderer, spr->texture, &src, &mid);
            if (ext) {
                SDL_Rect sl = { 0, 0, 8, ext->h };
                SDL_Rect dl = { tx, ty, 8, CELL_H };
                SDL_Rect sr = { 8, 0, 8, ext->h };
                SDL_Rect dr = { tx + 40, ty, 8, CELL_H };
                SDL_RenderCopy(game.renderer, ext->texture, &sl, &dl);
                SDL_RenderCopy(game.renderer, ext->texture, &sr, &dr);
            }
        } else {
            SDL_Rect dst = { tx, ty, tw, CELL_H };
            SDL_RenderCopy(game.renderer, spr->texture, &src, &dst);
        }
        /* floor number, on serviced stops only (as the original) */
        ElevatorShaft *es = find_people_shaft(t->x, t->type);
        int fi = floor_to_index(t->floor);
        if (es && fi >= es->lo && fi <= es->hi && es->serviced[fi] &&
            elv_structural_stop(es, fi)) {
            int hot = 0;   /* red plate: a car of this group is on this floor */
            for (int c = 0; c < es->num_cars; c++)
                if (es->car[c].active && es->car[c].floor == fi) { hot = 1; break; }
            draw_shaft_digits(tx, ty, tw, t->floor, hot);
        }
    }

    /* ====== PASS 3: Transport overlays (stairs, escalators) ======
     * Stairs (7 frames) and escalators (8 frames) are walk/ride animations:
     * frame 0 = empty, the rest = people in motion. Cycle them only on a leg
     * that's actually carrying someone, paused when the sim is. */
    static unsigned transport_anim = 0;
    if (game.sim.speed > 0) transport_anim++;

    /* Mark the exact stair/escalator each walker is on (walk_stair is the
     * carrying tenant's id — no more lighting up every unit on the floor
     * pair). Legacy fallback: a walker without an id (loaded mid-leg from
     * an older save) still lights its floor pair. */
    uint8_t walk_floor[TOWER_FLOOR_COUNT];
    uint8_t stair_busy[MAX_TENANTS];
    memset(walk_floor, 0, sizeof(walk_floor));
    memset(stair_busy, 0, sizeof(stair_busy));
    for (int i = 0; i < game.sim.people.people_high; i++) {
        Person *p = &game.sim.people.people[i];
        if (p->state != PERSON_WALKING) continue;
        if (p->walk_stair && p->walk_stair <= MAX_TENANTS) {
            stair_busy[p->walk_stair - 1] = 1;
            continue;
        }
        int lo = p->cur_floor < p->leg_floor ? p->cur_floor : p->leg_floor;
        if (lo >= 0 && lo < TOWER_FLOOR_COUNT) walk_floor[lo] = 1;
    }

    for (int i = 0; i < game.tower.tenant_count; i++) {
        Tenant *t = &game.tower.tenants[i];
        if (t->type != ITEM_STAIRS && t->type != ITEM_ESCALATOR) continue;

        int rise = t->height - 1; if (rise < 1) rise = 1;
        int fidx_lo = floor_to_index(t->floor);
        int busy = (game.anim_people &&
                    ((t->id && t->id <= MAX_TENANTS && stair_busy[t->id - 1]) ||
                     (fidx_lo >= 0 && fidx_lo < TOWER_FLOOR_COUNT &&
                      walk_floor[fidx_lo])));

        int tx, ty;
        grid_to_screen(t->floor, t->x, &tx, &ty);
        int tw = t->width * CELL_W;

        /* Grand-lobby tall variants (kinds 2-5): drawn from the CGPk raw
         * sheets 0x8FE9 (2-story) / 0x8FEA (3-story) — horizontal strips
         * of 64x36 bands, decoded by the raw loader. Layout (StairsT
         * 10c0:0345): stairs legs 0..rise x 11 bands each (frame 0 idle,
         * 1-10 walk), then escalators at base (rise+1)*11, 12 bands per
         * leg (frame 0 idle, 1-11 steps). Leg 0 is the TOP floor band. */
        if (rise > 1) {
            Sprite *sheet = sprites_find(&game.sprites,
                                         rise == 2 ? 0x8FE9 : 0x8FEA);
            if (sheet) {
                int legs = rise + 1;
                for (int L = 0; L < legs; L++) {
                    int band;
                    if (t->type == ITEM_STAIRS)
                        band = L * 11 + (busy ? (transport_anim / 2) % 10 + 1 : 0);
                    else
                        band = legs * 11 + L * 12 +
                               (busy ? (transport_anim / 2) % 11 + 1 : 0);
                    SDL_Rect src = { band * 64, 0, 64, 36 };
                    int ly;
                    grid_to_screen(t->floor + rise - L, t->x, &tx, &ly);
                    SDL_Rect dst = { tx, ly, tw, CELL_H };
                    SDL_RenderCopy(game.renderer, sheet->texture, &src, &dst);
                }
                continue;
            }
            /* no sheet -> fall through to the tinted-box fallback */
        }

        int frame_w_hint = 0, item_floors = 1;
        uint16_t spr_id = item_sprite_id(t->type, &frame_w_hint, &item_floors);
        spr_id = item_sprite_animated(t->type, spr_id);
        Sprite *spr = spr_id ? sprites_find(&game.sprites, spr_id) : NULL;
        if (rise > 1) { spr = NULL; item_floors = rise + 1; }

        if (spr && frame_w_hint > 0) {
            int nframes = spr->w / frame_w_hint;
            if (nframes < 1) nframes = 1;
            /* Empty unless someone's on this unit; then cycle the motion. */
            int frame_idx = 0;
            if (busy && nframes > 1)
                frame_idx = 1 + ((transport_anim / 4) % (nframes - 1));
            SDL_Rect src = { frame_idx * frame_w_hint, 0, frame_w_hint, spr->h };
            int draw_h = item_floors * CELL_H;
            int draw_y = ty - (item_floors - 1) * CELL_H;
            SDL_Rect dst = { tx, draw_y, tw, draw_h };
            SDL_RenderCopy(game.renderer, spr->texture, &src, &dst);
        } else {
            /* Fallback: semi-transparent overlay */
            SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
            uint8_t r, g, b;
            item_fallback_color(t->type, &r, &g, &b);
            SDL_SetRenderDrawColor(game.renderer, r, g, b, 150);
            int draw_h = item_floors * CELL_H;
            int draw_y = ty - (item_floors - 1) * CELL_H;
            SDL_Rect rect = { tx, draw_y, tw, draw_h };
            SDL_RenderFillRect(game.renderer, &rect);
            SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 200);
            SDL_RenderDrawLine(game.renderer, tx, draw_y + draw_h, tx + tw, draw_y);
            SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
        }
    }
    
    /* Floor number labels on the left edge */
    {
        int top_f, bot_f, dummy2;
        screen_to_grid(0, 0, &top_f, &dummy2);
        screen_to_grid(0, game.screen_h, &bot_f, &dummy2);
        if (top_f > TOWER_MAX_FLOOR) top_f = TOWER_MAX_FLOOR;
        if (bot_f < TOWER_MIN_FLOOR) bot_f = TOWER_MIN_FLOOR;
        
        if (game.font_small) {
            SDL_Color white = {255, 255, 255, 255};
            for (int f = bot_f; f <= top_f; f++) {
                int fsx, fsy;
                grid_to_screen(f, 0, &fsx, &fsy);
                
                /* Floor number label */
                char flabel[16];
                if (f < 0) snprintf(flabel, sizeof(flabel), "B%d", -f);
                else if (f == 0) snprintf(flabel, sizeof(flabel), "L");
                else snprintf(flabel, sizeof(flabel), "%d", f);
                
                SDL_Surface *ts = TTF_RenderText_Blended(game.font_small, flabel, white);
                if (ts) {
                    SDL_Texture *tt = SDL_CreateTextureFromSurface(game.renderer, ts);
                    SDL_Rect dst = { 2, fsy + CELL_H/2 - ts->h/2, ts->w, ts->h };
                    SDL_RenderCopy(game.renderer, tt, NULL, &dst);
                    SDL_DestroyTexture(tt);
                    SDL_FreeSurface(ts);
                }
            }
        } else {
            for (int f = bot_f; f <= top_f; f++) {
                int fsx, fsy;
                grid_to_screen(f, 0, &fsx, &fsy);
                SDL_SetRenderDrawColor(game.renderer, 200, 200, 200, 180);
                SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
                SDL_RenderDrawLine(game.renderer, 0, fsy + CELL_H - 1, 4, fsy + CELL_H - 1);
                SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
            }
        }
    }
    
    /* Diagnostic text labels next to each tenant (toggleable with F1) */
    if (game.font_small && game.show_debug) {
        SDL_Color yellow = {255, 255, 100, 255};
        static const char *state_names[] = {
            "empty", "BUILD", "movin", "OCCUP", "close", "vacnt", "STRES", "ABND"
        };
        for (int i = 0; i < game.tower.tenant_count; i++) {
            Tenant *t = &game.tower.tenants[i];
            if (t->type == ITEM_LOBBY || t->type == ITEM_FLOOR) continue;
            
            int tx, ty;
            grid_to_screen(t->floor, t->x + t->width, &tx, &ty);
            
            /* Build diagnostic string with state info */
            const char *sn = (t->state < 8) ? state_names[t->state] : "???";
            char info[256];
            if (t->state == TENANT_CONSTRUCTION) {
                snprintf(info, sizeof(info), "%s [%s %dt] cap:0x%02X",
                         tower_item_name(t->type), sn, t->construction, t->capacity);
            } else {
                snprintf(info, sizeof(info), "%s [%s] cap:0x%02X pop:%d str:%d z%d",
                         tower_item_name(t->type), sn, t->capacity, t->population,
                         t->stress, t->zone);
            }
            
            SDL_Surface *ts = TTF_RenderText_Blended(game.font_small, info, yellow);
            if (ts) {
                SDL_Texture *tt = SDL_CreateTextureFromSurface(game.renderer, ts);
                int label_y = ty + CELL_H / 2 - ts->h / 2;
                SDL_Rect dst = { tx + 4, label_y, ts->w, ts->h };
                SDL_RenderCopy(game.renderer, tt, NULL, &dst);
                SDL_DestroyTexture(tt);
                SDL_FreeSurface(ts);
            }
        }
    }
}

/* ---------- Analytics window (F3) ---------- */

#define WIN_TITLEBAR_H 18     /* Win3.1 style title bar height for dragging */
#define INSPECT_W      248    /* Tenant info dialog width (~176 DLU faithful) */
static void draw_win31_titlebar(int x, int y, int w, const char *title);

static void stats_plot(int gx, int gy, int gw, int gh,
                       const int64_t *v, int n, SDL_Color col,
                       int64_t vmin, int64_t vmax)
{
    if (n < 2 || vmax <= vmin) return;
    SDL_SetRenderDrawColor(game.renderer, col.r, col.g, col.b, 255);
    for (int i = 1; i < n; i++) {
        int x0 = gx + (i - 1) * (gw - 1) / (n - 1);
        int x1 = gx + i * (gw - 1) / (n - 1);
        int y0 = gy + gh - 1 - (int)((v[i-1] - vmin) * (gh - 1) / (vmax - vmin));
        int y1 = gy + gh - 1 - (int)((v[i] - vmin) * (gh - 1) / (vmax - vmin));
        SDL_RenderDrawLine(game.renderer, x0, y0, x1, y1);
    }
}

static void stats_label(int x, int y, const char *text, SDL_Color col)
{
    int tw, th;
    SDL_Texture *t = render_text(text, col, &tw, &th);
    if (!t) return;
    SDL_Rect dst = { x, y, tw, th };
    SDL_RenderCopy(game.renderer, t, NULL, &dst);
    SDL_DestroyTexture(t);
}

#define STATS_W 470
#define STATS_H 330

static void stats_window_origin(int *x, int *y)
{
    if (game.stats_x >= 0) { *x = game.stats_x; *y = game.stats_y; return; }
    *x = game.screen_w - STATS_W - 12;
    *y = 64;
}

/* Per-day rollup — the four quarters of a day aggregated into daily totals,
 * shown at the bottom of the Analytics window (works even before the
 * per-quarter graphs have enough samples). */
static void render_daily_rollup(int gx, int gy)
{
    char line[96];
    long today_net = game.sim.day_income - game.sim.day_expenses;
    snprintf(line, sizeof(line), "Today (Q%d/4):  in $%ld  out $%ld  net %s$%ld",
             game.sim.quarter + 1, (long)game.sim.day_income, (long)game.sim.day_expenses,
             today_net < 0 ? "-" : "+", labs(today_net));
    stats_label(gx, gy, line, (SDL_Color){ 0, 0, 0, 255 });
    gy += 16;
    if (game.sim.last_day_num > 0) {
        long y_net = game.sim.last_day_income - game.sim.last_day_expenses;
        snprintf(line, sizeof(line), "Day %d total:  in $%ld  out $%ld  net %s$%ld",
                 game.sim.last_day_num, (long)game.sim.last_day_income,
                 (long)game.sim.last_day_expenses, y_net < 0 ? "-" : "+", labs(y_net));
        stats_label(gx, gy, line, (SDL_Color){ 60, 60, 60, 255 });
        gy += 16;
    }
    char foot[64];
    snprintf(foot, sizeof(foot), "%d quarters logged (F3 to close)", game.sim.stats.count);
    stats_label(gx, gy, foot, (SDL_Color){ 80, 80, 80, 255 });
}

static void render_stats_window(void)
{
    if (!game.show_stats) return;
    const int W = STATS_W, H = STATS_H;
    int wx, wy;
    stats_window_origin(&wx, &wy);

    draw_win31_titlebar(wx, wy, W, "Analytics");
    SDL_Rect body = { wx, wy + WIN_TITLEBAR_H, W, H };
    SDL_SetRenderDrawColor(game.renderer, 192, 192, 192, 255);
    SDL_RenderFillRect(game.renderer, &body);
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(game.renderer, &body);

    StatsHistory *h = &game.sim.stats;
    int n = h->count > 120 ? 120 : h->count;
    if (n < 2) {
        stats_label(wx + 12, wy + WIN_TITLEBAR_H + 12,
                    "Collecting per-quarter graph data...",
                    (SDL_Color){ 60, 60, 60, 255 });
        render_daily_rollup(wx + 12, wy + WIN_TITLEBAR_H + 36);
        return;
    }
    int first = h->count - n;

    /* Gather the visible window of each series */
    static int64_t pop[120], com[120], inc[120], exp[120], blt[120], lst[120];
    for (int i = 0; i < n; i++) {
        const StatSample *s = stats_at(h, first + i);
        pop[i] = s->population;  com[i] = s->commuters;
        inc[i] = s->income;      exp[i] = s->expenses;
        blt[i] = s->built_value; lst[i] = s->lost_value;
    }
    const StatSample *last = stats_at(h, h->count - 1);

    struct {
        const char *title;
        const int64_t *a, *b;
        SDL_Color ca, cb;
        char cur[96];
    } graphs[3] = {
        { "Population / Commuters", pop, com,
          (SDL_Color){ 40, 80, 220, 255 }, (SDL_Color){ 0, 150, 160, 255 }, "" },
        { "Income / Expenses (per quarter)", inc, exp,
          (SDL_Color){ 0, 140, 0, 255 }, (SDL_Color){ 200, 30, 30, 255 }, "" },
        { "Value built / lost", blt, lst,
          (SDL_Color){ 90, 90, 90, 255 }, (SDL_Color){ 230, 130, 0, 255 }, "" },
    };
    snprintf(graphs[0].cur, sizeof(graphs[0].cur), "%d / %d",
             last->population, last->commuters);
    snprintf(graphs[1].cur, sizeof(graphs[1].cur), "$%ld / $%ld",
             (long)last->income, (long)last->expenses);
    snprintf(graphs[2].cur, sizeof(graphs[2].cur), "$%ld / $%ld",
             (long)last->built_value, (long)last->lost_value);

    int gx = wx + 10, gw = W - 20, gh = 64;
    int gy = wy + WIN_TITLEBAR_H + 8;
    for (int g = 0; g < 3; g++) {
        stats_label(gx, gy, graphs[g].title, (SDL_Color){ 0, 0, 0, 255 });
        stats_label(gx + gw - 150, gy, graphs[g].cur,
                    (SDL_Color){ 60, 60, 60, 255 });
        gy += 18;
        SDL_Rect box = { gx, gy, gw, gh };
        SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(game.renderer, &box);
        SDL_SetRenderDrawColor(game.renderer, 120, 120, 120, 255);
        SDL_RenderDrawRect(game.renderer, &box);

        int64_t lo = graphs[g].a[0], hi = lo;
        for (int i = 0; i < n; i++) {
            int64_t v1 = graphs[g].a[i], v2 = graphs[g].b[i];
            if (v1 < lo) lo = v1;
            if (v1 > hi) hi = v1;
            if (v2 < lo) lo = v2;
            if (v2 > hi) hi = v2;
        }
        if (hi == lo) hi = lo + 1;
        stats_plot(gx + 1, gy + 1, gw - 2, gh - 2, graphs[g].a, n, graphs[g].ca, lo, hi);
        stats_plot(gx + 1, gy + 1, gw - 2, gh - 2, graphs[g].b, n, graphs[g].cb, lo, hi);
        gy += gh + 8;
    }
    render_daily_rollup(gx, gy);
}

/* ---------- Tuning window (F4): the master values, live ----------
 * Every default is the EXE's own number (resource 0x7F05). Tweaks apply
 * to the running simulation immediately — this is the modding panel. */
typedef struct { const char *label; int *v; int step, min, max; } TuneRow;

static TuneRow tune_rows[] = {
    { "Wait cap (frustration)",   &TUNING.wait_cap,            10, 10, 2000 },
    { "Penalty: queue full",      &TUNING.penalty_queue_full,   1, 0, 1000 },
    { "Penalty: no route",        &TUNING.penalty_no_route,    10, 0, 1000 },
    { "Penalty: escalator/span",  &TUNING.penalty_esc_span,     1, 0, 300 },
    { "Penalty: stairs/span",     &TUNING.penalty_stair_span,   1, 0, 300 },
    { "Penalty: walk >= 80 cells",&TUNING.penalty_walk_80,      5, 0, 300 },
    { "Penalty: walk >= 125",     &TUNING.penalty_walk_125,     5, 0, 300 },
    { "Cost: stairs base",        &TUNING.cost_stair_base,     40, 0, 8000 },
    { "Cost: elevator base",      &TUNING.cost_elev_base,      40, 0, 8000 },
    { "Cost: elevator full",      &TUNING.cost_elev_full,      40, 0, 8000 },
    { "Cost: transfer",           &TUNING.cost_transfer,      100, 0, 16000 },
    { "Cost: transfer full",      &TUNING.cost_transfer_full, 100, 0, 16000 },
    { "Walk floors: escalator",   &TUNING.walk_floors_esc,      1, 0, 20 },
    { "Walk floors: with stairs", &TUNING.walk_floors_stair,    1, 0, 20 },
    { "Car capacity: express",    &TUNING.capacity_express,     1, 1, 42 },
    { "Car capacity: std/svc",    &TUNING.capacity_standard,    1, 1, 42 },
    { "Judge: moderate wait",     &TUNING.judge_moderate,       5, 0, 2000 },
    { "Judge: stressed wait",     &TUNING.judge_stressed,       5, 0, 2000 },
    { "Star 2 population",        &TUNING.star_pop[0],         50, 0, 100000 },
    { "Star 3 population",        &TUNING.star_pop[1],        100, 0, 100000 },
    { "Star 4 population",        &TUNING.star_pop[2],        500, 0, 100000 },
    { "Star 5 population",        &TUNING.star_pop[3],        500, 0, 100000 },
    { "Car add $: standard",      &TUNING.car_cost_std,      5000, 0, 1000000 },
    { "Car add $: express",       &TUNING.car_cost_express,  5000, 0, 1000000 },
    { "Car add $: service",       &TUNING.car_cost_service,  5000, 0, 1000000 },
    { "Upkeep $/car: standard",   &TUNING.maint_car_std,     1000, 0, 100000 },
    { "Upkeep $/car: express",    &TUNING.maint_car_express, 1000, 0, 100000 },
    { "Upkeep $/car: service",    &TUNING.maint_car_service, 1000, 0, 100000 },
    { "Upkeep $: escalator",      &TUNING.maint_escalator,   1000, 0, 100000 },
};
#define TUNE_ROWS ((int)(sizeof(tune_rows) / sizeof(tune_rows[0])))
#define TUNE_W      330
#define TUNE_ROW_H  19

static void tune_window_origin(int *x, int *y)
{
    if (game.tune_x >= 0) { *x = game.tune_x; *y = game.tune_y; return; }
    *x = game.screen_w / 2 - TUNE_W / 2;
    *y = 56;
}

/* Returns 1 if the click landed on the window (consumed) */
static int tuning_click(int mx, int my)
{
    if (!game.show_tuning) return 0;
    int wx, wy;
    tune_window_origin(&wx, &wy);
    int body_h = 26 + TUNE_ROWS * TUNE_ROW_H + 30;
    if (mx < wx || mx >= wx + TUNE_W ||
        my < wy || my >= wy + WIN_TITLEBAR_H + body_h) return 0;

    int top = wy + WIN_TITLEBAR_H + 22;
    int row = (my - top) / TUNE_ROW_H;
    if (my >= top && row >= 0 && row < TUNE_ROWS) {
        TuneRow *r = &tune_rows[row];
        if (mx >= wx + TUNE_W - 52 && mx < wx + TUNE_W - 34) {
            *r->v -= r->step;
            if (*r->v < r->min) *r->v = r->min;
        } else if (mx >= wx + TUNE_W - 26 && mx < wx + TUNE_W - 8) {
            *r->v += r->step;
            if (*r->v > r->max) *r->v = r->max;
        }
        return 1;
    }
    /* Reset button strip at the bottom */
    int ry = top + TUNE_ROWS * TUNE_ROW_H + 4;
    if (my >= ry && my < ry + 20 && mx >= wx + 8 && mx < wx + 150) {
        tuning_reset();
        return 1;
    }
    return 1;   /* clicks inside the window never fall through */
}

static void render_tuning_window(void)
{
    if (!game.show_tuning) return;
    int wx, wy;
    tune_window_origin(&wx, &wy);
    int body_h = 26 + TUNE_ROWS * TUNE_ROW_H + 30;

    draw_win31_titlebar(wx, wy, TUNE_W, "Tuning (EXE master values)");
    SDL_Rect body = { wx, wy + WIN_TITLEBAR_H, TUNE_W, body_h };
    SDL_SetRenderDrawColor(game.renderer, 192, 192, 192, 255);
    SDL_RenderFillRect(game.renderer, &body);
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(game.renderer, &body);

    stats_label(wx + 8, wy + WIN_TITLEBAR_H + 4,
                "Live values - defaults from SIMTOWER.EXE",
                (SDL_Color){ 70, 70, 70, 255 });

    int top = wy + WIN_TITLEBAR_H + 22;
    for (int i = 0; i < TUNE_ROWS; i++) {
        int ry = top + i * TUNE_ROW_H;
        stats_label(wx + 8, ry + 1, tune_rows[i].label,
                    (SDL_Color){ 0, 0, 0, 255 });
        char val[16];
        snprintf(val, sizeof(val), "%d", *tune_rows[i].v);
        stats_label(wx + TUNE_W - 110, ry + 1, val,
                    (SDL_Color){ 20, 20, 120, 255 });
        /* - and + buttons */
        SDL_Rect minus = { wx + TUNE_W - 52, ry + 1, 18, TUNE_ROW_H - 3 };
        SDL_Rect plus  = { wx + TUNE_W - 26, ry + 1, 18, TUNE_ROW_H - 3 };
        SDL_SetRenderDrawColor(game.renderer, 168, 168, 168, 255);
        SDL_RenderFillRect(game.renderer, &minus);
        SDL_RenderFillRect(game.renderer, &plus);
        SDL_SetRenderDrawColor(game.renderer, 60, 60, 60, 255);
        SDL_RenderDrawRect(game.renderer, &minus);
        SDL_RenderDrawRect(game.renderer, &plus);
        SDL_RenderDrawLine(game.renderer, minus.x + 5, minus.y + minus.h / 2,
                           minus.x + minus.w - 6, minus.y + minus.h / 2);
        SDL_RenderDrawLine(game.renderer, plus.x + 5, plus.y + plus.h / 2,
                           plus.x + plus.w - 6, plus.y + plus.h / 2);
        SDL_RenderDrawLine(game.renderer, plus.x + plus.w / 2, plus.y + 3,
                           plus.x + plus.w / 2, plus.y + plus.h - 4);
    }
    int ry = top + TUNE_ROWS * TUNE_ROW_H + 4;
    SDL_Rect reset = { wx + 8, ry, 142, 20 };
    SDL_SetRenderDrawColor(game.renderer, 168, 168, 168, 255);
    SDL_RenderFillRect(game.renderer, &reset);
    SDL_SetRenderDrawColor(game.renderer, 60, 60, 60, 255);
    SDL_RenderDrawRect(game.renderer, &reset);
    stats_label(reset.x + 10, reset.y + 2, "Reset to EXE values",
                (SDL_Color){ 0, 0, 0, 255 });
}

/* ---------- Elevator dialog (ElvDlogT, seg_1098) ----------
 * Double-click a shaft: the original's schedule + tuning + simulate panel,
 * rebuilt on its own artwork (EXE bitmap 0x8190) with the live data overlaid.
 * Interaction model verified against the decomp (2026-07-14 referee):
 *  - WD/WE day tabs; 6 editable time periods (night is hidden) each holding a
 *    3-value mode picked from ELVPOPUP (+0x20 matrix)
 *  - two per-(day,period) spinners: Waiting Car Response (threshold +0x12,
 *    1..100) and Standard Floor Departure (patience +0x2E, 0..3)
 *  - a live 9-col x 15-row shaft grid: col -1 = shared floor service (+0x42),
 *    cols 0..7 = each car's home floor (+0xBA); row 0 is the bottom floor
 *  - SHOW toggles car visibility (+0x3C); Simulate = the full-screen edit mode
 *    (seg_10f0, see elv_edit_* below); OK commits and closes. Cars are added
 *    by the build tool, not here. */
#define SPR_ELV_DIALOG 0x8190
#define ELV_DLG_W      200
#define ELV_DLG_H      428
#define ELV_W          ELV_DLG_W    /* window width for drag/hit bounds */
#define ELV_PERIODS    6         /* periods 0..5; night (6) is hidden by the EXE */
/* measured hit-rects, dialog-local px (x,y,w,h) */
#define ELV_WD_TAB     29,  4, 70, 14
#define ELV_WE_TAB    102,  4, 67, 14
#define ELV_PCELL_X0   29        /* first schedule cell x */
#define ELV_PCELL_Y    43
#define ELV_PCELL_W    22
#define ELV_PCELL_H    20
#define ELV_PCELL_PITCH 24
#define ELV_RESP_FLD   76, 93, 19, 22   /* Waiting Car Response value field */
#define ELV_WAIT_FLD   76,148, 19, 22   /* Standard Floor Departure value field */
#define ELV_GRID       18,195,132,195   /* live shaft/car grid */
#define ELV_SCROLLBAR 152,195,  6,195   /* grid scrollbar (>15-floor shafts) */
#define ELV_SHOW       161,206, 11, 20  /* SHOW On/Off */
#define ELV_GRID_ROWS  15                /* visible floor rows in the grid */

/* Max scroll offset for a shaft's edit grid: 0 when it fits in 15 rows,
 * else (floors - 15) so the top floor can reach the top row. */
static int elv_max_scroll(const ElevatorShaft *s)
{
    int total = s->hi - s->lo + 1;
    return total > ELV_GRID_ROWS ? total - ELV_GRID_ROWS : 0;
}
#define ELV_SIM_BTN     10,398, 80, 22  /* Simulate / Resume */
#define ELV_OK_BTN     115,398, 80, 22  /* OK */
/* Cost of an extra car, per type (decomp-verified, globals.md #54):
 * the EXE charges the tuning-resource values at +0x90 — standard $80k,
 * express $150k, service $50k. OpenSkyscraper's flat $80k was right for
 * standard cars only. Removing a car refunds nothing (also verified). */
static int elv_car_cost(ItemType t)
{
    if (t == ITEM_ELEVATOR_EXPRESS) return TUNING.car_cost_express;
    if (t == ITEM_ELEVATOR_SERVICE) return TUNING.car_cost_service;
    return TUNING.car_cost_std;
}

/* The dialog tracks the shaft by column+type, not index: a layout rebuild
 * reorders the shaft array, and the shaft itself may be bulldozed. */
static int elv_dialog_shaft(void)
{
    if (!game.elv_open) return -1;
    PeopleSim *ps = &game.sim.people;
    for (int i = 0; i < ps->shaft_count; i++)
        if (ps->shafts[i].active && ps->shafts[i].x == game.elv_sx &&
            ps->shafts[i].type == (ItemType)game.elv_stype)
            return i;
    return -1;
}

/* Express shafts structurally skip non-lobby floors regardless of flags */
static int elv_structural_stop(const ElevatorShaft *s, int fidx)
{
    if (s->type != ITEM_ELEVATOR_EXPRESS) return 1;
    int wf = index_to_floor(fidx);
    return wf <= 0 || (wf % 15) == 0;
}


/* ---- Elevator "Simulate" full-screen edit mode (seg_10f0 "ElvEditT") ----
 * The EXE's Simulate button opens a separate surface: the whole tower dims to
 * a flat silhouette and only the one selected shaft (cars + queues) stays lit,
 * so its stops can be edited against the real tower at full scale. The EXE
 * suppresses the normal draw path (flag 0xB3AE) and rewinds the sim clock so
 * real time doesn't pass while you edit — we mirror that by pausing the sim on
 * enter and restoring the prior speed on exit; schedule/stop edits survive.
 * (The EXE's pre-simulation that settles the cars to a representative steady
 * state is a HYPOTHESIS-confidence cosmetic in the decomp — we simply show the
 * cars frozen where they are, which is honest and needs no invented motion.) */
static void elv_edit_enter(void)
{
    if (game.elv_edit_mode || elv_dialog_shaft() < 0) return;
    game.elv_edit_mode = 1;
    game.elv_saved_speed =
        (game.sim.speed == SPEED_PAUSED) ? SPEED_NORMAL : game.sim.speed;
    game.sim.speed = SPEED_PAUSED;
}

static void elv_edit_exit(void)
{
    if (!game.elv_edit_mode) return;
    game.elv_edit_mode = 0;
    game.sim.speed = game.elv_saved_speed;
}

/* Toggle the selected shaft's stop at the tower cell under a screen point,
 * while in edit mode. This is the edit surface's whole interaction: clicking
 * a floor of the isolated shaft adds/removes its stop there. Returns 1 if a
 * stop was toggled. */
static int elv_edit_toggle_at(int mx, int my)
{
    if (!game.elv_edit_mode) return 0;
    int si = elv_dialog_shaft();
    if (si < 0) return 0;
    ElevatorShaft *s = &game.sim.people.shafts[si];
    int fl, cell;
    screen_to_grid(mx, my, &fl, &cell);
    int fidx = floor_to_index(fl);
    int w = ITEM_WIDTH[s->type];
    if (fidx >= s->lo && fidx <= s->hi &&
        cell >= s->x && cell < s->x + w &&
        elv_structural_stop(s, fidx)) {
        request_stop_toggle(si, fidx);
        return 1;
    }
    return 0;
}

/* Draw a small filled/outlined rect helper (dialog-local -> screen). */
static void elv_box(int ox, int oy, int x, int y, int w, int h,
                    int r, int g, int b, int fill)
{
    SDL_Rect rc = { ox + x, oy + y, w, h };
    SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
    if (fill) SDL_RenderFillRect(game.renderer, &rc);
    else      SDL_RenderDrawRect(game.renderer, &rc);
}

/* One of the three schedule modes (0/1/2). The REAL icons are ordinary
 * RT_BITMAP resources 0x8192/0x8193/0x8194 (24x21) — the earlier "boot-
 * loaded GDI blob we can't extract" note was wrong; the EXE composes them
 * into a strip at boot (seg_1098:0068) which is what hid them. Semantics
 * per the art: mode 0 = Full service (gray zigzag), 1 = Express Up (red
 * up-arrow), 2 = Express Down (red down-arrow). (Referee 2026-07-30.) */
static void elv_mode_glyph(int cx, int cy, int mode)
{
    Sprite *icon = sprites_find(&game.sprites,
                                (uint16_t)(0x8192 + (mode % 3)));
    if (icon) {
        SDL_Rect dst = { cx - icon->w / 2, cy - icon->h / 2,
                         icon->w, icon->h };
        SDL_RenderCopy(game.renderer, icon->texture, NULL, &dst);
        return;
    }
    /* fallback pictograph if the bitmap is missing */
    SDL_SetRenderDrawColor(game.renderer, 20, 20, 20, 255);
    if (mode == 1) {
        SDL_RenderDrawLine(game.renderer, cx, cy - 6, cx, cy + 6);
        for (int i = 0; i < 3; i++)
            SDL_RenderDrawLine(game.renderer, cx - i, cy - 6 + i, cx + i, cy - 6 + i);
    } else if (mode == 2) {
        SDL_Rect r = { cx - 4, cy - 4, 8, 8 };
        SDL_RenderFillRect(game.renderer, &r);
    } else {
        for (int i = 0; i < 4; i++) {
            SDL_RenderDrawLine(game.renderer, cx - i, cy - 2 - i, cx + i, cy - 2 - i);
            SDL_RenderDrawLine(game.renderer, cx - i, cy + 2 + i, cx + i, cy + 2 + i);
        }
    }
}

/* Faithful elevator dialog: the original 0x8190 artwork with live data
 * overlaid. Interaction model grounded in the decomp (seg_1098 ElvDlogT). */
/* Person-inspection popup opener (defined with the person popup code). */
static void open_person_popup_at(uint16_t pid, int x, int y);
/* Clickable silhouette strip shared by the occupant/rider lists. */
static void draw_person_strip(const uint16_t *pids, int n, int x, int y);
static uint16_t person_strip_hit(const uint16_t *pids, int n, int x, int y,
                                 int mx, int my);
#define PSTRIP_PITCH 18
#define PSTRIP_H     36

/* Riders strip under the elevator dialog: everyone aboard this shaft's
 * cars, as clickable silhouettes (the original's passenger list —
 * people-list family 1100:327f clicks through to PepleInfoDialog). */
#define ELV_RIDERS_H 46
static int elv_riders_collect(const ElevatorShaft *s, uint16_t *out, int max)
{
    int n = 0;
    for (int c = 0; c < CARS_PER_SHAFT && n < max; c++) {
        const ElevatorCar *car = &s->car[c];
        if (!car->active) continue;
        for (int k = 0; k < CAR_SLOTS && n < max; k++)
            if (car->pax[k]) out[n++] = car->pax[k];
    }
    return n;
}

static void render_elv_dialog_faithful(void)
{
    int si = elv_dialog_shaft();
    if (si < 0) { game.elv_open = 0; return; }
    PeopleSim *ps = &game.sim.people;
    ElevatorShaft *s = &ps->shafts[si];
    int wx = game.elv_x, wy = game.elv_y;
    int bx = wx, by = wy + WIN_TITLEBAR_H;   /* bitmap origin (client area) */

    /* Transport names from EXE string table 0x190 (0 Express, 1 Standard,
     * 2 Service). */
    const char *title =
        s->type == ITEM_ELEVATOR_EXPRESS
            ? exe_str(0x0190, 0, "Express Elevator")
      : s->type == ITEM_ELEVATOR_SERVICE
            ? exe_str(0x0190, 2, "Service Elevator")
            : exe_str(0x0190, 1, "Standard Elevator");
    draw_win31_titlebar(wx, wy, ELV_DLG_W, title);

    Sprite *bg = sprites_find(&game.sprites, SPR_ELV_DIALOG);
    if (bg) {
        SDL_Rect dst = { bx, by, ELV_DLG_W, ELV_DLG_H };
        SDL_RenderCopy(game.renderer, bg->texture, NULL, &dst);
    } else {
        elv_box(bx, by, 0, 0, ELV_DLG_W, ELV_DLG_H, 192, 192, 192, 1);
    }

    SDL_Color ink = { 0, 0, 0, 255 };

    /* WD/WE active-tab highlight (day type = game.elv_day). The art already
     * draws WD in black / WE in blue; we ring the active one. */
    {
        int tw, th, tx, ty;
        if (game.elv_day == 0) { tx = 29; ty = 4; tw = 70; th = 14; }
        else                   { tx = 102; ty = 4; tw = 67; th = 14; }
        SDL_Rect r = { bx + tx - 1, by + ty - 1, tw + 2, th + 2 };
        SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(game.renderer, &r);
    }

    /* Schedule: 6 period cells (period 6/night is hidden — the EXE clamps it,
     * seg_1098:078c). Each shows its mode glyph; the selected period is ringed. */
    for (int p = 0; p < ELV_PERIODS; p++) {
        int px = bx + ELV_PCELL_X0 + p * ELV_PCELL_PITCH;
        int py = by + ELV_PCELL_Y;
        elv_mode_glyph(px + ELV_PCELL_W / 2, py + ELV_PCELL_H / 2,
                       s->sched_mode[game.elv_day][p]);
        if (p == game.elv_period) {
            SDL_Rect r = { px - 1, py - 1, ELV_PCELL_W + 1, ELV_PCELL_H + 1 };
            SDL_SetRenderDrawColor(game.renderer, 200, 0, 0, 255);
            SDL_RenderDrawRect(game.renderer, &r);
        }
    }

    /* The two tuning spinner values for the selected (day, period). */
    {
        int d = game.elv_day, p = game.elv_period;
        char num[8];
        snprintf(num, sizeof(num), "%d", s->sched_threshold[d][p]);
        stats_label(bx + 76 + 3, by + 93 + 4, num, ink);
        snprintf(num, sizeof(num), "%d", s->sched_patience[d][p]);
        stats_label(bx + 76 + 3, by + 148 + 4, num, ink);
    }

    /* Live shaft grid (widget 6). Decomp geometry (seg_1098:1644): 9 columns —
     * col -1 = shared floor-service toggle at local x=0, cols 0..7 = the 8 car
     * slots at x=13..104 — and 15 rows of 13px with ROW 0 AT THE BOTTOM
     * (y = (14-row)*13). We show floors from a scroll base; a car draws in its
     * column at its live floor (+0x298a), and a car's home floor (+0xBA) marks
     * its column. SHOW gates the live-car overlay. */
    {
        int gx0 = bx + 18, gy0 = by + 195;
        int total = s->hi - s->lo + 1;
        int maxsc = elv_max_scroll(s);
        if (game.elv_scroll > maxsc) game.elv_scroll = maxsc;
        if (game.elv_scroll < 0) game.elv_scroll = 0;
        int base = s->lo + game.elv_scroll;      /* bottom-most visible floor idx */
        for (int r = 0; r < 15; r++) {
            int f = base + r;
            if (f > s->hi) break;
            int cy = gy0 + (14 - r) * 13;

            /* service column (col -1, local x=0) */
            SDL_Rect sc = { gx0, cy, 12, 12 };
            if (!elv_structural_stop(s, f))
                SDL_SetRenderDrawColor(game.renderer, 150, 150, 150, 255);
            else if (s->serviced[f])
                SDL_SetRenderDrawColor(game.renderer, 40, 150, 40, 255);
            else
                SDL_SetRenderDrawColor(game.renderer, 120, 50, 50, 255);
            SDL_RenderFillRect(game.renderer, &sc);
            SDL_SetRenderDrawColor(game.renderer, 90, 90, 90, 255);
            SDL_RenderDrawRect(game.renderer, &sc);

            /* car columns 0..7 */
            for (int c = 0; c < CARS_PER_SHAFT; c++) {
                int cx = gx0 + (c + 1) * 13;
                SDL_Rect cc = { cx, cy, 12, 12 };
                if (c >= s->num_cars) {           /* inactive slot */
                    SDL_SetRenderDrawColor(game.renderer, 205, 205, 205, 255);
                    SDL_RenderFillRect(game.renderer, &cc);
                    continue;
                }
                SDL_SetRenderDrawColor(game.renderer, 235, 235, 235, 255);
                SDL_RenderFillRect(game.renderer, &cc);
                SDL_SetRenderDrawColor(game.renderer, 150, 150, 150, 255);
                SDL_RenderDrawRect(game.renderer, &cc);
                /* home-floor marker (red diamond) */
                if (s->home[c] == f) {
                    SDL_SetRenderDrawColor(game.renderer, 200, 30, 30, 255);
                    int mx0 = cc.x + 6, my0 = cc.y + 6;
                    for (int d = -3; d <= 3; d++) {
                        int half = 3 - (d < 0 ? -d : d);
                        SDL_RenderDrawLine(game.renderer, mx0 - half, my0 + d,
                                           mx0 + half, my0 + d);
                    }
                }
                /* live car at this floor */
                if (!s->hidden && s->car[c].active && s->car[c].floor == f) {
                    ElevatorCar *car = &s->car[c];
                    if (car->passengers >= s->capacity)
                        SDL_SetRenderDrawColor(game.renderer, 200, 30, 30, 255);
                    else if (car->passengers > 0)
                        SDL_SetRenderDrawColor(game.renderer, 40, 80, 220, 255);
                    else
                        SDL_SetRenderDrawColor(game.renderer, 70, 70, 70, 255);
                    SDL_Rect cr = { cc.x + 2, cc.y + 2, 8, 8 };
                    SDL_RenderFillRect(game.renderer, &cr);
                }
            }
        }
        /* Scrollbar for shafts taller than the 15-row window: thumb sits at the
         * bottom when the base floor is shown (scroll 0) and rises toward the top
         * as you scroll up. */
        if (maxsc > 0) {
            int tx = bx + 152, ty = by + 195, tw = 6, th = 195;
            SDL_SetRenderDrawColor(game.renderer, 205, 205, 205, 255);
            SDL_Rect track = { tx, ty, tw, th };
            SDL_RenderFillRect(game.renderer, &track);
            SDL_SetRenderDrawColor(game.renderer, 120, 120, 120, 255);
            SDL_RenderDrawRect(game.renderer, &track);
            int thumb_h = th * ELV_GRID_ROWS / total;
            if (thumb_h < 12) thumb_h = 12;
            int thumb_y = ty + (th - thumb_h) * (maxsc - game.elv_scroll) / maxsc;
            SDL_SetRenderDrawColor(game.renderer, 90, 90, 90, 255);
            SDL_Rect thumb = { tx + 1, thumb_y, tw - 2, thumb_h };
            SDL_RenderFillRect(game.renderer, &thumb);
        }
    }

    /* SHOW On/Off marker (top half = On). */
    {
        int sx = bx + 161, sy = by + 206;
        SDL_SetRenderDrawColor(game.renderer, 20, 20, 20, 255);
        SDL_Rect m = { sx + 2, sy + (!s->hidden ? 1 : 11), 7, 7 };
        SDL_RenderFillRect(game.renderer, &m);
    }

    /* Simulate button reflects edit-mode state: the art reads "Simulate"; while
     * editing we overpaint it "Resume" and ring it so the toggle is legible. */
    if (game.elv_edit_mode) {
        int sxb = bx + 10, syb = by + 398;
        SDL_SetRenderDrawColor(game.renderer, 30, 60, 120, 255);
        SDL_Rect r = { sxb, syb, 80, 22 };
        SDL_RenderFillRect(game.renderer, &r);
        SDL_SetRenderDrawColor(game.renderer, 230, 230, 255, 255);
        SDL_RenderDrawRect(game.renderer, &r);
        SDL_Color w = { 235, 235, 255, 255 };
        stats_label(sxb + 20, syb + 5, "Resume", w);
    }

    /* Calibration overlay: outline every measured hit-rect so I can verify
     * alignment against the original art before wiring behavior. */
    if (getenv("ELV_CAL")) {
        elv_box(bx, by, ELV_WD_TAB, 255, 0, 0, 0);
        elv_box(bx, by, ELV_WE_TAB, 255, 0, 0, 0);
        for (int p = 0; p < ELV_PERIODS; p++)
            elv_box(bx, by, ELV_PCELL_X0 + p * ELV_PCELL_PITCH, ELV_PCELL_Y,
                    ELV_PCELL_W, ELV_PCELL_H, 255, 0, 0, 0);
        elv_box(bx, by, ELV_RESP_FLD, 0, 200, 0, 0);
        elv_box(bx, by, ELV_WAIT_FLD, 0, 200, 0, 0);
        elv_box(bx, by, ELV_GRID, 0, 120, 255, 0);
        elv_box(bx, by, ELV_SHOW, 255, 160, 0, 0);
        elv_box(bx, by, ELV_SIM_BTN, 255, 0, 255, 0);
        elv_box(bx, by, ELV_OK_BTN, 255, 0, 255, 0);
    }

    /* Riders strip — a panel below the EXE bitmap listing everyone
     * aboard this shaft's cars; click a silhouette to inspect them. */
    {
        uint16_t pids[10];
        int n = elv_riders_collect(s, pids, 10);
        elv_box(bx, by, 0, ELV_DLG_H, ELV_DLG_W, ELV_RIDERS_H,
                192, 192, 192, 1);
        if (n)
            draw_person_strip(pids, n, bx + 8, by + ELV_DLG_H + 5);
        else
            stats_label(bx + 8, by + ELV_DLG_H + 15, "No riders", ink);
    }
    (void)ps;
}

static void render_elv_dialog(void)
{
    render_elv_dialog_faithful();
}

/* =====================================================================
 * Financial report (CountT, seg_1060) — faithful rebuild on the EXE's own
 * bitmap 0x81f4. All labels are baked into the art; we overlay live numbers
 * at measured rects. Two category lists: a REVENUE list (population + income)
 * and an INFRASTRUCTURE list (maintenance expense), plus the summary block.
 * ===================================================================== */
#define SPR_FIN_DIALOG    0x81f4
#define SPR_FIN_DIALOG_OK 0x81f5   /* same art, OK button pressed */
#define FIN_DLG_W 343
#define FIN_DLG_H 364
#define FIN_OK_BTN 130, 325, 94, 23   /* OK-button rect re-measured off the
                                       * art's outline (was 11px low — the
                                       * top half of the button was dead) */

/* Right-aligned text, for the numeric ledger columns. */
static void draw_text_right(const char *text, int right_x, int y, SDL_Color c)
{
    int w, h;
    SDL_Texture *tex = render_text(text, c, &w, &h);
    if (!tex) return;
    SDL_Rect dst = { right_x - w, y, w, h };
    SDL_RenderCopy(game.renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
}

/* Small variant for the 13px-pitch ledger grid — the 14px UI font overflowed
 * the rows and looked nothing like the art's compact ledger digits. */
static void draw_text_right_small(const char *text, int right_x, int y,
                                  SDL_Color c)
{
    if (!game.font_small || !text || !text[0]) return;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(game.font_small, text, c);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(game.renderer, surf);
    SDL_Rect dst = { right_x - surf->w, y, surf->w, surf->h };
    SDL_RenderCopy(game.renderer, tex, NULL, &dst);
    SDL_FreeSurface(surf);
    SDL_DestroyTexture(tex);
}

/* y of ledger row `row` (0..8), dialog-local. The art's label rows sit at
 * exactly 83 + 13*row (pixel-measured off 0x81f4); -2 centers the 11px
 * small font on the 7px label band. */
static int fin_row_y(int row) { return 81 + row * 13; }

/* Compact money for the narrow per-category columns ($3.52M / $130k / $90).
 * The original's columns were sized for its ÷100 internal values (a documented
 * display quirk); we show real dollars, so large amounts need abbreviating to
 * fit. The wide Total/summary boxes keep full comma-grouped dollars. */
static void fin_compact_money(long v, char *buf, int n)
{
    long a = v < 0 ? -v : v;
    const char *s = v < 0 ? "-" : "";
    if (a >= 1000000)   snprintf(buf, n, "%s$%ld.%02ldM", s, a / 1000000, (a % 1000000) / 10000);
    else if (a >= 1000) snprintf(buf, n, "%s$%ldk", s, a / 1000);
    else                snprintf(buf, n, "%s$%ld", s, a);
}

static void render_fin_dialog(void)
{
    if (!game.fin_open) return;
    GameSim *sim = &game.sim;
    Tower *tw = &game.tower;
    int wx = game.fin_x, wy = game.fin_y;
    int bx = wx, by = wy + WIN_TITLEBAR_H;   /* client-area origin */

    draw_win31_titlebar(wx, wy, FIN_DLG_W, "Financial Statement");

    Sprite *bg = sprites_find(&game.sprites, SPR_FIN_DIALOG);
    if (bg) {
        SDL_Rect dst = { bx, by, FIN_DLG_W, FIN_DLG_H };
        SDL_RenderCopy(game.renderer, bg->texture, NULL, &dst);
    } else {
        SDL_SetRenderDrawColor(game.renderer, 192, 192, 192, 255);
        SDL_Rect r = { bx, by, FIN_DLG_W, FIN_DLG_H };
        SDL_RenderFillRect(game.renderer, &r);
    }

    SDL_Color ink = { 0, 0, 0, 255 };

    /* ---- Gather per-category data (population on demand; income/expense from
     * the quarterly ledgers). ---- */
    long pop[FIN_INCOME_CATS];
    for (int i = 0; i < FIN_INCOME_CATS; i++) pop[i] = 0;
    for (int i = 0; i < tw->tenant_count; i++) {
        Tenant *t = &tw->tenants[i];
        if (t->type == ITEM_NONE) continue;
        int c = game_fin_income_cat(t->type);
        if (c >= 0) pop[c] += t->population;
    }

    long total_income = 0, total_maint = 0;
    for (int i = 0; i < FIN_INCOME_CATS; i++)  total_income += sim->fin_income_q[i];
    for (int i = 0; i < FIN_EXPENSE_CATS; i++) total_maint  += sim->fin_expense_q[i];
    long net          = total_income - total_maint;
    long other        = sim->fin_other_income_q;
    long construction = (tw->built_value - sim->fin_built_at_q_start)
                        + sim->fin_construction_q;
    long last_bal     = sim->fin_last_balance;
    long total_bal    = tw->money;
    int  year         = (int)(tw->day / 12) + 1;
    int  quarter      = (int)((tw->day / 3) % 4) + 1;

    char buf[32];

    /* ---- Header: Year / Quarter / Total Income / Total Maintenance ---- */
    snprintf(buf, sizeof buf, "%d", year);
    draw_text_right(buf, bx + 183, by + 12, ink);
    snprintf(buf, sizeof buf, "%d", quarter);
    draw_text_right(buf, bx + 250, by + 12, ink);
    format_money(total_income, buf, sizeof buf);
    draw_text_right(buf, bx + 168, by + 46, ink);
    format_money(total_maint, buf, sizeof buf);
    draw_text_right(buf, bx + 322, by + 46, ink);

    /* ---- Left list: population + income per revenue category. Rows with no
     * income and no population are left blank ("Items with no income or
     * expenses are not displayed"). ---- */
    for (int r = 0; r < FIN_INCOME_CATS; r++) {
        int y = by + fin_row_y(r);
        if (pop[r] > 0) {
            snprintf(buf, sizeof buf, "%ld", pop[r]);
            draw_text_right_small(buf, bx + 130, y, ink);
        }
        if (sim->fin_income_q[r] != 0) {
            fin_compact_money(sim->fin_income_q[r], buf, sizeof buf);
            draw_text_right_small(buf, bx + 184, y, ink);
        }
    }

    /* ---- Right list: maintenance expense per infrastructure category. ---- */
    for (int r = 0; r < FIN_EXPENSE_CATS; r++) {
        if (sim->fin_expense_q[r] == 0) continue;
        int y = by + fin_row_y(r);
        fin_compact_money(sim->fin_expense_q[r], buf, sizeof buf);
        draw_text_right_small(buf, bx + 324, y, ink);
    }

    /* ---- Summary block (right-aligned values on each labelled line). ---- */
    struct { long v; int y; } sums[] = {
        { net,          by + 236 },
        { other,        by + 253 },
        { construction, by + 270 },
        { last_bal,     by + 287 },
        { total_bal,    by + 307 },
    };
    for (int i = 0; i < 5; i++) {
        format_money(sums[i].v, buf, sizeof buf);
        draw_text_right(buf, bx + 326, sums[i].y, ink);
    }

    /* Calibration overlay: outline the measured anchors to align vs the art. */
    if (getenv("FIN_CAL")) {
        SDL_SetRenderDrawColor(game.renderer, 255, 0, 0, 255);
        for (int r = 0; r < FIN_INCOME_CATS; r++) {
            SDL_Rect a = { bx + 110, by + fin_row_y(r), 40, 11 };
            SDL_Rect b = { bx + 170, by + fin_row_y(r), 40, 11 };
            SDL_Rect c = { bx + 284, by + fin_row_y(r), 40, 11 };
            SDL_RenderDrawRect(game.renderer, &a);
            SDL_RenderDrawRect(game.renderer, &b);
            SDL_RenderDrawRect(game.renderer, &c);
        }
        SDL_SetRenderDrawColor(game.renderer, 0, 120, 255, 255);
        SDL_Rect ok = { bx + 130, by + 325, 94, 23 };
        SDL_RenderDrawRect(game.renderer, &ok);
    }
    (void)ink;
}

static int pt_in(int mx, int my, int ox, int oy, int x, int y, int w, int h)
{
    return mx >= ox + x && mx < ox + x + w && my >= oy + y && my < oy + y + h;
}

/* Faithful dialog click handling. Coordinates are dialog-local (bx,by). Only
 * the CONFIRMED controls are wired here; grid/simulate follow the referee. */
static int elv_dialog_click_faithful(int mx, int my)
{
    int si = elv_dialog_shaft();
    if (si < 0) return 0;
    PeopleSim *ps = &game.sim.people;
    ElevatorShaft *s = &ps->shafts[si];
    int bx = game.elv_x, by = game.elv_y + WIN_TITLEBAR_H;

    if (mx < game.elv_x || mx >= game.elv_x + ELV_DLG_W ||
        my < game.elv_y || my >= by + ELV_DLG_H + ELV_RIDERS_H) return 0;

    /* Riders strip below the bitmap: click a silhouette to inspect. */
    if (my >= by + ELV_DLG_H) {
        uint16_t pids[10];
        int n = elv_riders_collect(s, pids, 10);
        uint16_t pid = person_strip_hit(pids, n, bx + 8, by + ELV_DLG_H + 5,
                                        mx, my);
        if (pid) open_person_popup_at(pid, mx, my);
        return 1;
    }

    /* WD / WE day-type tabs */
    if (pt_in(mx, my, bx, by, ELV_WD_TAB)) { game.elv_day = 0; return 1; }
    if (pt_in(mx, my, bx, by, ELV_WE_TAB)) { game.elv_day = 1; return 1; }

    /* schedule period cells (0..5) */
    for (int p = 0; p < ELV_PERIODS; p++) {
        if (pt_in(mx, my, bx, by, ELV_PCELL_X0 + p * ELV_PCELL_PITCH,
                  ELV_PCELL_Y, ELV_PCELL_W, ELV_PCELL_H)) {
            if (p == game.elv_period) {            /* re-click cycles mode */
                uint8_t *m = &s->sched_mode[game.elv_day][p];
                *m = (uint8_t)((*m + 1) % 3);
            }
            game.elv_period = p;
            return 1;
        }
    }

    /* Waiting Car Response spinner (threshold, EXE clamp 1..100) */
    uint8_t *th = &s->sched_threshold[game.elv_day][game.elv_period];
    if (pt_in(mx, my, bx, by, 64, 93, 12, 10)) { if (*th < 100) (*th)++; return 1; }
    if (pt_in(mx, my, bx, by, 64, 103, 12, 11)) { if (*th > 1) (*th)--; return 1; }

    /* Standard Floor Departure spinner (patience, EXE clamp 0..3) */
    uint8_t *pa = &s->sched_patience[game.elv_day][game.elv_period];
    if (pt_in(mx, my, bx, by, 64, 148, 12, 10)) { if (*pa < 3) (*pa)++; return 1; }
    if (pt_in(mx, my, bx, by, 64, 158, 12, 11)) { if (*pa > 0) (*pa)--; return 1; }

    /* SHOW On/Off (top half = On, bottom half = Off): per-shaft — Off draws
     * the shaft in the world as just its two guide rails so what's behind
     * (rooms the shaft passes through) is visible. */
    if (pt_in(mx, my, bx, by, ELV_SHOW)) {
        s->hidden = ((my - (by + 206)) < 10) ? 0 : 1;
        return 1;
    }

    /* OK closes (and leaves edit mode if it was open) */
    if (pt_in(mx, my, bx, by, ELV_OK_BTN)) {
        elv_edit_exit();
        game.elv_open = 0;
        return 1;
    }

    /* Shaft grid (HandleGridClick, seg_1098:1ff5): col -1 = toggle floor
     * service (+0x42, group-shared); cols 0..7 = set that car's home floor
     * (+0xBA). Geometry mirrors the renderer: 9 cols x 15 rows, row 0 bottom. */
    /* Scrollbar: click positions the 15-row window (top = highest floors). */
    if (pt_in(mx, my, bx, by, ELV_SCROLLBAR)) {
        int maxsc = elv_max_scroll(s);
        if (maxsc > 0) {
            int trel = my - (by + 195);
            if (trel < 0) trel = 0;
            if (trel > 195) trel = 195;
            game.elv_scroll = maxsc - (trel * maxsc) / 195;
            if (game.elv_scroll < 0) game.elv_scroll = 0;
            if (game.elv_scroll > maxsc) game.elv_scroll = maxsc;
        }
        return 1;
    }

    if (pt_in(mx, my, bx, by, ELV_GRID)) {
        int gx0 = bx + 18, gy0 = by + 195;
        int col = (mx - gx0) / 13 - 1;          /* -1 = service col, 0..7 = cars */
        int r = 14 - (my - gy0) / 13;           /* row 0 = bottom */
        int maxsc = elv_max_scroll(s);
        if (game.elv_scroll > maxsc) game.elv_scroll = maxsc;
        if (game.elv_scroll < 0) game.elv_scroll = 0;
        int base = s->lo + game.elv_scroll;
        int f = base + r;
        if (r >= 0 && r < 15 && f >= s->lo && f <= s->hi) {
            if (col == -1) {
                if (elv_structural_stop(s, f))
                    request_stop_toggle(si, f);
            } else if (col >= 0 && col < s->num_cars) {
                people_set_home(ps, si, col, f);
            }
        }
        return 1;
    }

    /* Simulate (widget 2) = toggle the full-screen shaft-edit mode (seg_10f0
     * ElvEditT): dims the tower to a silhouette and isolates this shaft. */
    if (pt_in(mx, my, bx, by, ELV_SIM_BTN)) {
        if (game.elv_edit_mode) elv_edit_exit(); else elv_edit_enter();
        return 1;
    }
    return 1;   /* clicks inside the window never fall through */
}

/* Returns 1 if the click was consumed by the dialog */
static int elv_dialog_click(int mx, int my)
{
    return elv_dialog_click_faithful(mx, my);
}

/* Toggle the financial report, centering it on first open. */
static void toggle_fin_dialog(void)
{
    game.fin_open = !game.fin_open;
    if (game.fin_open) {
        game.fin_x = (game.screen_w - FIN_DLG_W) / 2;
        game.fin_y = (game.screen_h - WIN_TITLEBAR_H - FIN_DLG_H) / 2;
        if (game.fin_y < MENU_BAR_H) game.fin_y = MENU_BAR_H;
    }
}

/* Financial report click: OK closes; any click inside the body is swallowed so
 * it doesn't fall through to the world. Returns 1 if consumed. */
static int fin_dialog_click(int mx, int my)
{
    if (!game.fin_open) return 0;
    int bx = game.fin_x, by = game.fin_y + WIN_TITLEBAR_H;
    if (mx < game.fin_x || mx >= game.fin_x + FIN_DLG_W ||
        my < game.fin_y || my >= by + FIN_DLG_H)
        return 0;
    if (pt_in(mx, my, bx, by, FIN_OK_BTN)) { game.fin_open = 0; return 1; }
    return 1;   /* swallow all body clicks */
}

/* Open the elevator dialog for the shaft under the current mouse cell, if any.
 * Shared by the double-click path and the finger/pointer tool. Returns 1 if a
 * dialog opened. btn_x/btn_y are the click's screen coords (dialog anchor). */
static int open_elv_dialog_at_mouse(int btn_x, int btn_y)
{
    int fidx = floor_to_index(game.mouse_floor);
    if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT ||
        game.mouse_cell < 0 || game.mouse_cell >= TOWER_WIDTH ||
        !item_is_elevator(game.tower.grid[fidx][game.mouse_cell].type))
        return 0;
    TowerCell *cell = &game.tower.grid[fidx][game.mouse_cell];
    Tenant *t = tower_tenant(&game.tower, cell->tenant_id);
    if (!t) return 0;
    game.elv_open = 1;
    game.elv_sx = t->x;
    game.elv_stype = t->type;
    game.elv_x = btn_x + 24;
    game.elv_y = btn_y - 60;
    if (game.elv_x + ELV_W > game.screen_w)
        game.elv_x = game.screen_w - ELV_W - 8;
    int dlg_h = WIN_TITLEBAR_H + ELV_DLG_H + ELV_RIDERS_H;
    if (game.elv_y + dlg_h > game.screen_h)
        game.elv_y = game.screen_h - dlg_h - 8;
    if (game.elv_y < 0) game.elv_y = 8;
    game.dragging = 0;
    return 1;
}

/* ---------- People layer: elevator cars + waiting queues ----------
 * Real art: car sheets 0x8428/29/2A/2B (5 fullness frames, the 5th is the
 * red F "full" diamond), queue silhouettes 0x8468, engines at the sheet
 * tails. Frame choice = GetCarSprite (1090:221f). */
/* In-tenant occupants (AnimPeple): the little people the sim rolled into
 * each unit. Frame codes span the concatenated type-8 people band —
 * sprite sheets 0x85E8..0x85EE, 16x24px frames, drawn in the lower
 * 24px of the 36px floor exactly like the EXE's 24px slot-8 compositor. */
static void render_occupants(void)
{
    if (!game.anim_people) return;   /* Options -> Anim: People ([0xDE30]) */
    static const struct { uint16_t id; int first, count; } BAND[] = {
        { 0x85E8, 0x00, 30 }, { 0x85E9, 0x1E, 27 }, { 0x85EA, 0x39, 6 },
        { 0x85EB, 0x3F, 10 }, { 0x85EC, 0x49, 7 },  { 0x85ED, 0x50, 16 },
        { 0x85EE, 0x60, 3 },
    };
    for (int i = 0; i < game.tower.tenant_count; i++) {
        TenantOccupants *o = &game.sim.occupants[i];
        if (!o->count) continue;
        Tenant *t = &game.tower.tenants[i];
        /* A condo whose residents are all asleep draws dark and EMPTY —
         * no one paces a pitch-black living room at 3am. */
        if (t->type == ITEM_CONDO && !condo_lit[i] &&
            (game.sim.time_of_day == TOD_NIGHT ||
             game.sim.time_of_day == TOD_EVENING))
            continue;
        for (int k = 0; k < o->count && k < OCCUPANTS_MAX; k++) {
            int frame = o->frame[k];
            Sprite *spr = NULL;
            int fx = 0;
            for (size_t b = 0; b < sizeof BAND / sizeof BAND[0]; b++) {
                if (frame >= BAND[b].first &&
                    frame < BAND[b].first + BAND[b].count) {
                    spr = sprites_find(&game.sprites, BAND[b].id);
                    fx = (frame - BAND[b].first) * 16;
                    break;
                }
            }
            if (!spr) continue;
            int sx, sy;
            grid_to_screen(t->floor, t->x + o->x[k], &sx, &sy);
            SDL_Rect src = { fx, 0, 16, 24 };
            SDL_Rect dst = { sx, sy + CELL_H - 24, 16, 24 };
            SDL_RenderCopy(game.renderer, spr->texture, &src, &dst);
        }
    }
}

static void render_shaft(ElevatorShaft *s)
{
    Sprite *queue_spr = sprites_find(&game.sprites, SPR_ELEV_QUEUE);

    if (!s->active) return;
    /* Show Off hides cars and queues with the shaft body (caps stay — they
     * are the extend handles). Edit mode always shows the shaft it's editing. */
    if (s->hidden && !game.elv_edit_mode) queue_spr = NULL;
    {
        int shaft_w = ITEM_WIDTH[s->type] * CELL_W;

        /* Machinery caps: the sheet tail is TWO one-shaft-wide tiles —
         * tile A (up arrow + winch) is the motor room ABOVE the shaft,
         * tile B (buffer springs + down arrow) is the pit BELOW it.
         * (OpenSkyscraper splits them the same way: topMotor/bottomMotor;
         * the arrows are the original's extend-the-shaft handles.) */
        {
            int is_exp = (s->type == ITEM_ELEVATOR_EXPRESS);
            /* The motors animate (palette cycle) while any car is moving */
            int moving = 0;
            for (int ci = 0; ci < s->num_cars && !moving; ci++)
                if (s->car[ci].active && s->car[ci].target != s->car[ci].floor)
                    moving = 1;
            int ef = moving ? (game.sim.frame / 6) % 3 : 0;
            uint16_t eng_id = is_exp
                ? (ef == 0 ? SPR_ELEV_EXPRESS
                 : ef == 1 ? SPR_ELEV_EXP_F1 : SPR_ELEV_EXP_F2)
                : (ef == 0 ? SPR_ELEV_STD_LOADED
                 : ef == 1 ? SPR_ELEV_STD_F1 : SPR_ELEV_STD_F2);
            Sprite *eng = sprites_find(&game.sprites, eng_id);
            if (eng) {
                int tile_w  = is_exp ? 48 : 32;  /* wide double-drum = express */
                int tail    = is_exp ? 5 : 4;
                int ex, ey;
                grid_to_screen(index_to_floor(s->hi) + 1, s->x, &ex, &ey);
                SDL_Rect src_top = { tail * tile_w, 0, tile_w, 36 };
                SDL_Rect dst_top = { ex, ey, shaft_w, CELL_H };
                SDL_RenderCopy(game.renderer, eng->texture, &src_top, &dst_top);

                grid_to_screen(index_to_floor(s->lo) - 1, s->x, &ex, &ey);
                SDL_Rect src_bot = { (tail + 1) * tile_w, 0, tile_w, 36 };
                SDL_Rect dst_bot = { ex, ey, shaft_w, CELL_H };
                SDL_RenderCopy(game.renderer, eng->texture, &src_bot, &dst_bot);
            }
        }

        /* Waiting queues: lines of silhouettes at the shaft door (ElvPeple).
         * Figures picked per person id so the crowd stays varied but stable.
         * The line forms on whichever side of the shaft has building — so
         * it waits indoors instead of marching into the street.
         * Options -> Anim: People ([0xDE30]) hides the crowd. */
        for (int f = s->lo; f <= s->hi && queue_spr && game.anim_people; f++) {
            const ElevatorStop *st = &s->stop[f];
            int n = st->up_count + st->down_count;
            if (!n) continue;
            int sx, sy;
            grid_to_screen(index_to_floor(f), s->x, &sx, &sy);
            int left_in = 0, right_in = 0;
            for (int d = 1; d <= 4 && !(left_in && right_in); d++) {
                int lx = s->x - d;
                int rx = s->x + ITEM_WIDTH[s->type] + d - 1;
                if (lx >= 0 && game.tower.grid[f][lx].type != ITEM_NONE)
                    left_in = 1;
                if (rx < TOWER_WIDTH &&
                    game.tower.grid[f][rx].type != ITEM_NONE)
                    right_in = 1;
            }
            int rightward = right_in && !left_in;
            /* Draw the WHOLE line (both 40-deep rings) — the original lets
             * a drowning shaft's queue snake across the lobby; capping at
             * 10 hid exactly that signal. (person_hit_test mirrors this.) */
            int shown = n;
            for (int k = 0; k < shown; k++) {
                uint16_t pid;
                if (k < st->up_count)
                    pid = st->up_ring[(st->up_head + k) % QUEUE_CAP];
                else
                    pid = st->down_ring[(st->down_head + k - st->up_count)
                                        % QUEUE_CAP];
                if (!pid) continue;
                int fig = (pid * 7) % 40;     /* 40 silhouettes of 16px */
                SDL_Rect src = { fig * 16, 0, 16, 36 };
                int px = rightward ? sx + shaft_w + k * 9
                                   : sx - 16 - k * 9;
                SDL_Rect dst = { px, sy, 16, CELL_H };
                /* The original's frustration display: the longer a person
                 * waits, the pinker then redder their silhouette (wait_accum
                 * against the wait cap — the eval-stress input). */
                {
                    const Person *qp = &game.sim.people.people[pid - 1];
                    int cap = TUNING.wait_cap > 0 ? TUNING.wait_cap : 1;
                    /* banked frustration PLUS the wait they're suffering
                     * right now — wait_accum only banks at board/give-up,
                     * so queues never reddened while actually waiting */
                    int live = game.sim.frame - qp->wait_start;
                    if (live < 0) live = 0;
                    float wr = (float)(qp->wait_accum + live) / (float)cap;
                    if (wr > 1.0f) wr = 1.0f;
                    if (wr > 0.25f && game.queue_hot) {
                        /* swap to the white clone — mod can only darken,
                         * so the black sheet itself can never redden */
                        uint8_t sub = (uint8_t)(220.0f * (wr - 0.25f) / 0.75f);
                        SDL_SetTextureColorMod(game.queue_hot,
                                               160 + (uint8_t)(95.0f * wr),
                                               160 - sub > 0 ? (uint8_t)(160 - sub) : 0,
                                               160 - sub > 0 ? (uint8_t)(160 - sub) : 0);
                        SDL_RenderCopy(game.renderer, game.queue_hot,
                                       &src, &dst);
                        continue;
                    }
                }
                SDL_RenderCopy(game.renderer, queue_spr->texture, &src, &dst);
            }
            SDL_SetTextureColorMod(queue_spr->texture, 255, 255, 255);
        }

        /* Cars, with smooth travel between floors */
        for (int ci = 0; ci < s->num_cars; ci++) {
            ElevatorCar *c = &s->car[ci];
            if (!c->active) continue;
            if (s->hidden && !game.elv_edit_mode) continue;
            int sx, sy;
            grid_to_screen(index_to_floor(c->floor), s->x, &sx, &sy);
            if (c->target != c->floor && c->move_total) {
                int gone = c->move_total - c->move_timer;
                int off = gone * CELL_H / c->move_total;
                sy += c->dir ? -off : off;
            }

            /* GetCarSprite: 0/1 pax -> frame 0/1, 2-3 -> 2, partial -> 3,
             * full -> 4 (red F) */
            int frame;
            if (c->passengers <= 1)               frame = c->passengers;
            else if (c->passengers <= 3)          frame = 2;
            else if (c->passengers < s->capacity) frame = 3;
            else                                  frame = 4;

            Sprite *spr; SDL_Rect src;
            if (s->type == ITEM_ELEVATOR_EXPRESS) {
                /* express = the wide 48px sheet (42-person car) */
                spr = sprites_find(&game.sprites, SPR_ELEV_EXPRESS);
                src = (SDL_Rect){ frame * 48, 0, 48, 36 };
            } else if (s->type == ITEM_ELEVATOR_SERVICE) {
                spr = sprites_find(&game.sprites, SPR_ELEV_SERVICE);
                src = (SDL_Rect){ frame * 32, 0, 32, 36 };
            } else if (frame == 0) {
                spr = sprites_find(&game.sprites, SPR_ELEV_STD_EMPTY);
                src = (SDL_Rect){ 0, 0, 32, 36 };
            } else {
                spr = sprites_find(&game.sprites, SPR_ELEV_STD_LOADED);
                src = (SDL_Rect){ (frame - 1) * 32, 0, 32, 36 };
            }
            SDL_Rect dst = { sx, sy, shaft_w, CELL_H };
            if (spr) {
                SDL_RenderCopy(game.renderer, spr->texture, &src, &dst);
            } else {
                SDL_SetRenderDrawColor(game.renderer, 90, 90, 140, 255);
                SDL_RenderFillRect(game.renderer, &dst);
            }
        }
    }
}

static void render_people(void)
{
    PeopleSim *ps = &game.sim.people;
    for (int i = 0; i < ps->shaft_count; i++)
        render_shaft(&ps->shafts[i]);
}

/* Render the elevator "Simulate" edit surface: the tower dimmed to a flat
 * silhouette with only the selected shaft lit (seg_10f0 ElvEditT). Replaces the
 * normal world layers while game.elv_edit_mode is set. */
static void render_elv_edit_mode(void)
{
    if (!game.elv_open) { elv_edit_exit(); return; }
    int si = elv_dialog_shaft();
    if (si < 0) { elv_edit_exit(); return; }
    PeopleSim *ps = &game.sim.people;
    ElevatorShaft *sel = &ps->shafts[si];

    int top_floor, bot_floor, dummy;
    screen_to_grid(0, 0, &top_floor, &dummy);
    screen_to_grid(0, game.screen_h, &bot_floor, &dummy);
    top_floor += 2; bot_floor -= 2;
    if (top_floor > TOWER_TOP_FLOOR) top_floor = TOWER_TOP_FLOOR;
    if (bot_floor < TOWER_MIN_FLOOR) bot_floor = TOWER_MIN_FLOOR;

    /* Flat dimmed silhouette: every tenant becomes a uniform run of cells,
     * ignoring its type sprite (DrawTowerSilhouette, seg_10f0:01f9). The shaft
     * being edited is left out of the wash so it reads bright below. */
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
    for (int ti = 0; ti < game.tower.tenant_count; ti++) {
        Tenant *t = &game.tower.tenants[ti];
        if (t->type == ITEM_NONE) continue;
        if (t->floor > top_floor || t->floor + t->height - 1 < bot_floor)
            continue;
        int is_elv = item_is_elevator(t->type);
        if (is_elv && t->x == sel->x && t->type == (ItemType)sel->type)
            continue;                     /* the shaft we're editing */
        int sx, sy;
        grid_to_screen(t->floor + t->height - 1, t->x, &sx, &sy);
        SDL_Rect r = { sx, sy, t->width * CELL_W, t->height * CELL_H };
        if (is_elv) SDL_SetRenderDrawColor(game.renderer, 38, 46, 60, 255);
        else        SDL_SetRenderDrawColor(game.renderer, 58, 66, 86, 255);
        SDL_RenderFillRect(game.renderer, &r);
    }

    /* The selected shaft's tube, lit (mirrors render_tower PASS 2.5), with a
     * per-floor stop marker: green = serviced, dim = a stop you could add. */
    Sprite *shaftspr = sprites_find(&game.sprites, SPR_ELEV_SHAFT);
    Sprite *ext = sprites_find(&game.sprites, SPR_ELEV_EXT);
    int tw = ITEM_WIDTH[sel->type] * CELL_W;
    for (int f = sel->lo; f <= sel->hi; f++) {
        int wf = index_to_floor(f);
        if (wf > top_floor || wf < bot_floor) continue;
        int tx, ty;
        grid_to_screen(wf, sel->x, &tx, &ty);
        if (shaftspr) {
            SDL_Rect src = { 0, 0, 32, shaftspr->h };
            if (tw == 48) {
                SDL_Rect mid = { tx + 8, ty, 32, CELL_H };
                SDL_RenderCopy(game.renderer, shaftspr->texture, &src, &mid);
                if (ext) {
                    SDL_Rect sl = { 0, 0, 8, ext->h }, dl = { tx, ty, 8, CELL_H };
                    SDL_Rect sr = { 8, 0, 8, ext->h }, dr = { tx + 40, ty, 8, CELL_H };
                    SDL_RenderCopy(game.renderer, ext->texture, &sl, &dl);
                    SDL_RenderCopy(game.renderer, ext->texture, &sr, &dr);
                }
            } else {
                SDL_Rect dst = { tx, ty, tw, CELL_H };
                SDL_RenderCopy(game.renderer, shaftspr->texture, &src, &dst);
            }
        }
        if (elv_structural_stop(sel, f)) {
            SDL_Rect tab = { tx + tw - 4, ty + 4, 4, CELL_H - 8 };
            if (sel->serviced[f])
                SDL_SetRenderDrawColor(game.renderer, 60, 200, 60, 255);
            else
                SDL_SetRenderDrawColor(game.renderer, 90, 110, 90, 255);
            SDL_RenderFillRect(game.renderer, &tab);
        }
    }

    /* Cars, queues and motor caps for the selected shaft only. */
    render_shaft(sel);

    /* Banner along the bottom (clear of the map/toolbox/info windows): what this
     * mode is and how to leave it. */
    {
        int by = game.screen_h - 22;
        SDL_SetRenderDrawColor(game.renderer, 18, 22, 34, 235);
        SDL_Rect band = { 84, by, game.screen_w - 84, 22 };
        SDL_RenderFillRect(game.renderer, &band);
        SDL_Color w = { 220, 225, 240, 255 };
        stats_label(94, by + 5,
                    "ELEVATOR EDIT \xe2\x80\x94 click a floor of this shaft to "
                    "toggle its stop \xc2\xb7 Resume (or OK) to exit", w);
    }
}

/* Column lock for a vertical elevator drag: if the drag started on (or one
 * floor off — e.g. on the motor cap / pit arrows) an existing same-type
 * shaft, extend that shaft's column rather than starting a new one at the
 * click cell. The original's cap arrows are exactly this affordance. */
static int elevator_drag_column(void)
{
    int w = ITEM_WIDTH[game.build_type];
    static const int dfs[] = { 0, -1, 1 };
    for (int i = 0; i < 3; i++) {
        int fidx = floor_to_index(game.drag_start_floor + dfs[i]);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
        for (int cx = game.drag_start_cell - w + 1;
             cx <= game.drag_start_cell + w - 1; cx++) {
            if (cx < 0 || cx >= TOWER_WIDTH) continue;
            TowerCell *cell = &game.tower.grid[fidx][cx];
            if (cell->type != game.build_type) continue;
            Tenant *t = tower_tenant(&game.tower, cell->tenant_id);
            if (t) return t->x;
        }
    }
    return game.drag_start_cell;
}

/* Build origin cell, centered on the cursor so the tile straddles the pointer
 * instead of hanging entirely to its right. Clamped to the tower width. */
static int build_origin_cell(int mouse_cell)
{
    /* The floor tool anchors at the pressed cell — its 63-cell nominal
     * width is only the bare-click stamp size, and centering it made a
     * drag's span start 31 cells left of the press (impossible to lay
     * deck over a small footprint without overhang rejects). */
    if (game.build_type == ITEM_FLOOR) return mouse_cell;
    int w = ITEM_WIDTH[game.build_type];
    int c = mouse_cell - (w - 1) / 2;
    if (c < 0) c = 0;
    if (c + w > TOWER_WIDTH) c = TOWER_WIDTH - w;
    return c;
}

/* Stairs/escalators anchor on the clicked floor as their UPPER landing
 * (StairsT driver 11f8:1452: the record stores clicked-1 as the lower
 * floor); everything else builds on the clicked floor itself. */
static int build_origin_floor(ItemType ty, int mouse_floor)
{
    return (ty == ITEM_STAIRS || ty == ITEM_ESCALATOR) ? mouse_floor - 1
                                                       : mouse_floor;
}

static void render_build_ghost(void)
{
    if (game.demolish_mode || game.build_type == ITEM_NONE) return;
    /* A live cap drag lays real shaft as the cursor moves — the growing
     * shaft itself is the feedback; a ghost shadow on top is just noise. */
    if (game.cap_drag) return;

    int width = ITEM_WIDTH[game.build_type];
    int floors = ITEM_HEIGHT[game.build_type];

    if (game.dragging && item_is_elevator(game.build_type)) {
        /* Elevators drag VERTICALLY: one shaft segment per floor from the
         * drag start to the mouse, locked to one column. */
        int x = elevator_drag_column();
        int f0 = game.drag_start_floor, f1 = game.mouse_floor;
        int df = (f1 >= f0) ? 1 : -1;
        SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
        for (int f = f0; ; f += df) {
            int gx, gy;
            grid_to_screen(f, x, &gx, &gy);
            if (tower_can_place(&game.tower, game.build_type, f, x))
                SDL_SetRenderDrawColor(game.renderer, 0, 200, 0, 80);
            else
                SDL_SetRenderDrawColor(game.renderer, 200, 0, 0, 80);
            SDL_Rect ghost = { gx, gy, width * CELL_W, CELL_H };
            SDL_RenderFillRect(game.renderer, &ghost);
            SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 160);
            SDL_RenderDrawRect(game.renderer, &ghost);
            if (f == f1) break;
        }
        SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
    } else if (game.dragging) {
        /* Drag placement: show ghost row of units from start to current cell */
        int start = game.drag_start_cell;
        int end = game.mouse_cell;
        int floor = build_origin_floor(game.build_type, game.drag_start_floor);
        
        /* Determine direction and iterate */
        if (end < start) { int tmp = start; start = end; end = tmp; }
        
        /* Snap to unit boundaries: place units starting from drag_start_cell
         * towards the mouse, filling in unit-width increments */
        int unit_start = game.drag_start_cell;
        int mouse_end = game.mouse_cell;
        
        int step_dir = (mouse_end >= unit_start) ? 1 : -1;
        int cur;
        
        if (step_dir > 0) {
            for (cur = unit_start; cur + width - 1 < TOWER_WIDTH && cur <= mouse_end; cur += width) {
                int gx, gy;
                grid_to_screen(floor, cur, &gx, &gy);
                int can = tower_can_place(&game.tower, game.build_type, floor, cur);
                
                SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
                if (can)
                    SDL_SetRenderDrawColor(game.renderer, 0, 200, 0, 80);
                else
                    SDL_SetRenderDrawColor(game.renderer, 200, 0, 0, 80);
                
                int ghost_h = floors * CELL_H;
                int ghost_y = gy - (floors - 1) * CELL_H;
                SDL_Rect ghost = { gx, ghost_y, width * CELL_W, ghost_h };
                SDL_RenderFillRect(game.renderer, &ghost);
                SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 160);
                SDL_RenderDrawRect(game.renderer, &ghost);
                SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
            }
        } else {
            for (cur = unit_start; cur >= 0 && cur >= mouse_end; cur -= width) {
                int place_x = cur;
                /* Align to left edge of unit */
                if (place_x + width > TOWER_WIDTH) continue;
                
                int gx, gy;
                grid_to_screen(floor, place_x, &gx, &gy);
                int can = tower_can_place(&game.tower, game.build_type, floor, place_x);
                
                SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
                if (can)
                    SDL_SetRenderDrawColor(game.renderer, 0, 200, 0, 80);
                else
                    SDL_SetRenderDrawColor(game.renderer, 200, 0, 0, 80);
                
                int ghost_h = floors * CELL_H;
                int ghost_y = gy - (floors - 1) * CELL_H;
                SDL_Rect ghost = { gx, ghost_y, width * CELL_W, ghost_h };
                SDL_RenderFillRect(game.renderer, &ghost);
                SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 160);
                SDL_RenderDrawRect(game.renderer, &ghost);
                SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
            }
        }
    } else {
        /* Single-unit ghost, centered on the cursor */
        int oc = build_origin_cell(game.mouse_cell);
        int of = build_origin_floor(game.build_type, game.mouse_floor);
        int gx, gy;
        grid_to_screen(of, oc, &gx, &gy);

        int can = tower_can_place(&game.tower, game.build_type, of, oc);
        
        SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
        if (can) {
            SDL_SetRenderDrawColor(game.renderer, 0, 200, 0, 100);
        } else {
            SDL_SetRenderDrawColor(game.renderer, 200, 0, 0, 100);
        }
        int ghost_h = floors * CELL_H;
        int ghost_y = gy - (floors - 1) * CELL_H;
        SDL_Rect ghost = { gx, ghost_y, width * CELL_W, ghost_h };
        SDL_RenderFillRect(game.renderer, &ghost);
        SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 200);
        SDL_RenderDrawRect(game.renderer, &ghost);
        SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
    }
}

/* Forward declarations for Win3.1 drawing helpers */
static void draw_win31_rect(int x, int y, int w, int h, int raised);
static int draw_menu_text(const char *text, int x, int y, int selected);

/* ========== Win3.1-style Sub-Windows ========== */

/* --- Info Window (clock, stars, money, events) --- */
/* The original's "Time Window" — has analog clock, star rating,
 * money display, population, and a scrolling event feed. */

/* Original SimTower layout (from time.rml / map.rml / toolbox.rml):
 *  - Info bar: 431×41px across TOP RIGHT (watch + stars + money + date + message)
 *  - Map: 200px wide, TOP LEFT (buttons + 200×288 map + 24px ground)
 *  - Toolbox: 128px wide, LEFT below map (speed + tools + items)
 * 
 * Our layout follows this but adapts sizes slightly for our resolution. */

#define INFO_BAR_W  431       /* Info/time bar width — faithful to original (time.rml: 431px) */
#define INFO_BAR_H  41        /* Faithful time-bar height (time.rml: 41px) */
#define CLOCK_R     14        /* Small clock for horizontal bar */

#define MAP_WIN_W   200       /* Map window (left side) */
#define MAP_WIN_H   280       /* Map height (sky + ground) */

#define TOOL_WIN_W  72        /* Narrow toolbox to match the original (2-column item palette) */
#define SPEED_BTN_W 64        /* Single wide play/pause toggle — fills the narrow window */
#define SPEED_BTN_H 26
#define TOOL_WIN_H  264       /* Fits play/pause + tools + 5-row icon grid + cost */

static void draw_analog_clock(int cx, int cy, int r, int hour, int minute)
{
    /* Clock face */
    SDL_SetRenderDrawColor(game.renderer, 255, 255, 240, 255);
    /* Fill circle approximation with filled rects */
    for (int y = -r; y <= r; y++) {
        int xspan = (int)sqrt((double)(r*r - y*y));
        SDL_Rect row = { cx - xspan, cy + y, xspan * 2, 1 };
        SDL_RenderFillRect(game.renderer, &row);
    }
    
    /* Clock border */
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
    /* Draw circle outline */
    for (int deg = 0; deg < 360; deg++) {
        double rad = deg * M_PI / 180.0;
        int px = cx + (int)(r * cos(rad));
        int py = cy + (int)(r * sin(rad));
        SDL_RenderDrawPoint(game.renderer, px, py);
    }
    
    /* Hour tick marks */
    for (int h = 0; h < 12; h++) {
        double angle = (h * 30 - 90) * M_PI / 180.0;
        int x1 = cx + (int)((r - 4) * cos(angle));
        int y1 = cy + (int)((r - 4) * sin(angle));
        int x2 = cx + (int)((r - 1) * cos(angle));
        int y2 = cy + (int)((r - 1) * sin(angle));
        SDL_RenderDrawLine(game.renderer, x1, y1, x2, y2);
    }
    
    /* Hour hand (shorter, thicker) */
    {
        double h_angle = ((hour % 12) * 30 + minute * 0.5 - 90) * M_PI / 180.0;
        int hx = cx + (int)((r * 0.55) * cos(h_angle));
        int hy = cy + (int)((r * 0.55) * sin(h_angle));
        SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
        SDL_RenderDrawLine(game.renderer, cx, cy, hx, hy);
        SDL_RenderDrawLine(game.renderer, cx+1, cy, hx+1, hy);
        SDL_RenderDrawLine(game.renderer, cx, cy+1, hx, hy+1);
    }
    
    /* Minute hand (longer, thinner) */
    {
        double m_angle = (minute * 6 - 90) * M_PI / 180.0;
        int mx = cx + (int)((r * 0.8) * cos(m_angle));
        int my = cy + (int)((r * 0.8) * sin(m_angle));
        SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
        SDL_RenderDrawLine(game.renderer, cx, cy, mx, my);
    }
    
    /* Center dot */
    SDL_Rect center = { cx - 1, cy - 1, 3, 3 };
    SDL_RenderFillRect(game.renderer, &center);
}

/* Event message buffer for the feed */
#define EVENT_MSG_COUNT 8
#define EVENT_MSG_LEN   64
static char event_messages[EVENT_MSG_COUNT][EVENT_MSG_LEN];
static int  event_msg_head = 0;
static int  event_msg_total = 0;

/* Save file path: $CT_SAVE overrides; default lives next to the cwd */
static const char *save_path(void)
{
    const char *env = getenv("CT_SAVE");
    return env && *env ? env : "concilliatower.sav";
}

static void add_event_message(const char *msg)
{
    snprintf(event_messages[event_msg_head], EVENT_MSG_LEN, "%s", msg);
    event_msg_head = (event_msg_head + 1) % EVENT_MSG_COUNT;
    if (event_msg_total < EVENT_MSG_COUNT) event_msg_total++;
}

/* Draw a Win3.1 style title bar (navy blue with white text, for window dragging) */
static void draw_win31_titlebar(int x, int y, int w, const char *title)
{
    /* Navy blue background */
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 128, 255);
    SDL_Rect bg = { x, y, w, WIN_TITLEBAR_H };
    SDL_RenderFillRect(game.renderer, &bg);
    
    /* 3D border */
    SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 255);
    SDL_RenderDrawLine(game.renderer, x, y, x + w - 1, y);
    SDL_RenderDrawLine(game.renderer, x, y, x, y + WIN_TITLEBAR_H - 1);
    SDL_SetRenderDrawColor(game.renderer, 64, 64, 64, 255);
    SDL_RenderDrawLine(game.renderer, x, y + WIN_TITLEBAR_H - 1, x + w - 1, y + WIN_TITLEBAR_H - 1);
    SDL_RenderDrawLine(game.renderer, x + w - 1, y, x + w - 1, y + WIN_TITLEBAR_H - 1);
    
    /* Title text */
    if (game.font_small && title) {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface *ts = TTF_RenderText_Blended(game.font_small, title, white);
        if (ts) {
            SDL_Texture *tt = SDL_CreateTextureFromSurface(game.renderer, ts);
            SDL_Rect dst = { x + 4, y + 2, ts->w, ts->h };
            SDL_RenderCopy(game.renderer, tt, NULL, &dst);
            SDL_DestroyTexture(tt);
            SDL_FreeSurface(ts);
        }
    }
}

static void render_info_window(void)
{
    /* Info bar: horizontal strip (like original time.rml).
     * Layout: [clock 29×29] [star rating] [date info] [message] [funds/pop right-aligned] */
    int wx = game.info_x;
    int wy = game.info_y;
    
    /* Title bar for dragging */
    draw_win31_titlebar(wx, wy, INFO_BAR_W, "Tower Info");
    wy += WIN_TITLEBAR_H;
    
    /* Background — original uses bitmap 0x8140 (431×41, tiled horizontal) */
    if (game.ui_timebar) {
        /* Tile the time bar background across the width */
        for (int tx = wx; tx < wx + INFO_BAR_W; tx += game.ui_timebar_w) {
            int tw = game.ui_timebar_w;
            if (tx + tw > wx + INFO_BAR_W) tw = wx + INFO_BAR_W - tx;
            SDL_Rect src = { 0, 0, tw, game.ui_timebar_h };
            SDL_Rect dst = { tx, wy, tw, INFO_BAR_H };
            SDL_RenderCopy(game.renderer, game.ui_timebar, &src, &dst);
        }
    } else {
        draw_win31_rect(wx, wy, INFO_BAR_W, INFO_BAR_H, 1);
    }
    
    /* Analog clock (left side, like original) */
    int clock_cx = wx + 6 + CLOCK_R;
    int clock_cy = wy + INFO_BAR_H / 2;
    draw_analog_clock(clock_cx, clock_cy, CLOCK_R, game.sim.hour, game.sim.minute);
    
    /* Star rating — left 42, top 2. The original shows the earned stars in
     * gold plus ONE grey star for the next level to reach (that's why the EXE
     * has exactly two star bitmaps). The 24px-wide bitmaps carry 3 columns of
     * pure-white PADDING on the right (pixel-scanned; the star highlights are
     * cream, never white) — the real cell is 21px, so crop and pitch at 21. */
    #define STAR_CELL_W 21
    int star_x = wx + 42;
    if (game.tower.star_rating >= 6 && game.font_small) {
        /* TOWER status: the original replaces the stars entirely */
        SDL_Color gold = {220, 180, 0, 255};
        SDL_Surface *ts = TTF_RenderUTF8_Blended(game.font_small, "T O W E R", gold);
        if (ts) {
            SDL_Texture *tt = SDL_CreateTextureFromSurface(game.renderer, ts);
            SDL_Rect dst = { star_x, wy + 4, ts->w, ts->h };
            SDL_RenderCopy(game.renderer, tt, NULL, &dst);
            SDL_DestroyTexture(tt);
            SDL_FreeSurface(ts);
        }
    } else if (game.ui_star[0] && game.ui_star[1]) {
        /* Always show all five slots: earned stars filled, the rest empty. */
        for (int i = 0; i < 5; i++) {
            int filled = (i < game.tower.star_rating) ? 1 : 0;
            SDL_Rect src = { 0, 0, STAR_CELL_W, game.ui_star_h };
            SDL_Rect dst = { star_x + i * STAR_CELL_W, wy + 2,
                            STAR_CELL_W, game.ui_star_h };
            SDL_RenderCopy(game.renderer, game.ui_star[filled], &src, &dst);
        }
    } else if (game.font_small) {
        /* Fallback: UTF-8 stars */
        SDL_Color gold = {200, 170, 0, 255};
        char stars[32];
        int pos = 0;
        for (int i = 0; i < 5; i++) {
            if (i < game.tower.star_rating) {
                stars[pos++] = (char)0xE2; stars[pos++] = (char)0x98; stars[pos++] = (char)0x85;
            } else {
                stars[pos++] = (char)0xE2; stars[pos++] = (char)0x98; stars[pos++] = (char)0x86;
            }
        }
        stars[pos] = '\0';
        SDL_Surface *ts = TTF_RenderUTF8_Blended(game.font_small, stars, gold);
        if (ts) {
            SDL_Texture *tt = SDL_CreateTextureFromSurface(game.renderer, ts);
            SDL_Rect dst = { star_x, wy + 3, ts->w, ts->h };
            SDL_RenderCopy(game.renderer, tt, NULL, &dst);
            SDL_DestroyTexture(tt);
            SDL_FreeSurface(ts);
        }
    }
    
    /* Date — time.rml: left 160, top 3, 13px, "1st WD / 2Q / 83rd Year" */
    TTF_Font *finfo = game.font_info ? game.font_info : game.font_small;
    if (finfo) {
        SDL_Color black = {0, 0, 0, 255};
        /* The EXE calendar: day name = day%3, quarter = 3-day cycle
         * (4 per year), year = 12 days. The old display read the 6h
         * bookkeeping slice as the day name and compressed a year to
         * 4 days — reconciled 2026-07-12; BARKLE now shows the same
         * date the original would. */
        static const char *wd_names[] = { "1st WD", "2nd WD", "WE" };
        const char *wd = wd_names[((game.tower.day % 3) + 3) % 3];
        int q = (game.tower.day / 3) % 4 + 1;
        int year = game.tower.day / 12 + 1;
        int ylast = year % 10, yten = year % 100;
        const char *ysuf = (yten >= 11 && yten <= 13) ? "th"
                         : ylast == 1 ? "st" : ylast == 2 ? "nd"
                         : ylast == 3 ? "rd" : "th";
        char date_buf[64];
        snprintf(date_buf, sizeof(date_buf), "%s / %dQ / %d%s Year",
                 wd, q, year, ysuf);
        SDL_Surface *ts = TTF_RenderText_Blended(finfo, date_buf, black);
        if (ts) {
            SDL_Texture *tt = SDL_CreateTextureFromSurface(game.renderer, ts);
            SDL_Rect dst = { wx + 160, wy + 3, ts->w, ts->h };
            SDL_RenderCopy(game.renderer, tt, NULL, &dst);
            SDL_DestroyTexture(tt);
            SDL_FreeSurface(ts);
        }
    }

    /* Funds — time.rml: right-aligned 6px from edge, top 2 */
    if (finfo) {
        SDL_Color black = {0, 0, 0, 255};
        char money_buf[64];
        format_money(game.tower.money, money_buf, sizeof(money_buf));
        SDL_Surface *ts = TTF_RenderText_Blended(finfo, money_buf, black);
        if (ts) {
            SDL_Texture *tt = SDL_CreateTextureFromSurface(game.renderer, ts);
            /* The bg art's Fund box interior is y3..18 — baseline-align with
             * the baked-in "Fund" label, not the rml's top:2. */
            SDL_Rect dst = { wx + INFO_BAR_W - ts->w - 6, wy + 4, ts->w, ts->h };
            SDL_RenderCopy(game.renderer, tt, NULL, &dst);
            SDL_DestroyTexture(tt);
            SDL_FreeSurface(ts);
        }

        /* Population — time.rml: right 6, top 19, bare number */
        char pop_buf[32];
        snprintf(pop_buf, sizeof(pop_buf), "%d", game.tower.population);
        ts = TTF_RenderText_Blended(finfo, pop_buf, black);
        if (ts) {
            SDL_Texture *tt = SDL_CreateTextureFromSurface(game.renderer, ts);
            /* Pop box interior is y21..36 in the bg art */
            SDL_Rect dst = { wx + INFO_BAR_W - ts->w - 6, wy + 21, ts->w, ts->h };
            SDL_RenderCopy(game.renderer, tt, NULL, &dst);
            SDL_DestroyTexture(tt);
            SDL_FreeSurface(ts);
        }
    }

    /* Message strip — time.rml: top 22, padding-left 42, padding-right 130, 11px */
    if (game.font_small && event_msg_total > 0) {
        SDL_Color dk = {0, 0, 100, 255};
        int idx = (event_msg_head - 1 + EVENT_MSG_COUNT) % EVENT_MSG_COUNT;
        SDL_Surface *ts = TTF_RenderText_Blended(game.font_small, event_messages[idx], dk);
        if (ts) {
            SDL_Texture *tt = SDL_CreateTextureFromSurface(game.renderer, ts);
            /* The bg art's sunken message groove interior is x41..301, y25..35 */
            int max_w = INFO_BAR_W - 42 - 130;
            int dw = ts->w > max_w ? max_w : ts->w;
            SDL_Rect src2 = { 0, 0, dw, ts->h };
            SDL_Rect dst = { wx + 43, wy + 24, dw, ts->h };
            SDL_RenderCopy(game.renderer, tt, &src2, &dst);
            SDL_DestroyTexture(tt);
            SDL_FreeSurface(ts);
        }
    }
}

/* --- Minimap Window --- */
/* Shows the entire tower in a tiny overview with colored dots for tenants.
 * Positioned at TOP LEFT (like original map.rml: left:0, top:0). */

static void render_minimap(void)
{
    int wx = game.map_x;
    int wy = game.map_y;
    
    /* Title bar for dragging */
    /* Leading spaces clear the close box (title text is left-aligned). */
    draw_win31_titlebar(wx, wy, MAP_WIN_W, "      Map");
    /* Close box at the left of the title bar (the original's map window
     * has its own close box; reopen from Windows -> Map Window). */
    {
        draw_win31_rect(wx + 3, wy + 3, 13, WIN_TITLEBAR_H - 6, 1);
        SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
        SDL_Rect dash = { wx + 6, wy + WIN_TITLEBAR_H / 2 - 1, 7, 2 };
        SDL_RenderFillRect(game.renderer, &dash);
    }
    wy += WIN_TITLEBAR_H;
    
    /* Minimap body */
    draw_win31_rect(wx, wy, MAP_WIN_W, MAP_WIN_H - WIN_TITLEBAR_H, 1);
    
    /* Map content area */
    int map_x = wx + 4;
    int map_y = wy + 4;
    int map_w = MAP_WIN_W - 8;
    int map_h = MAP_WIN_H - WIN_TITLEBAR_H - 24;
    
    /* Map background — use original bitmap 0x8160 (200×288) if available.
     * Top 264px = sky, bottom 24px = ground strip (from OpenSkyscraper). */
    if (game.ui_map) {
        SDL_Rect dst = { map_x, map_y, map_w, map_h };
        SDL_RenderCopy(game.renderer, game.ui_map, NULL, &dst);
    } else {
        /* Fallback: sky gradient + earth */
        uint8_t tr, tg, tb, ta;
        game_sky_tint(&game.sim, &tr, &tg, &tb, &ta);
        int sr = 120 - (int)ta * (120 - (int)tr) / 255;
        int sg = 180 - (int)ta * (180 - (int)tg) / 255;
        int sb = 220 - (int)ta * (220 - (int)tb) / 255;
        SDL_SetRenderDrawColor(game.renderer, sr, sg, sb, 255);
        SDL_Rect sky = { map_x, map_y, map_w, map_h };
        SDL_RenderFillRect(game.renderer, &sky);
    }
    
    /* Vertical mapping anchored to the background art's ground line
     * (0x8160 is 288px tall with the ground strip starting at 264 —
     * conveniently 110 floors * 288/110 px ≈ that exact split, so one
     * uniform floor scale lines the lobby up with the painted ground). */
    float pf = (float)map_h / (TOWER_TOP_FLOOR - TOWER_MIN_FLOOR + 1);
    float ground_line = map_y + map_h * 264.0f / 288.0f;
    int row_h = (int)pf + 1;               /* contiguous rows, no gaps */

    if (!game.ui_map) {
        SDL_SetRenderDrawColor(game.renderer, 140, 120, 90, 255);
        SDL_Rect earth = { map_x, (int)ground_line, map_w,
                           map_y + map_h - (int)ground_line };
        SDL_RenderFillRect(game.renderer, &earth);
    }

    /* Deck silhouette from the grid extents — floor-tool deck and
     * gap-fill cells have no tenant record, so the shell must come from
     * the grid, not the tenant list. */
    {
        int16_t dleft[TOWER_FLOOR_COUNT], dright[TOWER_FLOOR_COUNT];
        tower_floor_extents(&game.tower, dleft, dright);
        SDL_SetRenderDrawColor(game.renderer, 70, 70, 70, 255);
        for (int fi = 0; fi < TOWER_FLOOR_COUNT; fi++) {
            if (dright[fi] <= dleft[fi]) continue;
            int fl = fi + TOWER_MIN_FLOOR;
            int ty = (int)(ground_line - (fl + 1) * pf);
            int tx = map_x + (dleft[fi] * map_w / TOWER_WIDTH);
            int tw = map_x + (dright[fi] * map_w / TOWER_WIDTH) - tx;
            if (tw < 1) tw = 1;
            SDL_Rect row = { tx, ty, tw, row_h };
            SDL_RenderFillRect(game.renderer, &row);
        }
    }

    /* Tenants as colored rows; floor strips as the dark shell so the
     * silhouette includes empty floors (skipped before = invisible).
     * Overlay modes recolor tenants per the EXE's map views (seg59
     * GetTenantOverlayColor + legend bitmaps 0x139-0x13B):
     *   1 Eval: Excellent cyan / Good yellow / Terrible red
     *   2 Rent: High red / Average yellow / Low green / Very Low cyan
     *   3 Hotel: Dirty Rooms red */
    for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < game.tower.tenant_count; i++) {
        Tenant *t = &game.tower.tenants[i];
        if (t->type == ITEM_NONE) continue;
        if ((t->type == ITEM_FLOOR) != (pass == 0)) continue;

        int ty = (int)(ground_line - (t->floor + t->height) * pf);
        int tx = map_x + (t->x * map_w / TOWER_WIDTH);
        /* Right edge from x+width so adjacent tenants tile exactly —
         * flooring width separately leaves 1px sky gaps that stack into
         * vertical lines wherever room edges align up the tower. */
        int tw = map_x + ((t->x + t->width) * map_w / TOWER_WIDTH) - tx;
        if (tw < 1) tw = 1;
        /* The original's map paints each elevator group as a 1px line
         * (seg19:0b10), not a scaled box — center it in the shaft span. */
        if (t->type == ITEM_ELEVATOR_SHAFT || t->type == ITEM_ELEVATOR_SERVICE ||
            t->type == ITEM_ELEVATOR_EXPRESS) {
            tx += tw / 2;
            tw = 1;
        }

        uint8_t r, g, b;
        int is_shell = (t->type == ITEM_FLOOR) || item_is_transport(t->type);
        if (game.map_mode == 0) {
            if (t->type == ITEM_FLOOR) { r = 70; g = 70; b = 70; }
            else item_fallback_color(t->type, &r, &g, &b);
            if (t->state == TENANT_STRESSED) { r = 255; g = 50; b = 50; }
            else if (t->state == TENANT_ABANDONED) { r = 100; g = 30; b = 30; }
            else if (t->state == TENANT_CONSTRUCTION) { r = 200; g = 180; b = 0; }
        } else if (is_shell) {
            r = 70; g = 70; b = 70;        /* shell only in overlay modes */
        } else if (game.map_mode == 1) {
            /* Eval reads the JUDGE'S verdict where one exists — the same
             * demand categories that drive move-outs, re-lets, and hotel
             * booking (2 content cyan / 1 middle yellow / 0 stressed
             * red). ABANDONED stays red: it's the rescue target the map
             * is for. Types without a judge fall back to live stress. */
            int judged = (t->type == ITEM_OFFICE || t->type == ITEM_CONDO ||
                          t->type == ITEM_SHOP ||
                          t->type == ITEM_RESTAURANT ||
                          t->type == ITEM_FAST_FOOD ||
                          item_is_hotel_room(t->type)) &&
                         t->demand_category != 0xFF;
            if (t->state == TENANT_EMPTY || t->state == TENANT_CONSTRUCTION)
                { r = g = b = 200; }
            else if (judged) {
                if (t->demand_category == 0)      { r = 230; g = 40; b = 40; }
                else if (t->demand_category == 1) { r = 220; g = 210; b = 40; }
                else                              { r = 60; g = 220; b = 230; }
            }
            else if (t->state == TENANT_VACANT) { r = g = b = 200; }
            else if (t->stress >= 67 || t->state == TENANT_STRESSED ||
                     t->state == TENANT_ABANDONED) { r = 230; g = 40; b = 40; }
            else if (t->stress >= 34) { r = 220; g = 210; b = 40; }
            else { r = 60; g = 220; b = 230; }
        } else if (game.map_mode == 2) {
            switch (t->rent_class) {
            case 0:  r = 230; g = 40;  b = 40;  break;   /* High */
            default: r = 220; g = 210; b = 40;  break;   /* Average */
            case 2:  r = 60;  g = 210; b = 60;  break;   /* Low */
            case 3:  r = 60;  g = 220; b = 230; break;   /* Very Low */
            }
        } else { /* mode 3: hotel housekeeping (dirty amber, infested red) */
            if (item_is_hotel_room(t->type) && t->condition == ROOM_DIRTY)
                { r = 230; g = 150; b = 40; }
            else if (item_is_hotel_room(t->type) &&
                     t->condition == ROOM_INFESTED)
                { r = 230; g = 40; b = 40; }
            else { r = 70; g = 70; b = 70; }
        }

        SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
        SDL_Rect dot = { tx, ty, tw, row_h * t->height };
        SDL_RenderFillRect(game.renderer, &dot);
    }

    /* Elevator group lines from the LIVE shaft structs, drawn over the
     * per-floor tenant rows: cap-drag extensions add grid cells without
     * fresh tenant records, which left gaps in the tenant-driven 1px
     * lines (Jonah's map report, 2026-08-02). The shaft struct's lo..hi
     * is the authoritative span. */
    for (int si = 0; si < game.sim.people.shaft_count; si++) {
        ElevatorShaft *s = &game.sim.people.shafts[si];
        if (!s->active) continue;
        int fl_lo = s->lo + TOWER_MIN_FLOOR;
        int fl_hi = s->hi + TOWER_MIN_FLOOR;
        int ty = (int)(ground_line - (fl_hi + 1) * pf);
        int by = (int)(ground_line - fl_lo * pf);
        int tx = map_x + (s->x * map_w / TOWER_WIDTH);
        int tw = map_x + ((s->x + 4) * map_w / TOWER_WIDTH) - tx;
        uint8_t r, g, b;
        if (game.map_mode == 0) item_fallback_color(s->type, &r, &g, &b);
        else { r = 70; g = 70; b = 70; }        /* shell in overlays */
        SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
        SDL_Rect line = { tx + tw / 2, ty, 1, by - ty };
        SDL_RenderFillRect(game.renderer, &line);
    }

    /* Legend strip (the EXE blits bitmap 0x138+mode over the map). Anchor it to
     * the TOP of the map (sky) rather than the bottom — the lowest floors
     * (lobby + basements) are the densest part of the silhouette and the legend
     * was sitting right on top of them. */
    if (game.map_mode > 0) {
        Sprite *lg = sprites_find(&game.sprites, (uint16_t)(0x8138 + game.map_mode));
        if (lg) {
            int lw = lg->w * map_w / 200;       /* legends drawn for a 200px map */
            int lh = lg->h;
            SDL_Rect dst = { map_x + (map_w - lw) / 2, map_y + 2, lw, lh };
            SDL_RenderCopy(game.renderer, lg->texture, NULL, &dst);
        }
    }

    /* Mode buttons in the strip below the map content */
    {
        static const char *mode_label[4] = { "Map", "Eval", "Rent", "Hotel" };
        int bw = map_w / 4;
        for (int m = 0; m < 4; m++) {
            int bx = map_x + m * bw;
            int by = map_y + map_h + 2;
            /* 4th overlay is star-gated (unlocks at 2 stars) */
            int locked = (m == 3 && game.tower.star_rating < 2 &&
                          game.sim.mode != MODE_SANDBOX);
            draw_win31_rect(bx, by, bw - 2, 16, game.map_mode == m ? 0 : 1);
            stats_label(bx + 6, by + 2, mode_label[m],
                        locked ? (SDL_Color){ 140, 140, 140, 255 }
                               : (SDL_Color){ 0, 0, 0, 255 });
        }
    }
    
    /* Camera viewport indicator — tracks BOTH axes of the view */
    {
        int cam_floor_top, cam_floor_bot, cam_xl, cam_xr;
        screen_to_grid(0, HUD_HEIGHT + MENU_BAR_H, &cam_floor_top, &cam_xl);
        screen_to_grid(game.screen_w, game.screen_h, &cam_floor_bot, &cam_xr);

        int vt = (int)(ground_line - (cam_floor_top + 1) * pf);
        int vb = (int)(ground_line - cam_floor_bot * pf);
        int vl = map_x + (cam_xl * map_w / TOWER_WIDTH);
        int vr = map_x + (cam_xr * map_w / TOWER_WIDTH);
        if (vt < map_y) vt = map_y;
        if (vb > map_y + map_h) vb = map_y + map_h;
        if (vl < map_x) vl = map_x;
        if (vr > map_x + map_w) vr = map_x + map_w;

        /* Red rectangle with white inner for visibility */
        SDL_SetRenderDrawColor(game.renderer, 255, 0, 0, 255);
        SDL_Rect vp = { vl, vt, vr - vl, vb - vt };
        SDL_RenderDrawRect(game.renderer, &vp);
        SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 255);
        SDL_Rect vp2 = { vl + 1, vt + 1, vr - vl - 2, vb - vt - 2 };
        SDL_RenderDrawRect(game.renderer, &vp2);
    }
}

/* --- Toolbox Window --- */
/* The build tool selector with icons for each building type.
 * Stacked below the minimap on the LEFT (like original toolbox.rml). */

#define TOOL_BTN_SIZE 32    /* Match original 32×32 icon size */
#define TOOL_BTN_PAD  0     /* Original buttons abut edge-to-edge (bevel is in the art) */
#define TOOL_COLS   2       /* 2-column item palette — matches the original toolbox */

/* Tool button layout.
 * icon_idx = position in the items bitmap (from OpenSkyscraper Item headers).
 * -1 = no bitmap icon, use label text fallback. */
/* A toolbar slot. If sub_count == 0 it's a plain button that selects `type`.
 * If sub_count > 0 it's a GROUP: the button shows icon_idx (the primary, = sub[0]),
 * and click-and-hold pulls down a menu of the sub-items (faithful to the original
 * "click and hold for a pop-up menu" behavior). */
#define TOOL_SUB_MAX 4
typedef struct {
    const char *label;
    ItemType    type;
    int         icon_idx;
    uint8_t     r, g, b;  /* Fallback icon color */
    ItemType    sub[TOOL_SUB_MAX];
    int         sub_icon[TOOL_SUB_MAX];
    int         sub_count;
} ToolButton;

/* Icon indices — AUTHORITATIVE, from OpenSkyscraper's item prototypes (p->icon)
 * cross-checked against the dumped SIMTOWER.EXE sheet (res 0x812C, 26 icons,
 * row-major 8/row; the loader flattens to a 26-wide strip so icon_idx = flat
 * 0..25 cell):
 *  0=Lobby 1=Floor 2=Stairs 3=Escalator 4=Elevator(standard) 5=Elevator(service)
 *  6=Elevator(express) 7=Office 8=HotelSingle 9=HotelTwin 10=HotelSuite
 *  11=FastFood 12=Restaurant 13=Shop 14=Cinema 15=PartyHall 17=Parking
 *  18=Recycling 19=Metro 20=Cathedral 22=Medical 23=Housekeeping 24=Condo
 *  25=Security. (4/5/6 are the three elevator types; 24=Condo, not a generic
 *  room — confirmed by Jonah + OS Condo.h.) */
/* Plain (non-group) button — zero-fills the sub-menu fields. */
#define TB(lbl, ty, ic, r, g, b) { lbl, ty, ic, r, g, b, {0}, {0}, 0 }
/* Campaign gates items behind the star rating; Sandbox unlocks everything. */
static int item_unlocked(ItemType type)
{
    if (game.sim.mode == MODE_SANDBOX) return 1;
    if (type <= ITEM_NONE || type >= ITEM_TYPE_COUNT) return 1;
    return ITEM_STAR_REQ[type] <= game.tower.star_rating;
}

static const ToolButton tool_buttons[] = {
    TB("LOB",  ITEM_LOBBY,          0,  210, 200, 160),
    TB("FLR",  ITEM_FLOOR,          1,  200, 200, 190),
    TB("OFF",  ITEM_OFFICE,         7,  200, 200, 150),
    TB("CND",  ITEM_CONDO,         24,  180, 220, 180),
    /* Hotel group: single / twin / suite (click-and-hold) */
    { "HTL",  ITEM_HOTEL_SINGLE,   8,  150, 150, 220,
      { ITEM_HOTEL_SINGLE, ITEM_HOTEL_TWIN, ITEM_HOTEL_SUITE }, { 8, 9, 10 }, 3 },
    TB("FF",   ITEM_FAST_FOOD,     11,  220, 220, 100),
    TB("RST",  ITEM_RESTAURANT,    12,  220, 180, 150),
    TB("SHP",  ITEM_SHOP,          13,  220, 160, 220),
    TB("CIN",  ITEM_CINEMA,        14,   80,  60, 120),
    TB("PTY",  ITEM_PARTY_HALL,    15,  200, 100, 180),
    /* Steps group: stairs / escalator (click-and-hold) */
    { "STR",  ITEM_STAIRS,         2,  180, 175, 170,
      { ITEM_STAIRS, ITEM_ESCALATOR }, { 2, 3 }, 2 },
    /* Elevator group: standard / service / express (click-and-hold) */
    { "ELV",  ITEM_ELEVATOR_SHAFT, 4,  160, 170, 180,
      { ITEM_ELEVATOR_SHAFT, ITEM_ELEVATOR_SERVICE, ITEM_ELEVATOR_EXPRESS }, { 4, 5, 6 }, 3 },
    /* Parking group: spaces / ramp (click-and-hold). Spaces are gated on
     * a same-floor ramp, so the ramp rides along in the pull-down. */
    { "PKG",  ITEM_PARKING,       17,  160, 160, 160,
      { ITEM_PARKING, ITEM_RAMP }, { 17, 16 }, 2 },
    TB("RCY",  ITEM_RECYCLING,     18,  100, 180, 100),
    TB("MTR",  ITEM_METRO,         19,  100, 100, 120),
    TB("CTH",  ITEM_CATHEDRAL,     20,  230, 220, 200),
    TB("MED",  ITEM_MEDICAL,       22,  220, 240, 240),
    /* Services group: security / housekeeping (click-and-hold) */
    { "SEC",  ITEM_SECURITY,      25,  180, 180, 200,
      { ITEM_SECURITY, ITEM_HOUSEKEEPING }, { 25, 23 }, 2 },
};
#undef TB
#define TOOL_BTN_COUNT 18

/* Geometry helpers — single source of truth shared by render_toolbox,
 * toolbox_click and the pull-down popup so they never drift apart. */
static int tool_grid_origin_y(void)
{
    int wy = game.tool_y + WIN_TITLEBAR_H;
    int speed_y = wy + 8;
    int tools_y = speed_y + 28;
    return tools_y + 26;            /* grid_y */
}

/* A button is shown when it (or any of its sub-items) is currently unlocked. */
static int tool_button_shown(int i)
{
    if (i < 0 || i >= TOOL_BTN_COUNT) return 0;
    const ToolButton *tb = &tool_buttons[i];
    if (item_unlocked(tb->type)) return 1;
    for (int j = 0; j < tb->sub_count; j++)
        if (item_unlocked(tb->sub[j])) return 1;
    return 0;
}

/* Compacted grid slot for button i (skipping hidden buttons), or -1 if hidden.
 * The whole toolbox flows up to fill the gaps, so it grows with the stars. */
static int tool_visible_slot(int i)
{
    if (!tool_button_shown(i)) return -1;
    int slot = 0;
    for (int k = 0; k < i; k++) if (tool_button_shown(k)) slot++;
    return slot;
}

static int toolbox_visible_count(void)
{
    int n = 0;
    for (int i = 0; i < TOOL_BTN_COUNT; i++) if (tool_button_shown(i)) n++;
    return n;
}

/* Number of icon rows currently shown — drives the window height so the toolbox
 * physically grows and shrinks with the unlocked item count. */
static int tool_visible_rows(void)
{
    int r = (toolbox_visible_count() + TOOL_COLS - 1) / TOOL_COLS;
    return r < 1 ? 1 : r;
}

/* Toolbox window height for the current row count (title + speed + tools row +
 * icon grid + a cost strip). */
static int tool_win_height(void)
{
    int grid_off = tool_grid_origin_y() - game.tool_y;   /* top chrome height */
    return grid_off + tool_visible_rows() * (TOOL_BTN_SIZE + TOOL_BTN_PAD) + 20;
}

/* The j-th VISIBLE (unlocked) sub-item of group i, or -1. */
static int tool_sub_visible_index(int i, int vis_j)
{
    const ToolButton *g = &tool_buttons[i];
    int k = 0;
    for (int j = 0; j < g->sub_count; j++)
        if (item_unlocked(g->sub[j]) && k++ == vis_j) return j;
    return -1;
}

static int tool_sub_visible_count(int i)
{
    const ToolButton *g = &tool_buttons[i];
    int n = 0;
    for (int j = 0; j < g->sub_count; j++) if (item_unlocked(g->sub[j])) n++;
    return n;
}

static void tool_button_rect(int i, int *bx, int *by)
{
    int slot = tool_visible_slot(i);
    if (slot < 0) { *bx = *by = -10000; return; }   /* hidden: off-screen */
    int col = slot % TOOL_COLS, row = slot / TOOL_COLS;
    int margin = (TOOL_WIN_W - TOOL_COLS * (TOOL_BTN_SIZE + TOOL_BTN_PAD)) / 2;
    *bx = game.tool_x + margin + col * (TOOL_BTN_SIZE + TOOL_BTN_PAD);
    *by = tool_grid_origin_y() + row * (TOOL_BTN_SIZE + TOOL_BTN_PAD);
}

/* Rect of sub-item j (0-based) in the pull-down for group button i — a vertical
 * column directly below the group button. */
static void tool_sub_rect(int i, int j, int *bx, int *by)
{
    int gx, gy;
    tool_button_rect(i, &gx, &gy);
    *bx = gx;
    *by = gy + (j + 1) * (TOOL_BTN_SIZE + TOOL_BTN_PAD);
}

/* Draw one 32×32 item icon (from ui_items) with a raised/sunken 3D border. */
static void draw_tool_icon(int bx, int by, int icon_idx, int selected)
{
    if (!selected) {
        SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(game.renderer, bx, by, bx + TOOL_BTN_SIZE - 1, by);
        SDL_RenderDrawLine(game.renderer, bx, by, bx, by + TOOL_BTN_SIZE - 1);
        SDL_SetRenderDrawColor(game.renderer, 128, 128, 128, 255);
        SDL_RenderDrawLine(game.renderer, bx, by + TOOL_BTN_SIZE - 1, bx + TOOL_BTN_SIZE - 1, by + TOOL_BTN_SIZE - 1);
        SDL_RenderDrawLine(game.renderer, bx + TOOL_BTN_SIZE - 1, by, bx + TOOL_BTN_SIZE - 1, by + TOOL_BTN_SIZE - 1);
    } else {
        SDL_SetRenderDrawColor(game.renderer, 128, 128, 128, 255);
        SDL_RenderDrawLine(game.renderer, bx, by, bx + TOOL_BTN_SIZE - 1, by);
        SDL_RenderDrawLine(game.renderer, bx, by, bx, by + TOOL_BTN_SIZE - 1);
        SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(game.renderer, bx, by + TOOL_BTN_SIZE - 1, bx + TOOL_BTN_SIZE - 1, by + TOOL_BTN_SIZE - 1);
        SDL_RenderDrawLine(game.renderer, bx + TOOL_BTN_SIZE - 1, by, bx + TOOL_BTN_SIZE - 1, by + TOOL_BTN_SIZE - 1);
    }
    if (game.ui_items && icon_idx >= 0) {
        int off = selected ? 1 : 0;
        SDL_Rect src = { icon_idx * 32, selected ? 32 : 0, 32, 32 };
        SDL_Rect dst = { bx + off, by + off, TOOL_BTN_SIZE, TOOL_BTN_SIZE };
        SDL_RenderCopy(game.renderer, game.ui_items, &src, &dst);
    }
}

/* Solid triangle hugging the lower-right CORNER of a group (pull-down) button
 * — the right angle sits in the corner (◢), so it points into the corner
 * rather than up into the button face. */
static void draw_pulldown_marker(int bx, int by)
{
    int x1 = bx + TOOL_BTN_SIZE - 2;   /* corner: right edge */
    int y1 = by + TOOL_BTN_SIZE - 2;   /* corner: bottom edge */
    int T  = 6;
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
    /* Fill rows from the bottom up; each row up shrinks from the left, leaving
     * a hypotenuse from (x1-T, y1) to (x1, y1-T) and a solid bottom-right. */
    for (int k = 0; k <= T; k++) {
        SDL_RenderDrawLine(game.renderer, x1 - (T - k), y1 - k, x1, y1 - k);
    }
}

/* Render the open pull-down menu (if any) on top of the toolbox. */
static void render_tool_popup(void)
{
    int i = game.tool_popup;
    if (i < 0 || i >= TOOL_BTN_COUNT) return;
    const ToolButton *g = &tool_buttons[i];
    if (g->sub_count <= 0) return;
    int nvis = tool_sub_visible_count(i);   /* only unlocked subs, compacted */
    if (nvis <= 0) return;

    /* Sub-items use the same button-face grey as the toolbar (they're the same
     * button bitmaps in the original) — a pull-down menu is distinguished by a
     * drop-shadow, not a different fill colour. Draw a soft shadow behind the
     * whole column first so it reads as a floating menu. */
    int x0, y0, xn, yn;
    tool_sub_rect(i, 0, &x0, &y0);
    tool_sub_rect(i, nvis - 1, &xn, &yn);
    SDL_SetRenderDrawColor(game.renderer, 64, 64, 64, 255);
    SDL_Rect shadow = { x0 + 2, y0 + 2, TOOL_BTN_SIZE + 2, (yn - y0) + TOOL_BTN_SIZE + 2 };
    SDL_RenderFillRect(game.renderer, &shadow);

    for (int vj = 0; vj < nvis; vj++) {
        int j = tool_sub_visible_index(i, vj);
        if (j < 0) continue;
        int bx, by;
        tool_sub_rect(i, vj, &bx, &by);
        SDL_SetRenderDrawColor(game.renderer, 192, 192, 192, 255);
        SDL_Rect bg = { bx - 1, by - 1, TOOL_BTN_SIZE + 2, TOOL_BTN_SIZE + 2 };
        SDL_RenderFillRect(game.renderer, &bg);
        draw_tool_icon(bx, by, g->sub_icon[j], g->sub[j] == game.build_type);
    }
}

static void render_toolbox(void)
{
    int wx = game.tool_x;
    int wy = game.tool_y;
    
    /* Title bar for dragging */
    draw_win31_titlebar(wx, wy, TOOL_WIN_W, "Tools");
    wy += WIN_TITLEBAR_H;
    
    /* Toolbox body */
    draw_win31_rect(wx, wy, TOOL_WIN_W, tool_win_height() - WIN_TITLEBAR_H, 1);
    
    /* Speed buttons at top. The original toolbar has just TWO buttons — Play and
     * Pause — each with a normal + pressed state (verified by dumping the EXE):
     *   0x8258 play-normal, 0x8259 play-pressed, 0x825A pause-normal, 0x825B
     *   pause-pressed. The loader packs these into ui_speed (128×64): play at
     *   src x=0, pause at x=64; normal row y=0, pressed row y=32. Play shows
     *   pressed while the sim runs (speed>0); Pause shows pressed while stopped.
     *   (Multi-speed 1x/2x/3x is via menu/keyboard, not the toolbar.) */
    int speed_y = wy + 8;
    {
        /* ONE wide play/pause toggle (faithful to the original toolbox — Jonah's
         * reference shot shows a single button). While running it shows the
         * PAUSE icon (click to pause); while paused it shows PLAY (click to
         * resume). Fine speed (1x/2x/3x) lives in the Speed menu. */
        int bw = SPEED_BTN_W, bh = SPEED_BTN_H;
        int sx = wx + (TOOL_WIN_W - bw) / 2;
        int playing = (game.sim.speed > 0);
        if (game.ui_speed) {
            int icon_x = playing ? 64 : 0;        /* pause : play (64px cells) */
            SDL_Rect src = { icon_x, 0, 64, 32 };
            SDL_Rect dst = { sx, speed_y, bw, bh };
            SDL_RenderCopy(game.renderer, game.ui_speed, &src, &dst);
        }
    }
    
    /* Tool action buttons (bulldozer, finger, inspector) — between speed and items. */
    int tools_y = speed_y + 28;
    if (game.ui_tools) {
        /* ui_tools is 64×63: each source bitmap (0x825C/D/E = normal/pressed/
         * disabled) holds all 3 tools laid out HORIZONTALLY (bulldozer, finger,
         * inspector at x=0/21/42) and is stacked into rows 0/21/42. So tool t =
         * (x=t*21, normal row y=0). */
        int tx = wx + (TOOL_WIN_W - 3 * 21) / 2;
        for (int t = 0; t < 3; t++) {
            /* Each tool draws pressed while its mode is active. */
            int pressed = (t == 0 && game.demolish_mode) ||
                          (t == 1 && game.finger_mode) ||
                          (t == 2 && game.inspect_mode);
            int row_y = pressed ? 21 : 0;
            SDL_Rect src = { t * 21, row_y, 21, 21 };
            SDL_Rect dst = { tx + t * 23, tools_y, 21, 21 };
            SDL_RenderCopy(game.renderer, game.ui_tools, &src, &dst);
        }
    }
    
    /* Item buttons grid — hidden (locked) buttons are skipped, the rest flow up. */
    int grid_y = tools_y + 26;
    for (int i = 0; i < TOOL_BTN_COUNT; i++) {
        if (tool_visible_slot(i) < 0) continue;   /* locked in Campaign */
        int bx, by;
        tool_button_rect(i, &bx, &by);
        const ToolButton *tb = &tool_buttons[i];

        /* A group button stays highlighted when any of its sub-items is active,
         * and it shows the icon of the CURRENTLY SELECTED sub-item (so picking
         * "Restaurant" from the food group updates the button face) — falling
         * back to the primary icon when none of its subs is active. */
        int selected = (tb->type == game.build_type);
        int show_icon = tb->icon_idx;
        for (int j = 0; j < tb->sub_count; j++)
            if (tb->sub[j] == game.build_type) {
                selected = 1;
                show_icon = tb->sub_icon[j];
            }

        if (game.ui_items && show_icon >= 0) {
            draw_tool_icon(bx, by, show_icon, selected);
        } else if (game.font_small) {
            /* Fallback: colored square + text label */
            SDL_SetRenderDrawColor(game.renderer, tb->r, tb->g, tb->b, 255);
            SDL_Rect btn = { bx + 2, by + 2, TOOL_BTN_SIZE - 4, TOOL_BTN_SIZE - 4 };
            SDL_RenderFillRect(game.renderer, &btn);
            SDL_Color black = {0, 0, 0, 255};
            SDL_Surface *ts = TTF_RenderText_Blended(game.font_small, tb->label, black);
            if (ts) {
                SDL_Texture *tt = SDL_CreateTextureFromSurface(game.renderer, ts);
                SDL_Rect dst = { bx + TOOL_BTN_SIZE/2 - ts->w/2,
                                 by + TOOL_BTN_SIZE/2 - ts->h/2, ts->w, ts->h };
                SDL_RenderCopy(game.renderer, tt, NULL, &dst);
                SDL_DestroyTexture(tt);
                SDL_FreeSurface(ts);
            }
        }

        /* Pull-down marker only when there's an actual choice — a group with
         * just one unlocked sub-item acts as a plain button (no arrow/menu). */
        if (tool_sub_visible_count(i) > 1) draw_pulldown_marker(bx, by);
    }
    
    /* Cost display at bottom */
    if (game.build_type != ITEM_NONE && game.font_small) {
        SDL_Color black = {0, 0, 0, 255};
        char cost_buf[64];
        format_money(ITEM_COST[game.build_type], cost_buf, sizeof(cost_buf));
        char full_buf[96];
        snprintf(full_buf, sizeof(full_buf), "%s  %s",
                 tower_item_name(game.build_type), cost_buf);
        
        /* Place below the icon grid, which shrinks/grows with the unlocked set. */
        int rows = tool_visible_rows();
        int label_y = grid_y + rows * (TOOL_BTN_SIZE + TOOL_BTN_PAD) + 4;
        SDL_Surface *ts = TTF_RenderText_Blended(game.font_small, full_buf, black);
        if (ts) {
            SDL_Texture *tt = SDL_CreateTextureFromSurface(game.renderer, ts);
            int dw = ts->w > TOOL_WIN_W - 8 ? TOOL_WIN_W - 8 : ts->w;
            SDL_Rect src2 = { 0, 0, dw, ts->h };
            SDL_Rect dst = { wx + 4, label_y, dw, ts->h };
            SDL_RenderCopy(game.renderer, tt, &src2, &dst);
            SDL_DestroyTexture(tt);
            SDL_FreeSurface(ts);
        }
    }

    /* Pull-down menu (if a group button is open) draws last, on top of all else. */
    render_tool_popup();
}

/* Draw a Win3.1-style raised/sunken rectangle */
static void draw_win31_rect(int x, int y, int w, int h, int raised)
{
    uint8_t hi_r = raised ? 255 : 128, hi_g = raised ? 255 : 128, hi_b = raised ? 255 : 128;
    uint8_t lo_r = raised ? 128 : 255, lo_g = raised ? 128 : 255, lo_b = raised ? 128 : 255;
    
    /* Fill */
    SDL_SetRenderDrawColor(game.renderer, WIN31_BG, 255);
    SDL_Rect bg = { x, y, w, h };
    SDL_RenderFillRect(game.renderer, &bg);
    
    /* Top + left highlight */
    SDL_SetRenderDrawColor(game.renderer, hi_r, hi_g, hi_b, 255);
    SDL_RenderDrawLine(game.renderer, x, y, x + w - 1, y);
    SDL_RenderDrawLine(game.renderer, x, y, x, y + h - 1);
    
    /* Bottom + right shadow */
    SDL_SetRenderDrawColor(game.renderer, lo_r, lo_g, lo_b, 255);
    SDL_RenderDrawLine(game.renderer, x, y + h - 1, x + w - 1, y + h - 1);
    SDL_RenderDrawLine(game.renderer, x + w - 1, y, x + w - 1, y + h - 1);
}

/* Draw a text string in the small font for menus */
/* state: 0 = normal, 1 = selected/hover, 2 = disabled (greyed) */
static int draw_menu_text(const char *text, int x, int y, int state)
{
    if (!game.font_small || !text) return 0;
    SDL_Color color;
    if (state == 2)      { color = (SDL_Color){WIN31_DISABLED, 255}; }
    else if (state == 1) { color = (SDL_Color){WIN31_SEL_TEXT, 255}; }
    else                 { color = (SDL_Color){WIN31_TEXT, 255}; }
    SDL_Surface *surf = TTF_RenderUTF8_Blended(game.font_small, text, color);
    if (!surf) return 0;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(game.renderer, surf);
    SDL_Rect dst = { x, y, surf->w, surf->h };
    SDL_RenderCopy(game.renderer, tex, NULL, &dst);
    int w = surf->w;
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
    return w;
}

static void render_menu_bar(void)
{
    /* Win3.1 menu bar: gray with raised border */
    int bar_y = HUD_HEIGHT;
    draw_win31_rect(0, bar_y, game.screen_w, MENU_BAR_H, 1);
    
    /* Draw top-level menu labels */
    int cx = 8;
    for (int i = 0; i < TOP_MENU_COUNT; i++) {
        int tw = (int)strlen(top_menus[i].label) * 7 + MENU_ITEM_PAD * 2;
        
        int is_active = (game.menu_open == i);
        int is_hover = (game.menu_bar_hover == i && game.menu_open < 0);
        
        if (is_active) {
            /* Sunken look when menu is open */
            SDL_SetRenderDrawColor(game.renderer, WIN31_SEL_BG, 255);
            SDL_Rect sel = { cx, bar_y + 1, tw, MENU_BAR_H - 2 };
            SDL_RenderFillRect(game.renderer, &sel);
            draw_menu_text(top_menus[i].label, cx + MENU_ITEM_PAD, bar_y + 3, 1);
        } else if (is_hover) {
            /* Slight highlight on hover */
            SDL_SetRenderDrawColor(game.renderer, 220, 220, 220, 255);
            SDL_Rect hl = { cx, bar_y + 1, tw, MENU_BAR_H - 2 };
            SDL_RenderFillRect(game.renderer, &hl);
            draw_menu_text(top_menus[i].label, cx + MENU_ITEM_PAD, bar_y + 3, 0);
        } else {
            draw_menu_text(top_menus[i].label, cx + MENU_ITEM_PAD, bar_y + 3, 0);
        }
        cx += tw;
    }
}

static void render_dropdown(void)
{
    if (game.menu_open < 0 || game.menu_open >= TOP_MENU_COUNT) return;
    
    const TopMenu *tm = &top_menus[game.menu_open];
    
    /* Calculate dropdown position */
    int mx, my, mw, mh;
    get_top_menu_rect(game.menu_open, &mx, &my, &mw, &mh);
    
    int drop_x = mx;
    int drop_y = my + MENU_BAR_H;
    int drop_w = 180;
    int item_h = 18;
    int sep_h = 6;
    
    /* Calculate total dropdown height */
    int drop_h = 4; /* top/bottom padding */
    for (int i = 0; i < tm->count; i++) {
        drop_h += tm->items[i].label ? item_h : sep_h;
    }
    
    /* Draw dropdown shadow */
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 80);
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect shadow = { drop_x + 3, drop_y + 3, drop_w, drop_h };
    SDL_RenderFillRect(game.renderer, &shadow);
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
    
    /* Draw dropdown background with Win3.1 raised border */
    draw_win31_rect(drop_x, drop_y, drop_w, drop_h, 1);
    
    /* Draw items */
    int iy = drop_y + 2;
    for (int i = 0; i < tm->count; i++) {
        if (!tm->items[i].label) {
            /* Separator: thin sunken line */
            SDL_SetRenderDrawColor(game.renderer, 128, 128, 128, 255);
            SDL_RenderDrawLine(game.renderer, drop_x + 4, iy + sep_h/2,
                              drop_x + drop_w - 4, iy + sep_h/2);
            SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 255);
            SDL_RenderDrawLine(game.renderer, drop_x + 4, iy + sep_h/2 + 1,
                              drop_x + drop_w - 4, iy + sep_h/2 + 1);
            iy += sep_h;
            continue;
        }
        
        int is_hover = (game.menu_hover == i);
        
        if (is_hover) {
            SDL_SetRenderDrawColor(game.renderer, WIN31_SEL_BG, 255);
            SDL_Rect sel = { drop_x + 2, iy, drop_w - 4, item_h };
            SDL_RenderFillRect(game.renderer, &sel);
        }
        
        /* Check mark for current speed or active build type */
        int checked = 0;
        if (tm->items[i].build_type != ITEM_NONE && 
            tm->items[i].build_type == game.build_type) checked = 1;
        if (tm->items[i].action == ACT_SPEED_PAUSE && game.sim.speed == SPEED_PAUSED) checked = 1;
        if (tm->items[i].action == ACT_SPEED_1 && game.sim.speed == SPEED_NORMAL) checked = 1;
        if (tm->items[i].action == ACT_SPEED_2 && game.sim.speed == SPEED_FAST) checked = 1;
        if (tm->items[i].action == ACT_SPEED_3 && game.sim.speed == SPEED_TURBO) checked = 1;
        if (tm->items[i].action == ACT_DEBUG_TOGGLE && game.show_debug) checked = 1;
        if (tm->items[i].action == ACT_MODE_CAMPAIGN && game.sim.mode == MODE_CAMPAIGN) checked = 1;
        if (tm->items[i].action == ACT_MODE_SANDBOX && game.sim.mode == MODE_SANDBOX) checked = 1;
        if (tm->items[i].action == ACT_WIN_TOOLBAR && game.win_toolbar) checked = 1;
        if (tm->items[i].action == ACT_WIN_INFOBAR && game.win_infobar) checked = 1;
        if (tm->items[i].action == ACT_WIN_MAP && game.win_map) checked = 1;
        if (tm->items[i].action == ACT_ANIM_PEOPLE && game.anim_people) checked = 1;
        if (tm->items[i].action == ACT_ANIM_EFFECTS && game.anim_effects) checked = 1;
        if (tm->items[i].action == ACT_SND_ELEV && game.snd_elev) checked = 1;
        if (tm->items[i].action == ACT_SND_BG && game.snd_bg) checked = 1;
        if (tm->items[i].action == ACT_SND_EVENTS && game.snd_events) checked = 1;
        
        if (checked) {
            draw_menu_text("\xe2\x9c\x93", drop_x + 6, iy + 2, is_hover); /* ✓ */
        }
        
        /* Split label and shortcut at tab character */
        char label_buf[64], shortcut_buf[32];
        const char *tab = strchr(tm->items[i].label, '\t');
        if (tab) {
            int llen = (int)(tab - tm->items[i].label);
            if (llen > 63) llen = 63;
            memcpy(label_buf, tm->items[i].label, llen);
            label_buf[llen] = '\0';
            strncpy(shortcut_buf, tab + 1, 31);
            shortcut_buf[31] = '\0';
        } else {
            strncpy(label_buf, tm->items[i].label, 63);
            label_buf[63] = '\0';
            shortcut_buf[0] = '\0';
        }
        
        /* A build item that's still locked in Campaign greys out. */
        int locked = (tm->items[i].build_type != ITEM_NONE &&
                      !item_unlocked(tm->items[i].build_type));
        int tstate = locked ? 2 : is_hover;
        draw_menu_text(label_buf, drop_x + 22, iy + 2, tstate);
        if (shortcut_buf[0]) {
            /* Right-align shortcut */
            int sw = (int)strlen(shortcut_buf) * 7;
            draw_menu_text(shortcut_buf, drop_x + drop_w - sw - 8, iy + 2, tstate);
        }
        
        iy += item_h;
    }
}

static void render_ui(void)
{
    /* No separate top HUD — the info bar window handles all of it.
     * The original SimTower has no top status bar either. */
    
    /* Update window title with current state (for VNC title bar) */
    char title[256];
    snprintf(title, sizeof(title),
             "ConcilliaTower | %s | $%ld | %d★ | Pop: %d | Day %d | Build: %s",
             game.sim.mode == MODE_SANDBOX ? "Sandbox" : "Campaign",
             game.tower.money, game.tower.star_rating, game.tower.population,
             game.tower.day, tower_item_name(game.build_type));
    SDL_SetWindowTitle(game.window, title);
    
    /* Sub-windows (matching original SimTower layout); each can be hidden
     * from the Windows menu (original ids 40014-40016) */
    if (game.win_map)     render_minimap();      /* Top left */
    if (game.win_toolbar) render_toolbox();      /* Left, below map */
    if (game.win_infobar) render_info_window();  /* Top right, horizontal strip */
}

/* Construction crane — the EXE's real rule (OverlayT seg_11c0).
 * UpdateCrane (11c0:024a): the highest floor with floor-map records;
 * only when that floor CHANGES is the crane re-evaluated: a top floor
 * narrower than 7 cells gets no crane; otherwise the crane parks at
 * the extent's LEFT edge and stays there even if the floor later
 * grows sideways — the original's famous stuck crane. Draw gate
 * (11c0:0000): crane floor must be below floor 100 (file floor 0x6E),
 * so a topped-out tower or the cathedral crown hides it. 36x36, one
 * row above, no centering. Drawn after the people/elevator pass so it
 * tops the machinery caps. Uses the extents pass 4 computed. */
static void render_crane(void)
{
    int top_fi = -1;
    for (int fi = TOWER_FLOOR_COUNT - 1; fi >= 0; fi--)
        if (ovl_right[fi] > 0) { top_fi = fi; break; }

    if (top_fi < 0) {
        game.crane_floor = CRANE_NONE;
    } else {
        int top = index_to_floor(top_fi);
        if (game.crane_floor != top) {
            if (ovl_right[top_fi] - ovl_left[top_fi] < 7)
                game.crane_floor = CRANE_NONE;
            else { game.crane_floor = top; game.crane_x = ovl_left[top_fi]; }
        }
    }

    if (game.crane && game.crane_floor != CRANE_NONE &&
        game.crane_floor < TOWER_MAX_FLOOR) {
        int tx, ty;
        grid_to_screen(game.crane_floor + 1, game.crane_x, &tx, &ty);
        SDL_Rect crane_dst = { tx, ty, game.crane->w, game.crane->h };
        SDL_RenderCopy(game.renderer, game.crane->texture, NULL, &crane_dst);
    }

    /* Wedding procession: bride, groom and guests walk the cathedral's
     * entrance floor through the morning and stand for the ceremony. */
    if (game.sim.wedding.active) {
        Sprite *proc = sprites_find(&game.sprites, 0x8828);
        const Tenant *cath = NULL;
        for (int i = 0; i < game.tower.tenant_count; i++)
            if (game.tower.tenants[i].type == ITEM_CATHEDRAL) {
                cath = &game.tower.tenants[i];
                break;
            }
        if (proc && cath) {
            int minutes = (game.sim.hour - 7) * 60 + game.sim.minute;
            float walk = minutes / 240.0f;          /* arrive by 11am */
            if (walk < 0.0f) walk = 0.0f;
            if (walk > 1.0f) walk = 1.0f;
            int door_x = cath->x * CELL_W + (cath->width * CELL_W - proc->w) / 2;
            int start_x = cath->x * CELL_W - proc->w - 80;
            int wx = start_x + (int)((door_x - start_x) * walk);
            int tx, ty;
            grid_to_screen(cath->floor, 0, &tx, &ty);
            SDL_Rect dst = { tx + wx, ty, proc->w, CELL_H };
            SDL_RenderCopy(game.renderer, proc->texture, NULL, &dst);
        }
    }
}

/* Fire/bomb visual effects drawn IN the tower (on top of the burning floors,
 * so they aren't painted over by building facades). Real EXE art: animated
 * flame (0x8F68-0x8F6B) tiled across the burning span; terror-alert icon over
 * a bomb target. Falls back to the old colored shapes if a sprite is missing. */
static void render_events(void)
{
    if (!game.sim.event.active) return;

    int lobby_sx, lobby_sy;
    grid_to_screen(0, 0, &lobby_sx, &lobby_sy);
    int evt_floor = game.sim.event.target_floor;
    int floor_y = lobby_sy - (evt_floor * CELL_H);

    if (game.sim.event.type == EVENT_FIRE) {
        /* Per-floor fronts (FireT): each active front is one 96x36 flame
         * strip (12 cells) at its position — the burned-out span between
         * the fronts shows as rubble via the tenants' burned flags, not
         * as a wall of flame. 4-frame animation (frame = b3de % 4). */
        Sprite *flame = game.fire_frames[(game.sim.frame / 3) % 4];
        for (int fi = 0; fi < TOWER_FLOOR_COUNT; fi++) {
            int16_t fl = game.sim.event.fire_left[fi];
            int16_t fr = game.sim.event.fire_right[fi];
            if (fl < 0 && fr < 0) continue;
            int fy = lobby_sy - (index_to_floor(fi) * CELL_H);
            for (int front = 0; front < 2; front++) {
                int cell = front == 0 ? fl : fr;
                if (cell < 0) continue;
                if (front == 1 && fr == fl) continue;   /* fresh ignition: one strip */
                int fx = lobby_sx + cell * CELL_W;
                if (flame && flame->texture) {
                    SDL_Rect dst = { fx, fy + CELL_H - flame->h,
                                     flame->w, flame->h };
                    SDL_RenderCopy(game.renderer, flame->texture, NULL, &dst);
                } else {
                    int flicker = (game.sim.frame % 6 < 3) ? 200 : 255;
                    SDL_SetRenderDrawColor(game.renderer, flicker, flicker/4, 0, 120);
                    SDL_Rect fire_rect = { fx, fy, FIRE_FRONT_CELLS * CELL_W, CELL_H };
                    SDL_RenderFillRect(game.renderer, &fire_rect);
                }
            }
        }

        /* The firefighting helicopter (0x8F6D) — the real thing: paid for
         * with the $500k offer, sweeping right-to-left above the origin
         * floor, dousing every front to its right (10e8:0450/0856). */
        Sprite *heli = game.fire_chopper;
        if (game.sim.event.chopper_x > 0) {
            int hx = lobby_sx + game.sim.event.chopper_x * CELL_W;
            int bob = (game.sim.frame % 16 < 8) ? 0 : 2;
            int hy = floor_y - 2 * CELL_H + bob;
            if (heli && heli->texture) {
                SDL_Rect hd = { hx, hy - heli->h, heli->w, heli->h };
                SDL_RenderCopy(game.renderer, heli->texture, NULL, &hd);
                /* water streaming down onto the floors below */
                SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(game.renderer, 90, 160, 230, 200);
                for (int d = 0; d < 3; d++) {
                    int dy = (game.sim.frame * 5 + d * 17) % (2 * CELL_H);
                    SDL_Rect drop = { hx + heli->w / 2 - 6 + d * 6,
                                      hy + dy, 2, 6 };
                    SDL_RenderFillRect(game.renderer, &drop);
                }
                SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
            }
        }
    } else if (game.sim.event.type == EVENT_BOMB) {
        /* The hunt: every materialized guard, sweeping right-to-left.
         * Small dark-uniform figures; invisible while in floor transit. */
        for (int oi = 0; oi < game.sim.event.hunt.noffices; oi++) {
            const GuardOffice *o = &game.sim.event.hunt.o[oi];
            for (int gi = 0; gi < GUARDS_PER_OFFICE; gi++) {
                const GuardState *g = &o->g[gi];
                if (g->retired || g->transit > 0 || g->x < 0) continue;
                int gx = lobby_sx + g->x * CELL_W;
                int gy = lobby_sy - (g->floor * CELL_H);
                SDL_SetRenderDrawColor(game.renderer, 20, 20, 120, 255);
                SDL_Rect body = { gx + 2, gy + CELL_H - 12, 4, 10 };
                SDL_RenderFillRect(game.renderer, &body);
                SDL_SetRenderDrawColor(game.renderer, 230, 200, 160, 255);
                SDL_Rect head = { gx + 3, gy + CELL_H - 15, 2, 3 };
                SDL_RenderFillRect(game.renderer, &head);
            }
        }
        int bx = lobby_sx + game.sim.event.target_slot * CELL_W;
        Sprite *al = game.alert_terror;
        if (al && al->texture) {
            int bob = (game.sim.frame % 24 < 12) ? 0 : 2;
            SDL_Rect dst = { bx + CELL_W/2 - al->w/2,
                             floor_y - al->h - 2 + bob, al->w, al->h };
            SDL_RenderCopy(game.renderer, al->texture, NULL, &dst);
        } else {
            int pulse = 60 + (game.sim.frame % 20) * 4;
            if (pulse > 120) pulse = 180 - pulse;
            SDL_SetRenderDrawColor(game.renderer, 255, 0, 0, pulse);
            SDL_Rect bomb_rect = { bx - 16, floor_y - 8, 32, CELL_H + 16 };
            SDL_RenderFillRect(game.renderer, &bomb_rect);
        }
    }
}

/* Top-center disaster alert: real EXE alert icon + blinking label while an
 * event runs. Mirrors SimTower popping an alert when a fire/bomb strikes
 * (alert sprites 0xA714 fire / 0xA710 terrorist, decoded from EventT/FireT). */
/* ---------- Disaster decision modal (EventT dialog) ----------
 * When a disaster is proposed (sim->event.pending), the game pauses and this
 * modal asks the player what to do. Both are real paid choices in the EXE:
 * a fire offers firefighting helicopters for $500,000 (10e8:0147 — decline
 * and it burns until it hits the floor edges, or 9PM); a bomb threat can be
 * paid off at a star-scaled ransom, or security hunts it until 1PM. */

#define DMODAL_W 360
/* Fire carries two EXE texts (report 0xBC3 + crew offer 0xBC4) and needs
 * the taller card. */
#define DMODAL_H (game.sim.event.type == EVENT_FIRE ? 216 : 156)

static void disaster_modal_origin(int *x, int *y)
{
    *x = (game.screen_w - DMODAL_W) / 2;
    *y = (game.screen_h - DMODAL_H) / 2;
}

/* Bottom-row buttons. idx 0 = left, idx 1 = right. Fire uses idx 1 only. */
static SDL_Rect disaster_btn_rect(int idx)
{
    int wx, wy;
    disaster_modal_origin(&wx, &wy);
    int bw = 150, bh = 26;
    int by = wy + DMODAL_H - bh - 12;
    int left  = wx + 18;
    int right = wx + DMODAL_W - bw - 18;
    SDL_Rect r = { idx == 0 ? left : right, by, bw, bh };
    return r;
}

static int point_in_rect(int x, int y, SDL_Rect r)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void draw_modal_button(SDL_Rect r, const char *label)
{
    int hot = point_in_rect(game.mouse_x, game.mouse_y, r);
    draw_win31_rect(r.x, r.y, r.w, r.h, hot ? 0 : 1);
    SDL_Color fg = { 0, 0, 0, 255 };
    int tw, th;
    SDL_Texture *t = render_text(label, fg, &tw, &th);
    if (t) {
        SDL_Rect d = { r.x + (r.w - tw) / 2, r.y + (r.h - th) / 2, tw, th };
        SDL_RenderCopy(game.renderer, t, NULL, &d);
        SDL_DestroyTexture(t);
    }
}

static void render_disaster_modal(void)
{
    if (!game.disaster_modal) return;
    int is_fire = (game.sim.event.type == EVENT_FIRE);

    /* Dim the world behind the modal. */
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 150);
    SDL_Rect full = { 0, 0, game.screen_w, game.screen_h };
    SDL_RenderFillRect(game.renderer, &full);
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);

    int wx, wy;
    disaster_modal_origin(&wx, &wy);
    draw_win31_titlebar(wx, wy, DMODAL_W, is_fire ? "Fire!" : "Bomb Threat!");
    int body_y = wy + WIN_TITLEBAR_H;
    draw_win31_rect(wx, body_y, DMODAL_W, DMODAL_H - WIN_TITLEBAR_H, 1);

    /* Alert icon (reuse the in-world alert sprites). */
    Sprite *icon = is_fire ? game.alert_fire : game.alert_terror;
    int text_x = wx + 16;
    if (icon && icon->texture) {
        SDL_Rect d = { wx + 14, body_y + 14, icon->w, icon->h };
        SDL_RenderCopy(game.renderer, icon->texture, NULL, &d);
        text_x = wx + 14 + icon->w + 12;
    }

    SDL_Color black = { 0, 0, 0, 255 };
    char line[192], num[24];
    int ty = body_y + 16;
    int text_w = wx + DMODAL_W - 14 - text_x;
    if (is_fire) {
        /* The EXE's own words: outbreak report (dialog 0xBC3, ^0 = floor)
         * + the fire-crew offer (0xBC4, $#000 = the fee). */
        char rep[192];
        snprintf(num, sizeof num, "%d", game.sim.event.target_floor);
        str_subst(rep, sizeof rep,
                  exe_dlg_text(0xBC3, 0, "A fire has been reported on floor "
                               "^0!\nEveryone should take emergency refuge!"),
                  "^0", num);
        ty = draw_text_wrapped(rep, text_x, ty, text_w, black) + 6;
        format_money(FIRE_CHOPPER_COST, num, sizeof num);
        str_subst(line, sizeof line,
                  exe_dlg_text(0xBC4, 0, "Would you like to call an emergency "
                               "fire crew?\nIt will cost $#000."),
                  "$#000", num);
        draw_text_wrapped(line, text_x, ty, text_w, black);
    } else {
        /* Dialog 0xBCC: the blackmail note names the price but NOT the
         * floor — you pay blind. */
        format_money(game.sim.event.ransom_cost, num, sizeof num);
        str_subst(line, sizeof line,
                  exe_dlg_text(0xBCC, 2, "Blackmail from Terrorists!\nThey "
                               "demand $#000 or a hidden bomb will explode "
                               "at 3 o'clock."),
                  "$#000", num);
        draw_text_wrapped(line, text_x, ty, text_w, black);
    }

    if (is_fire) {
        /* 0xBC4's buttons are Yes/No; keep which-is-which explicit. */
        draw_modal_button(disaster_btn_rect(0),
                          exe_dlg_text(0xBC4, 2, "No"));
        draw_modal_button(disaster_btn_rect(1),
                          exe_dlg_text(0xBC4, 1, "Yes"));
    } else {
        draw_modal_button(disaster_btn_rect(0),
                          exe_dlg_text(0xBCC, 0, "Find the Bomb"));
        draw_modal_button(disaster_btn_rect(1),
                          exe_dlg_text(0xBCC, 1, "Pay Them"));
    }
}

static void disaster_close(void)
{
    game.disaster_modal = 0;
    game.sim.speed = game.disaster_saved_speed;
}

/* The free path: let the fire burn / send guards after the bomb. */
static void disaster_do_proceed(void)
{
    int is_fire = (game.sim.event.type == EVENT_FIRE);
    int fl = game.sim.event.target_floor;
    game_event_proceed(&game.sim, &game.tower);
    char b[64];
    if (is_fire) snprintf(b, sizeof b, "FIRE on floor %d - burning freely!", fl);
    else         snprintf(b, sizeof b, "Security deployed - hunting the bomb!");
    add_event_message(b);
    disaster_close();
}

/* The paid path: $500k helicopters (fire) / the star-scaled ransom (bomb). */
static void disaster_do_ransom(void)
{
    int is_fire = (game.sim.event.type == EVENT_FIRE);
    int cost = game.sim.event.ransom_cost;
    game_event_ransom(&game.sim, &game.tower);
    char b[64];
    if (is_fire) snprintf(b, sizeof b, "Helicopters dispatched - $%d.", cost);
    else         snprintf(b, sizeof b, "Paid off the threat - $%d. Crisis averted.", cost);
    add_event_message(b);
    disaster_close();
}

/* Returns 1 if the modal is up (so the caller swallows the click entirely). */
static int disaster_modal_click(int mx, int my)
{
    if (!game.disaster_modal) return 0;
    /* Both disasters: button 0 = the free path, button 1 = the paid one. */
    if (point_in_rect(mx, my, disaster_btn_rect(0)))      disaster_do_proceed();
    else if (point_in_rect(mx, my, disaster_btn_rect(1))) disaster_do_ransom();
    return 1;   /* fully modal: consume every click while open */
}

/* Keyboard shortcuts while the modal is up. */
static void disaster_modal_key(SDL_Keycode k)
{
    /* Both disasters are two-choice now: d/Enter/Esc = the free path
     * (let it burn / deploy security), p/y = pay (helicopters / ransom). */
    if (k == SDLK_d || k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_ESCAPE)
        disaster_do_proceed();
    else if (k == SDLK_p || k == SDLK_y)
        disaster_do_ransom();
}

/* ---- One-button notice dialogs (the EXE's resolution/VIP popups) ---- */
#define NOTICE_W 340
#define NOTICE_H 140

static void show_notice_modal(const char *text, const char *btn)
{
    snprintf(game.notice_text, sizeof game.notice_text, "%s", text);
    snprintf(game.notice_btn, sizeof game.notice_btn, "%s", btn);
    if (!game.notice_modal) {           /* don't clobber saved speed */
        game.notice_saved_speed = game.sim.speed;
        game.notice_modal = 1;
    }
    game.sim.speed = SPEED_PAUSED;
}

static SDL_Rect notice_btn_rect(void)
{
    int wx = (game.screen_w - NOTICE_W) / 2;
    int wy = (game.screen_h - NOTICE_H) / 2;
    SDL_Rect r = { wx + (NOTICE_W - 110) / 2, wy + NOTICE_H - 40, 110, 26 };
    return r;
}

static void render_notice_modal(void)
{
    if (!game.notice_modal) return;
    int wx = (game.screen_w - NOTICE_W) / 2;
    int wy = (game.screen_h - NOTICE_H) / 2;
    draw_win31_rect(wx, wy, NOTICE_W, NOTICE_H, 1);
    SDL_Color black = { 0, 0, 0, 255 };
    draw_text_wrapped(game.notice_text, wx + 18, wy + 16,
                      NOTICE_W - 36, black);
    draw_modal_button(notice_btn_rect(), game.notice_btn);
}

static void notice_close(void)
{
    game.notice_modal = 0;
    game.sim.speed = game.notice_saved_speed;
}

/* ---- Route-loss confirmations (res 0x3ed, text verbatim) ----
 * The original pops a system-modal Yes/No MessageBox before a stop-toggle
 * or an elevator demolition severs a floor's route (TransferT detectors,
 * traced 2026-07-29). Yes proceeds, No aborts with nothing changed. */
static const char *ROUTE_MSGS[4] = {
    /* detector result 1 */
    "If you change this setting, you will lose a key route to this floor.  "
    "Are you sure you want to change this setting?",
    /* 2 */
    "If the service elevator doesn't stop on this floor, housekeeping "
    "cannot reach it.  Are you sure you want to change this?",
    /* 3 */
    "This floor is part of the route to the Lobby.  Are you sure you want "
    "to change this setting?",
    /* removal (#5) */
    "If you remove this item, you will lose a key route to this floor.  "
    "Are you sure you want to change this setting?",
};

#define RCONF_W 380
#define RCONF_H 168

static SDL_Rect route_btn_rect(int idx)
{
    int wx = (game.screen_w - RCONF_W) / 2;
    int wy = (game.screen_h - RCONF_H) / 2;
    int bw = 90, bh = 26;
    SDL_Rect r = { wx + RCONF_W / 2 + (idx == 0 ? -bw - 12 : 12),
                   wy + RCONF_H - bh - 12, bw, bh };
    return r;
}

static void route_confirm_open(const char *text, int kind,
                               int shaft, int fidx, uint16_t tid)
{
    game.route_confirm = 1;
    game.route_confirm_text = text;
    game.route_confirm_kind = kind;
    game.route_confirm_shaft = shaft;
    game.route_confirm_fidx = fidx;
    game.route_confirm_tid = tid;
    game.route_saved_speed = game.sim.speed;
    game.sim.speed = SPEED_PAUSED;
}

static void route_confirm_close(void)
{
    game.route_confirm = 0;
    game.sim.speed = game.route_saved_speed;
}

static void route_confirm_yes(void)
{
    if (game.route_confirm_kind == 1) {
        people_set_serviced(&game.sim.people, game.route_confirm_shaft,
                            game.route_confirm_fidx, 0);
    } else if (tower_remove(&game.tower, game.route_confirm_tid)) {
        play_snd(SND_DELETE);
    }
    route_confirm_close();
}

static void render_route_confirm(void)
{
    if (!game.route_confirm) return;
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 120);
    SDL_Rect full = { 0, 0, game.screen_w, game.screen_h };
    SDL_RenderFillRect(game.renderer, &full);
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);

    int wx = (game.screen_w - RCONF_W) / 2;
    int wy = (game.screen_h - RCONF_H) / 2;
    draw_win31_titlebar(wx, wy, RCONF_W, "SimTower");
    int body_y = wy + WIN_TITLEBAR_H;
    draw_win31_rect(wx, body_y, RCONF_W, RCONF_H - WIN_TITLEBAR_H, 1);

    /* Word-wrap the message at the dialog width. */
    SDL_Color black = { 0, 0, 0, 255 };
    const char *p = game.route_confirm_text;
    int ty = body_y + 14;
    char line[64];
    while (*p) {
        int len = (int)strlen(p), take = len;
        if (take > 52) {
            take = 52;
            while (take > 0 && p[take] != ' ') take--;
            if (take == 0) take = 52;
        }
        snprintf(line, sizeof line, "%.*s", take, p);
        draw_text(line, wx + 16, ty, black);
        ty += 18;
        p += take;
        while (*p == ' ') p++;
    }

    draw_modal_button(route_btn_rect(0), "Yes");
    draw_modal_button(route_btn_rect(1), "No");
}

/* Returns 1 while the modal is up (caller swallows the click). */
static int route_confirm_click(int mx, int my)
{
    if (!game.route_confirm) return 0;
    if (point_in_rect(mx, my, route_btn_rect(0)))      route_confirm_yes();
    else if (point_in_rect(mx, my, route_btn_rect(1))) route_confirm_close();
    return 1;
}

static void route_confirm_key(SDL_Keycode k)
{
    if (k == SDLK_y || k == SDLK_RETURN || k == SDLK_KP_ENTER)
        route_confirm_yes();
    else if (k == SDLK_n || k == SDLK_ESCAPE)
        route_confirm_close();
}

/* Toggle a stop, warning first when the EXE would (turning OFF a floor's
 * only route). All stop-toggle UI paths come through here. */
static void request_stop_toggle(int si, int fidx)
{
    PeopleSim *ps = &game.sim.people;
    if (si < 0 || si >= ps->shaft_count) return;
    ElevatorShaft *s = &ps->shafts[si];
    if (s->serviced[fidx]) {
        int r = game_stop_route_loss(&game.sim, &game.tower, si, fidx);
        if (r) {
            route_confirm_open(ROUTE_MSGS[r - 1], 1, si, fidx, 0);
            return;
        }
    }
    people_set_serviced(ps, si, fidx, !s->serviced[fidx]);
}

/* Demolish a tenant, warning first for an elevator segment whose floor
 * would lose its only same-network stop. Returns 1 if handled (removed,
 * refused with a message, or pending confirmation). */
static int request_remove_tenant(uint16_t tid, ItemType ty)
{
    if (item_is_elevator(ty)) {
        Tenant *t = tower_tenant(&game.tower, tid);
        if (t) {
            PeopleSim *ps = &game.sim.people;
            int fidx = floor_to_index(t->floor);
            for (int si = 0; si < ps->shaft_count; si++) {
                ElevatorShaft *s = &ps->shafts[si];
                if (!s->active || s->type != ty || s->x != t->x) continue;
                if (fidx < s->lo || fidx > s->hi) continue;
                if (game_remove_route_loss(&game.sim, &game.tower, si, fidx)) {
                    route_confirm_open(ROUTE_MSGS[3], 2, si, fidx, tid);
                    return 1;
                }
                break;
            }
        }
    }
    if (tower_remove(&game.tower, tid)) {
        play_snd(SND_DELETE);   /* referee row 22 */
        printf("Demolish: %s\n", tower_item_name(ty));
    } else {
        printf("Can't demolish %s\n", tower_item_name(ty));
        if (tower_reject_reason()[0])
            add_event_message(tower_reject_reason());
    }
    return 1;
}

/* Fire-glow: a warm, pulsing tint washed over the scene while a fire burns.
 * (The decomp animates fire via palette entries 207/213; this is the SDL
 * equivalent — a screen-wide glow that intensifies with the blaze's size.) */
static void render_fire_glow(void)
{
    if (!game.sim.event.active || game.sim.event.type != EVENT_FIRE) return;
    /* size = how many floors have a live front */
    int spread = 0;
    for (int fi = 0; fi < TOWER_FLOOR_COUNT; fi++)
        if (game.sim.event.fire_left[fi] >= 0 || game.sim.event.fire_right[fi] >= 0)
            spread += 12;
    if (spread < 1) spread = 1;
    int a = 12 + spread;                 /* bigger fire -> stronger glow */
    if (a > 64) a = 64;
    a += (game.sim.frame % 8 < 4) ? 0 : 10;   /* flicker */
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(game.renderer, 255, 90, 0, a);
    SDL_Rect full = { 0, 0, game.screen_w, game.screen_h };
    SDL_RenderFillRect(game.renderer, &full);
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
}

/* Star-promotion certificate — a celebratory gold-framed card. SimTower has
 * no certificate bitmap (promotions fire event animations 0xBD4+star), so
 * this is a port-authored flourish built from the real star sprites. */
static void render_event_alert(void)
{
    if (!game.sim.event.active) return;
    if (game.sim.frame % 30 < 6) return;  /* blink for urgency */

    int is_fire = (game.sim.event.type == EVENT_FIRE);
    Sprite *icon = is_fire ? game.alert_fire : game.alert_terror;
    const char *label = is_fire ? "FIRE!" : "BOMB THREAT!";
    SDL_Color fg = is_fire ? (SDL_Color){ 230, 90, 0, 255 }
                           : (SDL_Color){ 210, 0, 0, 255 };

    int iw = icon ? icon->w : 0, ih = icon ? icon->h : 0;
    TTF_Font *f = game.font ? game.font : game.font_small;
    SDL_Surface *ts = f ? TTF_RenderText_Blended(f, label, fg) : NULL;
    int tw = ts ? ts->w : 0, th = ts ? ts->h : 0;

    int pad = 10, gap = (iw && tw) ? 8 : 0;
    int panel_w = iw + gap + tw + pad * 2;
    int panel_h = (ih > th ? ih : th) + pad * 2;
    int px = (game.screen_w - panel_w) / 2;
    int py = 48;

    draw_win31_rect(px, py, panel_w, panel_h, 1);
    if (icon && icon->texture) {
        SDL_Rect d = { px + pad, py + (panel_h - ih) / 2, iw, ih };
        SDL_RenderCopy(game.renderer, icon->texture, NULL, &d);
    }
    if (ts) {
        SDL_Texture *tt = SDL_CreateTextureFromSurface(game.renderer, ts);
        SDL_Rect d = { px + pad + iw + gap, py + (panel_h - th) / 2, tw, th };
        SDL_RenderCopy(game.renderer, tt, NULL, &d);
        SDL_DestroyTexture(tt);
        SDL_FreeSurface(ts);
    }
}

/* =====================================================================
 * TENANT / VENUE INFO DIALOG — faithful rebuild (InfoDlgT seg_1100,
 * templates res 748-760). The original is 13 procedural Win16 dialogs
 * (NO background bitmap), so this reproduces the real layout + controls:
 * a borderless grey panel with the tenant NAME painted as a header, up to
 * 3 diagnostic comment lines (seg_1108, real res 0x2C7 strings), per-
 * category labeled fields, an Eval gauge bar / patronage bar, the 4-tier
 * price DROPDOWN (item 0xD) showing real dollar amounts, and the
 * Rename (id 7) / OK (id 1) / New Movie (id 0xD, cinema) buttons, plus the
 * name-editor (res 0x2DC) and movie-chooser (res 0x2DB) sub-dialogs.
 * ===================================================================== */

/* ---- Custom tenant names. Stored on the Tenant record (Tenant.name) and
 * round-tripped through .TDT via the keyed name list (twr.c). ---- */
static const char *tenant_custom_name(uint16_t id)
{
    Tenant *t = tower_tenant(&game.tower, id);
    return (t && t->name[0]) ? t->name : NULL;
}
static void tenant_set_name(uint16_t id, const char *s)
{
    Tenant *t = tower_tenant(&game.tower, id);
    if (t) snprintf(t->name, sizeof t->name, "%s", s);
}
static void tenant_clear_name(uint16_t id)   /* the rename dialog's Delete */
{
    Tenant *t = tower_tenant(&game.tower, id);
    if (t) t->name[0] = 0;
}

/* The 14 movie titles, read from the EXE's string table 0x1A4 at display
 * time (movie_id 0..13; the table's 15th title is unreachable in the EXE
 * too — referee 2026-07-30). Literals kept as fallbacks. */
static const char *MOVIE_TITLES_FALLBACK[14] = {
    "Revenge of the Big Spider", "Northwest Romance", "Samurai Cop",
    "Big Wave", "Farewell to Morocco", "Fear of Shark Teeth",
    "Western Sheriff", "Dino Wars", "The Making of a Star",
    "Love in N.Y.", "Waikiki Moon", "My Man of War",
    "Christmas for Both of Us", "Casual Friends",
};
static const char *movie_title(int id)
{
    if (id < 0 || id >= 14) return "-";
    return exe_str(0x01a4, id, MOVIE_TITLES_FALLBACK[id]);
}
#define MOVIE_COST_HIT      300000   /* EXE 0xDE10=3000 x$100 */
#define MOVIE_COST_ORDINARY 150000   /* EXE 0xDE12=1500 x$100 */

/* The cinema hall (wide strip) is the only piece that shows film controls. */
static int inspect_is_cinema(const Tenant *t)
{
    return t && t->type == ITEM_CINEMA && t->width >= 20;
}

/* Retail variant names (res 0x2ca/0x2cb/0x2cc — the EXE titles the info
 * dialog with the unit's named variant, not the generic type). Indexed by
 * the same stable variant the renderer uses. */
static const char *RESTAURANT_NAMES[5] = {
    "English Pub", "French Restaurant", "Chinese Restaurant",
    "Sushi Bar", "Steak House",
};
static const char *FASTFOOD_NAMES[5] = {
    "Japanese Soba", "Chinese Cafe", "Hamburger Stand",
    "Ice Cream", "Coffee Shop",
};
static const char *SHOP_NAMES[11] = {
    "Men's Clothing", "Pet Store", "Flower Shop", "Book Store",
    "Drug Store", "Boutique", "Electronics", "Bank", "Hair Salon",
    "Post Office", "Sports Gear",
};

static const char *retail_variant_name(const Tenant *t)
{
    int v = twr_tenant_variant(&game.tower, t);
    if (v < 0) v = 0;
    switch (t->type) {
    case ITEM_RESTAURANT: return RESTAURANT_NAMES[v % 5];
    case ITEM_FAST_FOOD:  return FASTFOOD_NAMES[v % 5];
    case ITEM_SHOP:       return SHOP_NAMES[v % 11];
    default:              return NULL;
    }
}

/* Header line: custom name if set, else the variant/type name, with a
 * floor suffix. */
static void inspect_title(const Tenant *t, char *buf, int n)
{
    const char *custom = tenant_custom_name(t->id);
    const char *base = custom ? custom : retail_variant_name(t);
    if (!base) base = tower_item_name(t->type);
    if (t->floor == 0)     snprintf(buf, n, "%s  -  Lobby", base);
    else if (t->floor < 0) snprintf(buf, n, "%s  -  B%d", base, -t->floor);
    else                   snprintf(buf, n, "%s  -  %dF", base, t->floor);
}

/* "Length" (EXE tenant +0x17 as quarters -> "N Year M Q" / "Over 30 years"). */
static void inspect_length_str(const Tenant *t, char *buf, int n)
{
    int q = t->let_quarters;
    if (q >= 120) snprintf(buf, n, "Over 30 years");
    else          snprintf(buf, n, "%d Year %d Q", q / 4, (q % 4) + 1);
}
/* "Length of Showing" (venue age -> "N Q" / "Over 1 year"). */
static void inspect_showing_str(const Tenant *t, char *buf, int n)
{
    int a = t->venue_age_days;
    if (a >= 12) snprintf(buf, n, "Over 1 year");
    else         snprintf(buf, n, "%d Q", a / 3 + 1);
}

/* ---- Layout: computed once, shared by render and click so rects never
 * drift. All rects are dialog-LOCAL (add the window origin at use). ---- */
typedef enum { TIF_TEXT, TIF_EVAL, TIF_PRICE, TIF_PATRON } TiFieldKind;
typedef struct { TiFieldKind kind; char label[24]; char value[40]; int ival, imax; } TiField;
typedef struct {
    int w, h;
    int name_y, comment_y, comment_n, field_y0;
    char comments[3][48];
    SDL_Rect picture;
    TiField fields[6];
    int nf, price_field;              /* index of the FLD_PRICE row, or -1 */
    SDL_Rect price_box, price_items[4];
    int has_newmovie;
    SDL_Rect newmovie_btn, rename_btn, ok_btn;
    SDL_Rect occ_row;                 /* occupants-now silhouette strip */
    uint16_t occ_pid[12];
    int occ_n;
} TiLayout;

static void ti_build(const Tenant *t, TiLayout *L)
{
    memset(L, 0, sizeof *L);
    L->w = INSPECT_W;
    L->price_field = -1;

    L->comment_n = game_tenant_comments(&game.sim, &game.tower, t, L->comments, 3);

    TiField *f = L->fields;
    int nf = 0, riders_field = -1;
    int evbar = (game.tower.star_rating >= 4) ? 200 : 150;
    ItemType ty = t->type;
    if (ty == ITEM_OFFICE || ty == ITEM_CONDO) {
        f[nf].kind = TIF_EVAL; snprintf(f[nf].label, 24, "Eval");
        f[nf].ival = game_tenant_eval_metric(&game.sim, &game.tower, t);
        f[nf].imax = evbar; nf++;
        f[nf].kind = TIF_TEXT; snprintf(f[nf].label, 24, "Length");
        inspect_length_str(t, f[nf].value, 40); nf++;
        f[nf].kind = TIF_PRICE; snprintf(f[nf].label, 24, ty == ITEM_CONDO ? "Price" : "Rent");
        L->price_field = nf; nf++;
        /* Status word (res 0x2c8): condos sell, offices rent. */
        f[nf].kind = TIF_TEXT; snprintf(f[nf].label, 24, "Status");
        snprintf(f[nf].value, 40, "%s",
                 t->state == TENANT_OCCUPIED ? "Occupied"
               : ty == ITEM_CONDO            ? "For Sale" : "For Rent");
        nf++;
    } else if (item_is_hotel_room(ty)) {
        f[nf].kind = TIF_EVAL; snprintf(f[nf].label, 24, "Eval");
        f[nf].ival = game_tenant_eval_metric(&game.sim, &game.tower, t);
        f[nf].imax = evbar; nf++;
        f[nf].kind = TIF_PRICE; snprintf(f[nf].label, 24, "Rate");
        L->price_field = nf; nf++;
        f[nf].kind = TIF_TEXT; snprintf(f[nf].label, 24, "Status");
        snprintf(f[nf].value, 40, "%s",
                 t->condition != ROOM_CLEAN ? "Dirty"
               : t->state == TENANT_OCCUPIED ? "Occupied" : "Clean"); nf++;
    } else if (ty == ITEM_SHOP) {
        f[nf].kind = TIF_PATRON; snprintf(f[nf].label, 24, "Patronage");
        f[nf].ival = t->customers_today; f[nf].imax = 30; nf++;
        f[nf].kind = TIF_PRICE; snprintf(f[nf].label, 24, "Rent");
        L->price_field = nf; nf++;
        f[nf].kind = TIF_TEXT; snprintf(f[nf].label, 24, "Status");
        snprintf(f[nf].value, 40, "%s",
                 t->state == TENANT_OCCUPIED ? "Occupied" : "For Rent");
        nf++;
    } else if (inspect_is_cinema(t)) {
        f[nf].kind = TIF_TEXT; snprintf(f[nf].label, 24, "Playing");
        snprintf(f[nf].value, 40, "%s", movie_title(t->movie_id)); nf++;
        f[nf].kind = TIF_TEXT; snprintf(f[nf].label, 24, "Showing");
        inspect_showing_str(t, f[nf].value, 40); nf++;
        f[nf].kind = TIF_TEXT; snprintf(f[nf].label, 24, "Today");
        { char m[24]; format_money(game_venue_today_income(t), m, sizeof m);
          snprintf(f[nf].value, 40, "%s", m); } nf++;
        L->has_newmovie = 1;
    } else if (ty == ITEM_RESTAURANT || ty == ITEM_FAST_FOOD) {
        f[nf].kind = TIF_PATRON; snprintf(f[nf].label, 24, "Patronage");
        f[nf].ival = t->customers_today; f[nf].imax = 50; nf++;
        f[nf].kind = TIF_TEXT; snprintf(f[nf].label, 24, "Yest. Profit");
        { char m[24]; format_money(t->yesterday_profit, m, sizeof m);
          snprintf(f[nf].value, 40, "%s", m); } nf++;
    } else if (ty == ITEM_STAIRS || ty == ITEM_ESCALATOR) {
        f[nf].kind = TIF_TEXT; snprintf(f[nf].label, 24, "Riders");
        riders_field = nf; nf++;
    }
    L->nf = nf;

    int pad = 10;
    L->name_y = 8;
    L->picture = (SDL_Rect){ pad, 32, INSPECT_W - 2 * pad, 46 };
    L->comment_y = L->picture.y + L->picture.h + 6;
    L->field_y0 = L->comment_y + L->comment_n * 14 + (L->comment_n ? 4 : 0);
    int y = L->field_y0;
    for (int i = 0; i < nf; i++) {
        if (i == L->price_field) {
            L->price_box = (SDL_Rect){ pad + 96, y - 1, 116, 18 };
            for (int k = 0; k < 4; k++)
                L->price_items[k] = (SDL_Rect){ L->price_box.x, L->price_box.y + 18 * (k + 1),
                                                L->price_box.w, 18 };
        }
        y += 20;
    }
    /* Occupants-now row (the original's people-list family, 1100:327f):
     * everyone whose home is this tenant and who is inside it right now,
     * as clickable silhouettes feeding the person popup. */
    L->occ_n = 0;
    if (ty == ITEM_STAIRS || ty == ITEM_ESCALATOR) {
        /* Riders mid-leg on this stair/escalator (the original lists
         * people in transit too — same people-list widget). */
        PeopleSim *ps = &game.sim.people;
        int total = 0;
        for (int i = 0; i < ps->people_high; i++) {
            const Person *p = &ps->people[i];
            if (p->state != PERSON_WALKING || p->walk_stair != t->id)
                continue;
            total++;
            if (L->occ_n < 12) L->occ_pid[L->occ_n++] = (uint16_t)(i + 1);
        }
        if (riders_field >= 0)
            snprintf(L->fields[riders_field].value, 40, "%d", total);
    } else {
        PeopleSim *ps = &game.sim.people;
        int fidx = floor_to_index(t->floor);
        for (int i = 0; i < ps->people_high && L->occ_n < 12; i++) {
            const Person *p = &ps->people[i];
            if (p->home_tenant != t->id) continue;
            if (p->state != PERSON_AT_DEST) continue;
            if (p->cur_floor != (uint8_t)fidx) continue;
            L->occ_pid[L->occ_n++] = (uint16_t)(i + 1);
        }
    }
    if (L->occ_n) {
        L->occ_row = (SDL_Rect){ pad, y + 4, INSPECT_W - 2 * pad, PSTRIP_H };
        y += 4 + PSTRIP_H + 4;
    }

    int btn_y = y + 6;
    L->rename_btn = (SDL_Rect){ pad, btn_y, 64, 20 };
    if (L->has_newmovie) L->newmovie_btn = (SDL_Rect){ pad + 72, btn_y, 74, 20 };
    L->ok_btn = (SDL_Rect){ INSPECT_W - pad - 56, btn_y, 56, 20 };
    L->h = btn_y + 20 + pad;
}

/* Total dialog height, for on-open clamping. */
static int inspect_body_h(const Tenant *t) { TiLayout L; ti_build(t, &L); return L.h; }

/* Open the tenant info popup anchored near a click, clamped on-screen. */
static void open_tenant_popup_at(uint16_t tid, int x, int y)
{
    game.inspect_open = 1;
    game.inspect_tid = tid;
    game.rent_dd_open = 0;   /* fresh dialog, dropdown shut */
    game.inspect_x = x + 16;
    game.inspect_y = y - 40;
    if (game.inspect_x + INSPECT_W > game.screen_w)
        game.inspect_x = game.screen_w - INSPECT_W - 8;
    /* keep the whole body (incl. button rows) on screen — basement
     * clicks used to hang off the bottom edge */
    int pbh = inspect_body_h(tower_tenant(&game.tower, tid));
    if (game.inspect_y + pbh > game.screen_h)
        game.inspect_y = game.screen_h - pbh - 8;
    if (game.inspect_y < 0) game.inspect_y = 8;
}

/* Stair/escalator under a world click: records anchor on the LOWER landing
 * and span two floors; their grid cells keep the underlying tenant, so the
 * overlay is found by scanning the tenant list. */
static uint16_t stair_hit_test(int floor, int cell)
{
    for (int i = 0; i < game.tower.tenant_count; i++) {
        Tenant *t = &game.tower.tenants[i];
        if (t->type != ITEM_STAIRS && t->type != ITEM_ESCALATOR) continue;
        if (t->state == TENANT_ABANDONED) continue;
        int rise = t->height - 1; if (rise < 1) rise = 1;
        if (floor < t->floor || floor > t->floor + rise) continue;
        if (cell < t->x || cell >= t->x + t->width) continue;
        return t->id;
    }
    return 0;
}

/* ---- small drawing helpers ---- */
static void draw_centered(SDL_Rect r, const char *s, SDL_Color c)
{
    int w = 0, h = 0; SDL_Texture *tex = render_text(s, c, &w, &h);
    if (!tex) return;
    SDL_Rect d = { r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h };
    SDL_RenderCopy(game.renderer, tex, NULL, &d);
    SDL_DestroyTexture(tex);
}
static void draw_bevel(SDL_Rect r, int raised)
{
    SDL_SetRenderDrawColor(game.renderer, raised ? 255 : 90, raised ? 255 : 90, raised ? 255 : 90, 255);
    SDL_RenderDrawLine(game.renderer, r.x, r.y, r.x + r.w - 1, r.y);
    SDL_RenderDrawLine(game.renderer, r.x, r.y, r.x, r.y + r.h - 1);
    SDL_SetRenderDrawColor(game.renderer, raised ? 90 : 255, raised ? 90 : 255, raised ? 90 : 255, 255);
    SDL_RenderDrawLine(game.renderer, r.x, r.y + r.h - 1, r.x + r.w - 1, r.y + r.h - 1);
    SDL_RenderDrawLine(game.renderer, r.x + r.w - 1, r.y, r.x + r.w - 1, r.y + r.h - 1);
}
static void draw_dlg_button(SDL_Rect r, const char *label, int enabled)
{
    SDL_SetRenderDrawColor(game.renderer, 200, 200, 200, 255);
    SDL_RenderFillRect(game.renderer, &r);
    draw_bevel(r, 1);
    SDL_Color c = enabled ? (SDL_Color){ 0, 0, 0, 255 } : (SDL_Color){ 132, 132, 132, 255 };
    draw_centered(r, label, c);
}
static void draw_eval_gauge(SDL_Rect box, int val, int tick1, int tick2)
{
    SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(game.renderer, &box);
    int v = val < 0 ? 0 : val > 300 ? 300 : val;
    SDL_Color c = val >= tick2 ? (SDL_Color){ 200, 40, 40, 255 }
                : val >= tick1 ? (SDL_Color){ 212, 162, 0, 255 }
                :                (SDL_Color){ 40, 160, 60, 255 };
    SDL_SetRenderDrawColor(game.renderer, c.r, c.g, c.b, 255);
    SDL_Rect fill = { box.x, box.y, box.w * v / 300, box.h };
    SDL_RenderFillRect(game.renderer, &fill);
    SDL_SetRenderDrawColor(game.renderer, 70, 70, 70, 255);
    int t1 = box.x + box.w * tick1 / 300, t2 = box.x + box.w * tick2 / 300;
    SDL_RenderDrawLine(game.renderer, t1, box.y - 1, t1, box.y + box.h);
    SDL_RenderDrawLine(game.renderer, t2, box.y - 1, t2, box.y + box.h);
    SDL_RenderDrawRect(game.renderer, &box);
}
static void draw_patron_bar(SDL_Rect box, int val, int max)
{
    SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(game.renderer, &box);
    int v = val < 0 ? 0 : val > max ? max : val;
    SDL_SetRenderDrawColor(game.renderer, 60, 110, 200, 255);
    SDL_Rect fill = { box.x, box.y, max ? box.w * v / max : 0, box.h };
    SDL_RenderFillRect(game.renderer, &fill);
    SDL_SetRenderDrawColor(game.renderer, 70, 70, 70, 255);
    SDL_RenderDrawRect(game.renderer, &box);
}
/* The tenant schematic (item id 2): the unit shown IN CONTEXT — its floor,
 * centered on the unit, with the same-floor neighbors that fit, the focused
 * unit ringed. Mirrors the EXE's item-2 panel (seg_1100:4869 walks the
 * stable-id neighbor map and blits the floor strip). */
static void draw_tenant_picture(const Tenant *t, SDL_Rect box)
{
    SDL_SetRenderDrawColor(game.renderer, 176, 196, 222, 255);   /* sky backing */
    SDL_RenderFillRect(game.renderer, &box);
    SDL_Rect clip = { box.x + 1, box.y + 1, box.w - 2, box.h - 2 };
    SDL_RenderSetClipRect(game.renderer, &clip);

    int inner = box.w - 8;
    int ppc = (t->width > 0) ? inner * 62 / 100 / t->width : 6;  /* unit ~62% wide */
    if (ppc < 2) ppc = 2;
    int center_cell = t->x + t->width / 2;
    int win_cells = ppc ? inner / ppc : t->width;
    int win_left = center_cell - win_cells / 2;

    for (int i = 0; i < game.tower.tenant_count; i++) {
        Tenant *n = &game.tower.tenants[i];
        if (n->type == ITEM_NONE || n->floor != t->floor) continue;
        if (n->x + n->width <= win_left || n->x >= win_left + win_cells) continue;
        int fw = 0, floors = 1;
        uint16_t sid = item_sprite_id(n->type, &fw, &floors);
        Sprite *spr = sid ? sprites_find(&game.sprites, sid) : NULL;
        if (!spr || !spr->texture || fw <= 0 || spr->h <= 0) continue;
        SDL_Rect src = { 0, 0, fw, spr->h };
        int dw = n->width * ppc;
        int dh = spr->h * dw / fw;
        if (dh > box.h - 8) dh = box.h - 8;
        int dx = box.x + 4 + (n->x - win_left) * ppc;
        int dy = box.y + box.h - 4 - dh;              /* bottom-aligned floor line */
        SDL_Rect dst = { dx, dy, dw, dh };
        SDL_RenderCopy(game.renderer, spr->texture, &src, &dst);
        if (n->id == t->id) {                          /* ring the focused unit */
            SDL_SetRenderDrawColor(game.renderer, 255, 226, 40, 255);
            SDL_RenderDrawRect(game.renderer, &dst);
            SDL_Rect d2 = { dst.x - 1, dst.y - 1, dst.w + 2, dst.h + 2 };
            SDL_RenderDrawRect(game.renderer, &d2);
        }
    }
    SDL_RenderSetClipRect(game.renderer, NULL);
    SDL_SetRenderDrawColor(game.renderer, 90, 90, 90, 255);
    SDL_RenderDrawRect(game.renderer, &box);
}
static void draw_price_dropdown(const Tenant *t, SDL_Rect box, const SDL_Rect *items)
{
    SDL_Color ink = { 0, 0, 0, 255 };
    char m[24]; format_money(tenant_rent(t->type, t->rent_class), m, sizeof m);
    SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(game.renderer, &box);
    SDL_SetRenderDrawColor(game.renderer, 60, 60, 60, 255);
    SDL_RenderDrawRect(game.renderer, &box);
    stats_label(box.x + 6, box.y + 2, m, ink);
    int ax = box.x + box.w - 15, ay = box.y + 7;
    SDL_Point tri[4] = { { ax, ay }, { ax + 8, ay }, { ax + 4, ay + 5 }, { ax, ay } };
    SDL_RenderDrawLines(game.renderer, tri, 4);
    if (game.rent_dd_open) {
        for (int k = 0; k < 4; k++) {
            SDL_Rect r = items[k];
            int sel = (k == t->rent_class);
            SDL_SetRenderDrawColor(game.renderer, sel ? 208 : 246, sel ? 224 : 246, sel ? 255 : 246, 255);
            SDL_RenderFillRect(game.renderer, &r);
            SDL_SetRenderDrawColor(game.renderer, 90, 90, 90, 255);
            SDL_RenderDrawRect(game.renderer, &r);
            char mk[24]; format_money(tenant_rent(t->type, k), mk, sizeof mk);
            stats_label(r.x + 6, r.y + 2, mk, ink);
        }
    }
}

/* ================ Person inspection (InfoPeple seg_1110) ================
 * The original stores no person type: the label is derived at draw time
 * from (home tenant type, numInTenant) by the classifier at 1100:3856,
 * with two code quirks kept — "Homebody" is always replaced by "Mother
 * with Baby", and Housekeepers become "Janitor" in the evening. */

typedef enum {
    PK_NONE = -1, PK_MAN = 0, PK_SALESMAN, PK_WOMAN, PK_CHILD, PK_WOMAN2,
    PK_SECURITY, PK_WOMANKID, PK_MOTHER, PK_HOUSEKEEPER
} PersonKind;

static int person_home_is_indoor(ItemType ty)
{
    return ty == ITEM_OFFICE || ty == ITEM_CONDO || ty == ITEM_SECURITY ||
           ty == ITEM_HOUSEKEEPING || item_is_hotel_room(ty);
}

static PersonKind person_kind(const Person *p)
{
    Tenant *home = tower_tenant(&game.tower, p->home_tenant);
    if (!home) return PK_NONE;
    int n = p->member;
    switch (home->type) {
    case ITEM_HOTEL_SINGLE: case ITEM_HOTEL_TWIN: case ITEM_HOTEL_SUITE:
        return n == 2 ? PK_WOMAN2 : PK_MAN;
    case ITEM_OFFICE:
        if (n <= 1) return PK_SALESMAN;
        if (n <= 3) return PK_MAN;
        return n == 4 ? PK_WOMAN : PK_WOMAN2;
    case ITEM_CONDO:
        if (n == 0) return PK_MAN;
        if (n == 1) return PK_MOTHER;
        return n == 2 ? PK_CHILD : PK_NONE;
    case ITEM_SECURITY:     return PK_SECURITY;
    case ITEM_HOUSEKEEPING: return PK_HOUSEKEEPER;
    default:
        /* every visitor class classifies on numInTenant & 7 */
        switch (n & 7) {
        case 1:  return PK_WOMAN;
        case 3:  return PK_WOMAN2;
        case 5:  return PK_WOMANKID;
        case 7:  return PK_MOTHER;
        default: return PK_MAN;
        }
    }
}

static const char *person_kind_label(PersonKind k)
{
    switch (k) {
    case PK_MAN:      return "Man";
    case PK_SALESMAN: return "Salesman";
    case PK_WOMAN:
    case PK_WOMAN2:   return "Woman";
    case PK_CHILD:    return "Child";
    case PK_SECURITY: return "Security";
    case PK_WOMANKID: return "Woman with Kid";
    case PK_MOTHER:   return "Mother with Baby";
    case PK_HOUSEKEEPER:
        return game.sim.hour >= 17 ? "Janitor" : "Housekeeper";
    default:          return "";
    }
}

static void person_fmt_floor(int floor, char *buf, int n)
{
    if (floor == 0)     snprintf(buf, n, "Lobby");
    else if (floor < 0) snprintf(buf, n, "B%d", -floor);
    else                snprintf(buf, n, "%dF", floor);
}

/* Home line: the tenant's title for residents/workers/staff, "Outside"
 * (res 0x2bc entry 1) for every visitor class. */
static void person_home_line(const Person *p, char *buf, int n)
{
    Tenant *home = tower_tenant(&game.tower, p->home_tenant);
    if (!home) { snprintf(buf, n, "-"); return; }
    if (person_home_is_indoor(home->type)) {
        char fl[12];
        person_fmt_floor(home->floor, fl, sizeof fl);
        snprintf(buf, n, "%s, %s", tower_item_name(home->type), fl);
    } else {
        snprintf(buf, n, "Outside");
    }
}

/* Whereabouts line, mapped from the port's trip state (the EXE derives
 * it from person status +0x05; the port's states cover the same trips
 * except the office midday lobby errand, which the sim doesn't model). */
static void person_where_line(const Person *p, char *buf, int bufn)
{
    Tenant *home = tower_tenant(&game.tower, p->home_tenant);
    int visitor = home && !person_home_is_indoor(home->type);
    char fl[12];
    switch ((PersonState)p->state) {
    case PERSON_PLANNING: case PERSON_WALKING:
    case PERSON_QUEUED:   case PERSON_RIDING:
        if (p->errand == 1 || p->errand == 2) {
            /* status 0x40/0x21: "Lobby" + STRL 0x2BD #1 */
            snprintf(buf, bufn, "Lobby for sales calls");
        } else if (p->errand == 5) {
            person_fmt_floor(index_to_floor(p->dest_floor), fl, sizeof fl);
            snprintf(buf, bufn, "Medical Center, %s", fl);
        } else if (p->going_home) {
            /* condo residents "leave" (STRL 0x2BD #2); everyone else
             * goes home */
            snprintf(buf, bufn, home && home->type == ITEM_CONDO
                     ? "Lobby to leave"
                     : p->parked_cat ? "Parking Space to go home"
                                     : "Lobby to go home");
        } else if (visitor) {
            const char *vn = retail_variant_name(home);
            person_fmt_floor(home->floor, fl, sizeof fl);
            snprintf(buf, bufn, "%s, %s",
                     vn ? vn : tower_item_name(home->type), fl);
        } else {
            person_fmt_floor(index_to_floor(p->dest_floor), fl, sizeof fl);
            snprintf(buf, bufn, "to %s", fl);
        }
        break;
    default:
        if (!home) { snprintf(buf, bufn, "-"); break; }
        if (p->errand == 2) {   /* parked at the lobby, making calls */
            snprintf(buf, bufn, "Lobby for sales calls");
            break;
        }
        if (p->errand == 6) {   /* being seen at the clinic */
            person_fmt_floor(index_to_floor(p->cur_floor), fl, sizeof fl);
            snprintf(buf, bufn, "Medical Center, %s", fl);
            break;
        }
        if (p->service && home->type == ITEM_HOUSEKEEPING &&
            p->cur_floor != (uint8_t)floor_to_index(home->floor)) {
            person_fmt_floor(index_to_floor(p->cur_floor), fl, sizeof fl);
            snprintf(buf, bufn, "Housekeeping, %s", fl);
        } else {
            const char *vn = visitor ? retail_variant_name(home) : NULL;
            person_fmt_floor(home->floor, fl, sizeof fl);
            snprintf(buf, bufn, "%s, %s",
                     vn ? vn : tower_item_name(home->type), fl);
        }
        break;
    }
}

/* Hit-test the rendered queue figures — the mirror of render_shaft's
 * queue layout (the EXE walks the same draw cells: exact 8px column, no
 * radius; only queue-standing people are world-clickable, car riders go
 * through the elevator dialog). Returns people[] slot + 1, or 0. */
static uint16_t person_hit_test(int mx, int my)
{
    if (!game.anim_people) return 0;   /* hidden crowd isn't clickable */
    PeopleSim *ps = &game.sim.people;
    for (int i = 0; i < ps->shaft_count; i++) {
        ElevatorShaft *s = &ps->shafts[i];
        if (!s->active) continue;
        if (s->hidden && !game.elv_edit_mode) continue;
        int shaft_w = ITEM_WIDTH[s->type] * CELL_W;
        for (int f = s->lo; f <= s->hi; f++) {
            const ElevatorStop *st = &s->stop[f];
            int n = st->up_count + st->down_count;
            if (!n) continue;
            int sx, sy;
            grid_to_screen(index_to_floor(f), s->x, &sx, &sy);
            if (my < sy || my >= sy + CELL_H) continue;
            int left_in = 0, right_in = 0;
            for (int d = 1; d <= 4 && !(left_in && right_in); d++) {
                int lx = s->x - d;
                int rx = s->x + ITEM_WIDTH[s->type] + d - 1;
                if (lx >= 0 && game.tower.grid[f][lx].type != ITEM_NONE)
                    left_in = 1;
                if (rx < TOWER_WIDTH &&
                    game.tower.grid[f][rx].type != ITEM_NONE)
                    right_in = 1;
            }
            int rightward = right_in && !left_in;
            int shown = n;   /* full line, mirroring the queue render */
            for (int k = shown - 1; k >= 0; k--) {   /* topmost drawn first */
                uint16_t pid;
                if (k < st->up_count)
                    pid = st->up_ring[(st->up_head + k) % QUEUE_CAP];
                else
                    pid = st->down_ring[(st->down_head + k - st->up_count)
                                        % QUEUE_CAP];
                if (!pid) continue;
                int px = rightward ? sx + shaft_w + k * 9
                                   : sx - 16 - k * 9;
                if (mx >= px && mx < px + 16) return pid;
            }
        }
    }
    return 0;
}

#define PINFO_W 236
#define PINFO_H 172

static SDL_Rect person_popup_rect(void)
{ return (SDL_Rect){ game.person_x, game.person_y, PINFO_W, PINFO_H }; }
static SDL_Rect person_btn_name(SDL_Rect d)
{ return (SDL_Rect){ d.x + 12, d.y + d.h - 34, 78, 22 }; }
static SDL_Rect person_btn_ok(SDL_Rect d)
{ return (SDL_Rect){ d.x + d.w - 76, d.y + d.h - 34, 64, 22 }; }

/* Figure-frame selector (1100:3856): home-tenant type x member index.
 * -1 = the EXE draws no figure (condo members past the kid). */
static int person_figure_frame(const Person *p)
{
    Tenant *home = tower_tenant(&game.tower, p->home_tenant);
    if (!home) return 0;
    int m = p->member;
    switch (home->type) {
    case ITEM_HOTEL_SINGLE: case ITEM_HOTEL_TWIN: case ITEM_HOTEL_SUITE:
        return m == 2 ? 4 : 0;
    case ITEM_OFFICE:                       /* 0/1 = the Salesmen */
        return m <= 1 ? 1 : m <= 3 ? 0 : m == 4 ? 2 : 4;
    case ITEM_CONDO:                        /* 1 = Mother with Baby */
        return m == 0 ? 0 : m == 1 ? 8 : m == 2 ? 3 : -1;
    case ITEM_SECURITY:     return 5;
    case ITEM_HOUSEKEEPING: return 10;
    default:                                /* patrons: member & 7 wheel */
        switch (m & 7) {
        case 1: return 2; case 3: return 4;
        case 5: return 6; case 7: return 8;
        default: return 0;
        }
    }
}

/* Blit one person figure from the sheet (row by naming state, per the
 * EXE: row = VIP ? 2 : named ? 1 : 0 — VIP row waits on the VIP-person
 * feature). Returns 0 if the sheets are missing (caller falls back). */
static int draw_person_figure(uint16_t pid, int x, int y, int scale)
{
    const Person *p = &game.sim.people.people[pid - 1];
    int frame = person_figure_frame(p);
    if (frame < 0) return 1;   /* faithfully draw nothing */
    const char *nm = tower_person_name(&game.tower, p->home_tenant,
                                       p->member);
    /* row = VIP ? 2 : named ? 1 : 0 (1240:020d / 1188:04db). The VIP is
     * exactly the person auto-registered under the name "VIP". */
    int vip = nm && strcmp(nm, "VIP") == 0;
    Sprite *sheet = sprites_find(&game.sprites,
                                 vip ? SPR_FIGURE_VIP
                                     : nm ? SPR_FIGURE_NAMED
                                          : SPR_FIGURE_NORMAL);
    if (!sheet) return 0;
    int fw = frame >= 6 ? 16 : 8;
    SDL_Rect src = { frame * 8, 0, fw, 24 };
    /* wide frames shift left half a cell so they stay centered (372c) */
    SDL_Rect dst = { x - (frame >= 6 ? 4 * scale : 0), y,
                     fw * scale, 24 * scale };
    SDL_RenderCopy(game.renderer, sheet->texture, &src, &dst);
    return 1;
}

static void draw_person_strip(const uint16_t *pids, int n, int x, int y)
{
    Sprite *qs = sprites_find(&game.sprites, SPR_ELEV_QUEUE);
    for (int k = 0; k < n; k++) {
        if (!pids[k]) continue;
        /* the original's people lists blit the figure sheet 1:1 */
        if (draw_person_figure(pids[k], x + k * PSTRIP_PITCH + 4, y + 6, 1))
            continue;
        if (qs) {
            int fig = (pids[k] * 7) % 40;   /* legacy silhouette fallback */
            SDL_Rect src = { fig * 16, 0, 16, PSTRIP_H };
            SDL_Rect dst = { x + k * PSTRIP_PITCH, y, 16, PSTRIP_H };
            SDL_RenderCopy(game.renderer, qs->texture, &src, &dst);
        } else {
            SDL_SetRenderDrawColor(game.renderer, 40, 40, 40, 255);
            SDL_Rect dst = { x + k * PSTRIP_PITCH, y + 8, 12, PSTRIP_H - 8 };
            SDL_RenderFillRect(game.renderer, &dst);
        }
    }
}

static uint16_t person_strip_hit(const uint16_t *pids, int n, int x, int y,
                                 int mx, int my)
{
    if (my < y || my >= y + PSTRIP_H) return 0;
    int k = (mx - x) / PSTRIP_PITCH;
    if (mx < x || k < 0 || k >= n) return 0;
    return pids[k];
}

static void open_person_popup_at(uint16_t pid, int x, int y)
{
    game.person_open = 1;
    game.person_pid = pid;
    game.person_x = x + 16;
    game.person_y = y - 30;
    if (game.person_x + PINFO_W > game.screen_w)
        game.person_x = game.screen_w - PINFO_W - 8;
    if (game.person_y + PINFO_H > game.screen_h)
        game.person_y = game.screen_h - PINFO_H - 8;
    if (game.person_y < 0) game.person_y = 8;
}

static const Person *person_popup_person(void)
{
    if (!game.person_open || !game.person_pid) return NULL;
    const Person *p = &game.sim.people.people[game.person_pid - 1];
    if (!p->home_tenant) return NULL;   /* despawned under us */
    return p;
}

static void render_person_popup(void)
{
    const Person *p = person_popup_person();
    if (!p) { game.person_open = 0; return; }

    SDL_Rect d = person_popup_rect();
    char title[48], line[64], fl[12];
    person_fmt_floor(index_to_floor(p->cur_floor), fl, sizeof fl);
    snprintf(title, sizeof title, "%s  -  %s",
             person_kind_label(person_kind(p)), fl);
    draw_win31_titlebar(d.x, d.y, d.w, title);
    draw_win31_rect(d.x, d.y + WIN_TITLEBAR_H, d.w, d.h - WIN_TITLEBAR_H, 1);

    /* Portrait: the real figure sheet at 2x (blitter 1100:364a via
     * WinGStretchBlt; row picked by naming state). Falls back to the
     * old queue-silhouette stand-in if the sheets didn't load. */
    int tx = d.x + 14, ty = d.y + WIN_TITLEBAR_H + 10;
    if (!draw_person_figure(game.person_pid, tx + 8, ty + 12, 2)) {
        Sprite *qs = sprites_find(&game.sprites, SPR_ELEV_QUEUE);
        if (qs) {
            int fig = (game.person_pid * 7) % 40;
            SDL_Rect src = { fig * 16, 0, 16, 36 };
            SDL_Rect dst = { tx, ty, 32, 72 };
            SDL_RenderCopy(game.renderer, qs->texture, &src, &dst);
        }
    }

    SDL_Color ink = { 0, 0, 0, 255 };
    int lx = tx + 44, ly = ty;
    const char *nm = tower_person_name(&game.tower, p->home_tenant, p->member);
    snprintf(line, sizeof line, "Name: %s", nm ? nm : "-");
    draw_text(line, lx, ly, ink); ly += 18;
    {
        char hm[56];
        person_home_line(p, hm, sizeof hm);
        snprintf(line, sizeof line, "Home: %s", hm);
        draw_text(line, lx, ly, ink); ly += 18;
    }
    {
        char wh[56];
        person_where_line(p, wh, sizeof wh);
        snprintf(line, sizeof line, "Now: %s", wh);
        draw_text(line, lx, ly, ink); ly += 18;
    }
    {
        int cap = TUNING.wait_cap > 0 ? TUNING.wait_cap : 1;
        int pct = (int)(100L * p->wait_accum / cap);
        if (pct > 100) pct = 100;
        snprintf(line, sizeof line, "Stress: %d%%", pct);
        draw_text(line, lx, ly, ink);
    }

    draw_dlg_button(person_btn_name(d), "Name...", 1);
    draw_dlg_button(person_btn_ok(d), "OK", 1);
}

static void open_person_name_editor(void);

/* Returns 1 when the click was inside the popup (swallowed). */
static int person_popup_click(int mx, int my)
{
    if (!game.person_open) return 0;
    SDL_Rect d = person_popup_rect();
    if (!point_in_rect(mx, my, d)) { game.person_open = 0; return 0; }
    if (point_in_rect(mx, my, person_btn_ok(d)))        game.person_open = 0;
    else if (point_in_rect(mx, my, person_btn_name(d))) open_person_name_editor();
    return 1;
}

static void render_inspect_popup(void)
{
    if (!game.inspect_open) return;
    Tenant *t = tower_tenant(&game.tower, game.inspect_tid);
    if (!t) { game.inspect_open = 0; return; }
    TiLayout L; ti_build(t, &L);
    int wx = game.inspect_x, wy = game.inspect_y;
    SDL_Color ink = { 0, 0, 0, 255 }, warn = { 150, 20, 20, 255 };

    SDL_Rect panel = { wx, wy, L.w, L.h };
    SDL_SetRenderDrawColor(game.renderer, 198, 198, 198, 255);
    SDL_RenderFillRect(game.renderer, &panel);
    draw_bevel(panel, 1);
    SDL_SetRenderDrawColor(game.renderer, 90, 90, 90, 255);
    SDL_RenderDrawRect(game.renderer, &panel);

    char title[72]; inspect_title(t, title, sizeof title);
    stats_label(wx + 10, wy + L.name_y, title, ink);
    SDL_SetRenderDrawColor(game.renderer, 120, 120, 120, 255);
    SDL_RenderDrawLine(game.renderer, wx + 8, wy + 28, wx + L.w - 8, wy + 28);

    SDL_Rect pic = { wx + L.picture.x, wy + L.picture.y, L.picture.w, L.picture.h };
    draw_tenant_picture(t, pic);

    for (int i = 0; i < L.comment_n; i++)
        stats_label(wx + 10, wy + L.comment_y + i * 14, L.comments[i], warn);

    int y = L.field_y0;
    for (int i = 0; i < L.nf; i++) {
        TiField *f = &L.fields[i];
        stats_label(wx + 10, wy + y, f->label, ink);
        if (f->kind == TIF_EVAL) {
            SDL_Rect g = { wx + 10 + 96, wy + y + 1, 116, 12 };
            draw_eval_gauge(g, f->ival, 80, f->imax);
        } else if (f->kind == TIF_PATRON) {
            SDL_Rect g = { wx + 10 + 96, wy + y + 1, 84, 12 };
            draw_patron_bar(g, f->ival, f->imax);
            char num[16]; snprintf(num, 16, "%d", f->ival);
            stats_label(g.x + g.w + 6, wy + y, num, ink);
        } else if (f->kind == TIF_PRICE) {
            /* the dropdown is drawn last, so its open list overlays fields */
        } else {
            stats_label(wx + 10 + 96, wy + y, f->value, ink);
        }
        y += 20;
    }

    if (L.price_field >= 0) {
        SDL_Rect box = { wx + L.price_box.x, wy + L.price_box.y, L.price_box.w, L.price_box.h };
        SDL_Rect items[4];
        for (int k = 0; k < 4; k++)
            items[k] = (SDL_Rect){ wx + L.price_items[k].x, wy + L.price_items[k].y,
                                   L.price_items[k].w, L.price_items[k].h };
        draw_price_dropdown(t, box, items);
    }

    if (L.occ_n)
        draw_person_strip(L.occ_pid, L.occ_n,
                          wx + L.occ_row.x, wy + L.occ_row.y);

    draw_dlg_button((SDL_Rect){ wx + L.rename_btn.x, wy + L.rename_btn.y, L.rename_btn.w, L.rename_btn.h },
                    "Rename", 1);
    if (L.has_newmovie)
        draw_dlg_button((SDL_Rect){ wx + L.newmovie_btn.x, wy + L.newmovie_btn.y,
                                    L.newmovie_btn.w, L.newmovie_btn.h }, "New Movie", 1);
    draw_dlg_button((SDL_Rect){ wx + L.ok_btn.x, wy + L.ok_btn.y, L.ok_btn.w, L.ok_btn.h }, "OK", 1);
}

/* ---- Name-editor sub-dialog (res 0x2DC: EDIT + Rename/Delete/Cancel) ----
 * Shared between tenants and people (name_edit_person selects). */
static void close_name_editor(void) { game.name_edit_open = 0; SDL_StopTextInput(); }
static void open_name_editor(const Tenant *t)
{
    const char *cur = tenant_custom_name(t->id);
    snprintf(game.name_edit_buf, sizeof game.name_edit_buf, "%s", cur ? cur : "");
    game.name_edit_len = (int)strlen(game.name_edit_buf);
    game.name_edit_open = 1;
    game.name_edit_person = 0;
    SDL_StartTextInput();
}
static void open_person_name_editor(void)
{
    const Person *p = person_popup_person();
    if (!p) return;
    const char *cur = tower_person_name(&game.tower, p->home_tenant, p->member);
    snprintf(game.name_edit_buf, sizeof game.name_edit_buf, "%s", cur ? cur : "");
    game.name_edit_len = (int)strlen(game.name_edit_buf);
    game.name_edit_open = 1;
    game.name_edit_person = 1;
    SDL_StartTextInput();
}
static void commit_name(void)
{
    if (game.name_edit_len > 0) {
        if (game.name_edit_person) {
            const Person *p = person_popup_person();
            if (p && tower_person_name_set(&game.tower, p->home_tenant,
                                           p->member, game.name_edit_buf) < 0)
                add_event_message("You may only name 20 people.");
        } else {
            tenant_set_name(game.inspect_tid, game.name_edit_buf);
        }
    }
    close_name_editor();
}
#define NAME_DLG_W 244
#define NAME_DLG_H 98
static SDL_Rect name_dlg_rect(void)
{ return (SDL_Rect){ (game.screen_w - NAME_DLG_W) / 2, (game.screen_h - NAME_DLG_H) / 2, NAME_DLG_W, NAME_DLG_H }; }
static SDL_Rect name_dlg_edit(SDL_Rect d)   { return (SDL_Rect){ d.x + 12, d.y + 34, d.w - 24, 22 }; }
static SDL_Rect name_dlg_ok(SDL_Rect d)     { return (SDL_Rect){ d.x + 12, d.y + 66, 64, 22 }; }
static SDL_Rect name_dlg_del(SDL_Rect d)    { return (SDL_Rect){ d.x + 84, d.y + 66, 64, 22 }; }
static SDL_Rect name_dlg_cancel(SDL_Rect d) { return (SDL_Rect){ d.x + d.w - 76, d.y + 66, 64, 22 }; }

static void render_name_editor(void)
{
    if (!game.name_edit_open) return;
    SDL_Color ink = { 0, 0, 0, 255 };
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 96);
    SDL_Rect full = { 0, 0, game.screen_w, game.screen_h };
    SDL_RenderFillRect(game.renderer, &full);
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);

    SDL_Rect d = name_dlg_rect();
    SDL_SetRenderDrawColor(game.renderer, 198, 198, 198, 255);
    SDL_RenderFillRect(game.renderer, &d);
    draw_bevel(d, 1);
    SDL_SetRenderDrawColor(game.renderer, 90, 90, 90, 255);
    SDL_RenderDrawRect(game.renderer, &d);
    stats_label(d.x + 12, d.y + 10,
                game.name_edit_person ? "Person's name:" : "Tenant's name:",
                ink);

    SDL_Rect e = name_dlg_edit(d);
    SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(game.renderer, &e);
    SDL_SetRenderDrawColor(game.renderer, 60, 60, 60, 255);
    SDL_RenderDrawRect(game.renderer, &e);
    int tw = 0, th = 0;
    if (game.name_edit_len > 0) {
        SDL_Texture *tex = render_text(game.name_edit_buf, ink, &tw, &th);
        if (tex) { SDL_Rect td = { e.x + 5, e.y + 4, tw, th };
                   SDL_RenderCopy(game.renderer, tex, NULL, &td); SDL_DestroyTexture(tex); }
    }
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
    int cx = e.x + 5 + tw + 1;
    SDL_RenderDrawLine(game.renderer, cx, e.y + 4, cx, e.y + e.h - 4);

    draw_dlg_button(name_dlg_ok(d), "Rename", 1);
    {
        int has_name;
        if (game.name_edit_person) {
            const Person *p = person_popup_person();
            has_name = p && tower_person_name(&game.tower, p->home_tenant,
                                              p->member) != NULL;
        } else {
            has_name = tenant_custom_name(game.inspect_tid) != NULL;
        }
        draw_dlg_button(name_dlg_del(d), "Delete", has_name);
    }
    draw_dlg_button(name_dlg_cancel(d), "Cancel", 1);
}
static int name_editor_click(int mx, int my)
{
    if (!game.name_edit_open) return 0;
    SDL_Rect d = name_dlg_rect();
    if (point_in_rect(mx, my, name_dlg_ok(d)))          commit_name();
    else if (point_in_rect(mx, my, name_dlg_del(d))) {
        if (game.name_edit_person) {
            const Person *p = person_popup_person();
            if (p) tower_person_name_clear(&game.tower, p->home_tenant,
                                           p->member);
        } else {
            tenant_clear_name(game.inspect_tid);
        }
        close_name_editor();
    }
    else if (point_in_rect(mx, my, name_dlg_cancel(d))) close_name_editor();
    return 1;   /* modal: swallow every click */
}
static void name_editor_textinput(const char *txt)
{
    for (const char *p = txt; *p; p++)
        if (game.name_edit_len < 15 && (unsigned char)*p >= 32) {
            game.name_edit_buf[game.name_edit_len++] = *p;
            game.name_edit_buf[game.name_edit_len] = 0;
        }
}
static void name_editor_key(SDL_Keycode k)
{
    if (k == SDLK_BACKSPACE && game.name_edit_len > 0) game.name_edit_buf[--game.name_edit_len] = 0;
    else if (k == SDLK_RETURN || k == SDLK_KP_ENTER)  commit_name();
    else if (k == SDLK_ESCAPE)                        close_name_editor();
}

/* ---- Movie-chooser sub-dialog (res 0x2DB: two buy-buttons + Cancel) ---- */
#define MOVIE_DLG_W 260
#define MOVIE_DLG_H 128
static SDL_Rect movie_dlg_rect(void)
{ return (SDL_Rect){ (game.screen_w - MOVIE_DLG_W) / 2, (game.screen_h - MOVIE_DLG_H) / 2, MOVIE_DLG_W, MOVIE_DLG_H }; }
static SDL_Rect movie_btn_hit(SDL_Rect d)    { return (SDL_Rect){ d.x + 12, d.y + 60, d.w - 24, 22 }; }
static SDL_Rect movie_btn_ord(SDL_Rect d)    { return (SDL_Rect){ d.x + 12, d.y + 86, d.w - 24, 22 }; }
static SDL_Rect movie_btn_cancel(SDL_Rect d) { return (SDL_Rect){ d.x + d.w - 76, d.y + d.h - 26, 64, 20 }; }

static void render_movie_chooser(void)
{
    if (!game.movie_dlg_open) return;
    Tenant *t = tower_tenant(&game.tower, game.inspect_tid);
    if (!t || !inspect_is_cinema(t)) { game.movie_dlg_open = 0; return; }
    SDL_Color ink = { 0, 0, 0, 255 };
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 96);
    SDL_Rect full = { 0, 0, game.screen_w, game.screen_h };
    SDL_RenderFillRect(game.renderer, &full);
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);

    SDL_Rect d = movie_dlg_rect();
    SDL_SetRenderDrawColor(game.renderer, 198, 198, 198, 255);
    SDL_RenderFillRect(game.renderer, &d);
    draw_bevel(d, 1);
    SDL_SetRenderDrawColor(game.renderer, 90, 90, 90, 255);
    SDL_RenderDrawRect(game.renderer, &d);
    stats_label(d.x + 12, d.y + 10, "Book a new movie:", ink);
    char now[64]; snprintf(now, sizeof now, "Now: %s",
                           movie_title(t->movie_id));
    stats_label(d.x + 12, d.y + 32, now, ink);

    long money = game.tower.money;
    draw_dlg_button(movie_btn_hit(d), "Hit movie          $300,000", money >= MOVIE_COST_HIT);
    draw_dlg_button(movie_btn_ord(d), "Ordinary movie   $150,000", money >= MOVIE_COST_ORDINARY);
    draw_dlg_button(movie_btn_cancel(d), "Cancel", 1);
}
static int movie_chooser_click(int mx, int my)
{
    if (!game.movie_dlg_open) return 0;
    Tenant *t = tower_tenant(&game.tower, game.inspect_tid);
    if (!t) { game.movie_dlg_open = 0; return 1; }
    SDL_Rect d = movie_dlg_rect();
    long money = game.tower.money;
    if (point_in_rect(mx, my, movie_btn_hit(d)) && money >= MOVIE_COST_HIT) {
        game_change_movie(&game.sim, &game.tower, t, 1);
        add_event_message("New movie showing! - $300,000");
        game.movie_dlg_open = 0;
    } else if (point_in_rect(mx, my, movie_btn_ord(d)) && money >= MOVIE_COST_ORDINARY) {
        game_change_movie(&game.sim, &game.tower, t, 0);
        add_event_message("New movie showing! - $150,000");
        game.movie_dlg_open = 0;
    } else if (point_in_rect(mx, my, movie_btn_cancel(d))) {
        game.movie_dlg_open = 0;
    }
    return 1;   /* modal: swallow every click */
}

/* Returns 1 if the click landed in the popup (consumed). */
static int inspect_popup_click(int mx, int my)
{
    if (!game.inspect_open) return 0;
    Tenant *t = tower_tenant(&game.tower, game.inspect_tid);
    if (!t) { game.inspect_open = 0; return 0; }
    TiLayout L; ti_build(t, &L);
    int wx = game.inspect_x, wy = game.inspect_y;
    #define TIABS(r) (SDL_Rect){ wx + (r).x, wy + (r).y, (r).w, (r).h }

    /* An open dropdown: an item click selects; anything else just closes it. */
    if (game.rent_dd_open && L.price_field >= 0) {
        for (int k = 0; k < 4; k++)
            if (point_in_rect(mx, my, TIABS(L.price_items[k]))) {
                if (game_set_rent_class(&game.sim, &game.tower, t, k)) {
                    char msg[EVENT_MSG_LEN];
                    snprintf(msg, sizeof msg, "%s on F%d back on the market",
                             tower_item_name(t->type), t->floor);
                    add_event_message(msg);
                }
                game.rent_dd_open = 0;
                return 1;
            }
        game.rent_dd_open = 0;
    }
    if (L.price_field >= 0 && point_in_rect(mx, my, TIABS(L.price_box))) {
        game.rent_dd_open = !game.rent_dd_open;
        return 1;
    }
    if (point_in_rect(mx, my, TIABS(L.rename_btn))) { open_name_editor(t); return 1; }
    if (L.has_newmovie && point_in_rect(mx, my, TIABS(L.newmovie_btn))) {
        game.movie_dlg_open = 1;
        return 1;
    }
    if (point_in_rect(mx, my, TIABS(L.ok_btn))) {
        game.inspect_open = 0; game.rent_dd_open = 0;
        return 1;
    }
    if (L.occ_n) {
        uint16_t pid = person_strip_hit(L.occ_pid, L.occ_n,
                                        wx + L.occ_row.x, wy + L.occ_row.y,
                                        mx, my);
        if (pid) { open_person_popup_at(pid, mx, my); return 1; }
    }
    SDL_Rect panel = { wx, wy, L.w, L.h };
    if (point_in_rect(mx, my, panel)) return 1;   /* swallow body clicks */
    return 0;
    #undef TIABS
}

/* ---------- Find Person... / Find Tenant... (menu 40019/40020) ----------
 * The EXE's modal dialogs (10d8:0000, RT_DIALOG 0x1FE/0x208): a listbox
 * of NAMED entries only (the 20-slot registries), OK / Remove / Find,
 * Remove+Find disabled until a selection, double-click = Find. Find
 * pauses the game, arms the inspect tool, and centers the camera on the
 * target (10e0:0cea); a despawned person gets "^0 is not in this tower."
 * The sim halts while the dialog is open — DialogBoxParam is modal. */
#define FIND_W     240
#define FIND_ROW_H 14
#define FIND_ROWS  20

typedef struct { uint16_t tid; uint8_t member; const char *name; } FindRow;

static int find_build_rows(FindRow *rows)
{
    int n = 0;
    if (game.find_open == 1) {
        for (int i = 0; i < 20 && n < FIND_ROWS; i++) {
            const struct PersonNameSlot *s = &game.tower.person_names[i];
            if (!s->tenant_id) continue;
            rows[n++] = (FindRow){ s->tenant_id, s->member, s->name };
        }
    } else {
        for (int i = 0; i < game.tower.tenant_count && n < FIND_ROWS; i++) {
            Tenant *t = &game.tower.tenants[i];
            if (t->name[0] && t->state != TENANT_ABANDONED)
                rows[n++] = (FindRow){ t->id, 0, t->name };
        }
    }
    return n;
}

static void find_window_rect(int n, SDL_Rect *r)
{
    int h = WIN_TITLEBAR_H + 8 + (n > 0 ? n : 1) * FIND_ROW_H + 40;
    r->x = (game.screen_w - FIND_W) / 2;
    r->y = (game.screen_h - h) / 2;
    r->w = FIND_W; r->h = h;
}

static void render_find_dialog(void)
{
    if (!game.find_open) return;
    FindRow rows[FIND_ROWS];
    int n = find_build_rows(rows);
    SDL_Rect r; find_window_rect(n, &r);
    draw_win31_titlebar(r.x, r.y, r.w, game.find_open == 1 ? "Find Person"
                                                           : "Find Tenant");
    draw_win31_rect(r.x, r.y + WIN_TITLEBAR_H, r.w, r.h - WIN_TITLEBAR_H, 1);

    SDL_Color ink = { 0, 0, 0, 255 };
    SDL_Color dim = { 130, 130, 130, 255 };
    int ly = r.y + WIN_TITLEBAR_H + 4;
    /* listbox panel: white, like the EXE's LISTBOX with WHITE_BRUSH */
    SDL_Rect lb = { r.x + 6, ly, r.w - 12, (n > 0 ? n : 1) * FIND_ROW_H + 2 };
    SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(game.renderer, &lb);
    SDL_SetRenderDrawColor(game.renderer, 90, 90, 90, 255);
    SDL_RenderDrawRect(game.renderer, &lb);
    if (!n)
        stats_label(lb.x + 6, ly + 2, "(no one has been named)", dim);
    for (int k = 0; k < n; k++) {
        if (k == game.find_sel) {
            SDL_Rect hi = { lb.x + 1, ly + 1 + k * FIND_ROW_H,
                            lb.w - 2, FIND_ROW_H };
            SDL_SetRenderDrawColor(game.renderer, 0, 0, 128, 255);
            SDL_RenderFillRect(game.renderer, &hi);
        }
        stats_label(lb.x + 6, ly + 2 + k * FIND_ROW_H, rows[k].name,
                    k == game.find_sel ? (SDL_Color){255,255,255,255} : ink);
    }
    int by = lb.y + lb.h + 8;
    int sel = game.find_sel >= 0 && game.find_sel < n;
    draw_win31_rect(r.x + 8, by, 56, 20, 1);
    stats_label(r.x + 20, by + 4, "OK", ink);
    draw_win31_rect(r.x + 72, by, 70, 20, 1);
    stats_label(r.x + 82, by + 4, "Remove", sel ? ink : dim);
    draw_win31_rect(r.x + 150, by, 60, 20, 1);
    stats_label(r.x + 166, by + 4, "Find", sel ? ink : dim);
}

/* Center the camera on a grid cell with the SmoothScroll glide. */
static void find_center_camera(int fidx, int cell)
{
    game.cam_ty = (float)(-index_to_floor(fidx) * CELL_H);
    game.cam_tx = (float)(cell * CELL_W - game.screen_w / 2);
    game.cam_anim = 1;
}

static void find_do_find(const FindRow *row)
{
    if (game.find_open == 1) {
        PeopleSim *ps = &game.sim.people;
        for (int i = 0; i < ps->people_high; i++) {
            Person *p = &ps->people[i];
            if (p->home_tenant != row->tid || p->member != row->member ||
                !p->home_tenant) continue;
            /* found: pause ([0x783E]=0), inspect tool ([0x783C]=2),
             * CenterCameraOn, and open their popup */
            game.find_open = 0;
            game.sim.speed = SPEED_PAUSED;
            game.inspect_mode = 1;
            game.demolish_mode = game.finger_mode = 0;
            game.build_type = ITEM_NONE;
            find_center_camera(p->cur_floor, p->x);
            open_person_popup_at((uint16_t)(i + 1),
                                 game.screen_w / 2, game.screen_h / 2);
            return;
        }
        char msg[64];
        snprintf(msg, sizeof msg, "%s is not in this tower.", row->name);
        add_event_message(msg);           /* template 0x7f01:0x3EA */
        return;                           /* dialog stays open */
    }
    Tenant *t = tower_tenant(&game.tower, row->tid);
    if (!t) return;
    game.find_open = 0;
    game.sim.speed = SPEED_PAUSED;
    game.inspect_mode = 1;
    game.demolish_mode = game.finger_mode = 0;
    game.build_type = ITEM_NONE;
    find_center_camera(floor_to_index(t->floor), t->x + t->width / 2);
    open_tenant_popup_at(row->tid, game.screen_w / 2, game.screen_h / 2);
}

static void find_dialog_click(int mx, int my, int clicks)
{
    FindRow rows[FIND_ROWS];
    int n = find_build_rows(rows);
    SDL_Rect r; find_window_rect(n, &r);
    int ly = r.y + WIN_TITLEBAR_H + 4;
    SDL_Rect lb = { r.x + 6, ly, r.w - 12, (n > 0 ? n : 1) * FIND_ROW_H + 2 };
    if (mx >= lb.x && mx < lb.x + lb.w && my >= lb.y && my < lb.y + lb.h) {
        int k = (my - ly - 1) / FIND_ROW_H;
        if (k >= 0 && k < n) {
            game.find_sel = k;
            if (clicks >= 2) find_do_find(&rows[k]);   /* LBN_DBLCLK */
        }
        return;
    }
    int by = lb.y + lb.h + 8;
    int sel = game.find_sel >= 0 && game.find_sel < n;
    if (my >= by && my < by + 20) {
        if (mx >= r.x + 8 && mx < r.x + 64) { game.find_open = 0; return; }
        if (sel && mx >= r.x + 72 && mx < r.x + 142) {     /* Remove */
            if (game.find_open == 1)
                tower_person_name_clear(&game.tower, rows[game.find_sel].tid,
                                        rows[game.find_sel].member);
            else {
                Tenant *t = tower_tenant(&game.tower, rows[game.find_sel].tid);
                if (t) t->name[0] = 0;
            }
            game.find_sel = -1;
            return;
        }
        if (sel && mx >= r.x + 150 && mx < r.x + 210)      /* Find */
            find_do_find(&rows[game.find_sel]);
    }
}

static void render(void)
{
    /* Cursor follows the active tool: crosshair while bulldozing, else arrow. */
    SDL_Cursor *want = game.demolish_mode ? game.cursor_demolish : game.cursor_arrow;
    if (want && SDL_GetCursor() != want) SDL_SetCursor(want);

    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
    SDL_RenderClear(game.renderer);

    render_sky();
    if (game.elv_edit_mode) {
        render_elv_edit_mode();   /* silhouette tower + isolated shaft */
    } else {
        render_tower();        /* includes in-tenant people, under the shafts */
        render_people();
        render_events();       /* fire/bomb effects ON TOP of the burning floors */
        render_fire_glow();    /* warm tint washed over the world while it burns */
        render_crane();
        render_build_ghost();
    }
    render_ui();
    render_stats_window();
    render_tuning_window();
    render_elv_dialog();
    render_fin_dialog();
    render_inspect_popup();
    render_person_popup();
    render_movie_chooser();   /* sub-dialogs draw on top of the info popup */
    render_name_editor();
    render_event_alert();
    render_find_dialog();      /* modal: above the popups */
    render_menu_bar();         /* Win3.1 top bar + its open dropdown */
    render_dropdown();
    render_disaster_modal();   /* on top of everything — it's modal */
    render_notice_modal();     /* resolution/VIP notices, same layer */
    render_route_confirm();    /* likewise */

    SDL_RenderPresent(game.renderer);
}

/* ---------- Build type cycling ---------- */
/* All placeable types for keyboard cycling */
static const ItemType CYCLE_TYPES[] = {
    ITEM_LOBBY,
    ITEM_OFFICE, ITEM_CONDO, ITEM_RESTAURANT, ITEM_FAST_FOOD,
    ITEM_HOTEL_SINGLE, ITEM_HOTEL_TWIN, ITEM_HOTEL_SUITE,
    ITEM_SHOP, ITEM_CINEMA, ITEM_PARTY_HALL,
    ITEM_SECURITY, ITEM_MEDICAL, ITEM_CATHEDRAL,
    ITEM_PARKING, ITEM_METRO, ITEM_RECYCLING,
    ITEM_STAIRS, ITEM_ESCALATOR
};
#define CYCLE_COUNT (int)(sizeof(CYCLE_TYPES)/sizeof(CYCLE_TYPES[0]))

static int cycle_index = 0;

static void cycle_build_type(int direction)
{
    cycle_index = (cycle_index + direction + CYCLE_COUNT) % CYCLE_COUNT;
    game.build_type = CYCLE_TYPES[cycle_index];
    printf("Build: %s (%d cells × %d floors, $%d)\n",
           tower_item_name(game.build_type),
           ITEM_WIDTH[game.build_type],
           ITEM_HEIGHT[game.build_type],
           ITEM_COST[game.build_type]);
}

/* ---------- Drag placement ---------- */

/* The faithful way to add an elevator car: select the elevator tool and click
 * an EXISTING shaft of the same type (within its span). Returns 1 if it handled
 * the click (car added, or refused for funds/max) so the caller skips the
 * normal placement attempt; 0 if there's no such shaft here (e.g. clicking the
 * cap to extend, or empty space to build a new shaft). */
static int elev_add_car_at(int x, int floor)
{
    int fidx = floor_to_index(floor);
    if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) return 0;
    PeopleSim *ps = &game.sim.people;
    for (int i = 0; i < ps->shaft_count; i++) {
        ElevatorShaft *s = &ps->shafts[i];
        if (!s->active || s->type != (ItemType)game.build_type || s->x != x)
            continue;
        if (fidx < s->lo || fidx > s->hi) continue;   /* outside span = cap/extend */
        if (s->num_cars >= CARS_PER_SHAFT) {
            printf("Shaft x=%d already at the max %d cars\n", x, CARS_PER_SHAFT);
            return 1;
        }
        int cost = elv_car_cost(s->type);
        if (game.tower.money < cost) {
            printf("Can't afford a car ($%d)\n", cost);
            return 1;
        }
        game.tower.money -= cost;
        game.tower.built_value += cost;
        people_set_num_cars(ps, i, s->num_cars + 1);
        /* The EXE sets the new car's home to the CLICKED floor (PlaceElevator
         * add-a-car path: car home (+0xBA+slot) = floor) and spawns it there. */
        {
            int ci = s->num_cars - 1;
            people_set_home(ps, i, ci, fidx);
            s->car[ci].floor = s->car[ci].target = (uint8_t)fidx;
        }
        printf("Added car to shaft x=%d: now %d cars (home F%d, cost $%d)\n",
               x, s->num_cars, floor, cost);
        return 1;
    }
    return 0;
}

static void drag_place_units(void)
{
    if (game.build_type <= ITEM_NONE || game.build_type >= ITEM_TYPE_COUNT) return;
    if (!item_unlocked(game.build_type)) return;   /* locked in Campaign */

    int width = ITEM_WIDTH[game.build_type];
    int floor = build_origin_floor(game.build_type, game.drag_start_floor);
    int start = game.drag_start_cell;
    int end = game.mouse_cell;
    int step_dir = (end >= start) ? 1 : -1;
    int placed = 0;

    /* Elevators drag vertically: extend the shaft one segment per floor,
     * column locked to the shaft the drag started on (or its caps). */
    if (item_is_elevator(game.build_type)) {
        int x = elevator_drag_column();
        int f1 = game.mouse_floor;
        int df = (f1 >= floor) ? 1 : -1;
        for (int f = floor; ; f += df) {
            if (tower_place(&game.tower, game.build_type, f, x))
                placed++;
            if (f == f1) break;
        }
        if (placed > 0) {
            play_snd(SND_BUILD_PLACE);   /* shaft extend (referee row 17) */
            printf("Drag-extended %s at x=%d by %d floor(s)\n",
                   tower_item_name(game.build_type), x, placed);
        } else {
            /* Nothing new placed — the click/drag landed entirely on an existing
             * shaft, so add a car to it instead. */
            elev_add_car_at(x, game.drag_start_floor);
        }
        return;
    }

    /* The floor tool extends the deck over the exact dragged span, charged
     * per newly-decked cell (a bare click goes through tower_place and
     * stamps a default-width strip instead). */
    if (game.build_type == ITEM_FLOOR) {
        int lo = start < end ? start : end;
        int hi = start < end ? end : start;
        if (tower_extend_deck(&game.tower, floor, lo, hi + 1)) {
            play_snd(SND_BUILD_PLACE);
        } else if (tower_reject_reason()[0]) {
            add_event_message(tower_reject_reason());
        }
        return;
    }

    if (step_dir > 0) {
        for (int cur = start; cur + width - 1 < TOWER_WIDTH && cur <= end; cur += width) {
            if (tower_place(&game.tower, game.build_type, floor, cur))
                placed++;
        }
    } else {
        for (int cur = start; cur >= 0 && cur >= end; cur -= width) {
            if (cur + width <= TOWER_WIDTH) {
                if (tower_place(&game.tower, game.build_type, floor, cur))
                    placed++;
            }
        }
    }
    
    if (placed > 0) {
        play_snd(SND_BUILD_PLACE);   /* place/stamp confirm (referee row 17) */
        printf("Drag-placed %d %s(s) on floor %d\n",
               placed, tower_item_name(game.build_type), floor);
    } else if (tower_reject_reason()[0]) {
        /* Nothing went down — tell the player why (the original shows the
         * res-0x3eb placement error; ours lands in the event feed). */
        add_event_message(tower_reject_reason());
    }
}

/* ---------- Toolbox click helper ---------- */
static int toolbox_click(int mx, int my)
{
    if (!game.win_toolbar) return 0;
    int wx = game.tool_x;
    int wy = game.tool_y + WIN_TITLEBAR_H;  /* Skip title bar */
    
    /* Single play/pause toggle — must match render_toolbox layout. */
    int speed_y = wy + 8;
    {
        int bw = SPEED_BTN_W, bh = SPEED_BTN_H;
        int sx = wx + (TOOL_WIN_W - bw) / 2;
        if (my >= speed_y && my < speed_y + bh && mx >= sx && mx < sx + bw) {
            game.sim.speed = (game.sim.speed > 0) ? SPEED_PAUSED : SPEED_NORMAL;
            return 1;
        }
    }

    /* Tool action buttons (bulldozer / finger / inspector) — must match render. */
    {
        int tools_y = speed_y + 28;
        int tx = wx + (TOOL_WIN_W - 3 * 21) / 2;
        if (my >= tools_y && my < tools_y + 21) {
            for (int t = 0; t < 3; t++) {
                int bxt = tx + t * 23;
                if (mx >= bxt && mx < bxt + 21) {
                    if (t == 0) {                 /* Bulldozer: toggle demolish mode */
                        game.demolish_mode = !game.demolish_mode;
                        if (game.demolish_mode) {
                            game.tool_popup = -1;
                            game.finger_mode = game.inspect_mode = 0;
                        }
                        printf("Bulldozer %s\n", game.demolish_mode ? "ON" : "off");
                    } else if (t == 1) {          /* Finger/pointer: interact mode */
                        game.finger_mode = !game.finger_mode;
                        game.demolish_mode = game.inspect_mode = 0;
                        if (game.finger_mode) {
                            game.build_type = ITEM_NONE;
                            game.tool_popup = -1;
                        }
                        printf("Finger tool %s\n", game.finger_mode ? "ON" : "off");
                    } else {                      /* Inspector: click a unit for info */
                        game.inspect_mode = !game.inspect_mode;
                        game.demolish_mode = game.finger_mode = 0;
                        if (game.inspect_mode) {
                            game.build_type = ITEM_NONE;
                            game.tool_popup = -1;
                        }
                        printf("Inspector %s\n", game.inspect_mode ? "ON" : "off");
                    }
                    return 1;
                }
            }
        }
    }

    /* If a pull-down is open, a click on one of its sub-items selects that item. */
    if (game.tool_popup >= 0 && game.tool_popup < TOOL_BTN_COUNT) {
        int nvis = tool_sub_visible_count(game.tool_popup);
        const ToolButton *g = &tool_buttons[game.tool_popup];
        for (int vj = 0; vj < nvis; vj++) {
            int j = tool_sub_visible_index(game.tool_popup, vj);
            if (j < 0) continue;
            int bx, by;
            tool_sub_rect(game.tool_popup, vj, &bx, &by);
            if (mx >= bx && mx < bx + TOOL_BTN_SIZE && my >= by && my < by + TOOL_BTN_SIZE) {
                game.build_type = g->sub[j];
                game.tool_popup = -1;
                game.demolish_mode = 0;
                game.finger_mode = game.inspect_mode = 0;
                printf("Build: %s\n", tower_item_name(game.build_type));
                return 1;
            }
        }
    }

    /* Item buttons grid (groups toggle their pull-down; singles select directly). */
    for (int i = 0; i < TOOL_BTN_COUNT; i++) {
        if (tool_visible_slot(i) < 0) continue;   /* locked in Campaign */
        int bx, by;
        tool_button_rect(i, &bx, &by);
        if (mx >= bx && mx < bx + TOOL_BTN_SIZE && my >= by && my < by + TOOL_BTN_SIZE) {
            const ToolButton *tb = &tool_buttons[i];
            if (tool_sub_visible_count(i) > 1) {
                game.tool_popup = (game.tool_popup == i) ? -1 : i;  /* toggle menu */
                game.build_type = tb->type;   /* primary (= sub[0] for a group) */
            } else if (tb->sub_count > 0) {
                /* Group with a single unlocked option — select it directly,
                 * no pull-down (matches the hidden arrow). */
                game.tool_popup = -1;
                int vj = tool_sub_visible_index(i, 0);
                game.build_type = (vj >= 0) ? tb->sub[vj] : tb->type;
            } else {
                game.tool_popup = -1;
                game.build_type = tb->type;
            }
            game.demolish_mode = 0;
            game.finger_mode = game.inspect_mode = 0;
            printf("Build: %s\n", tower_item_name(game.build_type));
            return 1;
        }
    }

    /* Click elsewhere while a pull-down is open dismisses it (and consumes the
     * click so it can't accidentally place a facility in the tower). */
    if (game.tool_popup >= 0) {
        game.tool_popup = -1;
        return 1;
    }
    return 0;
}

/* ---------- Window title bar hit test ---------- */
/* Returns: 1=info, 2=map, 3=toolbox, 0=none */
static int titlebar_hit_test(int mx, int my)
{
    /* Info bar title bar (only the thin title strip, not the whole bar) */
    if (game.win_infobar &&
        mx >= game.info_x && mx < game.info_x + INFO_BAR_W &&
        my >= game.info_y && my < game.info_y + WIN_TITLEBAR_H) {
        return 1;
    }

    /* Minimap title bar */
    if (game.win_map &&
        mx >= game.map_x && mx < game.map_x + MAP_WIN_W &&
        my >= game.map_y && my < game.map_y + WIN_TITLEBAR_H) {
        return 2;
    }

    /* Toolbox title bar */
    if (game.win_toolbar &&
        mx >= game.tool_x && mx < game.tool_x + TOOL_WIN_W &&
        my >= game.tool_y && my < game.tool_y + WIN_TITLEBAR_H) {
        return 3;
    }

    /* Elevator dialog title bar */
    if (game.elv_open &&
        mx >= game.elv_x && mx < game.elv_x + ELV_W &&
        my >= game.elv_y && my < game.elv_y + WIN_TITLEBAR_H) {
        return 4;
    }

    /* Financial report title bar */
    if (game.fin_open &&
        mx >= game.fin_x && mx < game.fin_x + FIN_DLG_W &&
        my >= game.fin_y && my < game.fin_y + WIN_TITLEBAR_H) {
        return 7;
    }

    /* Analytics (F3) title bar */
    if (game.show_stats) {
        int wx, wy;
        stats_window_origin(&wx, &wy);
        if (mx >= wx && mx < wx + STATS_W &&
            my >= wy && my < wy + WIN_TITLEBAR_H)
            return 5;
    }

    /* Tuning (F4) title bar */
    if (game.show_tuning) {
        int wx, wy;
        tune_window_origin(&wx, &wy);
        if (mx >= wx && mx < wx + TUNE_W &&
            my >= wy && my < wy + WIN_TITLEBAR_H)
            return 6;
    }

    return 0;
}

/* Check if a point is inside any UI window (body or title bar).
 * Used to prevent clicks from falling through to the game world. */
static int point_in_any_window(int mx, int my)
{
    /* Info bar */
    if (game.win_infobar &&
        mx >= game.info_x && mx < game.info_x + INFO_BAR_W &&
        my >= game.info_y && my < game.info_y + INFO_BAR_H + WIN_TITLEBAR_H) {
        return 1;
    }
    /* Minimap */
    if (game.win_map &&
        mx >= game.map_x && mx < game.map_x + MAP_WIN_W &&
        my >= game.map_y && my < game.map_y + MAP_WIN_H) {
        return 1;
    }
    /* Toolbox */
    if (game.win_toolbar &&
        mx >= game.tool_x && mx < game.tool_x + TOOL_WIN_W &&
        my >= game.tool_y && my < game.tool_y + TOOL_WIN_H) {
        return 1;
    }
    /* Elevator dialog */
    if (game.elv_open &&
        mx >= game.elv_x && mx < game.elv_x + ELV_W &&
        my >= game.elv_y && my < game.elv_y + WIN_TITLEBAR_H + ELV_DLG_H + ELV_RIDERS_H) {
        return 1;
    }
    /* Financial report */
    if (game.fin_open &&
        mx >= game.fin_x && mx < game.fin_x + FIN_DLG_W &&
        my >= game.fin_y && my < game.fin_y + WIN_TITLEBAR_H + FIN_DLG_H) {
        return 1;
    }
    /* Analytics window */
    if (game.show_stats) {
        int wx, wy;
        stats_window_origin(&wx, &wy);
        if (mx >= wx && mx < wx + STATS_W &&
            my >= wy && my < wy + WIN_TITLEBAR_H + STATS_H)
            return 1;
    }
    /* Tuning window */
    if (game.show_tuning) {
        int wx, wy;
        tune_window_origin(&wx, &wy);
        if (mx >= wx && mx < wx + TUNE_W &&
            my >= wy && my < wy + WIN_TITLEBAR_H + 26 + TUNE_ROWS * TUNE_ROW_H + 30)
            return 1;
    }
    return 0;
}

/* ---------- Minimap click helper ---------- */
static int minimap_click(int mx, int my)
{
    if (!game.win_map) return 0;
    int wx = game.map_x;
    int wy = game.map_y + WIN_TITLEBAR_H;  /* Skip title bar */
    int map_x = wx + 4;
    int map_y = wy + 4;
    int map_w = MAP_WIN_W - 8;
    int map_h = MAP_WIN_H - WIN_TITLEBAR_H - 24;

    if (mx >= map_x && mx < map_x + map_w &&
        my >= map_y && my < map_y + map_h) {
        /* Click in minimap: SmoothScroll the camera there (the original
         * animates click-nav, MapWndProc -> CameraT SmoothScroll; same
         * ground-anchored mapping as render_minimap). */
        float pf = (float)map_h / (TOWER_TOP_FLOOR - TOWER_MIN_FLOOR + 1);
        float ground_line = map_y + map_h * 264.0f / 288.0f;
        int clicked_floor = (int)((ground_line - my) / pf);
        game.cam_ty = -clicked_floor * CELL_H;
        game.cam_tx = (float)(mx - map_x) * TOWER_WIDTH / map_w * CELL_W;
        game.cam_anim = 1;
        return 1;
    }
    /* Mode button strip (Map/Eval/Rent/Hotel — EXE global 0x7840; the
     * 4th overlay is locked until the 2nd star, pass-3 MapWndProc). */
    if (mx >= map_x && mx < map_x + map_w &&
        my >= map_y + map_h + 2 && my < map_y + map_h + 18) {
        int m = (mx - map_x) / (map_w / 4);
        if (m >= 0 && m <= 3 &&
            (m < 3 || game.tower.star_rating >= 2 ||
             game.sim.mode == MODE_SANDBOX))
            game.map_mode = m;
        return 1;
    }
    return 0;
}

/* ---------- Menu interaction helpers ---------- */

static int menu_bar_hit_test(int mx, int my)
{
    if (my < HUD_HEIGHT || my >= HUD_HEIGHT + MENU_BAR_H) return -1;
    int cx = 8;
    for (int i = 0; i < TOP_MENU_COUNT; i++) {
        int tw = (int)strlen(top_menus[i].label) * 7 + MENU_ITEM_PAD * 2;
        if (mx >= cx && mx < cx + tw) return i;
        cx += tw;
    }
    return -1;
}

static int dropdown_hit_test(int mx, int my)
{
    if (game.menu_open < 0) return -1;
    const TopMenu *tm = &top_menus[game.menu_open];
    
    int dx, dy, dw, dh;
    get_top_menu_rect(game.menu_open, &dx, &dy, &dw, &dh);
    int drop_x = dx;
    int drop_y = dy + MENU_BAR_H;
    int drop_w = 180;
    int item_h = 18;
    int sep_h = 6;
    
    if (mx < drop_x || mx >= drop_x + drop_w) return -1;
    
    int iy = drop_y + 2;
    for (int i = 0; i < tm->count; i++) {
        int h = tm->items[i].label ? item_h : sep_h;
        if (my >= iy && my < iy + h && tm->items[i].label) return i;
        iy += h;
    }
    return -1;
}

/* Save/load/export bodies shared by the F-keys and the Game menu. */
static void do_save_game(void)
{
    if (game_save(&game.sim, &game.tower, save_path()) == 0)
        add_event_message("Game saved.");
    else
        add_event_message("Save FAILED!");
}

static void do_load_game(void)
{
    if (game_load(&game.sim, &game.tower, save_path()) == 0) {
        elv_edit_exit();
        game.elv_open = 0;          /* dialog target may be gone */
        add_event_message("Game loaded.");
    } else {
        add_event_message("Load failed (no/old save).");
    }
}

static void do_export_tdt(void)
{
    char terr[128];
    if (twr_export("ct_export.tdt", &game.tower, &game.sim,
                   terr, sizeof terr) == 0) {
        add_event_message("Exported ct_export.tdt (original format).");
        /* The original's file format carries at most 20 tenant names
         * (the port lets you name every tenant — a deliberate
         * divergence); warn when an export can't keep them all. */
        int named = 0;
        for (int i = 0; i < game.tower.tenant_count; i++)
            if (game.tower.tenants[i].name[0]) named++;
        if (named > 20) {
            char buf[64];
            snprintf(buf, sizeof buf,
                     "Note: .TDT keeps 20 of your %d tenant names.", named);
            add_event_message(buf);
        }
    } else {
        add_event_message("TDT export FAILED!");
        printf("TDT export: %s\n", terr);
    }
}

static void execute_menu_item(const MenuItem *item)
{
    if (item->build_type != ITEM_NONE) {
        if (!item_unlocked(item->build_type)) {   /* locked in Campaign */
            add_event_message("Locked — raise your star rating first.");
            game.menu_open = -1;
            return;
        }
        game.build_type = item->build_type;
        /* Toolbar-button click (#7002), suppressed during fire/emergency like
         * the EXE (event_flags & 9 == 0). referee_sound_events row 20. */
        if (!game.sim.event.active)
            play_snd(SND_BUILD_TOOL);
        printf("Build: %s\n", tower_item_name(game.build_type));
    }
    switch (item->action) {
    case ACT_SPEED_PAUSE: game.sim.speed = SPEED_PAUSED; break;
    case ACT_SPEED_1:     game.sim.speed = SPEED_NORMAL;  break;
    case ACT_SPEED_2:     game.sim.speed = SPEED_FAST;    break;
    case ACT_SPEED_3:     game.sim.speed = SPEED_TURBO;   break;
    case ACT_DEBUG_TOGGLE: game.show_debug = !game.show_debug; break;
    case ACT_SCREENSHOT: {
        SDL_Surface *sshot = SDL_CreateRGBSurface(0, game.screen_w, game.screen_h, 32,
            0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
        SDL_RenderReadPixels(game.renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                             sshot->pixels, sshot->pitch);
        SDL_SaveBMP(sshot, "/tmp/simtower_screenshot.bmp");
        SDL_FreeSurface(sshot);
        printf("Screenshot saved\n");
        break;
    }
    case ACT_QUIT: game.running = 0; break;
    case ACT_NEW_TOWER:
        /* Start over (the original's New Tower command). The current game
         * is NOT auto-saved — same as the EXE, which only asks on quit. */
        elv_edit_exit();
        game.elv_open = 0;
        game.inspect_tid = 0;
        tower_init(&game.tower);
        game_init(&game.sim);
        add_event_message("New tower started.");
        break;
    case ACT_FINANCE: toggle_fin_dialog(); break;
    case ACT_SAVE:       do_save_game();  break;
    case ACT_LOAD:       do_load_game();  break;
    case ACT_EXPORT_TDT: do_export_tdt(); break;
    case ACT_STATS:  game.show_stats  = !game.show_stats;  break;
    case ACT_TUNING: game.show_tuning = !game.show_tuning; break;
    case ACT_SANTA:
        if (!game.sim.santa.active) game_launch_santa(&game.sim, game.screen_w);
        break;
    case ACT_WIN_TOOLBAR:  game.win_toolbar ^= 1; break;
    case ACT_WIN_INFOBAR:  game.win_infobar ^= 1; break;
    case ACT_WIN_MAP:      game.win_map ^= 1;     break;
    case ACT_ANIM_PEOPLE:  game.anim_people ^= 1;  break;
    case ACT_ANIM_EFFECTS: game.anim_effects ^= 1; break;
    case ACT_SND_ELEV:     game.snd_elev ^= 1;   break;
    case ACT_SND_BG:       game.snd_bg ^= 1;     break;
    case ACT_SND_EVENTS:   game.snd_events ^= 1; break;
    case ACT_FIND_PERSON:  game.find_open = 1; game.find_sel = -1; break;
    case ACT_FIND_TENANT:  game.find_open = 2; game.find_sel = -1; break;
    case ACT_MODE_CAMPAIGN:
    case ACT_MODE_SANDBOX:
        game.sim.mode = (item->action == ACT_MODE_SANDBOX)
                      ? MODE_SANDBOX : MODE_CAMPAIGN;
        add_event_message(game.sim.mode == MODE_SANDBOX
                          ? "Sandbox: everything unlocked"
                          : "Campaign: star-gated unlocks");
        if (game.tool_popup >= 0 && !tool_button_shown(game.tool_popup))
            game.tool_popup = -1;
        break;
    default: break;
    }
    game.menu_open = -1;
}

/* Mouse-down on a shaft's motor room or pit: grab the cap and drag it away
 * from the shaft to extend — the original's mechanical-room handles. Works
 * from the plain pointer and the finger tool. Borrows the shaft's type as
 * the build tool for this one drag so the ghost/placement machinery runs
 * unchanged (cap_drag restores ITEM_NONE on release). Returns 1 if a cap
 * was grabbed. */
static int try_cap_drag(void)
{
    PeopleSim *ps = &game.sim.people;
    int fidx = floor_to_index(game.mouse_floor);
    for (int i = 0; i < ps->shaft_count; i++) {
        ElevatorShaft *s = &ps->shafts[i];
        if (!s->active) continue;
        if (game.mouse_cell < s->x ||
            game.mouse_cell >= s->x + ITEM_WIDTH[s->type]) continue;
        if (fidx != s->hi + 1 && fidx != s->lo - 1) continue;
        game.build_type = s->type;
        game.cap_drag = 1;
        game.dragging = 1;
        game.drag_start_cell = s->x;
        game.drag_start_floor = game.mouse_floor;
        /* Live extension state: the cap follows the cursor DURING the drag
         * (the original extends on drag, not on release), one segment per
         * floor the handle is pulled past. */
        game.cap_drag_dir = (fidx == s->hi + 1) ? 1 : -1;
        game.cap_drag_next = game.mouse_floor;
        game.cap_drag_placed = 0;
        return 1;
    }
    return 0;
}

/* Step a live cap drag toward the cursor: the grabbed motor room / pit
 * tracks the mouse, and every floor it's pulled past becomes shaft. The
 * segment under the handle itself is claimed only once the handle moves
 * BEYOND it, so a 1-floor nudge extends by exactly 1. */
static void cap_drag_track(void)
{
    if (!game.cap_drag || !game.dragging) return;
    int placed = 0;
    if (game.cap_drag_dir > 0) {
        while (game.cap_drag_next < game.mouse_floor &&
               tower_place(&game.tower, game.build_type,
                           game.cap_drag_next, game.drag_start_cell)) {
            game.cap_drag_next++;
            placed++;
        }
    } else {
        while (game.cap_drag_next > game.mouse_floor &&
               tower_place(&game.tower, game.build_type,
                           game.cap_drag_next, game.drag_start_cell)) {
            game.cap_drag_next--;
            placed++;
        }
    }
    if (placed > 0) {
        game.cap_drag_placed += placed;
        play_snd(SND_BUILD_PLACE);
    }
}

/* ---------- Input handling ---------- */
static void handle_event(SDL_Event *ev)
{
    /* The route-loss confirm is fully modal, like the EXE's MessageBox. */
    if (game.route_confirm) {
        switch (ev->type) {
        case SDL_QUIT:
            game.running = 0;
            break;
        case SDL_MOUSEMOTION:
            game.mouse_x = ev->motion.x;
            game.mouse_y = ev->motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (ev->button.button == SDL_BUTTON_LEFT)
                route_confirm_click(ev->button.x, ev->button.y);
            break;
        case SDL_KEYDOWN:
            route_confirm_key(ev->key.keysym.sym);
            break;
        }
        return;
    }

    /* Notice dialogs are modal too: OK / Enter / Esc / Space dismisses. */
    if (game.notice_modal) {
        switch (ev->type) {
        case SDL_QUIT:
            game.running = 0;
            break;
        case SDL_MOUSEMOTION:
            game.mouse_x = ev->motion.x;
            game.mouse_y = ev->motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (ev->button.button == SDL_BUTTON_LEFT &&
                point_in_rect(ev->button.x, ev->button.y, notice_btn_rect()))
                notice_close();
            break;
        case SDL_KEYDOWN:
            if (ev->key.keysym.sym == SDLK_RETURN ||
                ev->key.keysym.sym == SDLK_KP_ENTER ||
                ev->key.keysym.sym == SDLK_ESCAPE ||
                ev->key.keysym.sym == SDLK_SPACE)
                notice_close();
            break;
        }
        return;
    }

    /* The disaster modal is fully modal: it eats all input until dismissed
     * (only window-close still works). */
    if (game.disaster_modal) {
        switch (ev->type) {
        case SDL_QUIT:
            game.running = 0;
            break;
        case SDL_MOUSEMOTION:
            game.mouse_x = ev->motion.x;   /* keep button hover live */
            game.mouse_y = ev->motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (ev->button.button == SDL_BUTTON_LEFT)
                disaster_modal_click(ev->button.x, ev->button.y);
            break;
        case SDL_KEYDOWN:
            disaster_modal_key(ev->key.keysym.sym);
            break;
        }
        return;
    }

    /* Modal info sub-dialogs capture all input while open. The name editor
     * takes typed text; the movie chooser takes clicks + Esc. */
    if (game.name_edit_open) {
        if (ev->type == SDL_TEXTINPUT)            name_editor_textinput(ev->text.text);
        else if (ev->type == SDL_KEYDOWN)         name_editor_key(ev->key.keysym.sym);
        else if (ev->type == SDL_MOUSEBUTTONDOWN) name_editor_click(ev->button.x, ev->button.y);
        return;
    }
    if (game.movie_dlg_open) {
        if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_ESCAPE) game.movie_dlg_open = 0;
        else if (ev->type == SDL_MOUSEBUTTONDOWN) movie_chooser_click(ev->button.x, ev->button.y);
        return;
    }
    /* Find Person/Tenant is modal like the EXE's DialogBoxParam. */
    if (game.find_open) {
        if (ev->type == SDL_KEYDOWN && (ev->key.keysym.sym == SDLK_ESCAPE ||
                                        ev->key.keysym.sym == SDLK_RETURN))
            game.find_open = 0;
        else if (ev->type == SDL_MOUSEBUTTONDOWN &&
                 ev->button.button == SDL_BUTTON_LEFT)
            find_dialog_click(ev->button.x, ev->button.y, ev->button.clicks);
        else if (ev->type == SDL_QUIT)
            game.running = 0;
        return;
    }

    switch (ev->type) {
    case SDL_QUIT:
        game.running = 0;
        break;
        
    case SDL_KEYDOWN:
        switch (ev->key.keysym.sym) {
        case SDLK_ESCAPE:
            if (game.elv_edit_mode) { elv_edit_exit(); break; }
            game.running = 0;
            break;
        case SDLK_q:
            game.running = 0;
            break;
        
        /* Simulation speed */
        case SDLK_SPACE:
            if (game.sim.speed == SPEED_PAUSED)
                game.sim.speed = SPEED_NORMAL;
            else
                game.sim.speed = SPEED_PAUSED;
            printf("Speed: %s\n", game.sim.speed == SPEED_PAUSED ? "PAUSED" : "NORMAL");
            break;
        case SDLK_EQUALS:
        case SDLK_PLUS:
            /* UI cap is FAST (era-hardware approximation); TURBO is
             * mod/debug territory. */
            if (game.sim.speed < SPEED_FAST) {
                game.sim.speed++;
                printf("Speed: %d\n", game.sim.speed);
            }
            break;
        case SDLK_MINUS:
            if (game.sim.speed > SPEED_PAUSED) {
                game.sim.speed--;
                printf("Speed: %d\n", game.sim.speed);
            }
            break;
        
        /* Santa! */
        case SDLK_F2:
            if (!game.sim.santa.active) {
                game_launch_santa(&game.sim, game.screen_w);
            }
            break;

        /* Analytics window */
        case SDLK_F3:
            game.show_stats = !game.show_stats;
            break;

        /* Tuning/modding window */
        case SDLK_F4:
            game.show_tuning = !game.show_tuning;
            break;

        /* Save / load (whole state, including people mid-ride and mods) */
        case SDLK_F5:
            do_save_game();
            break;
        /* Export as an original-format save (SimTower 1.1 .TDT) */
        case SDLK_F6:
            do_export_tdt();
            break;
        /* Financial report (CountT) */
        case SDLK_F7:
            toggle_fin_dialog();
            break;
        case SDLK_F9:
            do_load_game();
            break;

        case SDLK_F8:   /* Toggle Campaign (star-gated) / Sandbox (all unlocked) */
            game.sim.mode = (game.sim.mode == MODE_CAMPAIGN)
                          ? MODE_SANDBOX : MODE_CAMPAIGN;
            add_event_message(game.sim.mode == MODE_SANDBOX
                              ? "Sandbox: everything unlocked"
                              : "Campaign: star-gated unlocks");
            /* A now-locked group's open pull-down would dangle — close it. */
            if (game.tool_popup >= 0 && !tool_button_shown(game.tool_popup))
                game.tool_popup = -1;
            break;

        /* Debug toggle — F1 gets eaten by browsers, use backtick */
        case SDLK_F1:
        case SDLK_BACKQUOTE:
            game.show_debug = !game.show_debug;
            printf("Debug labels: %s\n", game.show_debug ? "ON" : "OFF");
            break;
        
        /* Screenshot */
        case SDLK_F12: {
            SDL_Surface *sshot = SDL_CreateRGBSurface(0, game.screen_w, game.screen_h, 32,
                0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
            SDL_RenderReadPixels(game.renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                 sshot->pixels, sshot->pitch);
            SDL_SaveBMP(sshot, "/tmp/simtower_screenshot.bmp");
            SDL_FreeSurface(sshot);
            printf("Screenshot saved to /tmp/simtower_screenshot.bmp\n");
            break;
        }
        
        /* Camera movement */
        /* Arrow keys = the original's scrollbar line steps (16px both
         * axes); PgUp/PgDn = its page steps (view minus one line). */
        case SDLK_LEFT:  game.cam_fx -= 16; game.cam_anim = 0; break;
        case SDLK_RIGHT: game.cam_fx += 16; game.cam_anim = 0; break;
        case SDLK_UP:    game.cam_fy -= 16; game.cam_anim = 0; break;
        case SDLK_DOWN:  game.cam_fy += 16; game.cam_anim = 0; break;
        case SDLK_PAGEUP:
            game.cam_fy -= game.screen_h - MENU_BAR_H - 16;
            game.cam_anim = 0; break;
        case SDLK_PAGEDOWN:
            game.cam_fy += game.screen_h - MENU_BAR_H - 16;
            game.cam_anim = 0; break;
        
        /* Direct build type selection */
        case SDLK_1: game.build_type = ITEM_OFFICE;       break;
        case SDLK_2: game.build_type = ITEM_CONDO;        break;
        case SDLK_3: game.build_type = ITEM_RESTAURANT;   break;
        case SDLK_4: game.build_type = ITEM_FAST_FOOD;    break;
        case SDLK_5: game.build_type = ITEM_HOTEL_SINGLE; break;
        case SDLK_6: game.build_type = ITEM_HOTEL_TWIN;   break;
        case SDLK_7: game.build_type = ITEM_HOTEL_SUITE;  break;
        case SDLK_8: game.build_type = ITEM_STAIRS;       break;
        case SDLK_9: game.build_type = ITEM_ESCALATOR;    break;
        case SDLK_0: game.build_type = ITEM_NONE;         break;
        
        /* Extended types via letter keys */
        case SDLK_c: game.build_type = ITEM_CINEMA;       break;
        case SDLK_p: game.build_type = ITEM_PARTY_HALL;   break;
        case SDLK_m: game.build_type = ITEM_METRO;        break;
        case SDLK_k: game.build_type = ITEM_PARKING;      break;  /* K for parKing */
        case SDLK_h: game.build_type = ITEM_CATHEDRAL;    break;  /* H for catHedral */
        case SDLK_x: game.build_type = ITEM_MEDICAL;      break;  /* X for mediX */
        case SDLK_g: game.build_type = ITEM_SECURITY;     break;  /* G for Guard */
        case SDLK_r: game.build_type = ITEM_RECYCLING;    break;
        case SDLK_o: game.build_type = ITEM_SHOP;         break;  /* O for shOp */
        case SDLK_l: game.build_type = ITEM_LOBBY;        break;  /* L for Lobby */
        case SDLK_e: game.build_type = ITEM_ELEVATOR_SHAFT;   break;
        case SDLK_v: game.build_type = ITEM_ELEVATOR_SERVICE; break;  /* serVice */
        case SDLK_w: game.build_type = ITEM_ELEVATOR_EXPRESS; break;  /* Wide car */
        
        /* Cycle through types with Tab / Shift+Tab */
        case SDLK_TAB:
            if (ev->key.keysym.mod & KMOD_SHIFT)
                cycle_build_type(-1);
            else
                cycle_build_type(1);
            break;
        
        default: break;
        }
        /* Print current build type */
        if (ev->key.keysym.sym >= SDLK_0 && ev->key.keysym.sym <= SDLK_9) {
            printf("Build: %s\n", tower_item_name(game.build_type));
        }
        if (ev->key.keysym.sym >= SDLK_a && ev->key.keysym.sym <= SDLK_z) {
            if (game.build_type != ITEM_NONE)
                printf("Build: %s\n", tower_item_name(game.build_type));
        }
        break;
        
    case SDL_MOUSEMOTION:
        game.mouse_x = ev->motion.x;
        game.mouse_y = ev->motion.y;
        screen_to_grid(game.mouse_x, game.mouse_y,
                       &game.mouse_floor, &game.mouse_cell);
        cap_drag_track();   /* live shaft extension follows the cursor */

        /* Camera panning (middle/right-click drag) */
        if (game.cam_panning) {
            int dx = ev->motion.x - game.cam_pan_last_x;
            int dy = ev->motion.y - game.cam_pan_last_y;
            game.cam_fx -= dx;
            game.cam_fy -= dy;
            game.cam_anim = 0;
            game.cam_pan_last_x = ev->motion.x;
            game.cam_pan_last_y = ev->motion.y;
            break;
        }
        
        /* Window dragging */
        if (game.win_dragging) {
            int nx = ev->motion.x - game.win_drag_ox;
            int ny = ev->motion.y - game.win_drag_oy;
            /* Clamp to screen bounds */
            switch (game.win_dragging) {
            case 1: /* Info bar */
                if (nx < 0) nx = 0;
                if (nx + INFO_BAR_W > game.screen_w) nx = game.screen_w - INFO_BAR_W;
                if (ny < 0) ny = 0;
                if (ny + INFO_BAR_H > game.screen_h) ny = game.screen_h - INFO_BAR_H;
                game.info_x = nx;
                game.info_y = ny;
                break;
            case 2: /* Minimap */
                if (nx < 0) nx = 0;
                if (nx + MAP_WIN_W > game.screen_w) nx = game.screen_w - MAP_WIN_W;
                if (ny < 0) ny = 0;
                if (ny + MAP_WIN_H > game.screen_h) ny = game.screen_h - MAP_WIN_H;
                game.map_x = nx;
                game.map_y = ny;
                break;
            case 3: /* Toolbox */
                if (nx < 0) nx = 0;
                if (nx + TOOL_WIN_W > game.screen_w) nx = game.screen_w - TOOL_WIN_W;
                if (ny < 0) ny = 0;
                if (ny + TOOL_WIN_H > game.screen_h) ny = game.screen_h - TOOL_WIN_H;
                game.tool_x = nx;
                game.tool_y = ny;
                break;
            case 4: /* Elevator dialog */
                if (nx < 0) nx = 0;
                if (nx + ELV_W > game.screen_w) nx = game.screen_w - ELV_W;
                if (ny < 0) ny = 0;
                if (ny + WIN_TITLEBAR_H > game.screen_h)
                    ny = game.screen_h - WIN_TITLEBAR_H;
                game.elv_x = nx;
                game.elv_y = ny;
                break;
            case 7: /* Financial report */
                if (nx < 0) nx = 0;
                if (nx + FIN_DLG_W > game.screen_w) nx = game.screen_w - FIN_DLG_W;
                if (ny < 0) ny = 0;
                if (ny + WIN_TITLEBAR_H > game.screen_h)
                    ny = game.screen_h - WIN_TITLEBAR_H;
                game.fin_x = nx;
                game.fin_y = ny;
                break;
            case 5: /* Analytics */
                if (nx < 0) nx = 0;
                if (nx + STATS_W > game.screen_w) nx = game.screen_w - STATS_W;
                if (ny < 0) ny = 0;
                if (ny + WIN_TITLEBAR_H > game.screen_h)
                    ny = game.screen_h - WIN_TITLEBAR_H;
                game.stats_x = nx;
                game.stats_y = ny;
                break;
            case 6: /* Tuning */
                if (nx < 0) nx = 0;
                if (nx + TUNE_W > game.screen_w) nx = game.screen_w - TUNE_W;
                if (ny < 0) ny = 0;
                if (ny + WIN_TITLEBAR_H > game.screen_h)
                    ny = game.screen_h - WIN_TITLEBAR_H;
                game.tune_x = nx;
                game.tune_y = ny;
                break;
            }
            break;
        }
        
        /* Menu bar hover tracking */
        game.menu_bar_hover = menu_bar_hit_test(ev->motion.x, ev->motion.y);
        
        /* If a menu is open and we hover over a different menu bar item, switch */
        if (game.menu_open >= 0 && game.menu_bar_hover >= 0 && 
            game.menu_bar_hover != game.menu_open) {
            game.menu_open = game.menu_bar_hover;
            game.menu_hover = -1;
        }
        
        /* Track dropdown hover */
        if (game.menu_open >= 0) {
            game.menu_hover = dropdown_hit_test(ev->motion.x, ev->motion.y);
        }
        break;
        
    case SDL_MOUSEBUTTONDOWN:
        /* Middle or right click: start camera pan */
        if (ev->button.button == SDL_BUTTON_MIDDLE || 
            ev->button.button == SDL_BUTTON_RIGHT) {
            game.cam_panning = 1;
            game.cam_pan_last_x = ev->button.x;
            game.cam_pan_last_y = ev->button.y;
            break;
        }
        if (ev->button.button == SDL_BUTTON_LEFT) {
            /* Tuning window swallows its clicks */
            if (tuning_click(ev->button.x, ev->button.y)) break;

            /* Check menu bar click first */
            int bar_hit = menu_bar_hit_test(ev->button.x, ev->button.y);
            if (bar_hit >= 0) {
                if (game.menu_open == bar_hit) {
                    game.menu_open = -1;  /* Toggle off */
                } else {
                    game.menu_open = bar_hit;
                    game.menu_hover = -1;
                }
                break;
            }
            
            /* Check dropdown click */
            if (game.menu_open >= 0) {
                int drop_hit = dropdown_hit_test(ev->button.x, ev->button.y);
                if (drop_hit >= 0) {
                    execute_menu_item(&top_menus[game.menu_open].items[drop_hit]);
                    break;
                }
                /* Clicked outside menu — close it */
                game.menu_open = -1;
                break;
            }
            
            /* Window title bar drag start */
            {
                int win_hit = titlebar_hit_test(ev->button.x, ev->button.y);
                if (win_hit == 2 &&
                    ev->button.x >= game.map_x + 3 &&
                    ev->button.x < game.map_x + 16 &&
                    ev->button.y >= game.map_y + 3 &&
                    ev->button.y < game.map_y + WIN_TITLEBAR_H - 3) {
                    game.win_map = 0;      /* map close box */
                    break;
                }
                if (win_hit > 0) {
                    game.win_dragging = win_hit;
                    switch (win_hit) {
                    case 1: /* Info bar */
                        game.win_drag_ox = ev->button.x - game.info_x;
                        game.win_drag_oy = ev->button.y - game.info_y;
                        break;
                    case 2: /* Minimap */
                        game.win_drag_ox = ev->button.x - game.map_x;
                        game.win_drag_oy = ev->button.y - game.map_y;
                        break;
                    case 3: /* Toolbox */
                        game.win_drag_ox = ev->button.x - game.tool_x;
                        game.win_drag_oy = ev->button.y - game.tool_y;
                        break;
                    case 4: /* Elevator dialog */
                        game.win_drag_ox = ev->button.x - game.elv_x;
                        game.win_drag_oy = ev->button.y - game.elv_y;
                        break;
                    case 5: { /* Analytics */
                        int wx, wy;
                        stats_window_origin(&wx, &wy);
                        game.stats_x = wx; game.stats_y = wy;
                        game.win_drag_ox = ev->button.x - wx;
                        game.win_drag_oy = ev->button.y - wy;
                        break;
                    }
                    case 6: { /* Tuning */
                        int wx, wy;
                        tune_window_origin(&wx, &wy);
                        game.tune_x = wx; game.tune_y = wy;
                        game.win_drag_ox = ev->button.x - wx;
                        game.win_drag_oy = ev->button.y - wy;
                        break;
                    }
                    case 7: /* Financial report */
                        game.win_drag_ox = ev->button.x - game.fin_x;
                        game.win_drag_oy = ev->button.y - game.fin_y;
                        break;
                    }
                    break;
                }
            }

            /* Elevator dialog click (body, not title bar) */
            if (elv_dialog_click(ev->button.x, ev->button.y)) break;
            /* Financial report click (body, not title bar) */
            if (fin_dialog_click(ev->button.x, ev->button.y)) break;
            /* Inspector info popup click (close button / swallow body clicks) */
            if (inspect_popup_click(ev->button.x, ev->button.y)) break;
            /* Person info popup click */
            if (person_popup_click(ev->button.x, ev->button.y)) break;

            /* Toolbox click (body, not title bar) */
            if (toolbox_click(ev->button.x, ev->button.y)) break;

            /* Minimap click (body, not title bar) */
            if (minimap_click(ev->button.x, ev->button.y)) break;

            /* Block clicks that land on any window body from reaching the game */
            if (point_in_any_window(ev->button.x, ev->button.y)) break;

            /* Fire/bomb lockout (world-click dispatcher 1058:0000 gate,
             * [0xB406]&9): during an emergency the WORLD takes no clicks —
             * no building, demolition, or inspection until it's over.
             * Menus and dialogs stay live (the EXE's fire response is a
             * menu command). The EXE beeps; we have no beep sample, so
             * the click is swallowed silently. */
            if (game.sim.event.active) break;

            /* Simulate/edit mode: a click on the tower toggles the selected
             * shaft's stop at that floor. The world is frozen; no building. */
            if (game.elv_edit_mode) {
                elv_edit_toggle_at(ev->button.x, ev->button.y);
                break;
            }

            /* Bulldozer: remove the facility under the cursor. */
            if (game.demolish_mode) {
                int fidx = floor_to_index(game.mouse_floor);
                if (fidx >= 0 && fidx < TOWER_FLOOR_COUNT &&
                    game.mouse_cell >= 0 && game.mouse_cell < TOWER_WIDTH) {
                    uint16_t tid = game.tower.grid[fidx][game.mouse_cell].tenant_id;
                    if (tid) {
                        ItemType ty = game.tower.grid[fidx][game.mouse_cell].type;
                        request_remove_tenant(tid, ty);
                    }
                }
                break;
            }

            /* Inspector tool: click a unit to open its info popup.
             * Elevators aren't tenants — inspecting a shaft opens its real
             * elevator dialog instead of a garbage tenant popup. */
            if (game.inspect_mode) {
                if (open_elv_dialog_at_mouse(ev->button.x, ev->button.y))
                    break;
                /* Stairs/escalators are grid overlays (their cells keep the
                 * underlying tenant), so hit-test them explicitly. They rank
                 * between elevators and people in the EXE's inspect chain
                 * (elevator -> escalator -> person -> tenant). */
                {
                    uint16_t tid = stair_hit_test(game.mouse_floor,
                                                  game.mouse_cell);
                    if (tid) {
                        open_tenant_popup_at(tid, ev->button.x, ev->button.y);
                        break;
                    }
                }
                {
                    uint16_t pid = person_hit_test(ev->button.x, ev->button.y);
                    if (pid) {
                        open_person_popup_at(pid, ev->button.x, ev->button.y);
                        break;
                    }
                }
                int fidx = floor_to_index(game.mouse_floor);
                if (fidx >= 0 && fidx < TOWER_FLOOR_COUNT &&
                    game.mouse_cell >= 0 && game.mouse_cell < TOWER_WIDTH) {
                    uint16_t tid = game.tower.grid[fidx][game.mouse_cell].tenant_id;
                    if (tid)
                        open_tenant_popup_at(tid, ev->button.x, ev->button.y);
                    else
                        game.inspect_open = 0;   /* clicked empty space */
                }
                break;
            }

            /* Finger/pointer tool: grabbing a motor room / pit starts the
             * extend drag; a single click on a shaft body opens its dialog
             * (where cars are added). Double-click still works without the
             * tool — handy since double-click can be flaky over VNC. */
            if (game.finger_mode || ev->button.clicks >= 2) {
                if (game.finger_mode && game.build_type == ITEM_NONE &&
                    try_cap_drag())
                    break;
                if (open_elv_dialog_at_mouse(ev->button.x, ev->button.y))
                    break;
                if (game.finger_mode) break;   /* tool consumes the click */
            }

            /* Plain pointer on a shaft's motor room or pit: the extend drag. */
            if (game.build_type == ITEM_NONE && !game.demolish_mode &&
                !game.finger_mode && !game.inspect_mode &&
                try_cap_drag()) {
                /* drag armed by try_cap_drag */
            }
            /* Normal game click — anchor centered on the cursor, matching
             * the placement ghost. */
            else if (game.build_type != ITEM_NONE) {
                /* EXE build-dispatcher prologue (11f8:0955-09bb), runs
                 * while the lobby-height choice is still open: */
                if (game.tower.lobby_height == 0) {
                    /* The money doubler: virgin tower, exactly the
                     * starting $2M, build-tool click on the lot's
                     * bottom-left cell (B10, cell 0) → AwardMoney(
                     * current balance), click consumed, choice stays
                     * open. No fanfare in the EXE either. */
                    if (game.tower.money == 2000000L &&
                        game.tower.tenant_count == 0 &&
                        game.mouse_floor == TOWER_MIN_FLOOR &&
                        game.mouse_cell == 0) {
                        game.tower.money *= 2;
                        printf("Corner click doubled the starting "
                               "funds\n");
                        break;
                    }
                    /* First build click locks the ground-lobby height:
                     * plain = 1 story, Ctrl = 2, Ctrl+Shift = 3 —
                     * placement success not required (EXE quirk kept). */
                    SDL_Keymod m = SDL_GetModState();
                    tower_choose_lobby_height(&game.tower,
                        (m & KMOD_CTRL) ? ((m & KMOD_SHIFT) ? 3 : 2) : 1);
                }
                game.dragging = 1;
                game.drag_start_cell = build_origin_cell(game.mouse_cell);
                game.drag_start_floor = game.mouse_floor;
            }
        }
        break;
    
    case SDL_MOUSEBUTTONUP:
        /* Stop camera pan */
        if ((ev->button.button == SDL_BUTTON_MIDDLE || 
             ev->button.button == SDL_BUTTON_RIGHT) && game.cam_panning) {
            game.cam_panning = 0;
            break;
        }
        if (ev->button.button == SDL_BUTTON_LEFT) {
            /* Stop window dragging */
            if (game.win_dragging) {
                game.win_dragging = 0;
                break;
            }
            /* Click-and-hold pull-down: releasing over a sub-item selects it. */
            if (game.tool_popup >= 0 && game.tool_popup < TOOL_BTN_COUNT) {
                int nvis = tool_sub_visible_count(game.tool_popup);
                const ToolButton *g = &tool_buttons[game.tool_popup];
                for (int vj = 0; vj < nvis; vj++) {
                    int j = tool_sub_visible_index(game.tool_popup, vj);
                    if (j < 0) continue;
                    int bx, by;
                    tool_sub_rect(game.tool_popup, vj, &bx, &by);
                    if (ev->button.x >= bx && ev->button.x < bx + TOOL_BTN_SIZE &&
                        ev->button.y >= by && ev->button.y < by + TOOL_BTN_SIZE) {
                        game.build_type = g->sub[j];
                        game.tool_popup = -1;
                        break;
                    }
                }
            }
            /* Stop building placement drag */
            if (game.dragging && game.cap_drag) {
                /* Live cap drag: segments were already laid while the mouse
                 * moved. A motionless click on the cap still nudges the
                 * shaft one floor (the original's clickable arrows). */
                if (!game.cap_drag_placed &&
                    game.drag_start_floor == game.mouse_floor &&
                    !tower_place(&game.tower, game.build_type,
                                 game.drag_start_floor, game.drag_start_cell) &&
                    tower_reject_reason()[0])
                    add_event_message(tower_reject_reason());
                game.dragging = 0;
                game.cap_drag = 0;
                game.build_type = ITEM_NONE;   /* hand back the plain pointer */
            } else if (game.dragging) {
                /* No-drag click: the centered origin under the cursor still
                 * matches where the drag anchored. */
                if (game.drag_start_cell == build_origin_cell(game.mouse_cell) &&
                    game.drag_start_floor == game.mouse_floor) {
                    /* Elevator click snaps to the shaft column, so clicking
                     * the cap arrows extends the shaft by one floor — and
                     * clicking WITHIN an existing shaft adds a car to it. */
                    int px = item_is_elevator(game.build_type)
                                 ? elevator_drag_column()
                                 : game.drag_start_cell;
                    if (item_unlocked(game.build_type) &&
                        !(item_is_elevator(game.build_type) &&
                          elev_add_car_at(px, game.drag_start_floor))) {
                        int pf = build_origin_floor(game.build_type,
                                                    game.drag_start_floor);
                        if (!tower_place(&game.tower, game.build_type, pf, px) &&
                            tower_reject_reason()[0])
                            add_event_message(tower_reject_reason());
                    }
                } else {
                    drag_place_units();
                }
                game.dragging = 0;
            }
        }
        break;
        
    case SDL_MOUSEWHEEL: {
        /* Elevator dialog open: the wheel scrolls its floor grid (wheel up =
         * show higher floors), not the world camera. */
        if (game.elv_open) {
            game.elv_scroll += ev->wheel.y;
            if (game.elv_scroll < 0) game.elv_scroll = 0;
            /* upper clamp happens in the renderer against the live shaft */
            break;
        }
        /* Shift+wheel or horizontal wheel = scroll left/right */
        int shift = (SDL_GetModState() & KMOD_SHIFT);
        if (shift || ev->wheel.x != 0) {
            int dx = ev->wheel.x ? ev->wheel.x : ev->wheel.y;
            game.cam_fx += dx * 60;
        } else {
            game.cam_fy -= ev->wheel.y * 60;
        }
        game.cam_anim = 0;
        break;
    }
        
    case SDL_WINDOWEVENT:
        if (ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            game.screen_w = ev->window.data1;
            game.screen_h = ev->window.data2;
        }
        break;
    }
}

/* ---------- Font initialization ---------- */
static void init_fonts(void)
{
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        return;
    }
    
    /* Try fonts in order of preference */
    const char *font_paths[] = {
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        NULL
    };
    
    for (int i = 0; font_paths[i]; i++) {
        game.font = TTF_OpenFont(font_paths[i], 14);
        if (game.font) {
            printf("Font loaded: %s\n", font_paths[i]);
            game.font_small = TTF_OpenFont(font_paths[i], 11);
            game.font_info = TTF_OpenFont(font_paths[i], 13);
            return;
        }
    }
    
    fprintf(stderr, "Warning: no suitable TTF font found, HUD text disabled\n");
}

/* ---------- Main ---------- */

/* Sound: the sim triggers effects through g_sound_hook (sound_hook.h). This
 * shim forwards to the audio mixer. Set after audio init. */
/* Options -> Sound toggles ([0xDE2A]/[0xDE2C]/[0xDE2E]): category mute at
 * the mixer funnel. UI feedback (build/cash/delete/tool) always plays.
 * 0xA714 doubles as explosion AND build-complete in the EXE — same WAV,
 * so the Events toggle silences both roles, like the original would. */
static int sound_muted(int wav_id)
{
    switch (wav_id) {
    case SND_ELEV_DING: case SND_ELEV_DEPART:
        return !game.snd_elev;
    case SND_METRO: case SND_GARBAGE:
        return !game.snd_bg;
    case SND_EXPLOSION: case SND_BUILD_DONE1:
    case SND_EVENT_OK: case SND_FIRE_LOOP: case SND_FIRE_START:
    case SND_BOMB_THREAT: case SND_BOMB_ARM:
    case SND_WEDDING: case SND_GUARD_STEP:
    case SND_CHIME_9AM: case SND_FANFARE_8AM: case SND_FANFARE_830:
    case SND_NEWDAY: case SND_NEWDAY_SPEC: case SND_EVENING:
        return !game.snd_events;
    default:
        return 0;
    }
}

static void sound_shim(int wav_id)
{
    if (getenv("CT_SOUND_DEBUG")) {
        /* tally by id; dumped by the capture-exit path */
        extern void snd_tally(int);
        snd_tally(wav_id);
    }
    /* Mix headroom: the EXE's WAVs are already near full-scale, so overlapping
     * voices would hard-clip. WaveMix attenuates per active channel; 0.55 keeps
     * a busy tower's mix clean. The ambient bed is always on, so it plays
     * quieter (0.35) to leave room for foreground dings/chimes on top. */
    int amb = (wav_id == AMB_RESTAURANT_A || wav_id == AMB_RESTAURANT_B ||
               wav_id == AMB_OFFICE || wav_id == AMB_HOTEL ||
               wav_id == AMB_CONDO_RARE || wav_id == AMB_SHOP_FF_B ||
               wav_id == AMB_PARKING_A || wav_id == AMB_PARKING_B ||
               wav_id == AMB_PARTY ||
               wav_id == AMB_SEASON_DAY || wav_id == AMB_SEASON_EVE ||
               wav_id == AMB_SEASON_SANTA ||
               (wav_id >= 0xA329 && wav_id <= 0xA337));   /* cinema soundtracks */
    if (amb ? !game.snd_bg : sound_muted(wav_id)) return;
    audio_play((uint16_t)wav_id, amb ? 0.35f : 0.55f);
}

/* debug tally (CT_SOUND_DEBUG) */
static int s_tally_ids[64], s_tally_cnt[64], s_tally_n = 0;
void snd_tally(int id)
{
    for (int i = 0; i < s_tally_n; i++)
        if (s_tally_ids[i] == id) { s_tally_cnt[i]++; return; }
    if (s_tally_n < 64) { s_tally_ids[s_tally_n] = id; s_tally_cnt[s_tally_n] = 1; s_tally_n++; }
}
static void snd_tally_dump(void)
{
    for (int i = 0; i < s_tally_n; i++)
        printf("  sound 0x%04X fired %d times\n", s_tally_ids[i], s_tally_cnt[i]);
}

/* Headless capture mode: when CT_SOUND_CAPTURE=<path> is set the game opens no
 * audio device, records the sim's sounds, and writes a WAV at exit. */
static int g_sound_capture = 0;

int main(int argc, char *argv[])
{
    const char *exe_path = NULL;
    int auto_screenshot = 0;
    const char *screenshot_path = "/tmp/simtower_screenshot.bmp";
    
    /* Find SIMTOWER.EXE */
    const char *search_paths[] = {
        "SIMTOWER.EXE",
        "data/SIMTOWER.EXE",
        "../OpenSkyscraper/data/SIMTOWER.EXE",
        NULL
    };
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0) {
            auto_screenshot = 1;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                screenshot_path = argv[++i];
            }
        } else if (argv[i][0] != '-') {
            size_t n = strlen(argv[i]);
            if (n > 4 && (!strcasecmp(argv[i] + n - 4, ".tdt") ||
                          !strcasecmp(argv[i] + n - 4, ".twr")))
                continue;   /* SimTower save — handled after init */
            exe_path = argv[i];
        }
    }
    
    if (!exe_path) {
        for (int i = 0; search_paths[i]; i++) {
            FILE *test = fopen(search_paths[i], "rb");
            if (test) { fclose(test); exe_path = search_paths[i]; break; }
        }
    }
    
    if (!exe_path) {
        fprintf(stderr, "Cannot find SIMTOWER.EXE. Pass path as argument.\n");
        return 1;
    }
    
    printf("ConcilliaTower — SimTower for Linux\n");
    printf("Loading resources from: %s\n", exe_path);
    
    /* Load NE resources */
    if (ne_load(&game.exe, exe_path) != 0) {
        fprintf(stderr, "Failed to load %s\n", exe_path);
        return 1;
    }
    exe_strings_init(&game.exe);   /* 0x7f06 string tables + dialog texts */
    
    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    
    game.screen_w = WINDOW_W;
    game.screen_h = WINDOW_H;
    game.window = SDL_CreateWindow("ConcilliaTower",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    
    /* Software renderer for VNC visibility */
    game.renderer = SDL_CreateRenderer(game.window, -1, SDL_RENDERER_SOFTWARE);

    /* Replace the bare X-server root cursor with a proper arrow; the bulldozer
     * gets a crosshair. (SimTower swaps the cursor per active tool.) */
    game.cursor_arrow    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    game.cursor_demolish = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    SDL_ShowCursor(SDL_ENABLE);
    if (game.cursor_arrow) SDL_SetCursor(game.cursor_arrow);

    /* Initialize SDL_ttf fonts */
    init_fonts();
    
    /* Load sprites */
    sprites_init(&game.sprites, &game.exe, game.renderer);

    /* Audio: decode the EXE's 55 WAV effects and wire the sim's sound hook.
     * CT_SOUND_CAPTURE=<path> renders a WAV headless (no device) instead of
     * playing live. Event->sound map: referee_sound_events_2026-07-13.md. */
    g_sound_capture = getenv("CT_SOUND_CAPTURE") != NULL;
    if (g_sound_capture ? (audio_init_capture() == 0) : (audio_init() == 0)) {
        int nclips = audio_load_from_ne(&game.exe);
        printf("Audio: %d sound effects loaded%s\n", nclips,
               g_sound_capture ? " (capture mode)" : "");
        g_sound_hook = sound_shim;
        if (g_sound_capture) audio_capture_begin();
        play_snd(SND_STARTUP);          /* intro jingle (referee row 23) */
    }

    /* Queue silhouettes use white as transparent */
    sprites_apply_white_key(&game.sprites, game.renderer, SPR_ELEV_QUEUE);
    /* person figure sheets (portrait rows) are white-keyed too */
    sprites_apply_white_key(&game.sprites, game.renderer, SPR_FIGURE_NORMAL);
    sprites_apply_white_key(&game.sprites, game.renderer, SPR_FIGURE_NAMED);
    sprites_apply_white_key(&game.sprites, game.renderer, SPR_FIGURE_VIP);
    /* the in-tenant people band (AnimPeple, 0x85E8-0x85EE) is white-keyed */
    for (uint16_t id = 0x85E8; id <= 0x85EE; id++)
        sprites_apply_white_key(&game.sprites, game.renderer, id);
    /* grand-lobby tall stair/escalator sheets (CGPk) — white transparent,
     * same convention as the normal stair/escalator art */
    sprites_apply_white_key(&game.sprites, game.renderer, 0x8FE9);
    sprites_apply_white_key(&game.sprites, game.renderer, 0x8FEA);
    /* digit sheet background is the shaft's own near-black (as in OS) */
    sprites_apply_color_key(&game.sprites, game.renderer, SPR_ELEV_DIGITS,
                            25, 25, 25);
    sprites_apply_color_key(&game.sprites, game.renderer, SPR_ELEV_DIGITS_RED,
                            25, 25, 25);   /* red car-here twin, same key */
    /* engine animation frames (palette-cycled 'animated bitmaps') */
    sprites_load_palette_cycled(&game.sprites, &game.exe, game.renderer,
                                SPR_ELEV_STD_LOADED, SPR_ELEV_STD_F1, 1);
    sprites_load_palette_cycled(&game.sprites, &game.exe, game.renderer,
                                SPR_ELEV_STD_LOADED, SPR_ELEV_STD_F2, 2);
    sprites_load_palette_cycled(&game.sprites, &game.exe, game.renderer,
                                SPR_ELEV_EXPRESS, SPR_ELEV_EXP_F1, 1);
    sprites_load_palette_cycled(&game.sprites, &game.exe, game.renderer,
                                SPR_ELEV_EXPRESS, SPR_ELEV_EXP_F2, 2);
    /* security monitors (same palette-cycle scheme; pair entries only) */
    sprites_load_palette_cycled(&game.sprites, &game.exe, game.renderer,
                                SPR_SECURITY, SPR_SECURITY_F1, 1);

    /* Build composite sprites from raw parts */
    {
        int ok = 0, fail = 0;
        
        /* Fast food: 0x86E8 + 0x86E9 → horizontally */
        if (sprites_compose_h(&game.sprites, game.renderer, 0x86E8, 0x86E9, SPR_FASTFOOD_COMP) == 0)
            ok++; else fail++;
        /* Restaurant: 0x8568 + 0x8569 → horizontally */
        if (sprites_compose_h(&game.sprites, game.renderer, 0x8568, 0x8569, SPR_RESTAURANT_COMP) == 0)
            ok++; else fail++;
        /* Hotel single: 0x84A8 (door) + 0x84A9 (room) */
        if (sprites_compose_h(&game.sprites, game.renderer, 0x84A8, 0x84A9, SPR_HOTEL_S_COMP) == 0)
            ok++; else fail++;
        /* Hotel twin: 0x84E8 (door) + 0x84E9 (room) */
        if (sprites_compose_h(&game.sprites, game.renderer, 0x84E8, 0x84E9, SPR_HOTEL_T_COMP) == 0)
            ok++; else fail++;
        /* Hotel suite: 0x8528 + 0x8529 */
        if (sprites_compose_h(&game.sprites, game.renderer, 0x8528, 0x8529, SPR_HOTEL_SUITE_COMP) == 0)
            ok++; else fail++;
        /* Condo: 5 frames 0x8628..0x862c joined (OS loadCondo) — chained through
         * scratch IDs 0x00F4..0x00F6 into SPR_CONDO_COMP. */
        if (sprites_compose_h(&game.sprites, game.renderer, 0x8628, 0x8629, 0x00F4) == 0 &&
            sprites_compose_h(&game.sprites, game.renderer, 0x00F4, 0x862A, 0x00F5) == 0 &&
            sprites_compose_h(&game.sprites, game.renderer, 0x00F5, 0x862B, 0x00F6) == 0 &&
            sprites_compose_h(&game.sprites, game.renderer, 0x00F6, 0x862C, SPR_CONDO_COMP) == 0)
            ok++; else fail++;
        /* Style variants: hotel single/suite style 1, twin styles 1-3,
         * condo styles 1-2 (style trace 2026-07-29). Failures are
         * tolerated — draw falls back to the style-0 composite. */
        if (sprites_compose_h(&game.sprites, game.renderer, 0x84AA, 0x84AB, SPR_HOTEL_S_S1) == 0)
            ok++; else fail++;
        if (sprites_compose_h(&game.sprites, game.renderer, 0x84EA, 0x84EB, SPR_HOTEL_T_S1) == 0)
            ok++; else fail++;
        if (sprites_compose_h(&game.sprites, game.renderer, 0x84EC, 0x84ED, SPR_HOTEL_T_S2) == 0)
            ok++; else fail++;
        if (sprites_compose_h(&game.sprites, game.renderer, 0x84EE, 0x84EF, SPR_HOTEL_T_S3) == 0)
            ok++; else fail++;
        if (sprites_compose_h(&game.sprites, game.renderer, 0x852A, 0x852B, SPR_HOTEL_SUITE_S1) == 0)
            ok++; else fail++;
        if (sprites_compose_h(&game.sprites, game.renderer, 0x862D, 0x862E, 0x00C0) == 0 &&
            sprites_compose_h(&game.sprites, game.renderer, 0x00C0, 0x862F, 0x00C1) == 0 &&
            sprites_compose_h(&game.sprites, game.renderer, 0x00C1, 0x8630, 0x00C2) == 0 &&
            sprites_compose_h(&game.sprites, game.renderer, 0x00C2, 0x8631, SPR_CONDO_S1) == 0)
            ok++; else fail++;
        if (sprites_compose_h(&game.sprites, game.renderer, 0x8632, 0x8633, 0x00C3) == 0 &&
            sprites_compose_h(&game.sprites, game.renderer, 0x00C3, 0x8634, 0x00C4) == 0 &&
            sprites_compose_h(&game.sprites, game.renderer, 0x00C4, 0x8635, 0x00C5) == 0 &&
            sprites_compose_h(&game.sprites, game.renderer, 0x00C5, 0x8636, SPR_CONDO_S2) == 0)
            ok++; else fail++;
        /* Recycling: 5-frame trash cycle — each frame is top row 0x88E9+i over
         * bottom row 0x8929+i (OS loadRecycling), then the 5 joined across. */
        {
            int rec_ok = 1;
            for (int f = 0; f < 5 && rec_ok; f++)
                if (sprites_compose_v(&game.sprites, game.renderer,
                                      0x88E9 + f, 0x8929 + f, 0x00E0 + f) != 0)
                    rec_ok = 0;
            if (rec_ok &&
                sprites_compose_h(&game.sprites, game.renderer, 0x00E0, 0x00E1, 0x00E5) == 0 &&
                sprites_compose_h(&game.sprites, game.renderer, 0x00E5, 0x00E2, 0x00E6) == 0 &&
                sprites_compose_h(&game.sprites, game.renderer, 0x00E6, 0x00E3, 0x00E7) == 0 &&
                sprites_compose_h(&game.sprites, game.renderer, 0x00E7, 0x00E4, SPR_RECYCLING_COMP) == 0)
                ok++; else fail++;
        }
        /* Stairs: TWO variants the original merges into one 14-frame walk cycle
         * (OS loadMergedByID stairs[0]=0x8968/0x89A8, stairs[1]=0x8969/0x89A9,
         * then side by side). variant0 frame 0 is the empty staircase; the rest
         * (both variants) are people mid-stride — a longer, smoother animation. */
        if (sprites_compose_v(&game.sprites, game.renderer, 0x8968, 0x89A8, 0x00E8) == 0 &&
            sprites_compose_v(&game.sprites, game.renderer, 0x8969, 0x89A9, 0x00E9) == 0 &&
            sprites_compose_h(&game.sprites, game.renderer, 0x00E8, 0x00E9, SPR_STAIRS_COMP) == 0)
            ok++; else fail++;
        /* Escalator: 0x8AA8 (top) + 0x8AE8 (bottom) vertically */
        if (sprites_compose_v(&game.sprites, game.renderer, 0x8AA8, 0x8AE8, SPR_ESCALATOR_COMP) == 0)
            ok++; else fail++;
        /* Retail variants: restaurants and fast food pair two sheets per
         * variant ([empty|busy] + [packed|closed]); variant 0 equals the
         * legacy COMP sprites. Shops need no composition. */
        for (int v = 0; v < 5; v++) {
            if (sprites_compose_h(&game.sprites, game.renderer,
                                  0x8568 + 2 * v, 0x8569 + 2 * v,
                                  SPR_RESTAURANT_V0 + v) == 0) ok++; else fail++;
            if (sprites_compose_h(&game.sprites, game.renderer,
                                  0x86E8 + 2 * v, 0x86E9 + 2 * v,
                                  SPR_FASTFOOD_V0 + v) == 0) ok++; else fail++;
        }
        /* Cathedral: five 448x36 strips stacked (dome 0x8CE8 ... entrance
         * 0x8DE8), then the sky backdrop keyed out so the dome silhouette
         * sits against the live sky. */
        if (sprites_compose_v(&game.sprites, game.renderer, 0x8CE8, 0x8D28, 0x00FC) == 0 &&
            sprites_compose_v(&game.sprites, game.renderer, 0x00FC, 0x8D68, 0x00FD) == 0 &&
            sprites_compose_v(&game.sprites, game.renderer, 0x00FD, 0x8DA8, 0x00FE) == 0 &&
            sprites_compose_v(&game.sprites, game.renderer, 0x00FE, 0x8DE8, SPR_CATHEDRAL_COMP) == 0) {
            sprites_apply_color_key(&game.sprites, game.renderer,
                                    SPR_CATHEDRAL_COMP, 74, 180, 255);
            ok++;
        } else fail++;
        /* TOWER ceremony: the six single-frame strips next to the normal
         * pairs — cherubs and "Welcome to Tower" banner on top, priest
         * and congregation below. Shown while the wedding runs. */
        if (sprites_compose_v(&game.sprites, game.renderer, 0x8CE9, 0x8D29, 0x00F8) == 0 &&
            sprites_compose_v(&game.sprites, game.renderer, 0x00F8, 0x8D69, 0x00F9) == 0 &&
            sprites_compose_v(&game.sprites, game.renderer, 0x00F9, 0x8DA9, 0x00FA) == 0 &&
            sprites_compose_v(&game.sprites, game.renderer, 0x00FA, 0x8DE9, 0x00FB) == 0 &&
            sprites_compose_v(&game.sprites, game.renderer, 0x00FB, 0x8DEA, SPR_CATH_CEREMONY) == 0) {
            sprites_apply_color_key(&game.sprites, game.renderer,
                                    SPR_CATH_CEREMONY, 74, 180, 255);
            ok++;
        } else fail++;
        /* Wedding procession (0x8828): bride, groom and guests on a
         * white background — keyed like the other deco sprites. */
        sprites_apply_white_key(&game.sprites, game.renderer, 0x8828);
        /* Recycling collection truck (0x892E): white-keyed deco overlay. */
        sprites_apply_white_key(&game.sprites, game.renderer, 0x892E);
        /* Construction workers (0x85EA, 6 16px frames): white-keyed. */
        sprites_apply_white_key(&game.sprites, game.renderer, SPR_CONST_WORKER);
        /* Construction floors: the scaffold grid's open diamonds and the
         * walls-up stage's gaps are white in the sheet = see-through. */
        sprites_apply_white_key(&game.sprites, game.renderer, SPR_CONST_GRID);
        sprites_apply_white_key(&game.sprites, game.renderer, SPR_CONST_SOLID);
        /* Medical: 0x8728 + 0x8729 horizontally (+ 0x872A via triple) */
        if (sprites_compose_h(&game.sprites, game.renderer, 0x8728, 0x8729, SPR_MEDICAL_COMP) == 0) {
            /* Try adding 0x872A if present */
            uint16_t temp_id = 0x00FF;
            if (sprites_compose_h(&game.sprites, game.renderer, SPR_MEDICAL_COMP, 0x872A, temp_id) == 0) {
                /* Replace medical comp with the triple */
                Sprite *old = sprites_find(&game.sprites, SPR_MEDICAL_COMP);
                Sprite *nw  = sprites_find(&game.sprites, temp_id);
                if (old && nw) {
                    SDL_DestroyTexture(old->texture);
                    old->texture = nw->texture;
                    old->w = nw->w;
                    old->h = nw->h;
                    nw->texture = NULL; /* prevent double-free */
                }
            }
            ok++;
        } else fail++;
        /* Parking: 0x86A8 + 0x86A9 horizontally */
        if (sprites_compose_h(&game.sprites, game.renderer, 0x86A8, 0x86A9, SPR_PARKING_COMP) == 0)
            ok++; else fail++;
        /* Party Hall: 0x8B28 (top) + 0x8B68 (bottom) vertically */
        if (sprites_compose_v(&game.sprites, game.renderer, 0x8B28, 0x8B68, SPR_PARTYHALL_COMP) == 0)
            ok++; else fail++;
        /* Cinema hall: 0x8868 (upper) + 0x88A8 (lower) vertically */
        if (sprites_compose_v(&game.sprites, game.renderer, 0x8868, 0x88A8, SPR_CINEMA_COMP) == 0)
            ok++; else fail++;
        /* Cinema marquee animation: cycled upper + same lower */
        sprites_load_palette_cycled(&game.sprites, &game.exe, game.renderer,
                                    SPR_CINEMA_UPPER, SPR_CINEMA_UPPER_F1, 1);
        if (sprites_compose_v(&game.sprites, game.renderer,
                              SPR_CINEMA_UPPER_F1, SPR_CINEMA_LOWER,
                              SPR_CINEMA_COMP_F1) == 0) ok++; else fail++;
        /* Metro: Try 0x8BA9 + 0x8BA8 horizontally for first row, then stack */
        {
            uint16_t metro_row0 = 0x00F0, metro_row1 = 0x00F1, metro_row2 = 0x00F2;
            uint16_t metro_01 = 0x00F3;
            int metro_ok = 1;
            if (sprites_compose_h(&game.sprites, game.renderer, 0x8BA9, 0x8BA8, metro_row0) != 0) metro_ok = 0;
            if (metro_ok && sprites_compose_h(&game.sprites, game.renderer, 0x8BE9, 0x8BE8, metro_row1) != 0) metro_ok = 0;
            if (metro_ok && sprites_compose_h(&game.sprites, game.renderer, 0x8C29, 0x8C28, metro_row2) != 0) metro_ok = 0;
            if (metro_ok && sprites_compose_v(&game.sprites, game.renderer, metro_row0, metro_row1, metro_01) == 0) {
                if (sprites_compose_v(&game.sprites, game.renderer, metro_01, metro_row2, SPR_METRO_COMP) == 0)
                    ok++; else fail++;
            } else fail++;
        }
        
        printf("Composites: %d built, %d failed\n", ok, fail);
    }
    
    /* Load cloud sprites (5 different shapes at 0x8384-0x8388)
     * White pixels (0xFF,0xFF,0xFF) must be transparent — same as original game.
     * Re-decode from NE resources with SDL_SetColorKey before texture creation. */
    game.cloud_count = 0;
    game.santa = NULL;
    {
        uint16_t cloud_ids[] = { SPR_CLOUD_0, SPR_CLOUD_1, SPR_CLOUD_2, SPR_CLOUD_3 };
        NEResourceList *dibs = ne_find_type(&game.exe, 0x8002);
        for (int i = 0; i < SPR_CLOUD_COUNT; i++) {
            game.clouds[i] = NULL;
            if (!dibs) continue;
            
            /* Find this resource in the DIB list */
            NEResource *res = NULL;
            for (int j = 0; j < dibs->count; j++) {
                if (dibs->items[j].id == cloud_ids[i]) {
                    res = &dibs->items[j];
                    break;
                }
            }
            if (!res) continue;
            
            /* Re-decode the DIB to a surface */
            SDL_Surface *surf = sprites_dib_to_surface(&game.sprites, res);
            if (!surf) continue;
            
            /* Apply white color key for transparency */
            SDL_SetColorKey(surf, SDL_TRUE, SDL_MapRGB(surf->format, 0xFF, 0xFF, 0xFF));
            
            /* Replace the existing sprite's texture */
            Sprite *cs = sprites_find(&game.sprites, cloud_ids[i]);
            if (cs) {
                SDL_DestroyTexture(cs->texture);
                cs->texture = SDL_CreateTextureFromSurface(game.renderer, surf);
                SDL_SetTextureBlendMode(cs->texture, SDL_BLENDMODE_BLEND);
                game.clouds[i] = cs;
                game.cloud_count++;
                printf("Cloud %d (0x%04x): %dx%d (white→transparent)\n",
                       i, cloud_ids[i], cs->w, cs->h);
            }
            SDL_FreeSurface(surf);
        }
    }
    if (game.cloud_count > 0) {
        printf("Loaded %d cloud sprites\n", game.cloud_count);
    } else {
        printf("No cloud sprites found (clouds disabled)\n");
    }
    
    /* Load Santa sprite (0x8388) with white transparency */
    {
        NEResourceList *dibs = ne_find_type(&game.exe, 0x8002);
        NEResource *res = NULL;
        if (dibs) {
            for (int j = 0; j < dibs->count; j++) {
                if (dibs->items[j].id == SPR_SANTA) { res = &dibs->items[j]; break; }
            }
        }
        if (res) {
            SDL_Surface *surf = sprites_dib_to_surface(&game.sprites, res);
            if (surf) {
                SDL_SetColorKey(surf, SDL_TRUE, SDL_MapRGB(surf->format, 0xFF, 0xFF, 0xFF));
                Sprite *ss = sprites_find(&game.sprites, SPR_SANTA);
                if (ss) {
                    SDL_DestroyTexture(ss->texture);
                    ss->texture = SDL_CreateTextureFromSurface(game.renderer, surf);
                    SDL_SetTextureBlendMode(ss->texture, SDL_BLENDMODE_BLEND);
                    game.santa = ss;
                    printf("🎅 Santa sprite loaded: %dx%d\n", ss->w, ss->h);
                }
                SDL_FreeSurface(surf);
            }
        }
    }
    
    /* Load decorative sprites with white transparency */
    game.crane_floor = CRANE_NONE;  /* no crane until the tower has a top */
    game.crane_x = 0;
    {
        struct { uint16_t id; Sprite **target; const char *name; } decos[] = {
            { SPR_ENTRANCES,    &game.entrances,      "Entrance awning" },
            { SPR_CRANE,        &game.crane,          "Construction crane" },
            { SPR_FIRELADDER,   &game.fireladder,     "Fire escape" },
            { SPR_FIRE_0,       &game.fire_frames[0], "Flame frame 0" },
            { SPR_FIRE_1,       &game.fire_frames[1], "Flame frame 1" },
            { SPR_FIRE_2,       &game.fire_frames[2], "Flame frame 2" },
            { SPR_FIRE_3,       &game.fire_frames[3], "Flame frame 3" },
            { SPR_FIRE_CHOPPER, &game.fire_chopper,   "Fire chopper" },
            { SPR_FIRE_DESTROY, &game.fire_destroyed, "Burnt cell" },
            { SPR_ALERT_TERROR, &game.alert_terror,   "Terror alert" },
            { SPR_ALERT_FIRE,   &game.alert_fire,     "Fire alert" },
        };
        NEResourceList *dibs = ne_find_type(&game.exe, 0x8002);
        for (int d = 0; d < (int)(sizeof(decos)/sizeof(decos[0])); d++) {
            *decos[d].target = NULL;
            NEResource *res = NULL;
            if (dibs) {
                for (int j = 0; j < dibs->count; j++) {
                    if (dibs->items[j].id == decos[d].id) { res = &dibs->items[j]; break; }
                }
            }
            if (res) {
                SDL_Surface *surf = sprites_dib_to_surface(&game.sprites, res);
                if (surf) {
                    SDL_SetColorKey(surf, SDL_TRUE, SDL_MapRGB(surf->format, 0xFF, 0xFF, 0xFF));
                    Sprite *sp = sprites_find(&game.sprites, decos[d].id);
                    if (sp) {
                        SDL_DestroyTexture(sp->texture);
                        sp->texture = SDL_CreateTextureFromSurface(game.renderer, surf);
                        SDL_SetTextureBlendMode(sp->texture, SDL_BLENDMODE_BLEND);
                        *decos[d].target = sp;
                        printf("🎨 %s loaded: %dx%d (0x%04X)\n", decos[d].name, sp->w, sp->h, decos[d].id);
                    }
                    SDL_FreeSurface(surf);
                }
            }
        }
    }
    
    /* Load skyline with sky-blue transparency (not white like other decos) */
    game.skyline = NULL;
    {
        NEResource *res = ne_find(&game.exe, NE_RT_BITMAP, SPR_SKYLINE);
        if (res) {
            SDL_Surface *surf = sprites_dib_to_surface(&game.sprites, res);
            if (surf) {
                /* The skyline bitmap has RGB(138,212,255) as its sky background.
                 * Make it transparent so our gradient sky shows through. */
                SDL_SetColorKey(surf, SDL_TRUE, SDL_MapRGB(surf->format, 138, 212, 255));
                Sprite *sp = sprites_find(&game.sprites, SPR_SKYLINE);
                if (sp) {
                    SDL_DestroyTexture(sp->texture);
                    sp->texture = SDL_CreateTextureFromSurface(game.renderer, surf);
                    SDL_SetTextureBlendMode(sp->texture, SDL_BLENDMODE_BLEND);
                    game.skyline = sp;
                    printf("🎨 City skyline loaded: %dx%d (sky-blue transparent)\n", sp->w, sp->h);
                }
                SDL_FreeSurface(surf);
            }
        }
    }

    /* Frustration tint base: a white clone of the queue-silhouette sheet.
     * SDL color mod multiplies, so the black figures can never redden —
     * the stressed draw swaps to this clone modded (255, 255-x, 255-x). */
    game.queue_hot = NULL;
    {
        NEResource *res = ne_find(&game.exe, NE_RT_BITMAP, SPR_ELEV_QUEUE);
        SDL_Surface *surf = res ? sprites_dib_to_surface(&game.sprites, res)
                                : NULL;
        if (surf) {
            SDL_SetColorKey(surf, SDL_TRUE,
                            SDL_MapRGB(surf->format, 0xFF, 0xFF, 0xFF));
            SDL_Surface *rgba =
                SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_RGBA8888, 0);
            SDL_FreeSurface(surf);
            if (rgba) {
                uint32_t *px = rgba->pixels;
                for (int i = 0; i < rgba->w * rgba->h; i++)
                    if (px[i] & 0x000000FF)          /* opaque -> white */
                        px[i] |= 0xFFFFFF00;
                game.queue_hot =
                    SDL_CreateTextureFromSurface(game.renderer, rgba);
                if (game.queue_hot)
                    SDL_SetTextureBlendMode(game.queue_hot,
                                            SDL_BLENDMODE_BLEND);
                SDL_FreeSurface(rgba);
            }
        }
    }

    /* ===== Load UI bitmaps from EXE ===== */
    game.ui_items = NULL;
    game.ui_timebar = NULL;
    game.ui_star[0] = game.ui_star[1] = NULL;
    game.ui_speed = NULL;
    game.ui_tools = NULL;
    game.ui_map = NULL;
    
    {
        /* Item icons: 3 bitmaps (0x812C-0x812E), each 256×128.
         * From OpenSkyscraper SimTowerLoader.cpp:
         *   items[i].create(32*26, 32);  // 26 icons per row
         *   for (n = 0..3) items[i].copy(tmp, n*256, 0, IntRect(0, n*32, 256, n*32+32));
         * Each bitmap has 4 rows × 8 cols → flattened to 26 icons (row3 clipped to 2).
         * 
         * Bitmap 0 (0x812C) = normal icons
         * Bitmap 1 (0x812D) = pressed/selected icons
         * Bitmap 2 (0x812E) = disabled icons
         * 
         * Final layout: 26 icons × 32px = 832px wide, stacked as:
         *   y=0-31:  normal  (from 0x812C)
         *   y=32-63: pressed (from 0x812D)
         */
        int icon_strip_w = 26 * 32;  /* 832px: 26 icons at 32px each */
        SDL_Surface *items_surf = SDL_CreateRGBSurface(0, icon_strip_w, 64, 32,
            0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
        
        if (items_surf) {
            SDL_FillRect(items_surf, NULL, SDL_MapRGBA(items_surf->format, 192, 192, 192, 255));
            
            /* Load normal (0x812C) and pressed (0x812D) bitmaps */
            for (int bi = 0; bi < 2; bi++) {
                uint16_t bid = 0x812C + bi;
                NEResource *res = ne_find(&game.exe, NE_RT_BITMAP, bid);
                if (!res) continue;
                
                SDL_Surface *bmp = sprites_dib_to_surface(&game.sprites, res);
                if (!bmp) continue;
                
                /* Rearrange: 4 rows of 256px → 1 row of 26 icons (4×8=32, clip to 26) */
                int dest_y = bi * 32;  /* 0 for normal, 32 for pressed */
                for (int row = 0; row < 4; row++) {
                    int icons_this_row = 8;
                    /* Row 3 clips: 26 - 3*8 = 2 icons */
                    if (row == 3) icons_this_row = 2;
                    
                    SDL_Rect src = { 0, row * 32, icons_this_row * 32, 32 };
                    SDL_Rect dst = { row * 8 * 32, dest_y, icons_this_row * 32, 32 };
                    SDL_BlitSurface(bmp, &src, items_surf, &dst);
                }
                SDL_FreeSurface(bmp);
            }
            
            /* Make gray background transparent (0x999999) */
            SDL_SetColorKey(items_surf, SDL_TRUE, SDL_MapRGB(items_surf->format, 0x99, 0x99, 0x99));
            
            game.ui_items = SDL_CreateTextureFromSurface(game.renderer, items_surf);
            game.ui_items_w = items_surf->w;
            game.ui_items_h = items_surf->h;
            SDL_SetTextureBlendMode(game.ui_items, SDL_BLENDMODE_BLEND);
            SDL_FreeSurface(items_surf);
            printf("🎨 Toolbox item icons loaded: %dx%d (26 icons)\n", 
                   game.ui_items_w, game.ui_items_h);
        }
        
        /* Time bar background: 0x8140 (431×41) */
        {
            NEResource *res = ne_find(&game.exe, NE_RT_BITMAP, 0x8140);
            if (res) {
                SDL_Surface *surf = sprites_dib_to_surface(&game.sprites, res);
                if (surf) {
                    game.ui_timebar = SDL_CreateTextureFromSurface(game.renderer, surf);
                    game.ui_timebar_w = surf->w;
                    game.ui_timebar_h = surf->h;
                    SDL_FreeSurface(surf);
                    printf("🎨 Time bar bg loaded: %dx%d\n", game.ui_timebar_w, game.ui_timebar_h);
                }
            }
        }
        
        /* Star rating: 0x8142=GOLD (earned), 0x8143=GREY (next to earn) —
         * verified by dumping both; the ids are the reverse of what the
         * names suggest. ui_star[0]=grey/empty, [1]=gold/filled. */
        for (int si = 0; si < 2; si++) {
            uint16_t sid = si == 0 ? 0x8143 : 0x8142;
            NEResource *res = ne_find(&game.exe, NE_RT_BITMAP, sid);
            if (res) {
                SDL_Surface *surf = sprites_dib_to_surface(&game.sprites, res);
                if (surf) {
                    /* Make gray (0x999999) transparent */
                    SDL_SetColorKey(surf, SDL_TRUE, SDL_MapRGB(surf->format, 0x99, 0x99, 0x99));
                    game.ui_star[si] = SDL_CreateTextureFromSurface(game.renderer, surf);
                    if (si == 0) { game.ui_star_w = surf->w; game.ui_star_h = surf->h; }
                    SDL_SetTextureBlendMode(game.ui_star[si], SDL_BLENDMODE_BLEND);
                    SDL_FreeSurface(surf);
                    printf("🎨 Star %s loaded: %dx%d\n", si ? "filled" : "empty",
                           game.ui_star_w, game.ui_star_h);
                }
            }
        }
        
        /* Speed buttons: 0x8258-0x825B (64×32 each, arranged as pairs).
         * OpenSkyscraper merges: speed[0] = 0x8258 + 0x8259 stacked Y,
         *                        speed[1] = 0x825A + 0x825B stacked Y,
         * then joins speed[0] + speed[1] horizontally.
         * Result: 4 speed states in a 128×64 image (normal top, pressed bottom). */
        {
            SDL_Surface *speed_surf = SDL_CreateRGBSurface(0, 128, 64, 32,
                0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
            if (speed_surf) {
                SDL_FillRect(speed_surf, NULL, SDL_MapRGBA(speed_surf->format, 192, 192, 192, 255));
                for (int si = 0; si < 4; si++) {
                    NEResource *res = ne_find(&game.exe, NE_RT_BITMAP, 0x8258 + si);
                    if (!res) continue;
                    SDL_Surface *bmp = sprites_dib_to_surface(&game.sprites, res);
                    if (!bmp) continue;
                    /* Each is 64×32. Pair 0+1 go left, pair 2+3 go right. 
                     * Within pair: first = normal, second = pressed. */
                    int x_off = (si / 2) * 64;
                    int y_off = (si % 2) * 32;
                    SDL_Rect dst = { x_off, y_off, 64, 32 };
                    SDL_BlitSurface(bmp, NULL, speed_surf, &dst);
                    SDL_FreeSurface(bmp);
                }
                game.ui_speed = SDL_CreateTextureFromSurface(game.renderer, speed_surf);
                game.ui_speed_w = speed_surf->w;
                game.ui_speed_h = speed_surf->h;
                SDL_FreeSurface(speed_surf);
                printf("🎨 Speed buttons loaded: %dx%d\n", game.ui_speed_w, game.ui_speed_h);
            }
        }
        
        /* Tool buttons: 0x825C-0x825E (64×21 each — bulldozer, finger, inspector).
         * OpenSkyscraper merges them vertically. */
        {
            SDL_Surface *tools_surf = SDL_CreateRGBSurface(0, 64, 63, 32,
                0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
            if (tools_surf) {
                SDL_FillRect(tools_surf, NULL, SDL_MapRGBA(tools_surf->format, 192, 192, 192, 255));
                for (int ti = 0; ti < 3; ti++) {
                    NEResource *res = ne_find(&game.exe, NE_RT_BITMAP, 0x825C + ti);
                    if (!res) continue;
                    SDL_Surface *bmp = sprites_dib_to_surface(&game.sprites, res);
                    if (!bmp) continue;
                    SDL_Rect dst = { 0, ti * 21, 64, 21 };
                    SDL_BlitSurface(bmp, NULL, tools_surf, &dst);
                    SDL_FreeSurface(bmp);
                }
                game.ui_tools = SDL_CreateTextureFromSurface(game.renderer, tools_surf);
                game.ui_tools_w = tools_surf->w;
                game.ui_tools_h = tools_surf->h;
                SDL_FreeSurface(tools_surf);
                printf("🎨 Tool buttons loaded: %dx%d\n", game.ui_tools_w, game.ui_tools_h);
            }
        }
        
        /* Map background: 0x8160 (200×288) */
        {
            NEResource *res = ne_find(&game.exe, NE_RT_BITMAP, 0x8160);
            if (res) {
                SDL_Surface *surf = sprites_dib_to_surface(&game.sprites, res);
                if (surf) {
                    game.ui_map = SDL_CreateTextureFromSurface(game.renderer, surf);
                    game.ui_map_w = surf->w;
                    game.ui_map_h = surf->h;
                    SDL_FreeSurface(surf);
                    printf("🎨 Map background loaded: %dx%d\n", game.ui_map_w, game.ui_map_h);
                }
            }
        }
    }
    
    /* Print key sprite info for debugging */
    {
        struct { uint16_t id; const char *name; } checks[] = {
            {SPR_LOBBY_BOT0, "lobby"},
            {SPR_OFFICE_BASE, "office"},
            {SPR_CONDO_BASE, "condo"},
            {SPR_RESTAURANT_COMP, "restaurant_comp"},
            {SPR_FASTFOOD_COMP, "fastfood_comp"},
            {SPR_HOTEL_S_COMP, "hotel_s_comp"},
            {SPR_HOTEL_T_COMP, "hotel_t_comp"},
            {SPR_HOTEL_SUITE_COMP, "hotel_suite_comp"},
            {SPR_STAIRS_COMP, "stairs_comp"},
            {SPR_ESCALATOR_COMP, "escalator_comp"},
            {SPR_SECURITY, "security"},
            {SPR_MEDICAL_COMP, "medical_comp"},
            {SPR_RECYCLING_EMPTY, "recycling"},
            {SPR_PARKING_COMP, "parking_comp"},
            {SPR_PARTYHALL_COMP, "partyhall_comp"},
            {SPR_CINEMA_COMP, "cinema_comp"},
            {SPR_METRO_COMP, "metro_comp"},
            {SPR_UNDERGROUND, "underground"},
            {0x8668, "shop"},
            {SPR_WEDDING_ANIM, "wedding_anim"},
            {SPR_CLOUD_0, "cloud_0"},
            {SPR_CLOUD_1, "cloud_1"},
            {SPR_CLOUD_2, "cloud_2"},
            {0x8352, "sky"},
            {0, NULL}
        };
        printf("\n=== Sprite diagnostics ===\n");
        for (int i = 0; checks[i].name; i++) {
            Sprite *s = sprites_find(&game.sprites, checks[i].id);
            if (s) printf("  0x%04x %-18s %dx%d (type 0x%04x)\n", 
                          checks[i].id, checks[i].name, s->w, s->h, s->type);
            else printf("  0x%04x %-18s NOT FOUND\n", checks[i].id, checks[i].name);
        }
        printf("===========================\n\n");
    }
    
    /* Initialize tower and simulation */
    tower_init(&game.tower);
    game_init(&game.sim);
    game.show_debug = 0;  /* Start with debug off, F1 to toggle */
    game.stats_x = game.stats_y = -1;   /* default placements until dragged */
    game.tune_x = game.tune_y = -1;
    game.menu_open = -1;
    game.menu_hover = -1;
    game.menu_bar_hover = -1;
    game.rainy_day = 0;
    
    /* Default window positions (matching original SimTower layout) — shifted
     * below the menu bar so nothing overlaps it. */
    game.map_x = 0;
    game.map_y = MENU_BAR_H;           /* Top left, under the bar */
    game.tool_x = 0;
    game.tool_y = MENU_BAR_H + MAP_WIN_H;  /* Below minimap */
    game.info_x = game.screen_w - INFO_BAR_W;
    game.info_y = MENU_BAR_H;          /* Top right, under the bar */
    game.win_dragging = 0;
    game.tool_popup = -1;
    game.demolish_mode = 0;
    game.win_toolbar = game.win_infobar = game.win_map = 1;
    game.anim_people = game.anim_effects = 1;
    game.snd_elev = game.snd_bg = game.snd_events = 1;
    /* Test affordances: --screenshot renders one input-less frame, so allow forcing
     * UI states for visual verification — TB_POPUP=<button index> opens a group's
     * pull-down; DEMOLISH=1 activates the bulldozer. */
    if (getenv("TB_POPUP")) game.tool_popup = atoi(getenv("TB_POPUP"));
    if (getenv("MENU_OPEN")) game.menu_open = atoi(getenv("MENU_OPEN"));
    if (getenv("DEMOLISH")) game.demolish_mode = 1;
    if (getenv("STATS")) game.show_stats = 1;
    if (getenv("TUNING")) game.show_tuning = 1;
    if (getenv("ELV_DLOG")) {       /* open the dialog on the demo shaft */
        game.elv_open = 1;
        game.elv_sx = getenv("ELV_SX") ? atoi(getenv("ELV_SX")) : 172;
        game.elv_stype = getenv("ELV_STYPE") ? atoi(getenv("ELV_STYPE"))
                                             : ITEM_ELEVATOR_SHAFT;
        game.elv_x = 340; game.elv_y = 120;
    }
    if (getenv("FIN_DLG")) {        /* open the financial report for a screenshot */
        game.fin_open = 1;
        game.fin_x = 300; game.fin_y = 30;
    }
    if (getenv("SIM_SPEED")) game.sim.speed = atoi(getenv("SIM_SPEED"));

    /* Game mode. Campaign (default): star-gated, starts on an empty lot.
     * Sandbox: everything unlocked. Toggle with --sandbox / --campaign,
     * CT_MODE=0|1, or F8 in game. */
    game.sim.mode = MODE_CAMPAIGN;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--campaign"))      game.sim.mode = MODE_CAMPAIGN;
        else if (!strcmp(argv[i], "--sandbox"))  game.sim.mode = MODE_SANDBOX;
    }
    if (getenv("CT_MODE")) game.sim.mode = atoi(getenv("CT_MODE"));

    add_event_message("Welcome to ConcilliaTower!");
    add_event_message("Click to build your tower.");
    
    /* Build demo tower only if --demo flag is passed */
    int demo_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--demo") == 0) { demo_mode = 1; break; }
    }
    if (demo_mode) {
        tower_build_demo(&game.tower);
    }

    /* Import an original SimTower save: any .tdt/.twr argument, or
     * CT_TWR=path. Replaces the fresh tower wholesale. */
    const char *twr_path = getenv("CT_TWR");
    for (int i = 1; i < argc; i++) {
        size_t n = strlen(argv[i]);
        if (n > 4 && (!strcasecmp(argv[i] + n - 4, ".tdt") ||
                      !strcasecmp(argv[i] + n - 4, ".twr")))
            twr_path = argv[i];
    }
    if (twr_path) {
        char err[256];
        if (twr_import(twr_path, &game.tower, &game.sim, err, sizeof(err)) == 0) {
            add_event_message("Imported original SimTower save!");
        } else {
            fprintf(stderr, "TWR import failed: %s\n", err);
            tower_init(&game.tower);
            game_init(&game.sim);
        }
    }

    /* Auto-load the last save at boot when CT_AUTOLOAD is set (the systemd
     * service sets it). Without this a service restart boots a FRESH LOT
     * even with a valid save on disk — and the F5-then-restart deploy
     * protocol then saves the fresh lot over the player's real tower
     * (2026-08-01 lost-tower incident; recovered from snapshot).
     * Env-gated so CT_* verification harnesses keep their clean fixtures. */
    int booted_from_save = 0;
    if (!demo_mode && !twr_path && getenv("CT_AUTOLOAD")) {
        FILE *sf = fopen(save_path(), "rb");
        if (sf) {
            fclose(sf);
            do_load_game();
            booted_from_save = game.tower.tenant_count > 0;
            printf("Boot autoload: %s (tenants=%d pop=%d money=%ld)\n",
                   save_path(), game.tower.tenant_count,
                   game.tower.population, (long)game.tower.money);
        }
    }

    if (getenv("CT_INSPECT")) {     /* open the tenant info dialog on a unit */
        int want = atoi(getenv("CT_INSPECT")); /* ItemType; 0 = first priced/venue */
        int skip = getenv("CT_INSPECT_SKIP") ? atoi(getenv("CT_INSPECT_SKIP")) : 0;
        for (int i = 0; i < game.tower.tenant_count; i++) {
            Tenant *t = &game.tower.tenants[i];
            if (t->type == ITEM_NONE) continue;
            int match = want ? ((int)t->type == want)
                : (t->type == ITEM_OFFICE || t->type == ITEM_CONDO ||
                   t->type == ITEM_SHOP || t->type == ITEM_CINEMA ||
                   t->type == ITEM_RESTAURANT || t->type == ITEM_FAST_FOOD ||
                   item_is_hotel_room(t->type));
            /* cinema: prefer the hall (the entrance strip has no film UI) */
            if (match && t->type == ITEM_CINEMA && t->width < 20) match = 0;
            if (match && skip-- > 0) continue;
            if (match) {
                game.inspect_open = 1;
                game.inspect_tid = t->id;
                game.rent_dd_open = getenv("CT_INSPECT_DD") ? 1 : 0;
                game.inspect_x = 40;
                game.inspect_y = 70;
                if (getenv("CT_NAME_EDIT")) open_name_editor(t);
                if (getenv("CT_MOVIE_DLG")) game.movie_dlg_open = 1;
                break;
            }
        }
    }

    /* Screenshot affordance: a small commuting scene — an office stack over
     * the lobby with a standard elevator beside it (COMMUTE_DEMO=1) */
    if (getenv("COMMUTE_DEMO")) {
        game.tower.money = 100000000L;
        for (int f = 1; f <= 6; f++)
            tower_place(&game.tower, ITEM_OFFICE, f, 183);
        for (int f = 0; f <= 6; f++)
            tower_place(&game.tower, ITEM_ELEVATOR_SHAFT, f, 172);
    }

    /* Campaign starts on an empty lot — drop the convenience starter lobby so
     * the player lays their own (lobby is a 1-star item, so it's available).
     * Skipped for Sandbox, imports, and demo scenes. */
    if (game.sim.mode == MODE_CAMPAIGN && !twr_path && !demo_mode &&
        !booted_from_save && !getenv("COMMUTE_DEMO")) {
        /* booted_from_save: the boot autoload already replaced the fresh
         * lot with a real tower — wiping the grid here (but not money/sim)
         * was exactly the half-loaded ghost state of the 2026-08-01
         * lost-tower incident's second act. */
        memset(game.tower.grid, 0, sizeof(game.tower.grid));
        game.tower.tenant_count = 0;
        add_event_message("Campaign: build a lobby to begin.");
    }

    /* Center camera — show the tower nicely.
     * If screenshot mode with "underground" path, show underground view */
    game.cam_fx = (TOWER_WIDTH * CELL_W) / 2.0f;
    int show_underground = 0;
    if (auto_screenshot && strstr(screenshot_path, "underground")) {
        game.cam_fy = 3.0f * CELL_H;  /* Show underground */
        show_underground = 1;
    } else {
        game.cam_fy = -2.0f * CELL_H;  /* Show floors 0-6 */
    }
    if (getenv("CT_CAM_FLOOR"))        /* center the view on a floor */
        game.cam_fy = -atof(getenv("CT_CAM_FLOOR")) * CELL_H;
    if (getenv("CT_CAM_X"))
        game.cam_fx = atof(getenv("CT_CAM_X")) * CELL_W;
    if (getenv("CT_MAP_MODE"))
        game.map_mode = atoi(getenv("CT_MAP_MODE")) & 3;
    if (getenv("CT_WEDDING"))          /* demo: run the TOWER ceremony */
        game.sim.wedding.active = 1;
    if (getenv("CT_FIRE")) {           /* demo: force a fire (arg = floor) —
                                        * goes through StartFire's real gates
                                        * (star>2, security, no cathedral),
                                        * like the EXE's debug-menu caller */
        game_start_fire(&game.sim, &game.tower, atoi(getenv("CT_FIRE")));
        if (!game.sim.event.active)
            printf("CT_FIRE: StartFire gates refused it (star>2? security? "
                   "cathedral built? floor extent >= 32 cells?)\n");
    }
    if (getenv("CT_BOMB")) {           /* demo: force a bomb threat (arg = floor);
                                        * gates: security + star 2/3/4 */
        game_offer_bomb(&game.sim, &game.tower, atoi(getenv("CT_BOMB")));
        if (!game.sim.event.pending)
            printf("CT_BOMB: TryStartEvent gates refused it (security? star 2-4?)\n");
    }
    if (getenv("CT_MODAL")) {          /* demo: open the disaster decision modal */
        const char *which = getenv("CT_MODAL");
        int is_fire = (which[0] == 'f' || which[0] == 'F');
        game.sim.event = (EventState){0};
        game.sim.event.type = is_fire ? EVENT_FIRE : EVENT_BOMB;
        game.sim.event.active = is_fire;   /* a pending fire is already burning */
        game.sim.event.pending = 1;
        game.sim.event.target_floor = 12;
        game.sim.event.target_slot = TOWER_WIDTH / 2;
        game.sim.event.ransom_cost = is_fire ? FIRE_CHOPPER_COST : 300000;
        game.disaster_modal = 1;
        game.disaster_saved_speed = SPEED_NORMAL;
        game.sim.speed = SPEED_PAUSED;
    }
    if (getenv("CT_CERT")) {           /* demo: pop the star-up dialog */
        int st = atoi(getenv("CT_CERT"));
        st = st < 2 ? 2 : st > 6 ? 6 : st;
        show_notice_modal(exe_dlg_text((uint16_t)(0xBD6 + st - 2), 0,
                                       "Congratulations!"),
                          exe_dlg_text((uint16_t)(0xBD6 + st - 2), 1, "OK"));
    }
    (void)show_underground;
    game.zoom = 1.0f;
    
    printf("\n=== ConcilliaTower running ===\n");
    printf("Controls:\n");
    printf("  Arrow keys: scroll camera\n");
    printf("  Mouse wheel: scroll vertically\n");
    printf("  Click+drag: place a ROW of buildings\n");
    printf("  Number keys: 1=Office 2=Condo 3=Restaurant 4=FastFood\n");
    printf("               5=Hotel(S) 6=Hotel(T) 7=Hotel(Suite)\n");
    printf("               8=Stairs 9=Escalator 0=Deselect\n");
    printf("  Letter keys: L=Lobby C=Cinema P=PartyHall M=Metro K=Parking\n");
    printf("               H=Cathedral X=Medical G=Security R=Recycling O=Shop\n");
    printf("  Tab/Shift+Tab: cycle through all types\n");
    printf("  Space: pause/unpause, +/-: speed up/slow down\n");
    printf("  F3: analytics graphs, F4: tuning/modding panel\n");
    printf("  ` (backtick): toggle debug labels, F12: screenshot, Q/Esc: quit\n\n");
    
    /* Seed the finance quarter baseline so the first report (before any 3-day
     * settlement snapshots it) reads sensibly instead of a $0 opening balance. */
    game.sim.fin_last_balance = game.tower.money;
    game.sim.fin_built_at_q_start = game.tower.built_value;

    /* Main loop */
    game.running = 1;
    game.build_type = ITEM_OFFICE;

    int frame = 0;
    while (game.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            handle_event(&ev);
        }

        /* Build-drag clatter (#7001, referee row 18): a looping sound that
         * rings while a build is being dragged out and stops on release. */
        {
            static int prev_drag = 0;
            int drag_now = game.dragging && game.build_type != ITEM_NONE;
            if (drag_now && !prev_drag)      audio_start_loop(SND_BUILD_DRAG, 0.5f);
            else if (!drag_now && prev_drag) audio_stop_loop();
            prev_drag = drag_now;
        }

        /* Advance simulation (halted while the Find modal is up, like
         * the EXE's DialogBoxParam) */
        if (!game.find_open) {
            int prev_hour = game.sim.hour;
            int prev_star = game.tower.star_rating;
            int prev_pop = game.tower.population;
            int prev_unreach = game.sim.unreachable_tenants;
            int prev_event_active = game.sim.event.active;
            game_update(&game.sim, &game.tower);

            /* Loop channel: fire crackle (#10009) while a fire burns, guard
             * search footsteps (#10014) while the bomb sweep runs. One event
             * at a time in the EXE, so the single loop channel suffices.
             * One-shots (outbreak #10006, explosion #10004) fire from game.c. */
            {
                static int prev_loop = 0;   /* 0 none / 1 fire / 2 hunt */
                int want = !game.sim.event.active ? 0
                         : game.sim.event.type == EVENT_FIRE ? 1
                         : (game.sim.event.type == EVENT_BOMB &&
                            game.sim.event.hunt.active) ? 2 : 0;
                if (want != prev_loop) {
                    audio_stop_loop();
                    if (want == 1) audio_start_loop(SND_FIRE_LOOP, 0.5f);
                    if (want == 2) audio_start_loop(SND_GUARD_STEP, 0.5f);
                    prev_loop = want;
                }
            }

            /* A freshly proposed disaster pauses the game for the player's
             * decision (handled by the modal). */
            if (game.sim.event.pending && !game.disaster_modal) {
                game.disaster_modal = 1;
                game.disaster_saved_speed =
                    (game.sim.speed == SPEED_PAUSED) ? SPEED_NORMAL : game.sim.speed;
                game.sim.speed = SPEED_PAUSED;
            }

            /* Demo: pin the clock so retail open/closed frames can be captured
             * (the sim re-derives time_of_day from hour each tick, so override
             * after the update to freeze it). */
            if (getenv("CT_HOUR")) {
                int h = atoi(getenv("CT_HOUR"));
                game.sim.hour = h;
                game.sim.time_of_day =
                    (h >= 5  && h < 7)  ? TOD_DAWN :
                    (h >= 7  && h < 12) ? TOD_MORNING :
                    (h >= 12 && h < 17) ? TOD_AFTERNOON :
                    (h >= 17 && h < 21) ? TOD_EVENING : TOD_NIGHT;
            }

            /* Disaster events: announce onset and resolution in the feed.
             * On the falling edge the sim leaves caught/damage_cost intact
             * (only cleared when the next event starts), so we can read them. */
            if (game.sim.event.active && !prev_event_active) {
                char buf[48];
                if (game.sim.event.type == EVENT_FIRE)
                    snprintf(buf, sizeof buf, "FIRE on floor %d! Evacuate!",
                             game.sim.event.target_floor);
                else
                    snprintf(buf, sizeof buf, "Bomb threat on floor %d! Security responding.",
                             game.sim.event.target_floor);
                add_event_message(buf);
            } else if (!game.sim.event.active && prev_event_active) {
                /* Resolution: the EXE pops a one-button dialog (0xBCF
                 * caught / 0xBD0 exploded / 0xBC5 fire out) — shown as a
                 * notice modal with its wording and button; the feed keeps
                 * a short log line. */
                char buf[EVENT_MSG_LEN];
                if (game.sim.event.type == EVENT_BOMB && game.sim.event.caught) {
                    show_notice_modal(
                        exe_dlg_text(0xBCF, 0,
                                     "Security Forces found the bomb.  "
                                     "Good work!"),
                        exe_dlg_text(0xBCF, 1, "Thanks!"));
                    snprintf(buf, sizeof buf,
                             "Security caught the bomb! Crisis averted.");
                } else if (game.sim.event.type == EVENT_BOMB) {
                    char full[192], num[8];
                    snprintf(num, sizeof num, "%d",
                             game.sim.event.target_floor);
                    str_subst(full, sizeof full,
                              exe_dlg_text(0xBD0, 0,
                                           "Security was not able to find "
                                           "the bomb in time.  The bomb has "
                                           "exploded on floor ^0!"),
                              "^0", num);
                    show_notice_modal(full, exe_dlg_text(0xBD0, 1, "Oh No!"));
                    snprintf(buf, sizeof buf, "BOMB EXPLODED! $%d in damage.",
                             game.sim.event.damage_cost);
                } else {
                    show_notice_modal(
                        exe_dlg_text(0xBC5, 0,
                                     "The fire was stopped.\nBecause your "
                                     "building has emergency stairs, no one "
                                     "was injured, but the tower is damaged."),
                        exe_dlg_text(0xBC5, 1, "I Know"));
                    snprintf(buf, sizeof buf, "Fire extinguished on floor %d.",
                             game.sim.event.target_floor);
                }
                add_event_message(buf);
            }

            /* VIP visits: the EXE's dialogs 0xBB9 (arrival), 0xBBA (happy
             * checkout), 0xBBB (unhappy) as notice modals — with the EXE's
             * button labels — plus a short feed log line. */
            if (game.sim.vip_notice) {
                if (game.sim.vip_notice == 1) {
                    show_notice_modal(
                        exe_dlg_text(0xBB9, 1,
                                     "A VIP has arrived at your Tower."),
                        exe_dlg_text(0xBB9, 0, "Oh No!"));
                    add_event_message("A VIP has arrived at your Tower.");
                } else if (game.sim.vip_notice == 2) {
                    char buf[192];
                    snprintf(buf, sizeof buf, "%s\n%s",
                             exe_dlg_text(0xBBA, 1, "The VIP has checked out."),
                             exe_dlg_text(0xBBA, 2,
                                          "They seem to have had a "
                                          "comfortable stay!"));
                    show_notice_modal(buf, exe_dlg_text(0xBBA, 0, "Whew!"));
                    add_event_message("The VIP had a comfortable stay!");
                } else {
                    show_notice_modal(
                        exe_dlg_text(0xBBB, 0,
                                     "Sorry! The VIP seems to have had an "
                                     "uncomfortable stay.  They are not "
                                     "pleased with your tower."),
                        exe_dlg_text(0xBBB, 1, "Rats!"));
                    add_event_message("The VIP was not pleased.");
                }
                game.sim.vip_notice = 0;
            }

            /* Star-requirement nags (res 0x3f2, verbatim; once a day). */
            {
                static const char *NAGS[5] = {
                    "Your tower needs Security",
                    "Your tower needs Hotel Suites",
                    "Your tower needs a Recycling Center",
                    "Recycling Centers are full!",
                    "Office workers demand Parking",
                };
                int nag = game_take_star_nag();
                if (nag >= 1 && nag <= 5)
                    add_event_message(NAGS[nag - 1]);
            }

            /* Santa holiday flyby — announce on the rising edge. */
            {
                static int prev_santa = 0;
                if (game.sim.santa.active && !prev_santa)
                    add_event_message("Happy holidays! Santa flies over the tower!");
                prev_santa = game.sim.santa.active;
            }

            /* Wedding / TOWER (5-star) promotion music (referee row 6):
             * ChurchT StartMarry plays it as the ceremony begins. */
            {
                static int prev_wedding = 0;
                if (game.sim.wedding.active && !prev_wedding)
                    play_snd(SND_WEDDING);
                prev_wedding = game.sim.wedding.active;
            }

            /* Bomb/terror explosion (referee row 2): fired as the blast lands,
             * i.e. when the destroyed-value counter jumps. */
            {
                static int prev_damage = 0;
                if (game.sim.event.damage_cost > prev_damage)
                    play_snd(SND_EXPLOSION);
                prev_damage = game.sim.event.damage_cost;
            }

            /* Medical shortage nag (the real MedicalT mechanic, MoreMedical-
             * Please): a sick worker couldn't reach a center. One-shot feed. */
            if (game.sim.medical_nag) {
                add_event_message("A sick worker found no medical center - build more!");
                game.sim.medical_nag = 0;
            }

            /* Star promotion: the EXE pops its congratulations dialog
             * (0xBD6..0xBDA — 2..5 stars + Tower), a plain message box.
             * The old parchment "certificate" card was a port invention
             * (and its star bitmaps dragged the info-bar background
             * along). Faithful = the real dialog text + OK. */
            if (game.sim.pending_star_up) {
                int st = game.sim.pending_star_up;
                st = st < 2 ? 2 : st > 6 ? 6 : st;
                show_notice_modal(
                    exe_dlg_text((uint16_t)(0xBD6 + st - 2), 0,
                                 "Congratulations!\nYour tower has been "
                                 "given a new Star Rating!"),
                    exe_dlg_text((uint16_t)(0xBD6 + st - 2), 1, "OK"));
                game.sim.pending_star_up = 0;
            }

            /* Commute feedback: units cut off from the entrance. Names the
             * lowest dark floor with the res-0x2cd floor-pair phrasing —
             * the actionable half of the warning. */
            if (game.sim.unreachable_tenants > prev_unreach) {
                int dark = TOWER_MAX_FLOOR + 1;
                for (int i = 0; i < game.tower.tenant_count; i++) {
                    const Tenant *t = &game.tower.tenants[i];
                    if (t->type == ITEM_NONE || t->type == ITEM_FLOOR ||
                        item_is_transport(t->type)) continue;
                    int fi = floor_to_index(t->floor);
                    if (fi < 0 || fi >= TOWER_FLOOR_COUNT) continue;
                    if (!game.sim.reach_public[fi] && t->floor < dark)
                        dark = t->floor;
                }
                char buf[64];
                if (dark <= TOWER_MAX_FLOOR) {
                    char fl[16];
                    if (dark < 0) snprintf(fl, sizeof fl, "B%d", -dark);
                    else          snprintf(fl, sizeof fl, "%d", dark);
                    snprintf(buf, sizeof buf,
                             "People on Floor 1 need path to Floor %s", fl);
                } else {
                    snprintf(buf, sizeof buf, "%d unit%s cannot be reached!",
                             game.sim.unreachable_tenants,
                             game.sim.unreachable_tenants == 1 ? "" : "s");
                }
                add_event_message(buf);
            } else if (prev_unreach > 0 && game.sim.unreachable_tenants == 0) {
                add_event_message("All units connected.");
            }
            
            /* Decide rainy day at 5 AM (from OpenSkyscraper: every 3rd day) */
            if (prev_hour == 4 && game.sim.hour == 5) {
                game.rainy_day = (rand() % 3 == 0);
                if (game.rainy_day) add_event_message("Bad weather incoming...");
                else add_event_message("Nice weather today!");
            }

            /* Day-clock chimes (referee_ambient_timing Q2). The EXE fires these
             * at exact times of day; the port's clock is uniform with real
             * minutes, so fire on the minute we cross each target. The 8:00 and
             * 8:30 fanfares only ring on a "special day" = every 8th day while
             * below 5 stars (the rainy-morning flag). The dawn jingle is 5:30 AM
             * (not 7:00), with a day%5==4 variant. */
            {
                static int prev_hm = -1;
                int hm = game.sim.hour * 60 + game.sim.minute;
                int special = (game.tower.day % 8 == 4) && (game.tower.star_rating < 5);
                #define CHIME_CROSS(t) (prev_hm >= 0 && prev_hm < (t) && hm >= (t))
                if (CHIME_CROSS(5*60+30))
                    play_snd((game.tower.day % 5 == 4) ? SND_NEWDAY_SPEC : SND_NEWDAY);
                if (special && CHIME_CROSS(8*60))     play_snd(SND_FANFARE_8AM);
                if (special && CHIME_CROSS(8*60+30))  play_snd(SND_FANFARE_830);
                if (CHIME_CROSS(9*60))                play_snd(SND_CHIME_9AM);
                if (CHIME_CROSS(18*60))               play_snd(SND_EVENING);
                #undef CHIME_CROSS
                prev_hm = hm;
            }

            /* Ambient soundscape: sample an on-screen cell, play its type's bed. */
            if (game.sim.speed != SPEED_PAUSED)
                ambient_tick();

            /* Floor-pair route warning (STRL 0x2CD via 10a8:1b58) */
            {
                const char *nr = people_take_noroute_msg();
                if (nr) add_event_message(nr);
            }
            
            /* Star rating change */
            if (game.tower.star_rating != prev_star) {
                char buf[48];
                snprintf(buf, sizeof(buf), "Tower promoted to %d stars!", game.tower.star_rating);
                add_event_message(buf);
            }
            
            /* Population milestones */
            if (prev_pop < 100 && game.tower.population >= 100) add_event_message("Population reached 100!");
            else if (prev_pop < 300 && game.tower.population >= 300) add_event_message("Population reached 300!");
            else if (prev_pop < 1000 && game.tower.population >= 1000) add_event_message("Population reached 1,000!");
            
            /* Event announcements (edge: went active this tick) */
            if (game.sim.event.active && !prev_event_active) {
                if (game.sim.event.type == EVENT_FIRE) add_event_message("FIRE! Fire in the tower!");
                else if (game.sim.event.type == EVENT_BOMB) add_event_message("BOMB THREAT reported!");
            }
            
            /* VIP visit */
            if (game.sim.vip_visiting && !game.sim.vip_satisfied) {
                /* announced once */
            }
        }

        /* SmoothScroll toward a minimap click-nav target (CameraT-style
         * animated glide; any manual scroll input cancels it). */
        if (game.cam_anim) {
            float dx = game.cam_tx - game.cam_fx;
            float dy = game.cam_ty - game.cam_fy;
            if (dx > -1.0f && dx < 1.0f && dy > -1.0f && dy < 1.0f) {
                game.cam_fx = game.cam_tx;
                game.cam_fy = game.cam_ty;
                game.cam_anim = 0;
            } else {
                game.cam_fx += dx * 0.22f;
                game.cam_fy += dy * 0.22f;
            }
        }
        clamp_camera();
        render();
        frame++;

        /* Capture mode: mix one frame's worth of audio (60fps -> real-time
         * pacing) into the WAV buffer, then stop after CT_SOUND_FRAMES. */
        if (g_sound_capture) {
            audio_advance(AUDIO_DEV_FREQ / 60);
            /* Capture-only marker: report the frame (=> WAV timestamp) when the
             * settlement cha-ching run kicks off, so a clip can be trimmed to it. */
            if (getenv("CT_SOUND_DEBUG")) {
                static int prev_cash = 0;
                if (game.sim.cash_pending > prev_cash)
                    printf("[cap] cash run @ frame %d (t=%.1fs) pending=%d\n",
                           frame, frame / 60.0, game.sim.cash_pending);
                prev_cash = game.sim.cash_pending;
            }
            int cap_frames = getenv("CT_SOUND_FRAMES")
                             ? atoi(getenv("CT_SOUND_FRAMES")) : 600;
            if (frame >= cap_frames) {
                const char *p = getenv("CT_SOUND_CAPTURE");
                if (audio_capture_write_wav(p) == 0)
                    printf("Sound capture written to %s (%d frames)\n", p, frame);
                else
                    fprintf(stderr, "Sound capture: nothing recorded\n");
                if (getenv("CT_SOUND_DEBUG")) snd_tally_dump();
                game.running = 0;
            }
        }

        /* Auto-screenshot: run sim for a bit first so tenants wake up.
         * SHOT_FRAME=N overrides how long the sim runs before the capture. */
        static int shot_frame = 0;
        if (!shot_frame)
            shot_frame = getenv("SHOT_FRAME") ? atoi(getenv("SHOT_FRAME")) : 200;
        if (auto_screenshot && frame == shot_frame) {
            /* Deterministic click-injection for headless UI tests:
             * ELV_TEST_CLICKS="x,y;x,y;..." routes each through the dialog
             * click handler before the capture, so state changes are visible. */
            const char *clicks = getenv("ELV_TEST_CLICKS");
            if (clicks) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s", clicks);
                for (char *tok = strtok(buf, ";"); tok; tok = strtok(NULL, ";")) {
                    int cx, cy;
                    if (sscanf(tok, "%d,%d", &cx, &cy) == 2) {
                        elv_dialog_click(cx, cy);
                        fin_dialog_click(cx, cy);
                        /* tenant info dialog + its modal sub-dialogs */
                        if (game.name_edit_open)       name_editor_click(cx, cy);
                        else if (game.movie_dlg_open)  movie_chooser_click(cx, cy);
                        else                           inspect_popup_click(cx, cy);
                        Tenant *it = tower_tenant(&game.tower, game.inspect_tid);
                        const char *nm = tenant_custom_name(game.inspect_tid);
                        printf("[test] click %d,%d -> dd_open=%d rent_class=%d "
                               "movie_id=%d name=%s\n", cx, cy, game.rent_dd_open,
                               it ? it->rent_class : -1, it ? it->movie_id : -1,
                               nm ? nm : "(none)");
                    }
                }
                render();
            }
            /* Headless test for the edit-mode stop toggle: ELV_WORLD_CLICKS
             * routes each point through the real world-click toggle path. */
            const char *wclicks = getenv("ELV_WORLD_CLICKS");
            if (wclicks && game.elv_edit_mode) {
                char buf[256];
                snprintf(buf, sizeof(buf), "%s", wclicks);
                for (char *tok = strtok(buf, ";"); tok; tok = strtok(NULL, ";")) {
                    int cx, cy;
                    if (sscanf(tok, "%d,%d", &cx, &cy) == 2) {
                        int fl, cell; screen_to_grid(cx, cy, &fl, &cell);
                        int hit = elv_edit_toggle_at(cx, cy);
                        printf("[test] world click %d,%d -> floor=%d cell=%d "
                               "toggled=%d\n", cx, cy, fl, cell, hit);
                    }
                }
                render();
            }
            SDL_Surface *sshot = SDL_CreateRGBSurface(0, game.screen_w, game.screen_h, 32,
                0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
            SDL_RenderReadPixels(game.renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                 sshot->pixels, sshot->pitch);
            SDL_SaveBMP(sshot, screenshot_path);
            SDL_FreeSurface(sshot);
            printf("Auto-screenshot saved to %s\n", screenshot_path);
            game.running = 0;
        }
        
        /* In headless --screenshot mode, fast-forward the warm-up frames with no
         * frame delay so a deep sim state (e.g. a settlement several game-days
         * out) is reachable quickly; pace normally otherwise. */
        if (!auto_screenshot) SDL_Delay(16); /* ~60fps */
    }
    
    /* Cleanup */
    sprites_free(&game.sprites);
    if (game.font) TTF_CloseFont(game.font);
    if (game.font_small) TTF_CloseFont(game.font_small);
    if (game.font_info) TTF_CloseFont(game.font_info);
    TTF_Quit();
    SDL_DestroyRenderer(game.renderer);
    SDL_DestroyWindow(game.window);
    SDL_Quit();
    ne_free(&game.exe);
    
    printf("Exited cleanly.\n");
    return 0;
}
