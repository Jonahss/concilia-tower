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

/* ---------- Window / display ---------- */
#define WINDOW_W    960
#define WINDOW_H    720
#define HUD_HEIGHT  0     /* No separate HUD — info window handles it */
#define MENU_BAR_H  0     /* No menu bar — toolbox is the interface */
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
#define SPR_LOBBY_MID0  0x8a28   /* Lobby above-ground segment (raw) */

/* Floor/ceiling color source — 96×36, extract column at x=16 for floor color */
#define SPR_FLOOR_SRC   0x83e8

/* Entrance decoration — red awning at ground level */
#define SPR_ENTRANCE    0x83e9

/* Office: 0x85A8-0x85AB — 288×24 (9 frames of 32px) */
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
 *        0x8673-0x8674 = end-cap/signage pieces, 96×24 each
 * OpenSkyscraper: loadMergedByID(shops[1], 'y', 0x8668..0x8672) */
#define SPR_SHOP_BASE   0x8668  /* 288×24, 3 frames of 96px */
#define SPR_SHOP_VARIANTS 11    /* 0x8668 through 0x8672 */
#define SPR_SHOP_END0   0x8673  /* Left/right end-cap, 96×24 */
#define SPR_SHOP_END1   0x8674

/* Security: 0x8768 (animated, 3 frames via palette cycling) */
#define SPR_SECURITY    0x8768

/* Medical: 0x8728+0x8729+0x872A (3 bitmaps horizontally) */
#define SPR_MEDICAL_A   0x8728
#define SPR_MEDICAL_B   0x8729
#define SPR_MEDICAL_C   0x872A

/* Cathedral: NO exterior building sprite exists!
 * 0x8828 (128×36) is the WEDDING PROCESSION ANIMATION — brides in white,
 * grooms in top hats, couples walking. Used during the CheckMarry event.
 * The building itself is rendered as a plain floor with fallback color.
 * ChurchT.c: OpenChurch, CloseChurch, StartMarry, CheckMarry
 * Wedding event required for ★★★★★→TOWER transition.
 * Cathedral can ONLY be placed on floor 100. */
#define SPR_WEDDING_ANIM  0x8828  /* NOT a building sprite — event animation */

/* Recycling: 0x88E8 (empty state, single DIB) */
#define SPR_RECYCLING_EMPTY 0x88e8

/* Parking space: 0x86A8+0x86A9 */
#define SPR_PARKING_A   0x86A8
#define SPR_PARKING_B   0x86A9

/* Party Hall: 0x8B28 (top) + 0x8B68 (bottom) vertically */
#define SPR_PARTYHALL_TOP 0x8B28
#define SPR_PARTYHALL_BOT 0x8B68

/* Cinema hall: 0x8868 (upper, animated) + 0x88A8 (lower) */
#define SPR_CINEMA_UPPER 0x8868
#define SPR_CINEMA_LOWER 0x88A8

/* Cinema screens: 0x8C68+0x8CA8 (screen 0), 0x8C69+0x8CA9 (screen 1) */
#define SPR_CINEMA_SCR0_TOP 0x8C68
#define SPR_CINEMA_SCR0_BOT 0x8CA8

/* Metro: 0x8BA8/0x8BA9 (row 0), +0x40 each row */
#define SPR_METRO_BASE  0x8BA8

/* Elevator: shaft + cars */
#define SPR_ELEV_CAR     0x8428   /* standard car, empty (32×36) */
#define SPR_ELEV_STD_LOADED 0x8429 /* standard car frames 1-4 + engine (×32) */
#define SPR_ELEV_SERVICE 0x842a   /* service car frames 0-4 (×32) */
#define SPR_ELEV_EXPRESS 0x842b   /* express car frames 0-4 + engine (×48) */
#define SPR_ELEV_QUEUE   0x8468   /* waiting people silhouettes (40 × 16px) */
#define SPR_ELEV_SHAFT   0x87e8   /* shaft sections: tile 0 plain, 1+ digits */
#define SPR_ELEV_DIGITS  0x87e9   /* floor digits 0-9, 11x17 glyphs at
                                   * (1+16*n, 16); bg 25,25,25 keyed out.
                                   * 0x87ec = red variant (purpose TBD) */

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
#define SPR_FIRELADDER   0x842D   /* Fire escape stairs — zigzag up the side */
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

/* ---------- Sprite mapping for item types ---------- */
static uint16_t item_sprite_id(ItemType type, int *frame_w, int *floors)
{
    *floors = ITEM_HEIGHT[type];
    switch (type) {
    case ITEM_LOBBY:         *frame_w = 0;   return SPR_LOBBY_BOT0;
    case ITEM_OFFICE:        *frame_w = 72;  return SPR_OFFICE_BASE;
    case ITEM_CONDO:         *frame_w = 128; return SPR_CONDO_BASE;
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
    case ITEM_CATHEDRAL:     *frame_w = 0;   return 0; /* No building exterior sprite exists!
        * 0x8828 is the wedding procession animation (brides+grooms).
        * Original game renders cathedral as a plain floor with the wedding
        * event animation overlaid. Use fallback color for now. */
    case ITEM_MEDICAL:       *frame_w = 208; return SPR_MEDICAL_COMP;  /* 3 states × 208px */
    case ITEM_SECURITY:      *frame_w = 128; return SPR_SECURITY;     /* 128px, palette animated */
    case ITEM_RECYCLING:     *frame_w = 200; return SPR_RECYCLING_EMPTY; /* 200×60, single frame */
    case ITEM_STAIRS:        *frame_w = 64;  return SPR_STAIRS_COMP;
    case ITEM_ESCALATOR:     *frame_w = 64;  return SPR_ESCALATOR_COMP;
    case ITEM_ELEVATOR_SHAFT:
    case ITEM_ELEVATOR_SERVICE:
    case ITEM_ELEVATOR_EXPRESS: *frame_w = 32; return SPR_ELEV_SHAFT; /* reuse shaft art for now */
    case ITEM_FLOOR:         *frame_w = 0;   return 0;
    case ITEM_HOUSEKEEPING:  *frame_w = 0;   return 0; /* no exterior sprite yet (fallback block) */
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
    int             elv_open;     /* Elevator dialog (double-click a shaft) */
    int             elv_sx;       /* shaft column the dialog is bound to */
    int             elv_stype;    /* shaft ItemType (column+type = identity) */
    int             elv_x, elv_y; /* dialog window position (draggable) */
    
    /* Build mode */
    ItemType        build_type;
    int             demolish_mode;   /* Bulldozer active: clicks remove facilities */
    int             mouse_x, mouse_y;
    int             mouse_floor, mouse_cell;
    
    /* Drag placement */
    int             dragging;       /* 1 if currently dragging to place */
    int             drag_start_cell;
    int             drag_start_floor;
    
    /* Camera smoothing */
    float           cam_fx, cam_fy;
    
    /* Zoom */
    float           zoom;
    
    /* Cloud sprites (up to 4 different shapes + Santa Easter egg) */
    Sprite         *clouds[SPR_CLOUD_COUNT];
    Sprite         *santa;       /* 0x8388 — Santa helicopter */
    Sprite         *entrances;   /* 0x83E9 — entrance awning */
    Sprite         *crane;       /* 0x83EA — construction crane */
    Sprite         *fireladder;  /* 0x842D — fire escape stairs */
    Sprite         *skyline;     /* 0x8389 — city skyline background */
    int             cloud_count;
    
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
} Game;

static Game game;

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
    { "Stairs\t8",        ITEM_STAIRS,       ACT_NONE },
    { "Escalator\t9",     ITEM_ESCALATOR,    ACT_NONE },
    { NULL, ITEM_NONE, ACT_NONE },
    { "Lobby\tL",         ITEM_LOBBY,        ACT_NONE },
    { "Parking\tK",       ITEM_PARKING,      ACT_NONE },
    { "Metro Station\tM", ITEM_METRO,        ACT_NONE },
};
#define MENU_BUILD_TRANS_COUNT 6

/* Build > Services */
static const MenuItem menu_build_svc[] = {
    { "Security\tG",      ITEM_SECURITY,     ACT_NONE },
    { "Medical Center\tX",ITEM_MEDICAL,      ACT_NONE },
    { "Recycling\tR",     ITEM_RECYCLING,    ACT_NONE },
    { NULL, ITEM_NONE, ACT_NONE },
    { "Cathedral\tH",     ITEM_CATHEDRAL,    ACT_NONE },
};
#define MENU_BUILD_SVC_COUNT 5

/* Speed menu */
static const MenuItem menu_speed[] = {
    { "Paused\tSpace",    ITEM_NONE,  ACT_SPEED_PAUSE },
    { "Normal\t+",        ITEM_NONE,  ACT_SPEED_1 },
    { "Fast\t++",         ITEM_NONE,  ACT_SPEED_2 },
    { "Turbo\t+++",       ITEM_NONE,  ACT_SPEED_3 },
};
#define MENU_SPEED_COUNT 4

/* View menu */
static const MenuItem menu_view[] = {
    { "Debug Labels\t`",   ITEM_NONE,  ACT_DEBUG_TOGGLE },
    { "Screenshot\tF12",   ITEM_NONE,  ACT_SCREENSHOT },
    { NULL, ITEM_NONE, ACT_NONE },
    { "Santa!\tF2",        ITEM_NONE,  ACT_SANTA },
};
#define MENU_VIEW_COUNT 4

/* File menu */
static const MenuItem menu_file[] = {
    { "Quit\tQ",           ITEM_NONE,  ACT_QUIT },
};
#define MENU_FILE_COUNT 1

/* Top-level menus */
typedef struct {
    const char     *label;
    const MenuItem *items;
    int             count;
} TopMenu;

static const TopMenu top_menus[] = {
    { "File",       menu_file,        MENU_FILE_COUNT },
    { "Build Res.", menu_build_res,   MENU_BUILD_RES_COUNT },
    { "Build Com.", menu_build_com,   MENU_BUILD_COM_COUNT },
    { "Transport",  menu_build_trans, MENU_BUILD_TRANS_COUNT },
    { "Services",   menu_build_svc,   MENU_BUILD_SVC_COUNT },
    { "Speed",      menu_speed,       MENU_SPEED_COUNT },
    { "View",       menu_view,        MENU_VIEW_COUNT },
};
#define TOP_MENU_COUNT 7

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
    
    *cell = world_x / CELL_W;
    *floor = -(world_y / CELL_H);
    
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
    
    /* Render active fire/bomb event visual effects */
    if (game.sim.event.active) {
        int evt_floor = game.sim.event.target_floor;
        int floor_y = lobby_sy - (evt_floor * CELL_H);
        
        if (game.sim.event.type == EVENT_FIRE) {
            /* Fire: orange/red flickering across burning slots */
            int fl = game.sim.event.fire_left;
            int fr = game.sim.event.fire_right;
            int fx = lobby_sx + fl * CELL_W;
            int fw = (fr - fl + 1) * CELL_W;
            
            /* Flickering fire overlay — alternates orange/red */
            int flicker = (game.sim.frame % 6 < 3) ? 200 : 255;
            SDL_SetRenderDrawColor(game.renderer, flicker, flicker/4, 0, 120);
            SDL_Rect fire_rect = { fx, floor_y, fw, CELL_H };
            SDL_RenderFillRect(game.renderer, &fire_rect);
            
            /* Fire "embers" — small bright spots */
            SDL_SetRenderDrawColor(game.renderer, 255, 255, 0, 180);
            for (int e = 0; e < 8; e++) {
                int ex = fx + (rand() % fw);
                int ey = floor_y + (rand() % CELL_H);
                SDL_Rect ember = { ex, ey, 3, 3 };
                SDL_RenderFillRect(game.renderer, &ember);
            }
        } else if (game.sim.event.type == EVENT_BOMB) {
            /* Bomb: pulsing red circle on target location */
            int bx = lobby_sx + game.sim.event.target_slot * CELL_W;
            int pulse = 60 + (game.sim.frame % 20) * 4;
            if (pulse > 120) pulse = 180 - pulse;
            SDL_SetRenderDrawColor(game.renderer, 255, 0, 0, pulse);
            SDL_Rect bomb_rect = { bx - 16, floor_y - 8, 32, CELL_H + 16 };
            SDL_RenderFillRect(game.renderer, &bomb_rect);
            
            /* Bomb icon — small bright spot */
            SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 200);
            SDL_Rect icon = { bx - 2, floor_y + CELL_H/2 - 2, 4, 4 };
            SDL_RenderFillRect(game.renderer, &icon);
        }
    }
    
    /* Render Santa flying across the sky (SantaT: x-=10, y+=1 per tick) */
    if (game.sim.santa.active && game.santa) {
        SDL_Rect dst = {
            game.sim.santa.x, game.sim.santa.y,
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
 * exactly like the original (OS Elevator::render). Serviced floors only. */
static void draw_shaft_digits(int tx, int ty, int wf)
{
    Sprite *d = sprites_find(&game.sprites, SPR_ELEV_DIGITS);
    if (!d || wf < 0) return;     /* basements unlabeled (only 0-9 glyphs) */
    char buf[8];
    int n = snprintf(buf, sizeof(buf), "%d", wf);
    if (n < 1 || n > 3) return;
    int x = tx + (32 - (n * 12 - 1)) / 2;
    for (int i = 0; i < n; i++) {
        SDL_Rect src = { 1 + 16 * (buf[i] - '0'), 16, 11, 17 };
        SDL_Rect dst = { x, ty + 10, 11, 17 };
        SDL_RenderCopy(game.renderer, d->texture, &src, &dst);
        x += 12;
    }
}

static void render_tower(void)
{
    /* Determine visible floor range */
    int top_floor, bot_floor, dummy;
    screen_to_grid(0, 0, &top_floor, &dummy);
    screen_to_grid(0, game.screen_h, &bot_floor, &dummy);
    top_floor += 2;
    bot_floor -= 2;
    if (top_floor > TOWER_MAX_FLOOR) top_floor = TOWER_MAX_FLOOR;
    if (bot_floor < TOWER_MIN_FLOOR) bot_floor = TOWER_MIN_FLOOR;
    
    /* Lobby sprite (raw bitmap, 992×36) */
    Sprite *lobby_spr = sprites_find(&game.sprites, SPR_LOBBY_BOT0);
    
    /* ====== PASS 1: Floor backgrounds ======
     * Draw ALL floor backgrounds first, so multi-floor sprites
     * can paint over them without being overwritten. */
    for (int floor = bot_floor; floor <= top_floor; floor++) {
        int fidx = floor_to_index(floor);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
        
        int sx_base, sy_base;
        grid_to_screen(floor, 0, &sx_base, &sy_base);
        
        /* Check if this floor has ANY content */
        int left = TOWER_WIDTH, right = 0;
        for (int x = 0; x < TOWER_WIDTH; x++) {
            if (game.tower.grid[fidx][x].type != ITEM_NONE) {
                if (x < left) left = x;
                if (x > right) right = x;
            }
        }
        if (left > right) continue;  /* Empty floor */

        
        /* Floor background — different for above/below ground */
        if (floor > 0) {
            /* Above ground: warm beige wall */
            int wall_x = sx_base + left * CELL_W;
            int wall_w = (right - left + 1) * CELL_W;
            SDL_SetRenderDrawColor(game.renderer, 198, 195, 182, 255);
            SDL_Rect wall_rect = { wall_x, sy_base, wall_w, CELL_H };
            SDL_RenderFillRect(game.renderer, &wall_rect);
            
            /* Ceiling strip */
            SDL_SetRenderDrawColor(game.renderer, 178, 172, 160, 255);
            SDL_Rect ceil_rect = { wall_x, sy_base, wall_w, CEIL_H };
            SDL_RenderFillRect(game.renderer, &ceil_rect);
            SDL_SetRenderDrawColor(game.renderer, 140, 135, 125, 255);
            SDL_RenderDrawLine(game.renderer, wall_x, sy_base + CEIL_H - 1,
                              wall_x + wall_w, sy_base + CEIL_H - 1);
        } else if (floor < 0) {
            /* Underground: dark brown earth fill where buildings are */
            int ug_x = sx_base + left * CELL_W;
            int ug_w = (right - left + 1) * CELL_W;
            int depth = -floor;
            int r = 120 - depth * 6; if (r < 60) r = 60;
            int g = 85 - depth * 5;  if (g < 35) g = 35;
            int b = 50 - depth * 4;  if (b < 15) b = 15;
            SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
            SDL_Rect ug_rect = { ug_x, sy_base, ug_w, CELL_H };
            SDL_RenderFillRect(game.renderer, &ug_rect);
            
            /* Thin rock layer line at ceiling */
            SDL_SetRenderDrawColor(game.renderer, r - 15, g - 10, b - 5, 255);
            SDL_RenderDrawLine(game.renderer, ug_x, sy_base,
                              ug_x + ug_w, sy_base);
        }
    }
    
    /* ====== PASS 2: Tenant sprites ======
     * Now render all tenants. Multi-floor items paint over the
     * backgrounds of upper floors without being overwritten. */
    for (int floor = bot_floor; floor <= top_floor; floor++) {
        int fidx = floor_to_index(floor);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
        
        int sx_base, sy_base;
        grid_to_screen(floor, 0, &sx_base, &sy_base);
        
        for (int x = 0; x < TOWER_WIDTH; ) {
            TowerCell *cell = &game.tower.grid[fidx][x];
            
            if (cell->type == ITEM_NONE) { x++; continue; }
            if (cell->cell_index != 0) { x++; continue; }
            
            Tenant *tenant = tower_tenant(&game.tower, cell->tenant_id);
            if (!tenant) { x++; continue; }
            
            /* Only render from the base floor of multi-floor items */
            if (tenant->floor != floor) { x++; continue; }
            
            int frame_w_hint = 0, item_floors = 1;
            uint16_t spr_id = item_sprite_id(tenant->type, &frame_w_hint, &item_floors);
            Sprite *spr = spr_id ? sprites_find(&game.sprites, spr_id) : NULL;
            
            int tx, ty;
            grid_to_screen(floor, tenant->x, &tx, &ty);
            int tw = tenant->width * CELL_W;
            
            int tenant_y = ty + CEIL_H;
            
            /* Calculate draw rect for this tenant */
            int draw_h = (item_floors > 1) ? item_floors * CELL_H : TENANT_H;
            int draw_y = (item_floors > 1) ? ty - (item_floors - 1) * CELL_H : tenant_y;
            
            if (tenant->type == ITEM_LOBBY && lobby_spr) {
                /* Lobby sprite 0x89E8 layout (992×36, verified by pixel inspection):
                 *   [0-255]   Interior (256px) — brown walls, booth seating, tiled
                 *   [256-327] Left entrance transition (72px) — plants → glass
                 *   [328-655] Entrance facade (328px) — teal glass, sky-blue bg
                 *   [656-911] Interior repeat (256px) — same as 0-255
                 *   [912-983] Right entrance transition (72px) — plants → glass
                 *   [984-991] Padding (8px)
                 *
                 * Rendering: [left_entrance] [interior tiled...] [right_entrance]
                 * Left entrance = pixels 256-655 (72 + 328 = 400px transition+facade)
                 * Right entrance = pixels 912-655 reversed? No — use endcap + flip.
                 *
                 * Simpler: use left_transition(72) at left edge,
                 *          tile interior(256) in the middle,
                 *          right_transition(72) at right edge. */
                int interior_src = 0;       /* Interior starts at pixel 0 */
                int interior_w = 256;       /* 256px repeating interior */
                int left_cap_src = 256;     /* Left transition at pixel 256 */
                int cap_w = 72;             /* Each transition is 72px */
                int right_cap_src = 912;    /* Right transition at pixel 912 */
                int lobby_pw = tenant->width * CELL_W;
                
                /* Left entrance transition */
                {
                    int ew = cap_w;
                    if (ew > lobby_pw) ew = lobby_pw;
                    SDL_Rect src_ec = { left_cap_src, 0, ew, lobby_spr->h };
                    SDL_Rect dst_ec = { tx, ty, ew, CELL_H };
                    SDL_RenderCopy(game.renderer, lobby_spr->texture, &src_ec, &dst_ec);
                }
                
                /* Repeating interior section */
                int mid_start = cap_w;
                int mid_end = lobby_pw - cap_w;
                for (int mx = mid_start; mx < mid_end; mx += interior_w) {
                    int mw = interior_w;
                    if (mx + mw > mid_end) mw = mid_end - mx;
                    int msx = tx + mx;
                    if (msx + mw < 0 || msx > game.screen_w) continue;
                    SDL_Rect src_mid = { interior_src, 0, mw, lobby_spr->h };
                    SDL_Rect dst_mid = { msx, ty, mw, CELL_H };
                    SDL_RenderCopy(game.renderer, lobby_spr->texture, &src_mid, &dst_mid);
                }
                
                /* Right entrance transition */
                if (lobby_pw > cap_w) {
                    int rx = tx + lobby_pw - cap_w;
                    SDL_Rect src_ec = { right_cap_src, 0, cap_w, lobby_spr->h };
                    SDL_Rect dst_ec = { rx, ty, cap_w, CELL_H };
                    SDL_RenderCopy(game.renderer, lobby_spr->texture, &src_ec, &dst_ec);
                }
                x = tenant->x + tenant->width;
                continue;
            } else if (item_is_elevator(tenant->type) && spr) {
                /* Shaft section: always tile 0 of 0x87E8. Tiles 1+ are the
                 * floor-digit variants (1..9, 100) — wiring the real
                 * per-floor digits comes with the shaft-label pass.
                 * The 36px tile spans the FULL floor height: the shaft
                 * overlays the ceiling strip/joists, as in the original. */
                SDL_Rect src = { 0, 0, 32, spr->h };
                SDL_Rect dst = { tx, ty, tw, CELL_H };
                SDL_RenderCopy(game.renderer, spr->texture, &src, &dst);
                /* floor number, on serviced stops only (as the original) */
                ElevatorShaft *es = find_people_shaft(tenant->x, tenant->type);
                int fi = floor_to_index(floor);
                if (es && fi >= es->lo && fi <= es->hi && es->serviced[fi] &&
                    elv_structural_stop(es, fi))
                    draw_shaft_digits(tx, ty, floor);
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
                int frame_idx;
                if (tenant->capacity <= CAP_EMPTY) {
                    frame_idx = 0;
                } else {
                    /* Proportional mapping: cap range [0x10..0x40] → [0..nframes-1] */
                    int cap_range = CAP_MAX - CAP_MIN;  /* 0x30 = 48 */
                    int cap_pos = tenant->capacity - CAP_MIN;
                    if (cap_pos < 0) cap_pos = 0;
                    if (cap_pos > cap_range) cap_pos = cap_range;
                    frame_idx = (cap_pos * (nframes - 1)) / cap_range;
                }
                if (frame_idx >= nframes) frame_idx = nframes - 1;
                if (frame_idx < 0) frame_idx = 0;
                
                /* Construction: show first frame with overlay */
                if (tenant->state == TENANT_CONSTRUCTION) frame_idx = 0;
                
                SDL_Rect src = { frame_idx * frame_w_hint, 0, frame_w_hint, spr->h };
                SDL_Rect dst = { tx, draw_y, tw, draw_h };
                SDL_RenderCopy(game.renderer, spr->texture, &src, &dst);
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
            
            /* Tenant state visual overlay (not for transports — a shaft
             * has no vacancy/stress state to tint) */
            if (!item_is_transport(tenant->type)) {
                SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
                
                if (tenant->state == TENANT_CONSTRUCTION) {
                    /* Under construction: yellow/amber overlay */
                    SDL_SetRenderDrawColor(game.renderer, 200, 160, 0, 80);
                    SDL_Rect overlay = { tx, draw_y, tw, draw_h };
                    SDL_RenderFillRect(game.renderer, &overlay);
                } else if (tenant->state == TENANT_STRESSED) {
                    /* Stressed: pulsing red (from MainteT stress cascade) */
                    int pulse = 40 + (game.sim.frame % 30) * 2;
                    if (pulse > 80) pulse = 120 - pulse;
                    SDL_SetRenderDrawColor(game.renderer, 255, 0, 0, pulse);
                    SDL_Rect overlay = { tx, draw_y, tw, draw_h };
                    SDL_RenderFillRect(game.renderer, &overlay);
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
            
            x += tenant->width;
        }
    }
    
    /* ====== PASS 3: Transport overlays (stairs, escalators) ====== */
    for (int i = 0; i < game.tower.tenant_count; i++) {
        Tenant *t = &game.tower.tenants[i];
        if (t->type != ITEM_STAIRS && t->type != ITEM_ESCALATOR) continue;
        
        int frame_w_hint = 0, item_floors = 1;
        uint16_t spr_id = item_sprite_id(t->type, &frame_w_hint, &item_floors);
        Sprite *spr = spr_id ? sprites_find(&game.sprites, spr_id) : NULL;
        
        int tx, ty;
        grid_to_screen(t->floor, t->x, &tx, &ty);
        int tw = t->width * CELL_W;
        
        if (spr && frame_w_hint > 0) {
            int nframes = spr->w / frame_w_hint;
            if (nframes < 1) nframes = 1;
            int frame_idx = t->state % nframes;
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
    
    /* ====== PASS 4: Decorative overlays (awning, crane, fire escape) ====== */
    
    /* Entrance awning — drawn over each lobby's edges */
    if (game.entrances) {
        for (int i = 0; i < game.tower.tenant_count; i++) {
            Tenant *t = &game.tower.tenants[i];
            if (t->type != ITEM_LOBBY) continue;
            
            int tx, ty;
            grid_to_screen(t->floor, t->x, &tx, &ty);
            int tw = t->width * CELL_W;
            
            /* Awning extends OUTWARD from lobby edges (overhanging the sidewalk).
             * Left half of sprite = left entrance, right half = right entrance.
             * Each half is entrances->w/2 pixels wide.
             * Left awning: right edge aligns with lobby's left edge.
             * Right awning: left edge aligns with lobby's right edge. */
            int half_w = game.entrances->w / 2;
            
            /* Left entrance — awning extends to the left of the lobby */
            SDL_Rect src_l = { 0, 0, half_w, game.entrances->h };
            SDL_Rect awning_l = { tx - half_w, ty, half_w, game.entrances->h };
            SDL_RenderCopy(game.renderer, game.entrances->texture, &src_l, &awning_l);
            
            /* Right entrance — awning extends to the right of the lobby */
            SDL_Rect src_r = { half_w, 0, half_w, game.entrances->h };
            SDL_Rect awning_r = { tx + tw, ty, half_w, game.entrances->h };
            SDL_RenderCopy(game.renderer, game.entrances->texture, &src_r, &awning_r);
        }
    }
    
    /* Construction crane — drawn at the TOP of the tower only (from Decorations.cpp).
     * The crane sits above the highest occupied floor, not on every construction site. */
    if (game.crane) {
        int max_floor = 0;
        int max_floor_minx = TOWER_WIDTH, max_floor_maxx = 0;
        for (int f = TOWER_MAX_FLOOR; f >= 1; f--) {
            int fidx = floor_to_index(f);
            if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
            int found = 0;
            for (int x = 0; x < TOWER_WIDTH; x++) {
                if (game.tower.grid[fidx][x].type != ITEM_NONE) {
                    if (!found) { max_floor = f; found = 1; }
                    if (x < max_floor_minx) max_floor_minx = x;
                    if (x > max_floor_maxx) max_floor_maxx = x;
                }
            }
            if (found) break;
        }
        if (max_floor > 0) {
            int cx = (max_floor_minx + max_floor_maxx + 1) / 2;
            int tx, ty;
            grid_to_screen(max_floor, cx, &tx, &ty);
            SDL_Rect crane_dst = {
                tx - game.crane->w / 2,
                ty - game.crane->h - 2,
                game.crane->w, game.crane->h
            };
            SDL_RenderCopy(game.renderer, game.crane->texture, NULL, &crane_dst);
        }
    }
    
    /* Fire escape — drawn on BOTH sides of each floor's extent.
     * Each floor gets its own left/right edges (not global building envelope). */
    if (game.fireladder) {
        int half_w = game.fireladder->w / 2;
        
        for (int floor = 1; floor <= TOWER_MAX_FLOOR; floor++) {
            int fidx = floor_to_index(floor);
            if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
            
            /* Find this floor's own left/right extent */
            int floor_left = TOWER_WIDTH, floor_right = -1;
            for (int cx = 0; cx < TOWER_WIDTH; cx++) {
                if (game.tower.grid[fidx][cx].type != ITEM_NONE) {
                    if (cx < floor_left) floor_left = cx;
                    if (cx > floor_right) floor_right = cx;
                }
            }
            if (floor_left > floor_right) continue;  /* Empty floor */
            
            int fsx, fsy;
            grid_to_screen(floor, 0, &fsx, &fsy);
            
            /* Left fire escape at THIS floor's left edge */
            int lx, ly;
            grid_to_screen(floor, floor_left, &lx, &ly);
            SDL_Rect fe_left = { lx - half_w, fsy, half_w, CELL_H };
            SDL_Rect src_left = { 0, 0, half_w, game.fireladder->h };
            SDL_RenderCopy(game.renderer, game.fireladder->texture, &src_left, &fe_left);
            
            /* Right fire escape at THIS floor's right edge */
            int rx_pos, ry;
            grid_to_screen(floor, floor_right + 1, &rx_pos, &ry);
            SDL_Rect fe_right = { rx_pos, fsy, half_w, CELL_H };
            SDL_Rect src_right = { half_w, 0, half_w, game.fireladder->h };
            SDL_RenderCopy(game.renderer, game.fireladder->texture, &src_right, &fe_right);
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

static void render_stats_window(void)
{
    if (!game.show_stats) return;
    const int W = 470, H = 330;
    int wx = game.screen_w - W - 12, wy = 64;

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
                    "Collecting data... (1 sample per quarter)",
                    (SDL_Color){ 60, 60, 60, 255 });
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
            if (v1 < lo) lo = v1; if (v1 > hi) hi = v1;
            if (v2 < lo) lo = v2; if (v2 > hi) hi = v2;
        }
        if (hi == lo) hi = lo + 1;
        stats_plot(gx + 1, gy + 1, gw - 2, gh - 2, graphs[g].a, n, graphs[g].ca, lo, hi);
        stats_plot(gx + 1, gy + 1, gw - 2, gh - 2, graphs[g].b, n, graphs[g].cb, lo, hi);
        gy += gh + 8;
    }
    char foot[64];
    snprintf(foot, sizeof(foot), "%d quarters of history (F3 to close)", h->count);
    stats_label(gx, gy, foot, (SDL_Color){ 80, 80, 80, 255 });
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
    { "Car capacity: standard",   &TUNING.capacity_standard,    1, 1, 42 },
    { "Car capacity: exp/svc",    &TUNING.capacity_service,     1, 1, 42 },
    { "Judge: moderate wait",     &TUNING.judge_moderate,       5, 0, 2000 },
    { "Judge: stressed wait",     &TUNING.judge_stressed,       5, 0, 2000 },
    { "Star 2 population",        &TUNING.star_pop[0],         50, 0, 100000 },
    { "Star 3 population",        &TUNING.star_pop[1],        100, 0, 100000 },
    { "Star 4 population",        &TUNING.star_pop[2],        500, 0, 100000 },
    { "Star 5 population",        &TUNING.star_pop[3],        500, 0, 100000 },
};
#define TUNE_ROWS ((int)(sizeof(tune_rows) / sizeof(tune_rows[0])))
#define TUNE_W      330
#define TUNE_ROW_H  19

static void tune_window_origin(int *x, int *y)
{
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
 * Double-click a shaft: grid of floors x 8 cars at the EXE's 13px cell
 * size, with per-floor stop toggles, live car positions, call markers,
 * and the car count. The original capped stop records at 30 per group;
 * we clamp the visible rows the same way. */
#define ELV_CELL      13
#define ELV_LABEL_W   34
#define ELV_W         (8 + ELV_LABEL_W + ELV_CELL + 6 + 8 * ELV_CELL + 6 + ELV_CELL + 8)
#define ELV_MAX_ROWS  30
/* Cost of an extra car. NOT yet decomp-verified (OpenSkyscraper folklore
 * says $80k; the EXE's number is a dig target — see backlog). */
#define ELV_CAR_COST  80000

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

static void elv_dialog_metrics(const ElevatorShaft *s, int *rows, int *body_h)
{
    int r = s->hi - s->lo + 1;
    if (r > ELV_MAX_ROWS) r = ELV_MAX_ROWS;
    *rows = r;
    *body_h = 22 + 16 + r * ELV_CELL + 30 + 28;
}

static void render_elv_dialog(void)
{
    int si = elv_dialog_shaft();
    if (si < 0) { game.elv_open = 0; return; }
    PeopleSim *ps = &game.sim.people;
    ElevatorShaft *s = &ps->shafts[si];

    int rows, body_h;
    elv_dialog_metrics(s, &rows, &body_h);
    int wx = game.elv_x, wy = game.elv_y;

    const char *title =
        s->type == ITEM_ELEVATOR_EXPRESS ? "Express Elevator"
      : s->type == ITEM_ELEVATOR_SERVICE ? "Service Elevator"
                                         : "Standard Elevator";
    draw_win31_titlebar(wx, wy, ELV_W, title);
    SDL_Rect body = { wx, wy + WIN_TITLEBAR_H, ELV_W, body_h };
    SDL_SetRenderDrawColor(game.renderer, 192, 192, 192, 255);
    SDL_RenderFillRect(game.renderer, &body);
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(game.renderer, &body);

    char hdr[64];
    snprintf(hdr, sizeof(hdr), "Floors %d to %d   stop / cars 1-8",
             index_to_floor(s->lo), index_to_floor(s->hi));
    stats_label(wx + 8, wy + WIN_TITLEBAR_H + 4, hdr,
                (SDL_Color){ 70, 70, 70, 255 });

    int gx = wx + 8 + ELV_LABEL_W;             /* serviced column */
    int cx = gx + ELV_CELL + 6;                /* first car column */
    int top = wy + WIN_TITLEBAR_H + 22 + 14;

    for (int r = 0; r < rows; r++) {
        int f = s->hi - r;
        int ry = top + r * ELV_CELL;
        char fl[8];
        int wf = index_to_floor(f);
        snprintf(fl, sizeof(fl), wf < 0 ? "B%d" : "%d", wf < 0 ? -wf : wf);
        stats_label(wx + 8, ry, fl, (SDL_Color){ 0, 0, 0, 255 });

        /* serviced toggle cell */
        SDL_Rect sc = { gx, ry, ELV_CELL - 1, ELV_CELL - 1 };
        if (!elv_structural_stop(s, f)) {
            SDL_SetRenderDrawColor(game.renderer, 150, 150, 150, 255);
            SDL_RenderFillRect(game.renderer, &sc);
        } else if (s->serviced[f]) {
            SDL_SetRenderDrawColor(game.renderer, 0, 150, 0, 255);
            SDL_RenderFillRect(game.renderer, &sc);
        } else {
            SDL_SetRenderDrawColor(game.renderer, 120, 40, 40, 255);
            SDL_RenderFillRect(game.renderer, &sc);
        }
        SDL_SetRenderDrawColor(game.renderer, 60, 60, 60, 255);
        SDL_RenderDrawRect(game.renderer, &sc);

        /* car grid cells */
        for (int ci = 0; ci < CARS_PER_SHAFT; ci++) {
            SDL_Rect cc = { cx + ci * ELV_CELL, ry, ELV_CELL - 1, ELV_CELL - 1 };
            int active = ci < s->num_cars;
            SDL_SetRenderDrawColor(game.renderer,
                active ? 255 : 172, active ? 255 : 172, active ? 255 : 172, 255);
            SDL_RenderFillRect(game.renderer, &cc);
            SDL_SetRenderDrawColor(game.renderer, 120, 120, 120, 255);
            SDL_RenderDrawRect(game.renderer, &cc);
            if (active && s->car[ci].active && s->car[ci].floor == f) {
                ElevatorCar *c = &s->car[ci];
                if (c->passengers >= s->capacity)
                    SDL_SetRenderDrawColor(game.renderer, 200, 30, 30, 255);
                else if (c->passengers > 0)
                    SDL_SetRenderDrawColor(game.renderer, 40, 80, 220, 255);
                else
                    SDL_SetRenderDrawColor(game.renderer, 90, 90, 90, 255);
                SDL_Rect car = { cc.x + 2, cc.y + 2, cc.w - 4, cc.h - 4 };
                SDL_RenderFillRect(game.renderer, &car);
            }
        }

        /* call markers: ▲ owned up call, ▼ owned down call */
        int mx2 = cx + CARS_PER_SHAFT * ELV_CELL + 6;
        if (s->up_call_car[f]) {
            SDL_SetRenderDrawColor(game.renderer, 0, 100, 0, 255);
            for (int i = 0; i < 5; i++)
                SDL_RenderDrawLine(game.renderer, mx2 + 5 - i, ry + 1 + i,
                                   mx2 + 5 + i, ry + 1 + i);
        }
        if (s->down_call_car[f]) {
            SDL_SetRenderDrawColor(game.renderer, 150, 60, 0, 255);
            for (int i = 0; i < 5; i++)
                SDL_RenderDrawLine(game.renderer, mx2 + 5 - i, ry + 11 - i,
                                   mx2 + 5 + i, ry + 11 - i);
        }
    }

    /* car count strip */
    int by = top + rows * ELV_CELL + 4;
    char cars[48];
    snprintf(cars, sizeof(cars), "Cars: %d   (add $%dK)",
             s->num_cars, ELV_CAR_COST / 1000);
    stats_label(wx + 8, by + 3, cars, (SDL_Color){ 0, 0, 0, 255 });
    SDL_Rect minus = { wx + ELV_W - 52, by, 18, 18 };
    SDL_Rect plus  = { wx + ELV_W - 28, by, 18, 18 };
    SDL_SetRenderDrawColor(game.renderer, 168, 168, 168, 255);
    SDL_RenderFillRect(game.renderer, &minus);
    SDL_RenderFillRect(game.renderer, &plus);
    SDL_SetRenderDrawColor(game.renderer, 60, 60, 60, 255);
    SDL_RenderDrawRect(game.renderer, &minus);
    SDL_RenderDrawRect(game.renderer, &plus);
    SDL_RenderDrawLine(game.renderer, minus.x + 5, minus.y + 9,
                       minus.x + 13, minus.y + 9);
    SDL_RenderDrawLine(game.renderer, plus.x + 5, plus.y + 9,
                       plus.x + 13, plus.y + 9);
    SDL_RenderDrawLine(game.renderer, plus.x + 9, plus.y + 5,
                       plus.x + 9, plus.y + 13);

    /* close strip */
    int cy = by + 26;
    SDL_Rect close = { wx + 8, cy, 64, 20 };
    SDL_SetRenderDrawColor(game.renderer, 168, 168, 168, 255);
    SDL_RenderFillRect(game.renderer, &close);
    SDL_SetRenderDrawColor(game.renderer, 60, 60, 60, 255);
    SDL_RenderDrawRect(game.renderer, &close);
    stats_label(close.x + 14, close.y + 2, "Close", (SDL_Color){ 0, 0, 0, 255 });
}

/* Returns 1 if the click was consumed by the dialog */
static int elv_dialog_click(int mx, int my)
{
    int si = elv_dialog_shaft();
    if (si < 0) return 0;
    PeopleSim *ps = &game.sim.people;
    ElevatorShaft *s = &ps->shafts[si];

    int rows, body_h;
    elv_dialog_metrics(s, &rows, &body_h);
    int wx = game.elv_x, wy = game.elv_y;
    if (mx < wx || mx >= wx + ELV_W ||
        my < wy || my >= wy + WIN_TITLEBAR_H + body_h) return 0;

    int gx = wx + 8 + ELV_LABEL_W;
    int top = wy + WIN_TITLEBAR_H + 22 + 14;

    /* serviced toggles */
    if (mx >= gx && mx < gx + ELV_CELL && my >= top &&
        my < top + rows * ELV_CELL) {
        int r = (my - top) / ELV_CELL;
        int f = s->hi - r;
        if (elv_structural_stop(s, f))
            people_set_serviced(ps, si, f, !s->serviced[f]);
        return 1;
    }

    /* car count */
    int by = top + rows * ELV_CELL + 4;
    if (my >= by && my < by + 18) {
        if (mx >= wx + ELV_W - 52 && mx < wx + ELV_W - 34) {
            people_set_num_cars(ps, si, s->num_cars - 1);
        } else if (mx >= wx + ELV_W - 28 && mx < wx + ELV_W - 10 &&
                   s->num_cars < CARS_PER_SHAFT) {
            if (game.tower.money >= ELV_CAR_COST) {
                game.tower.money -= ELV_CAR_COST;
                game.tower.built_value += ELV_CAR_COST;
                people_set_num_cars(ps, si, s->num_cars + 1);
            }
        }
        return 1;
    }

    /* close button */
    int cy = by + 26;
    if (my >= cy && my < cy + 20 && mx >= wx + 8 && mx < wx + 72) {
        game.elv_open = 0;
        return 1;
    }
    return 1;   /* clicks inside the window never fall through */
}

/* ---------- People layer: elevator cars + waiting queues ----------
 * Real art: car sheets 0x8428/29/2A/2B (5 fullness frames, the 5th is the
 * red F "full" diamond), queue silhouettes 0x8468, engines at the sheet
 * tails. Frame choice = GetCarSprite (1090:221f). */
static void render_people(void)
{
    PeopleSim *ps = &game.sim.people;
    Sprite *queue_spr = sprites_find(&game.sprites, SPR_ELEV_QUEUE);

    for (int i = 0; i < ps->shaft_count; i++) {
        ElevatorShaft *s = &ps->shafts[i];
        if (!s->active) continue;
        int shaft_w = ITEM_WIDTH[s->type] * CELL_W;

        /* Machinery caps: the sheet tail is TWO one-shaft-wide tiles —
         * tile A (up arrow + winch) is the motor room ABOVE the shaft,
         * tile B (buffer springs + down arrow) is the pit BELOW it.
         * (OpenSkyscraper splits them the same way: topMotor/bottomMotor;
         * the arrows are the original's extend-the-shaft handles.) */
        {
            Sprite *eng = sprites_find(&game.sprites,
                s->type == ITEM_ELEVATOR_EXPRESS ? SPR_ELEV_EXPRESS
                                                 : SPR_ELEV_STD_LOADED);
            if (eng) {
                int tile_w  = (s->type == ITEM_ELEVATOR_EXPRESS) ? 48 : 32;
                int tail    = (s->type == ITEM_ELEVATOR_EXPRESS) ? 5 : 4;
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
         * Figures picked per person id so the crowd stays varied but stable. */
        for (int f = s->lo; f <= s->hi && queue_spr; f++) {
            const ElevatorStop *st = &s->stop[f];
            int n = st->up_count + st->down_count;
            if (!n) continue;
            int sx, sy;
            grid_to_screen(index_to_floor(f), s->x, &sx, &sy);
            int shown = n > 10 ? 10 : n;
            for (int k = 0; k < shown; k++) {
                uint16_t pid;
                if (k < st->up_count)
                    pid = st->up_ring[(st->up_head + k) % QUEUE_CAP];
                else
                    pid = st->down_ring[(st->down_head + k - st->up_count)
                                        % QUEUE_CAP];
                int fig = (pid * 7) % 40;     /* 40 silhouettes of 16px */
                SDL_Rect src = { fig * 16, 0, 16, 36 };
                SDL_Rect dst = { sx - 16 - k * 9, sy, 16, CELL_H };
                SDL_RenderCopy(game.renderer, queue_spr->texture, &src, &dst);
            }
        }

        /* Cars, with smooth travel between floors */
        for (int ci = 0; ci < s->num_cars; ci++) {
            ElevatorCar *c = &s->car[ci];
            if (!c->active) continue;
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
                spr = sprites_find(&game.sprites, SPR_ELEV_EXPRESS);
                src = (SDL_Rect){ frame * 48, 0, 48, 36 };
            } else if (s->type == ITEM_ELEVATOR_SERVICE) {
                spr = sprites_find(&game.sprites, SPR_ELEV_SERVICE);
                src = (SDL_Rect){ frame * 32, 0, 32, 36 };
            } else if (frame == 0) {
                spr = sprites_find(&game.sprites, SPR_ELEV_CAR);
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

static void render_build_ghost(void)
{
    if (game.demolish_mode || game.build_type == ITEM_NONE) return;

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
        int floor = game.drag_start_floor;
        
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
        /* Single-unit ghost at mouse position */
        int gx, gy;
        grid_to_screen(game.mouse_floor, game.mouse_cell, &gx, &gy);
        
        int can = tower_can_place(&game.tower, game.build_type, 
                                   game.mouse_floor, game.mouse_cell);
        
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

#define TOOL_WIN_W  128       /* Faithful toolbox width (toolbox.rml: 128px = 4×32 icons) */
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
#define EVENT_MSG_LEN   48
static char event_messages[EVENT_MSG_COUNT][EVENT_MSG_LEN];
static int  event_msg_head = 0;
static int  event_msg_total = 0;

static void add_event_message(const char *msg)
{
    strncpy(event_messages[event_msg_head], msg, EVENT_MSG_LEN - 1);
    event_messages[event_msg_head][EVENT_MSG_LEN - 1] = '\0';
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
    if (game.ui_star[0] && game.ui_star[1]) {
        int shown = game.tower.star_rating < 5 ? game.tower.star_rating + 1 : 5;
        for (int i = 0; i < shown; i++) {
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
        static const char *wd_names[] = { "1st WD", "2nd WD", "3rd WD", "WE" };
        const char *wd = (game.sim.quarter >= 0 && game.sim.quarter < 4)
                         ? wd_names[game.sim.quarter] : "?";
        int q = (game.tower.day - 1) % 4 + 1;
        int year = (game.tower.day - 1) / 4 + 1;
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
    draw_win31_titlebar(wx, wy, MAP_WIN_W, "Map");
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
    
    /* Floor positioning for minimap overlay */
    int total_floors = TOWER_MAX_FLOOR - TOWER_MIN_FLOOR + 1;  /* 110 */
    int ground_offset = -TOWER_MIN_FLOOR;  /* 9 (floors below ground) */
    
    /* Earth below ground (on top of map bg) */
    int ground_map_y = map_y + map_h - (ground_offset * map_h / total_floors);
    if (!game.ui_map) {
        SDL_SetRenderDrawColor(game.renderer, 140, 120, 90, 255);
        SDL_Rect earth = { map_x, ground_map_y, map_w, map_y + map_h - ground_map_y };
        SDL_RenderFillRect(game.renderer, &earth);
    }
    
    /* Draw each tenant as a colored pixel/line */
    for (int i = 0; i < game.tower.tenant_count; i++) {
        Tenant *t = &game.tower.tenants[i];
        if (t->type == ITEM_NONE || t->type == ITEM_FLOOR) continue;
        
        /* Map tenant position to minimap coordinates */
        int floor_from_bottom = t->floor - TOWER_MIN_FLOOR;
        int ty = map_y + map_h - (floor_from_bottom * map_h / total_floors) - 1;
        int tx = map_x + (t->x * map_w / TOWER_WIDTH);
        int tw = (t->width * map_w / TOWER_WIDTH);
        if (tw < 1) tw = 1;
        
        /* Color by type */
        uint8_t r, g, b;
        item_fallback_color(t->type, &r, &g, &b);
        
        /* Darken stressed/abandoned */
        if (t->state == TENANT_STRESSED) { r = 255; g = 50; b = 50; }
        else if (t->state == TENANT_ABANDONED) { r = 100; g = 30; b = 30; }
        else if (t->state == TENANT_CONSTRUCTION) { r = 200; g = 180; b = 0; }
        
        SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
        SDL_Rect dot = { tx, ty, tw, 1 };
        SDL_RenderFillRect(game.renderer, &dot);
    }
    
    /* Camera viewport indicator */
    {
        int cam_floor_top, cam_floor_bot, dummy;
        screen_to_grid(0, HUD_HEIGHT + MENU_BAR_H, &cam_floor_top, &dummy);
        screen_to_grid(0, game.screen_h, &cam_floor_bot, &dummy);
        
        int vt = map_y + map_h - ((cam_floor_top - TOWER_MIN_FLOOR) * map_h / total_floors);
        int vb = map_y + map_h - ((cam_floor_bot - TOWER_MIN_FLOOR) * map_h / total_floors);
        if (vt < map_y) vt = map_y;
        if (vb > map_y + map_h) vb = map_y + map_h;
        
        /* Double-thick white rectangle with red inner for visibility */
        SDL_SetRenderDrawColor(game.renderer, 255, 0, 0, 255);
        SDL_Rect vp = { map_x + 1, vt, map_w - 2, vb - vt };
        SDL_RenderDrawRect(game.renderer, &vp);
        SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 255);
        SDL_Rect vp2 = { map_x + 2, vt + 1, map_w - 4, vb - vt - 2 };
        SDL_RenderDrawRect(game.renderer, &vp2);
    }
}

/* --- Toolbox Window --- */
/* The build tool selector with icons for each building type.
 * Stacked below the minimap on the LEFT (like original toolbox.rml). */

#define TOOL_BTN_SIZE 32    /* Match original 32×32 icon size */
#define TOOL_BTN_PAD  0     /* Original buttons abut edge-to-edge (bevel is in the art) */
#define TOOL_COLS   4       /* 4×32 = the faithful 128px window width */

/* Tool button layout.
 * icon_idx = position in the items bitmap (from OpenSkyscraper Item/*.h).
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
    TB("PKG",  ITEM_PARKING,       17,  160, 160, 160),
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

static void tool_button_rect(int i, int *bx, int *by)
{
    int col = i % TOOL_COLS, row = i / TOOL_COLS;
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

/* Small triangle in the lower-right corner marking a group (pull-down) button. */
static void draw_pulldown_marker(int bx, int by)
{
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
    for (int k = 0; k < 5; k++) {
        SDL_RenderDrawLine(game.renderer,
                           bx + TOOL_BTN_SIZE - 2 - k, by + TOOL_BTN_SIZE - 3,
                           bx + TOOL_BTN_SIZE - 2,     by + TOOL_BTN_SIZE - 3);
        SDL_RenderDrawLine(game.renderer,
                           bx + TOOL_BTN_SIZE - 2 - k, by + TOOL_BTN_SIZE - 3 - k,
                           bx + TOOL_BTN_SIZE - 2 - k, by + TOOL_BTN_SIZE - 3);
    }
}

/* Render the open pull-down menu (if any) on top of the toolbox. */
static void render_tool_popup(void)
{
    int i = game.tool_popup;
    if (i < 0 || i >= TOOL_BTN_COUNT) return;
    const ToolButton *g = &tool_buttons[i];
    if (g->sub_count <= 0) return;

    /* Sub-items use the same button-face grey as the toolbar (they're the same
     * button bitmaps in the original) — a pull-down menu is distinguished by a
     * drop-shadow, not a different fill colour. Draw a soft shadow behind the
     * whole column first so it reads as a floating menu. */
    int x0, y0, xn, yn;
    tool_sub_rect(i, 0, &x0, &y0);
    tool_sub_rect(i, g->sub_count - 1, &xn, &yn);
    SDL_SetRenderDrawColor(game.renderer, 64, 64, 64, 255);
    SDL_Rect shadow = { x0 + 2, y0 + 2, TOOL_BTN_SIZE + 2, (yn - y0) + TOOL_BTN_SIZE + 2 };
    SDL_RenderFillRect(game.renderer, &shadow);

    for (int j = 0; j < g->sub_count; j++) {
        int bx, by;
        tool_sub_rect(i, j, &bx, &by);
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
    draw_win31_rect(wx, wy, TOOL_WIN_W, TOOL_WIN_H - WIN_TITLEBAR_H, 1);
    
    /* Speed buttons at top. The original toolbar has just TWO buttons — Play and
     * Pause — each with a normal + pressed state (verified by dumping the EXE):
     *   0x8258 play-normal, 0x8259 play-pressed, 0x825A pause-normal, 0x825B
     *   pause-pressed. The loader packs these into ui_speed (128×64): play at
     *   src x=0, pause at x=64; normal row y=0, pressed row y=32. Play shows
     *   pressed while the sim runs (speed>0); Pause shows pressed while stopped.
     *   (Multi-speed 1x/2x/3x is via menu/keyboard, not the toolbar.) */
    int speed_y = wy + 8;
    {
        int bw = 40, bh = 24, gap = 4;
        int sx = wx + (TOOL_WIN_W - (2 * bw + gap)) / 2;
        int playing = (game.sim.speed > 0);
        if (game.ui_speed) {
            SDL_Rect psrc = { 0,  playing ? 32 : 0, 64, 32 };  /* Play */
            SDL_Rect pdst = { sx, speed_y, bw, bh };
            SDL_RenderCopy(game.renderer, game.ui_speed, &psrc, &pdst);
            SDL_Rect qsrc = { 64, playing ? 0 : 32, 64, 32 };  /* Pause */
            SDL_Rect qdst = { sx + bw + gap, speed_y, bw, bh };
            SDL_RenderCopy(game.renderer, game.ui_speed, &qsrc, &qdst);
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
            /* Bulldozer (t==0) draws pressed (row y=21) while demolish mode is on. */
            int row_y = (t == 0 && game.demolish_mode) ? 21 : 0;
            SDL_Rect src = { t * 21, row_y, 21, 21 };
            SDL_Rect dst = { tx + t * 23, tools_y, 21, 21 };
            SDL_RenderCopy(game.renderer, game.ui_tools, &src, &dst);
        }
    }
    
    /* Item buttons grid */
    int grid_y = tools_y + 26;
    for (int i = 0; i < TOOL_BTN_COUNT; i++) {
        int bx, by;
        tool_button_rect(i, &bx, &by);
        const ToolButton *tb = &tool_buttons[i];

        /* A group button stays highlighted when any of its sub-items is active. */
        int selected = (tb->type == game.build_type);
        for (int j = 0; j < tb->sub_count; j++)
            if (tb->sub[j] == game.build_type) selected = 1;

        if (game.ui_items && tb->icon_idx >= 0) {
            draw_tool_icon(bx, by, tb->icon_idx, selected);
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

        if (tb->sub_count > 0) draw_pulldown_marker(bx, by);
    }
    
    /* Cost display at bottom */
    if (game.build_type != ITEM_NONE && game.font_small) {
        SDL_Color black = {0, 0, 0, 255};
        char cost_buf[64];
        format_money(ITEM_COST[game.build_type], cost_buf, sizeof(cost_buf));
        char full_buf[96];
        snprintf(full_buf, sizeof(full_buf), "%s  %s",
                 tower_item_name(game.build_type), cost_buf);
        
        /* Place below the icon grid, not at a fixed window offset (the grid grew). */
        int rows = (TOOL_BTN_COUNT + TOOL_COLS - 1) / TOOL_COLS;
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
static int draw_menu_text(const char *text, int x, int y, int selected)
{
    if (!game.font_small || !text) return 0;
    SDL_Color color;
    if (selected) { color = (SDL_Color){WIN31_SEL_TEXT, 255}; }
    else          { color = (SDL_Color){WIN31_TEXT, 255}; }
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
        
        draw_menu_text(label_buf, drop_x + 22, iy + 2, is_hover);
        if (shortcut_buf[0]) {
            /* Right-align shortcut */
            int sw = (int)strlen(shortcut_buf) * 7;
            draw_menu_text(shortcut_buf, drop_x + drop_w - sw - 8, iy + 2, is_hover);
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
             "ConcilliaTower | $%ld | %d★ | Pop: %d | Day %d | Build: %s",
             game.tower.money, game.tower.star_rating, game.tower.population,
             game.tower.day, tower_item_name(game.build_type));
    SDL_SetWindowTitle(game.window, title);
    
    /* Sub-windows (matching original SimTower layout) */
    render_minimap();      /* Top left */
    render_toolbox();      /* Left, below map */
    render_info_window();  /* Top right, horizontal strip */
}

static void render(void)
{
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
    SDL_RenderClear(game.renderer);
    
    render_sky();
    render_tower();
    render_people();
    render_build_ghost();
    render_ui();
    render_stats_window();
    render_tuning_window();
    render_elv_dialog();
    
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

static void drag_place_units(void)
{
    if (game.build_type == ITEM_NONE) return;

    int width = ITEM_WIDTH[game.build_type];
    int floor = game.drag_start_floor;
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
            printf("Drag-extended %s at x=%d by %d floor(s)\n",
                   tower_item_name(game.build_type), x, placed);
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
        printf("Drag-placed %d %s(s) on floor %d\n",
               placed, tower_item_name(game.build_type), floor);
    }
}

/* ---------- Toolbox click helper ---------- */
static int toolbox_click(int mx, int my)
{
    int wx = game.tool_x;
    int wy = game.tool_y + WIN_TITLEBAR_H;  /* Skip title bar */
    
    /* Speed buttons — two only (Play / Pause), must match render_toolbox layout */
    int speed_y = wy + 8;
    {
        int bw = 40, bh = 24, gap = 4;
        int sx = wx + (TOOL_WIN_W - (2 * bw + gap)) / 2;
        if (my >= speed_y && my < speed_y + bh) {
            if (mx >= sx && mx < sx + bw) {            /* Play */
                if (game.sim.speed == 0) game.sim.speed = 1;
                return 1;
            }
            if (mx >= sx + bw + gap && mx < sx + 2 * bw + gap) {  /* Pause */
                game.sim.speed = 0;
                return 1;
            }
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
                        if (game.demolish_mode) game.tool_popup = -1;
                        printf("Bulldozer %s\n", game.demolish_mode ? "ON" : "off");
                    } else {                      /* Finger / inspector: not yet wired */
                        game.demolish_mode = 0;
                    }
                    return 1;
                }
            }
        }
    }

    /* If a pull-down is open, a click on one of its sub-items selects that item. */
    if (game.tool_popup >= 0 && game.tool_popup < TOOL_BTN_COUNT) {
        const ToolButton *g = &tool_buttons[game.tool_popup];
        for (int j = 0; j < g->sub_count; j++) {
            int bx, by;
            tool_sub_rect(game.tool_popup, j, &bx, &by);
            if (mx >= bx && mx < bx + TOOL_BTN_SIZE && my >= by && my < by + TOOL_BTN_SIZE) {
                game.build_type = g->sub[j];
                game.tool_popup = -1;
                game.demolish_mode = 0;
                printf("Build: %s\n", tower_item_name(game.build_type));
                return 1;
            }
        }
    }

    /* Item buttons grid (groups toggle their pull-down; singles select directly). */
    for (int i = 0; i < TOOL_BTN_COUNT; i++) {
        int bx, by;
        tool_button_rect(i, &bx, &by);
        if (mx >= bx && mx < bx + TOOL_BTN_SIZE && my >= by && my < by + TOOL_BTN_SIZE) {
            const ToolButton *tb = &tool_buttons[i];
            if (tb->sub_count > 0) {
                game.tool_popup = (game.tool_popup == i) ? -1 : i;  /* toggle */
            } else {
                game.tool_popup = -1;
            }
            game.build_type = tb->type;   /* primary (= sub[0] for a group) */
            game.demolish_mode = 0;
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
    if (mx >= game.info_x && mx < game.info_x + INFO_BAR_W &&
        my >= game.info_y && my < game.info_y + WIN_TITLEBAR_H) {
        return 1;
    }
    
    /* Minimap title bar */
    if (mx >= game.map_x && mx < game.map_x + MAP_WIN_W &&
        my >= game.map_y && my < game.map_y + WIN_TITLEBAR_H) {
        return 2;
    }
    
    /* Toolbox title bar */
    if (mx >= game.tool_x && mx < game.tool_x + TOOL_WIN_W &&
        my >= game.tool_y && my < game.tool_y + WIN_TITLEBAR_H) {
        return 3;
    }

    /* Elevator dialog title bar */
    if (game.elv_open &&
        mx >= game.elv_x && mx < game.elv_x + ELV_W &&
        my >= game.elv_y && my < game.elv_y + WIN_TITLEBAR_H) {
        return 4;
    }

    return 0;
}

/* Check if a point is inside any UI window (body or title bar).
 * Used to prevent clicks from falling through to the game world. */
static int point_in_any_window(int mx, int my)
{
    /* Info bar */
    if (mx >= game.info_x && mx < game.info_x + INFO_BAR_W &&
        my >= game.info_y && my < game.info_y + INFO_BAR_H + WIN_TITLEBAR_H) {
        return 1;
    }
    /* Minimap */
    if (mx >= game.map_x && mx < game.map_x + MAP_WIN_W &&
        my >= game.map_y && my < game.map_y + MAP_WIN_H) {
        return 1;
    }
    /* Toolbox */
    if (mx >= game.tool_x && mx < game.tool_x + TOOL_WIN_W &&
        my >= game.tool_y && my < game.tool_y + TOOL_WIN_H) {
        return 1;
    }
    /* Elevator dialog */
    if (game.elv_open) {
        int si = elv_dialog_shaft();
        if (si >= 0) {
            int rows, body_h;
            elv_dialog_metrics(&game.sim.people.shafts[si], &rows, &body_h);
            if (mx >= game.elv_x && mx < game.elv_x + ELV_W &&
                my >= game.elv_y &&
                my < game.elv_y + WIN_TITLEBAR_H + body_h) {
                return 1;
            }
        }
    }
    return 0;
}

/* ---------- Minimap click helper ---------- */
static int minimap_click(int mx, int my)
{
    int wx = game.map_x;
    int wy = game.map_y + WIN_TITLEBAR_H;  /* Skip title bar */
    int map_x = wx + 4;
    int map_y = wy + 4;
    int map_w = MAP_WIN_W - 8;
    int map_h = MAP_WIN_H - WIN_TITLEBAR_H - 24;
    
    if (mx >= map_x && mx < map_x + map_w &&
        my >= map_y && my < map_y + map_h) {
        /* Click in minimap: jump camera to that floor */
        int total_floors = TOWER_MAX_FLOOR - TOWER_MIN_FLOOR + 1;
        int clicked_floor = TOWER_MIN_FLOOR + 
            ((map_y + map_h - my) * total_floors / map_h);
        game.cam_fy = -clicked_floor * CELL_H;
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

static void execute_menu_item(const MenuItem *item)
{
    if (item->build_type != ITEM_NONE) {
        game.build_type = item->build_type;
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
    case ACT_SANTA:
        if (!game.sim.santa.active) game_launch_santa(&game.sim, game.screen_w);
        break;
    default: break;
    }
    game.menu_open = -1;
}

/* ---------- Input handling ---------- */
static void handle_event(SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_QUIT:
        game.running = 0;
        break;
        
    case SDL_KEYDOWN:
        switch (ev->key.keysym.sym) {
        case SDLK_ESCAPE:
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
            if (game.sim.speed < SPEED_TURBO) {
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
        case SDLK_LEFT:  game.cam_fx -= 40; break;
        case SDLK_RIGHT: game.cam_fx += 40; break;
        case SDLK_UP:    game.cam_fy -= 40; break;
        case SDLK_DOWN:  game.cam_fy += 40; break;
        
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
        
        /* Camera panning (middle/right-click drag) */
        if (game.cam_panning) {
            int dx = ev->motion.x - game.cam_pan_last_x;
            int dy = ev->motion.y - game.cam_pan_last_y;
            game.cam_fx -= dx;
            game.cam_fy -= dy;
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
                    }
                    break;
                }
            }
            
            /* Elevator dialog click (body, not title bar) */
            if (elv_dialog_click(ev->button.x, ev->button.y)) break;

            /* Toolbox click (body, not title bar) */
            if (toolbox_click(ev->button.x, ev->button.y)) break;

            /* Minimap click (body, not title bar) */
            if (minimap_click(ev->button.x, ev->button.y)) break;

            /* Block clicks that land on any window body from reaching the game */
            if (point_in_any_window(ev->button.x, ev->button.y)) break;
            
            /* Bulldozer: remove the facility under the cursor. */
            if (game.demolish_mode) {
                int fidx = floor_to_index(game.mouse_floor);
                if (fidx >= 0 && fidx < TOWER_FLOOR_COUNT &&
                    game.mouse_cell >= 0 && game.mouse_cell < TOWER_WIDTH) {
                    uint16_t tid = game.tower.grid[fidx][game.mouse_cell].tenant_id;
                    if (tid) {
                        printf("Demolish: %s\n",
                               tower_item_name(game.tower.grid[fidx][game.mouse_cell].type));
                        tower_remove(&game.tower, tid);
                    }
                }
                break;
            }

            /* Double-click on a shaft opens the elevator dialog (ElvDlogT) */
            if (ev->button.clicks >= 2) {
                int fidx = floor_to_index(game.mouse_floor);
                if (fidx >= 0 && fidx < TOWER_FLOOR_COUNT &&
                    game.mouse_cell >= 0 && game.mouse_cell < TOWER_WIDTH &&
                    item_is_elevator(game.tower.grid[fidx][game.mouse_cell].type)) {
                    TowerCell *cell = &game.tower.grid[fidx][game.mouse_cell];
                    Tenant *t = tower_tenant(&game.tower, cell->tenant_id);
                    if (t) {
                        game.elv_open = 1;
                        game.elv_sx = t->x;
                        game.elv_stype = t->type;
                        game.elv_x = ev->button.x + 24;
                        game.elv_y = ev->button.y - 60;
                        if (game.elv_x + ELV_W > game.screen_w)
                            game.elv_x = game.screen_w - ELV_W - 8;
                        if (game.elv_y < 0) game.elv_y = 8;
                        game.dragging = 0;
                        break;
                    }
                }
            }

            /* Normal game click */
            if (game.build_type != ITEM_NONE) {
                game.dragging = 1;
                game.drag_start_cell = game.mouse_cell;
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
                const ToolButton *g = &tool_buttons[game.tool_popup];
                for (int j = 0; j < g->sub_count; j++) {
                    int bx, by;
                    tool_sub_rect(game.tool_popup, j, &bx, &by);
                    if (ev->button.x >= bx && ev->button.x < bx + TOOL_BTN_SIZE &&
                        ev->button.y >= by && ev->button.y < by + TOOL_BTN_SIZE) {
                        game.build_type = g->sub[j];
                        game.tool_popup = -1;
                        break;
                    }
                }
            }
            /* Stop building placement drag */
            if (game.dragging) {
                if (game.drag_start_cell == game.mouse_cell &&
                    game.drag_start_floor == game.mouse_floor) {
                    /* Elevator click snaps to the shaft column, so clicking
                     * the cap arrows extends the shaft by one floor. */
                    int px = item_is_elevator(game.build_type)
                                 ? elevator_drag_column()
                                 : game.drag_start_cell;
                    tower_place(&game.tower, game.build_type,
                               game.drag_start_floor, px);
                } else {
                    drag_place_units();
                }
                game.dragging = 0;
            }
        }
        break;
        
    case SDL_MOUSEWHEEL: {
        /* Shift+wheel or horizontal wheel = scroll left/right */
        int shift = (SDL_GetModState() & KMOD_SHIFT);
        if (shift || ev->wheel.x != 0) {
            int dx = ev->wheel.x ? ev->wheel.x : ev->wheel.y;
            game.cam_fx += dx * 60;
        } else {
            game.cam_fy -= ev->wheel.y * 60;
        }
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
    
    /* Initialize SDL_ttf fonts */
    init_fonts();
    
    /* Load sprites */
    sprites_init(&game.sprites, &game.exe, game.renderer);

    /* Queue silhouettes use white as transparent */
    sprites_apply_white_key(&game.sprites, game.renderer, SPR_ELEV_QUEUE);
    /* digit sheet background is the shaft's own near-black (as in OS) */
    sprites_apply_color_key(&game.sprites, game.renderer, SPR_ELEV_DIGITS,
                            25, 25, 25);

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
        /* Stairs: 0x8968 (top) + 0x89A8 (bottom) vertically */
        if (sprites_compose_v(&game.sprites, game.renderer, 0x8968, 0x89A8, SPR_STAIRS_COMP) == 0)
            ok++; else fail++;
        /* Escalator: 0x8AA8 (top) + 0x8AE8 (bottom) vertically */
        if (sprites_compose_v(&game.sprites, game.renderer, 0x8AA8, 0x8AE8, SPR_ESCALATOR_COMP) == 0)
            ok++; else fail++;
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
    {
        struct { uint16_t id; Sprite **target; const char *name; } decos[] = {
            { SPR_ENTRANCES,   &game.entrances,  "Entrance awning" },
            { SPR_CRANE,       &game.crane,       "Construction crane" },
            { SPR_FIRELADDER,  &game.fireladder,  "Fire escape" },
        };
        NEResourceList *dibs = ne_find_type(&game.exe, 0x8002);
        for (int d = 0; d < 3; d++) {
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
    game.menu_open = -1;
    game.menu_hover = -1;
    game.menu_bar_hover = -1;
    game.rainy_day = 0;
    
    /* Default window positions (matching original SimTower layout) */
    game.map_x = 0;
    game.map_y = 0;    /* Top left */
    game.tool_x = 0;
    game.tool_y = MAP_WIN_H;  /* Below minimap */
    game.info_x = game.screen_w - INFO_BAR_W;
    game.info_y = 0;   /* Top right */
    game.win_dragging = 0;
    game.tool_popup = -1;
    game.demolish_mode = 0;
    /* Test affordances: --screenshot renders one input-less frame, so allow forcing
     * UI states for visual verification — TB_POPUP=<button index> opens a group's
     * pull-down; DEMOLISH=1 activates the bulldozer. */
    if (getenv("TB_POPUP")) game.tool_popup = atoi(getenv("TB_POPUP"));
    if (getenv("DEMOLISH")) game.demolish_mode = 1;
    if (getenv("STATS")) game.show_stats = 1;
    if (getenv("TUNING")) game.show_tuning = 1;
    if (getenv("ELV_DLOG")) {       /* open the dialog on the demo shaft */
        game.elv_open = 1;
        game.elv_sx = 174;
        game.elv_stype = ITEM_ELEVATOR_SHAFT;
        game.elv_x = 600; game.elv_y = 160;
    }
    if (getenv("SIM_SPEED")) game.sim.speed = atoi(getenv("SIM_SPEED"));
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

    /* Screenshot affordance: a small commuting scene — an office stack over
     * the lobby with a standard elevator beside it (COMMUTE_DEMO=1) */
    if (getenv("COMMUTE_DEMO")) {
        game.tower.money = 100000000L;
        for (int f = 1; f <= 6; f++)
            tower_place(&game.tower, ITEM_OFFICE, f, 183);
        for (int f = 0; f <= 6; f++)
            tower_place(&game.tower, ITEM_ELEVATOR_SHAFT, f, 174);
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
    
    /* Main loop */
    game.running = 1;
    game.build_type = ITEM_OFFICE;
    
    int frame = 0;
    while (game.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            handle_event(&ev);
        }
        
        /* Advance simulation */
        {
            int prev_hour = game.sim.hour;
            int prev_star = game.tower.star_rating;
            int prev_pop = game.tower.population;
            int prev_unreach = game.sim.unreachable_tenants;
            game_update(&game.sim, &game.tower);

            /* Commute feedback: units cut off from the entrance */
            if (game.sim.unreachable_tenants > prev_unreach) {
                char buf[48];
                snprintf(buf, sizeof(buf), "%d unit%s cannot be reached!",
                         game.sim.unreachable_tenants,
                         game.sim.unreachable_tenants == 1 ? "" : "s");
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
            
            /* Event announcements */
            if (game.sim.event.active && game.sim.event.timer == game.sim.event.duration) {
                if (game.sim.event.type == EVENT_FIRE) add_event_message("FIRE! Fire in the tower!");
                else if (game.sim.event.type == EVENT_BOMB) add_event_message("BOMB THREAT reported!");
            }
            
            /* VIP visit */
            if (game.sim.vip_visiting && !game.sim.vip_satisfied) {
                /* announced once */
            }
        }
        
        render();
        frame++;
        
        /* Auto-screenshot: run sim for a bit first so tenants wake up.
         * SHOT_FRAME=N overrides how long the sim runs before the capture. */
        static int shot_frame = 0;
        if (!shot_frame)
            shot_frame = getenv("SHOT_FRAME") ? atoi(getenv("SHOT_FRAME")) : 200;
        if (auto_screenshot && frame == shot_frame) {
            SDL_Surface *sshot = SDL_CreateRGBSurface(0, game.screen_w, game.screen_h, 32,
                0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
            SDL_RenderReadPixels(game.renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                 sshot->pixels, sshot->pitch);
            SDL_SaveBMP(sshot, screenshot_path);
            SDL_FreeSurface(sshot);
            printf("Auto-screenshot saved to %s\n", screenshot_path);
            game.running = 0;
        }
        
        SDL_Delay(16); /* ~60fps */
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
