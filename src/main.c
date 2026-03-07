/* SimTower for Linux - main.c
 *
 * A native Linux port of SimTower, using game mechanics extracted from
 * decompilation of SIMTOWER.EXE and guided by the YootTower code map.
 * Sprite IDs verified against OpenSkyscraper SimTowerLoader.cpp.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include "ne_resource.h"
#include "sprites.h"
#include "tower.h"
#include "game.h"

/* ---------- Window / display ---------- */
#define WINDOW_W    960
#define WINDOW_H    720
#define HUD_HEIGHT  32
#define MENU_BAR_H  20    /* Classic Win3.1 menu bar height */
#define MENU_ITEM_PAD 12  /* Horizontal padding for menu items */

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
#define SPR_ELEV_CAR     0x8428
#define SPR_ELEV_SERVICE 0x842a
#define SPR_ELEV_SHAFT   0x87e8

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
    case ITEM_ELEVATOR_SHAFT:*frame_w = 32;  return SPR_ELEV_SHAFT;
    case ITEM_FLOOR:         *frame_w = 0;   return 0;
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
    int             running;
    int             screen_w, screen_h;
    int             show_debug;   /* Toggle diagnostic labels */
    
    /* Build mode */
    ItemType        build_type;
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
    
    /* Win3.1 Menu system */
    int             menu_open;       /* -1 = closed, 0+ = which top-level menu is open */
    int             menu_hover;      /* which item is hovered in the open dropdown */
    int             menu_bar_hover;  /* which top-level menu is hovered */
    
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

/* ---------- Cloud positions (fixed, scattered in sky) ---------- */
typedef struct { int x, y; int cloud_idx; } CloudPos;
static const CloudPos CLOUD_POSITIONS[] = {
    { 50,  -80,  0 },
    { 220, -150, 2 },
    { 400, -60,  1 },
    { 150, -220, 3 },
    { 520, -180, 0 },
    { 320, -280, 2 },
    { 680, -100, 1 },
    { 80,  -320, 3 },
    { 450, -350, 0 },
    { 750, -250, 2 },
    { 900, -120, 1 },
    { 600, -380, 3 },
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
    
    /* Sky: tile 32×360 strips across the width, bottom-aligned to lobby top */
    Sprite *sky = sprites_find(&game.sprites, 0x8352);
    
    if (sky) {
        for (int x = 0; x < game.screen_w; x += sky->w) {
            int sy = lobby_sy - sky->h;
            SDL_Rect dst = { x, sy, sky->w, sky->h };
            SDL_RenderCopy(game.renderer, sky->texture, NULL, &dst);
            for (int y2 = sy - sky->h; y2 > -sky->h; y2 -= sky->h) {
                SDL_Rect dst2 = { x, y2, sky->w, sky->h };
                SDL_RenderCopy(game.renderer, sky->texture, NULL, &dst2);
            }
        }
    } else {
        /* Fallback: blue gradient */
        for (int y = 0; y < lobby_sy && y < game.screen_h; y++) {
            int t = (y * 255) / (lobby_sy > 0 ? lobby_sy : 1);
            SDL_SetRenderDrawColor(game.renderer, 
                80 + t/3, 150 + t/3, 220 + t/8, 255);
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
    
    /* Render clouds in the sky */
    if (game.cloud_count > 0) {
        for (int i = 0; i < CLOUD_COUNT; i++) {
            int ci = CLOUD_POSITIONS[i].cloud_idx % game.cloud_count;
            Sprite *cs = game.clouds[ci];
            if (!cs) continue;
            SDL_SetTextureAlphaMod(cs->texture, 180);
            
            /* Cloud positions are relative to the lobby top */
            int cx = lobby_sx + CLOUD_POSITIONS[i].x;
            int cy = lobby_sy + CLOUD_POSITIONS[i].y;
            
            /* Also tile clouds horizontally for wide views */
            for (int tx = cx - 1200; tx < game.screen_w + 200; tx += 1200) {
                if (tx + cs->w < 0) continue;
                if (tx > game.screen_w) break;
                if (cy + cs->h < 0 || cy > game.screen_h) continue;
                
                SDL_Rect dst = { tx, cy, cs->w, cs->h };
                SDL_RenderCopy(game.renderer, cs->texture, NULL, &dst);
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
    
    /* City skyline — tiled at ground level (from Decorations.cpp).
     * The skyline is 96px wide and drawn across the entire ground line,
     * behind the tower but in front of the sky. */
    if (game.skyline) {
        SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
        /* Skyline positioned at ground level (floor 0 top edge) */
        int skyline_y = lobby_sy - game.skyline->h;
        for (int sx = -game.skyline->w; sx < game.screen_w + game.skyline->w; sx += game.skyline->w) {
            SDL_Rect dst = { sx, skyline_y, game.skyline->w, game.skyline->h };
            SDL_RenderCopy(game.renderer, game.skyline->texture, NULL, &dst);
        }
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
                /* Lobby: tile the real lobby sprite across full width */
                int lobby_pw = TOWER_WIDTH * CELL_W;
                for (int lx = 0; lx < lobby_pw; lx += lobby_spr->w) {
                    int lsx = sx_base + lx;
                    int draw_w = lobby_spr->w;
                    if (lx + draw_w > lobby_pw) draw_w = lobby_pw - lx;
                    if (lsx + draw_w < 0 || lsx > game.screen_w) continue;
                    SDL_Rect src = { 0, 0, draw_w, lobby_spr->h };
                    SDL_Rect dst = { lsx, ty, draw_w, CELL_H };
                    SDL_RenderCopy(game.renderer, lobby_spr->texture, &src, &dst);
                }
                x = tenant->x + tenant->width;
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
            
            /* Tenant state visual overlay */
            {
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
            
            /* Awning on left entrance */
            SDL_Rect awning_l = { tx - 8, ty - 4, game.entrances->w / 2, game.entrances->h };
            SDL_Rect src_l = { 0, 0, game.entrances->w / 2, game.entrances->h };
            SDL_RenderCopy(game.renderer, game.entrances->texture, &src_l, &awning_l);
            
            /* Awning on right entrance (mirrored) */
            SDL_Rect awning_r = { tx + tw - game.entrances->w / 2 + 8, ty - 4,
                                  game.entrances->w / 2, game.entrances->h };
            SDL_Rect src_r = { game.entrances->w / 2, 0, game.entrances->w / 2, game.entrances->h };
            SDL_RenderCopy(game.renderer, game.entrances->texture, &src_r, &awning_r);
        }
    }
    
    /* Construction crane — drawn above any building under construction */
    if (game.crane) {
        for (int i = 0; i < game.tower.tenant_count; i++) {
            Tenant *t = &game.tower.tenants[i];
            if (t->state != TENANT_CONSTRUCTION) continue;
            if (t->type == ITEM_LOBBY || t->type == ITEM_FLOOR) continue;
            
            int tx, ty;
            grid_to_screen(t->floor, t->x, &tx, &ty);
            
            /* Crane sits above the building being constructed */
            SDL_Rect crane_dst = {
                tx + (t->width * CELL_W) / 2 - game.crane->w / 2,
                ty - game.crane->h - 4,
                game.crane->w, game.crane->h
            };
            SDL_RenderCopy(game.renderer, game.crane->texture, NULL, &crane_dst);
        }
    }
    
    /* Fire escape — drawn on BOTH sides of the tower (from Decorations.cpp).
     * The 48px sprite splits: left 24px on min edge, right 24px on max edge.
     * Fire stairs appear on every above-ground floor that has buildings. */
    if (game.fireladder) {
        int half_w = game.fireladder->w / 2;  /* 24px each side */
        for (int floor = 1; floor <= TOWER_MAX_FLOOR; floor++) {
            int fidx = floor_to_index(floor);
            if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
            
            /* Find leftmost and rightmost occupied cells */
            int left_x = -1, right_x = -1;
            for (int x = 0; x < TOWER_WIDTH; x++) {
                if (game.tower.grid[fidx][x].type != ITEM_NONE) {
                    if (left_x < 0) left_x = x;
                    right_x = x;
                }
            }
            if (left_x < 0) continue;
            
            int fsy_unused, fsy;
            grid_to_screen(floor, 0, &fsy_unused, &fsy);
            
            /* Left fire escape (first 24px of sprite, drawn mirrored) */
            int lx, ly;
            grid_to_screen(floor, left_x, &lx, &ly);
            SDL_Rect fe_left = { lx - half_w, fsy, half_w, CELL_H };
            SDL_Rect src_left = { 0, 0, half_w, game.fireladder->h };
            SDL_RenderCopy(game.renderer, game.fireladder->texture, &src_left, &fe_left);
            
            /* Right fire escape (second 24px of sprite) */
            int rx_pos, ry;
            grid_to_screen(floor, right_x + 1, &rx_pos, &ry);
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

static void render_build_ghost(void)
{
    if (game.build_type == ITEM_NONE) return;
    
    int width = ITEM_WIDTH[game.build_type];
    int floors = ITEM_HEIGHT[game.build_type];
    
    if (game.dragging) {
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
    /* Semi-transparent dark HUD background */
    SDL_SetRenderDrawColor(game.renderer, 20, 20, 35, 210);
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect bar = { 0, 0, game.screen_w, HUD_HEIGHT };
    SDL_RenderFillRect(game.renderer, &bar);
    /* Subtle bottom border */
    SDL_SetRenderDrawColor(game.renderer, 80, 80, 120, 180);
    SDL_RenderDrawLine(game.renderer, 0, HUD_HEIGHT - 1, game.screen_w, HUD_HEIGHT - 1);
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
    
    if (game.font) {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Color gold  = {255, 215, 80, 255};
        SDL_Color green = {100, 255, 100, 255};
        SDL_Color cyan  = {100, 200, 255, 255};
        SDL_Color pink  = {255, 150, 200, 255};
        
        int x = 10;
        int y = (HUD_HEIGHT - 16) / 2; /* Center vertically, assuming ~16px font */
        
        /* Money */
        char money_buf[64];
        format_money(game.tower.money, money_buf, sizeof(money_buf));
        x += draw_text(money_buf, x, y, green) + 20;
        
        /* Star rating: ★★★☆☆ */
        {
            char stars[32];
            int pos = 0;
            for (int i = 0; i < 5; i++) {
                if (i < game.tower.star_rating) {
                    /* UTF-8 for ★ (U+2605): E2 98 85 */
                    stars[pos++] = (char)0xE2;
                    stars[pos++] = (char)0x98;
                    stars[pos++] = (char)0x85;
                } else {
                    /* UTF-8 for ☆ (U+2606): E2 98 86 */
                    stars[pos++] = (char)0xE2;
                    stars[pos++] = (char)0x98;
                    stars[pos++] = (char)0x86;
                }
            }
            stars[pos] = '\0';
            x += draw_text(stars, x, y, gold) + 20;
        }
        
        /* Population */
        {
            char pop_buf[64];
            snprintf(pop_buf, sizeof(pop_buf), "Pop: %d", game.tower.population);
            x += draw_text(pop_buf, x, y, cyan) + 20;
        }
        
        /* Day counter + time */
        {
            char time_buf[32];
            game_format_time(&game.sim, time_buf, sizeof(time_buf));
            char day_buf[96];
            const char *speed_str[] = {"⏸", "▶", "▶▶", "▶▶▶"};
            int spd = game.sim.speed;
            if (spd < 0) spd = 0;
            if (spd > 3) spd = 3;
            snprintf(day_buf, sizeof(day_buf), "Day %d  %s  %s  %s",
                     game.tower.day, time_buf, 
                     game_quarter_name(game.sim.quarter),
                     speed_str[spd]);
            x += draw_text(day_buf, x, y, white) + 30;
        }
        
        /* Tenant count */
        {
            char tenant_buf[64];
            snprintf(tenant_buf, sizeof(tenant_buf), "T:%d/%d",
                     game.sim.tenants_occupied, game.sim.tenants_total);
            x += draw_text(tenant_buf, x, y, cyan) + 20;
        }
        
        /* Build tool + cost (right-aligned) */
        if (game.build_type != ITEM_NONE) {
            char tool_buf[128];
            int cost = ITEM_COST[game.build_type];
            if (cost > 0) {
                char cost_str[32];
                format_money(cost, cost_str, sizeof(cost_str));
                snprintf(tool_buf, sizeof(tool_buf), "[%s %s]",
                         tower_item_name(game.build_type), cost_str);
            } else {
                snprintf(tool_buf, sizeof(tool_buf), "[%s]",
                         tower_item_name(game.build_type));
            }
            /* Measure text width for right-alignment */
            int tw, th;
            SDL_Texture *tex = render_text(tool_buf, pink, &tw, &th);
            if (tex) {
                SDL_Rect dst = { game.screen_w - tw - 10, y, tw, th };
                SDL_RenderCopy(game.renderer, tex, NULL, &dst);
                SDL_DestroyTexture(tex);
            }
        }
    }
    
    /* Update window title with current state (for VNC title bar) */
    char title[256];
    snprintf(title, sizeof(title), 
             "ConcilliaTower | $%ld | %d★ | Pop: %d | Day %d | Build: %s",
             game.tower.money, game.tower.star_rating, game.tower.population,
             game.tower.day, tower_item_name(game.build_type));
    SDL_SetWindowTitle(game.window, title);
    
    /* Win3.1 menu bar (below the HUD) */
    render_menu_bar();
    
    /* Open dropdown menu */
    render_dropdown();
}

static void render(void)
{
    SDL_SetRenderDrawColor(game.renderer, 0, 0, 0, 255);
    SDL_RenderClear(game.renderer);
    
    render_sky();
    render_tower();
    render_build_ghost();
    render_ui();
    
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
        if (ev->button.button == SDL_BUTTON_LEFT) {
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
            
            /* Normal game click */
            if (game.build_type != ITEM_NONE) {
                game.dragging = 1;
                game.drag_start_cell = game.mouse_cell;
                game.drag_start_floor = game.mouse_floor;
            }
        }
        break;
    
    case SDL_MOUSEBUTTONUP:
        if (ev->button.button == SDL_BUTTON_LEFT && game.dragging) {
            if (game.drag_start_cell == game.mouse_cell && 
                game.drag_start_floor == game.mouse_floor) {
                tower_place(&game.tower, game.build_type,
                           game.drag_start_floor, game.drag_start_cell);
            } else {
                drag_place_units();
            }
            game.dragging = 0;
        }
        break;
        
    case SDL_MOUSEWHEEL:
        game.cam_fy -= ev->wheel.y * 60;
        break;
        
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
            { SPR_SKYLINE,     &game.skyline,     "City skyline" },
        };
        NEResourceList *dibs = ne_find_type(&game.exe, 0x8002);
        for (int d = 0; d < 4; d++) {
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
    
    /* Build demo tower with all unit types */
    tower_build_demo(&game.tower);
    
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
            game_update(&game.sim, &game.tower);
            /* Decide rainy day at 5 AM (from OpenSkyscraper: every 3rd day) */
            if (prev_hour == 4 && game.sim.hour == 5) {
                game.rainy_day = (rand() % 3 == 0);
            }
        }
        
        render();
        frame++;
        
        /* Auto-screenshot: run sim for a bit first so tenants wake up */
        if (auto_screenshot && frame == 200) {
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
    TTF_Quit();
    SDL_DestroyRenderer(game.renderer);
    SDL_DestroyWindow(game.window);
    SDL_Quit();
    ne_free(&game.exe);
    
    printf("Exited cleanly.\n");
    return 0;
}
