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

/* Security: 0x8768 (animated, 3 frames via palette cycling) */
#define SPR_SECURITY    0x8768

/* Medical: 0x8728+0x8729+0x872A (3 bitmaps horizontally) */
#define SPR_MEDICAL_A   0x8728
#define SPR_MEDICAL_B   0x8729
#define SPR_MEDICAL_C   0x872A

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
/* 0x8388 is Santa Claus! (confirmed via OpenSkyscraper: "deco/santa") */
#define SPR_SANTA        0x8388   /* 140×48 — Easter egg, not a cloud! */

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
    case ITEM_SHOP:          *frame_w = 96;  return 0x8668; /* 288×24, 3 frames of 96px */
    case ITEM_CINEMA:        *frame_w = 192; return SPR_CINEMA_COMP;   /* 768/4=192 per frame */
    case ITEM_PARTY_HALL:    *frame_w = 192; return SPR_PARTYHALL_COMP; /* 576/3=192 per frame */
    case ITEM_METRO:         *frame_w = 240; return SPR_METRO_COMP;  /* 720/3=240 per frame */
    case ITEM_PARKING:       *frame_w = 32;  return SPR_PARKING_COMP;
    case ITEM_CATHEDRAL:     *frame_w = 0;   return 0x8828; /* 128×36, single sprite */
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
    case ITEM_CATHEDRAL:    *r=210; *g=190; *b=140; break;
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
    Sprite         *santa;   /* 0x8388 — shows up at Christmas? */
    int             cloud_count;
} Game;

static Game game;

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
    
    /* Underground: brown earth gradient below lobby level, gets darker with depth */
    int ug_start = lobby_sy + CELL_H;
    if (ug_start < game.screen_h) {
        for (int y = ug_start; y < game.screen_h; y++) {
            int depth = y - ug_start;
            /* Start warm brown (139,90,43) → dark earth (80,50,20) */
            int r = 139 - depth / 4; if (r < 60) r = 60;
            int g = 90  - depth / 5; if (g < 35) g = 35;
            int b = 43  - depth / 6; if (b < 15) b = 15;
            SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
            SDL_RenderDrawLine(game.renderer, 0, y, game.screen_w, y);
        }
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
                 * Frame 0 = primary occupied appearance.
                 * Don't use tenant->state for frame selection — that's the
                 * lifecycle state (empty/occupied/vacant), not animation. */
                int nframes = spr->w / frame_w_hint;
                if (nframes < 1) nframes = 1;
                int frame_idx = 0;  /* Default: first (occupied) frame */
                /* TODO: use proper animation state per tenant type */
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
                
                if (tenant->state == TENANT_VACANT || tenant->state == TENANT_EMPTY) {
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
            "empty", "movin", "OCCUP", "close", "vacnt", "STRES", "ABND"
        };
        for (int i = 0; i < game.tower.tenant_count; i++) {
            Tenant *t = &game.tower.tenants[i];
            if (t->type == ITEM_LOBBY || t->type == ITEM_FLOOR) continue;
            
            int tx, ty;
            grid_to_screen(t->floor, t->x + t->width, &tx, &ty);
            
            /* Build diagnostic string with state info */
            const char *sn = (t->state < 7) ? state_names[t->state] : "???";
            char info[256];
            snprintf(info, sizeof(info), "%s [%s] pop:%d str:%d",
                     tower_item_name(t->type), sn, t->population, t->stress);
            
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
        
        /* Debug toggle */
        case SDLK_F1:
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
        break;
        
    case SDL_MOUSEBUTTONDOWN:
        if (ev->button.button == SDL_BUTTON_LEFT && game.build_type != ITEM_NONE) {
            /* Start drag placement */
            game.dragging = 1;
            game.drag_start_cell = game.mouse_cell;
            game.drag_start_floor = game.mouse_floor;
        }
        break;
    
    case SDL_MOUSEBUTTONUP:
        if (ev->button.button == SDL_BUTTON_LEFT && game.dragging) {
            /* End drag — check if it was a single click or a drag */
            if (game.drag_start_cell == game.mouse_cell && 
                game.drag_start_floor == game.mouse_floor) {
                /* Single click: place one unit */
                tower_place(&game.tower, game.build_type,
                           game.drag_start_floor, game.drag_start_cell);
            } else {
                /* Drag: place row of units */
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
            {0x8828, "cathedral"},
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
    printf("  F1: toggle debug labels, F12: screenshot, Q/Esc: quit\n\n");
    
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
        game_update(&game.sim, &game.tower);
        
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
