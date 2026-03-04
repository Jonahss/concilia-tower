/* SimTower for Linux - main.c
 *
 * A native Linux port of SimTower, using game mechanics extracted from
 * decompilation of SIMTOWER.EXE and guided by the YootTower code map.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "ne_resource.h"
#include "sprites.h"
#include "tower.h"

/* ---------- Window / display ---------- */
#define WINDOW_W    960
#define WINDOW_H    720

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

/* Condo: 0x8628+ — 128×24 (4 frames of 32px) */
#define SPR_CONDO_BASE  0x8628

/* Restaurant: 0x8568+ — 384×24 (6 frames of 64px) */
#define SPR_RESTAURANT_BASE 0x8568

/* Fast food: 0x85e8 — 480×24 */
#define SPR_FASTFOOD_BASE 0x85e8

/* Hotel single: 0x84A8 — 32×24 */
#define SPR_HOTEL_S_BASE 0x84a8

/* Hotel double: 0x84E8 — 48×24 */
#define SPR_HOTEL_D_BASE 0x84e8

/* Elevator: 0x8428 = standard car, 0x842a = service, 0x87E8 = shaft */
#define SPR_ELEV_CAR     0x8428
#define SPR_ELEV_SERVICE 0x842a
#define SPR_ELEV_SHAFT   0x87e8

/* Stairs: 0x88e8 — 200×60 */
#define SPR_STAIRS_BASE   0x88e8

/* Escalator: 0x8928 — 200×36 */
#define SPR_ESCALATOR_BASE 0x8928

/* Clouds */
#define SPR_CLOUD_BASE   0x8258

/* UI */
#define SPR_TOOLBAR      0x8140

/* ---------- Sprite mapping for item types ---------- */
/* Returns sprite ID for a tenant type. frame_w = pixel width of ONE frame.
 * Frame widths verified from OpenSkyscraper setTextureRect() calls. */
static uint16_t item_sprite_id(ItemType type, int *frame_w)
{
    switch (type) {
    case ITEM_LOBBY:         *frame_w = 0;   return SPR_LOBBY_BOT0; /* special render */
    case ITEM_OFFICE:        *frame_w = 72;  return SPR_OFFICE_BASE;  /* 9 cells × 8px */
    case ITEM_CONDO:         *frame_w = 128; return SPR_CONDO_BASE;   /* 16 cells × 8px */
    case ITEM_HOTEL_SINGLE:  *frame_w = 32;  return SPR_HOTEL_S_BASE; /* 4 cells × 8px */
    case ITEM_RESTAURANT:    *frame_w = 192; return SPR_RESTAURANT_BASE; /* 24 cells × 8px */
    case ITEM_FAST_FOOD:     *frame_w = 128; return SPR_FASTFOOD_BASE;   /* 16 cells × 8px */
    case ITEM_STAIRS:        *frame_w = 200; return SPR_STAIRS_BASE;
    case ITEM_ESCALATOR:     *frame_w = 200; return SPR_ESCALATOR_BASE;
    case ITEM_ELEVATOR_SHAFT:*frame_w = 32;  return SPR_ELEV_SHAFT;
    case ITEM_FLOOR:         *frame_w = 0;   return 0; /* drawn as colored bar */
    default:                 *frame_w = 0;   return 0;
    }
}

/* ---------- Game state ---------- */
typedef struct {
    NEResourceTable exe;
    SpriteAtlas     sprites;
    Tower           tower;
    SDL_Window     *window;
    SDL_Renderer   *renderer;
    int             running;
    int             screen_w, screen_h;
    
    /* Build mode */
    ItemType        build_type;
    int             mouse_x, mouse_y;
    int             mouse_floor, mouse_cell;
    
    /* Camera smoothing */
    float           cam_fx, cam_fy;
    
    /* Zoom */
    float           zoom;
} Game;

static Game game;

/* ---------- Coordinate conversion ---------- */

/* Convert screen coordinates to tower grid position */
static void screen_to_grid(int sx, int sy, int *floor, int *cell)
{
    /* Tower pixel origin: the lobby floor at the center of the world */
    int world_x = sx + (int)game.cam_fx - game.screen_w / 2;
    int world_y = sy + (int)game.cam_fy - game.screen_h / 2;
    
    *cell = world_x / CELL_W;
    /* Y axis: floor 0 is at world_y = 0, positive floors go up (negative world_y) */
    *floor = -(world_y / CELL_H);
    
    /* Clamp */
    if (*cell < 0) *cell = 0;
    if (*cell >= TOWER_WIDTH) *cell = TOWER_WIDTH - 1;
}

/* Convert grid position to screen coordinates */
static void grid_to_screen(int floor, int cell, int *sx, int *sy)
{
    int world_x = cell * CELL_W;
    int world_y = -floor * CELL_H;
    
    *sx = world_x - (int)game.cam_fx + game.screen_w / 2;
    *sy = world_y - (int)game.cam_fy + game.screen_h / 2;
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
    
    /* Underground: brown earth below lobby level */
    for (int y = lobby_sy + CELL_H; y < game.screen_h; y++) {
        int depth = y - (lobby_sy + CELL_H);
        int r = 139 - depth / 6; if (r < 50) r = 50;
        int g = 110 - depth / 6; if (g < 35) g = 35;
        int b =  70 - depth / 6; if (b < 20) b = 20;
        SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
        SDL_RenderDrawLine(game.renderer, 0, y, game.screen_w, y);
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
    
    for (int floor = bot_floor; floor <= top_floor; floor++) {
        int fidx = floor_to_index(floor);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
        
        int sx_base, sy_base;
        grid_to_screen(floor, 0, &sx_base, &sy_base);
        
        /* Check if this floor has ANY content */
        int floor_has_content = 0;
        int left = TOWER_WIDTH, right = 0;
        for (int x = 0; x < TOWER_WIDTH; x++) {
            if (game.tower.grid[fidx][x].type != ITEM_NONE) {
                floor_has_content = 1;
                if (x < left) left = x;
                if (x > right) right = x;
            }
        }
        
        if (!floor_has_content) continue;
        
        /* Floor background wall — fills behind rooms so sky doesn't show through.
         * In the original game, Floor items provide this solid backdrop. */
        {
            int wall_x = sx_base + left * CELL_W;
            int wall_w = (right - left + 1) * CELL_W;
            /* Wall color: warm beige matching original SimTower interiors */
            SDL_SetRenderDrawColor(game.renderer, 198, 195, 182, 255);
            SDL_Rect wall_rect = { wall_x, sy_base, wall_w, CELL_H };
            SDL_RenderFillRect(game.renderer, &wall_rect);
        }
        
        /* Ceiling strip (top 12px of floor) — darker gray-beige bar */
        if (floor > 0) {
            int ceil_x = sx_base + left * CELL_W;
            int ceil_w = (right - left + 1) * CELL_W;
            SDL_SetRenderDrawColor(game.renderer, 178, 172, 160, 255);
            SDL_Rect ceil_rect = { ceil_x, sy_base, ceil_w, CEIL_H };
            SDL_RenderFillRect(game.renderer, &ceil_rect);
            /* Thin dark line at bottom of ceiling for definition */
            SDL_SetRenderDrawColor(game.renderer, 140, 135, 125, 255);
            SDL_RenderDrawLine(game.renderer, ceil_x, sy_base + CEIL_H - 1,
                              ceil_x + ceil_w, sy_base + CEIL_H - 1);
        }
        
        /* Underground floors: darker brown fill for basement areas */
        if (floor < 0) {
            int ug_x = sx_base + left * CELL_W;
            int ug_w = (right - left + 1) * CELL_W;
            SDL_SetRenderDrawColor(game.renderer, 120, 95, 65, 255);
            SDL_Rect ug_rect = { ug_x, sy_base, ug_w, CELL_H };
            SDL_RenderFillRect(game.renderer, &ug_rect);
        }
        /* (old underground tile code removed — using brown fill above) */
        
        /* Render tenants on this floor */
        for (int x = 0; x < TOWER_WIDTH; ) {
            TowerCell *cell = &game.tower.grid[fidx][x];
            
            if (cell->type == ITEM_NONE) { x++; continue; }
            if (cell->cell_index != 0) { x++; continue; }
            
            Tenant *tenant = tower_tenant(&game.tower, cell->tenant_id);
            if (!tenant) { x++; continue; }
            
            int frame_w_hint = 0;
            uint16_t spr_id = item_sprite_id(tenant->type, &frame_w_hint);
            Sprite *spr = spr_id ? sprites_find(&game.sprites, spr_id) : NULL;
            
            int tx, ty;
            grid_to_screen(floor, tenant->x, &tx, &ty);
            int tw = tenant->width * CELL_W;
            
            /* Tenant sprite goes below the ceiling strip */
            int tenant_y = ty + CEIL_H;
            
            if (tenant->type == ITEM_LOBBY && lobby_spr) {
                /* Lobby: tile the real lobby sprite (992×36) across full width.
                 * Lobby occupies the full 36px height (no ceiling strip). */
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
                /* Skip to end of lobby */
                x = tenant->x + tenant->width;
                continue;
            } else if (spr && frame_w_hint > 0) {
                /* Extract one frame from the sprite sheet.
                 * Sprite sheets have frames packed horizontally.
                 * frame_w_hint = pixel width of one frame.
                 * tenant state selects which frame to show. */
                int frame_idx = tenant->state % (spr->w / frame_w_hint);
                SDL_Rect src = { frame_idx * frame_w_hint, 0, frame_w_hint, spr->h };
                SDL_Rect dst = { tx, tenant_y, tw, TENANT_H };
                SDL_RenderCopy(game.renderer, spr->texture, &src, &dst);
            } else if (spr) {
                /* Full sprite, no frame extraction */
                SDL_Rect dst = { tx, tenant_y, tw, TENANT_H };
                SDL_RenderCopy(game.renderer, spr->texture, NULL, &dst);
            } else {
                /* Fallback: colored rectangle */
                uint8_t r = 100, g = 100, b = 100;
                switch (tenant->type) {
                case ITEM_OFFICE:       r=200; g=200; b=150; break;
                case ITEM_CONDO:        r=180; g=220; b=180; break;
                case ITEM_HOTEL_SINGLE: r=150; g=150; b=220; break;
                case ITEM_RESTAURANT:   r=220; g=180; b=150; break;
                case ITEM_FAST_FOOD:    r=220; g=220; b=100; break;
                case ITEM_FLOOR:        r=200; g=200; b=200; break;
                default: break;
                }
                SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
                SDL_Rect rect = { tx, tenant_y, tw, TENANT_H };
                SDL_RenderFillRect(game.renderer, &rect);
                SDL_SetRenderDrawColor(game.renderer, 80, 80, 80, 255);
                SDL_RenderDrawRect(game.renderer, &rect);
            }
            
            x += tenant->width;
        }
    }
}

static void render_build_ghost(void)
{
    if (game.build_type == ITEM_NONE) return;
    
    int width = ITEM_WIDTH[game.build_type];
    int gx, gy;
    grid_to_screen(game.mouse_floor, game.mouse_cell, &gx, &gy);
    
    int can = tower_can_place(&game.tower, game.build_type, 
                               game.mouse_floor, game.mouse_cell);
    
    /* Ghost rectangle */
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
    if (can) {
        SDL_SetRenderDrawColor(game.renderer, 0, 200, 0, 100);
    } else {
        SDL_SetRenderDrawColor(game.renderer, 200, 0, 0, 100);
    }
    SDL_Rect ghost = { gx, gy, width * CELL_W, CELL_H };
    SDL_RenderFillRect(game.renderer, &ghost);
    SDL_SetRenderDrawColor(game.renderer, 255, 255, 255, 200);
    SDL_RenderDrawRect(game.renderer, &ghost);
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
}

static void render_ui(void)
{
    /* Simple HUD at top of screen */
    SDL_SetRenderDrawColor(game.renderer, 40, 40, 60, 220);
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect bar = { 0, 0, game.screen_w, 28 };
    SDL_RenderFillRect(game.renderer, &bar);
    SDL_SetRenderDrawBlendMode(game.renderer, SDL_BLENDMODE_NONE);
    
    /* We'll render text when SDL_ttf is added. For now, use the window title. */
    char title[256];
    const char *type_names[] = {
        "None", "Lobby", "Floor", "Office", "Condo", "Hotel(S)", "Hotel(T)", 
        "Hotel(Suite)", "Restaurant", "Fast Food", "Shop", "Cinema", "Party Hall",
        "Metro", "Parking", "Cathedral", "Medical", "Security", "Recycling",
        "Stairs", "Escalator", "Elevator"
    };
    snprintf(title, sizeof(title), 
             "SimTower | $%ld | %d★ | Pop: %d | Day %d | Build: %s [1-6 to select, click to place]",
             game.tower.money, game.tower.star_rating, game.tower.population,
             game.tower.day, type_names[game.build_type]);
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
        case SDLK_LEFT:  case SDLK_a: game.cam_fx -= 40; break;
        case SDLK_RIGHT: case SDLK_d: game.cam_fx += 40; break;
        case SDLK_UP:    case SDLK_w: game.cam_fy -= 40; break;
        case SDLK_DOWN:  case SDLK_s: game.cam_fy += 40; break;
        
        /* Build type selection */
        case SDLK_1: game.build_type = ITEM_OFFICE; break;
        case SDLK_2: game.build_type = ITEM_CONDO; break;
        case SDLK_3: game.build_type = ITEM_RESTAURANT; break;
        case SDLK_4: game.build_type = ITEM_FAST_FOOD; break;
        case SDLK_5: game.build_type = ITEM_HOTEL_SINGLE; break;
        case SDLK_6: game.build_type = ITEM_STAIRS; break;
        case SDLK_0: game.build_type = ITEM_NONE; break;
        
        default: break;
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
            tower_place(&game.tower, game.build_type, 
                        game.mouse_floor, game.mouse_cell);
        }
        break;
        
    case SDL_MOUSEWHEEL:
        /* Scroll to move camera vertically */
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

/* ---------- Main ---------- */
int main(int argc, char *argv[])
{
    const char *exe_path = NULL;
    
    /* Find SIMTOWER.EXE */
    const char *search_paths[] = {
        "SIMTOWER.EXE",
        "data/SIMTOWER.EXE",
        "../OpenSkyscraper/data/SIMTOWER.EXE",
        NULL
    };
    
    if (argc > 1 && argv[1][0] != '-') {
        exe_path = argv[1];
    } else {
        for (int i = 0; search_paths[i]; i++) {
            FILE *test = fopen(search_paths[i], "rb");
            if (test) { fclose(test); exe_path = search_paths[i]; break; }
        }
    }
    
    if (!exe_path) {
        fprintf(stderr, "Cannot find SIMTOWER.EXE. Pass path as argument.\n");
        return 1;
    }
    
    printf("SimTower for Linux\n");
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
    game.window = SDL_CreateWindow("SimTower for Linux",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    
    game.renderer = SDL_CreateRenderer(game.window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!game.renderer)
        game.renderer = SDL_CreateRenderer(game.window, -1, SDL_RENDERER_SOFTWARE);
    
    /* Load sprites */
    sprites_init(&game.sprites, &game.exe, game.renderer);
    
    /* Print key sprite info for debugging */
    {
        struct { uint16_t id; const char *name; } checks[] = {
            {0x8468, "lobby_day"}, {0x8469, "lobby_night"},
            {0x85a8, "office0"}, {0x85a9, "office1"}, {0x85aa, "office2"},
            {0x8628, "condo0"}, {0x8568, "restaurant0"},
            {0x85e8, "fastfood0"}, {0x84a8, "hotel_s0"},
            {0x8428, "ceil"}, {0x842b, "floor_walk"},
            {0x8f28, "underground"}, {0x8352, "sky"},
            {0x8258, "cloud"}, {0x8140, "toolbar"},
            {0x8100, "title"},
            {0, NULL}
        };
        printf("\n=== Sprite diagnostics ===\n");
        for (int i = 0; checks[i].name; i++) {
            Sprite *s = sprites_find(&game.sprites, checks[i].id);
            if (s) printf("  0x%04x %-14s %dx%d (type 0x%04x)\n", 
                          checks[i].id, checks[i].name, s->w, s->h, s->type);
            else printf("  0x%04x %-14s NOT FOUND\n", checks[i].id, checks[i].name);
        }
        printf("===========================\n\n");
    }
    
    /* Initialize tower */
    tower_init(&game.tower);
    
    /* Center camera on the tower */
    game.cam_fx = (TOWER_WIDTH * CELL_W) / 2.0f;  /* Center horizontally on tower */
    game.cam_fy = -game.screen_h * 0.25f;          /* Lobby in lower third */
    game.zoom = 1.0f;
    
    /* Pre-build a small sample tower for visual testing */
    /* A few offices on floor 1 */
    for (int x = 20; x <= 40; x += ITEM_WIDTH[ITEM_OFFICE]) {
        tower_place(&game.tower, ITEM_OFFICE, 1, x);
    }
    /* Condos on floor 2 */
    for (int x = 20; x <= 40; x += ITEM_WIDTH[ITEM_CONDO]) {
        tower_place(&game.tower, ITEM_CONDO, 2, x);
    }
    /* A restaurant */
    tower_place(&game.tower, ITEM_RESTAURANT, 1, 8);
    
    printf("\n=== SimTower for Linux running ===\n");
    printf("Controls:\n");
    printf("  Arrow keys / WASD: scroll camera\n");
    printf("  Mouse wheel: scroll vertically\n");
    printf("  1-6: select building type\n");
    printf("  Left click: place building\n");
    printf("  0: deselect (no build)\n");
    printf("  Q/Escape: quit\n\n");
    
    /* Auto-screenshot mode for headless testing */
    int auto_screenshot = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0) auto_screenshot = 1;
    }
    
    /* Main loop */
    game.running = 1;
    game.build_type = ITEM_OFFICE;
    
    int frame = 0;
    while (game.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            handle_event(&ev);
        }
        render();
        frame++;
        
        /* Auto-screenshot after a few frames (let rendering stabilize) */
        if (auto_screenshot && frame == 3) {
            SDL_Surface *sshot = SDL_CreateRGBSurface(0, game.screen_w, game.screen_h, 32,
                0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
            SDL_RenderReadPixels(game.renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                 sshot->pixels, sshot->pitch);
            SDL_SaveBMP(sshot, "/tmp/simtower_screenshot.bmp");
            SDL_FreeSurface(sshot);
            printf("Auto-screenshot saved.\n");
            game.running = 0;
        }
    }
    
    /* Cleanup */
    sprites_free(&game.sprites);
    SDL_DestroyRenderer(game.renderer);
    SDL_DestroyWindow(game.window);
    SDL_Quit();
    ne_free(&game.exe);
    
    printf("Exited cleanly.\n");
    return 0;
}
