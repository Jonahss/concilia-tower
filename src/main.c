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
/* Sky background tiles (32×360 each, palette-swapped for time of day)
 * 0x8351-0x835b are 11 sky column strips, tiled horizontally.
 * 0x8352 is the second strip (with clouds/rain marks). */
#define SPR_SKY_BASE    0x8351
#define SPR_SKY_COUNT   11

/* Lobby: 0x8468/0x8469 — 640×36 (80 cells, multiple states) */
#define SPR_LOBBY_DAY   0x8468
#define SPR_LOBBY_NIGHT 0x8469

/* Office: 0x85A8-0x85AB — 288×24 (36 cells) */
#define SPR_OFFICE_BASE 0x85a8

/* Condo: 0x8628 + i*5 — series of 128×24 sprites */
#define SPR_CONDO_BASE  0x8628

/* Floor/ceiling */
#define SPR_FLOOR_CEIL  0x8428
#define SPR_FLOOR_WALK  0x842b

/* Restaurant: 0x8568 + i*2 — 384×24 (48 cells) */
#define SPR_RESTAURANT_BASE 0x8568

/* Fast food: 0x85e8 */
#define SPR_FASTFOOD_BASE 0x85e8

/* Hotel single: 0x84A8 + i*2 */
#define SPR_HOTEL_S_BASE 0x84a8

/* Hotel double: 0x84E8 + i*2 */
#define SPR_HOTEL_D_BASE 0x84e8

/* Underground dirt */
#define SPR_UNDERGROUND  0x8f28

/* Clouds */
#define SPR_CLOUD_BASE   0x8258

/* Elevator shaft */
#define SPR_ELEVATOR_BASE 0x8351

/* Stairs */
#define SPR_STAIRS_BASE   0x88e8

/* Escalator */
#define SPR_ESCALATOR_BASE 0x8928

/* UI */
#define SPR_TOOLBAR      0x8140

/* ---------- Sprite mapping for item types ---------- */
/* Sprite mapping: returns sprite ID and the pixel width of one frame */
static uint16_t item_sprite_id(ItemType type, int *frame_w)
{
    switch (type) {
    case ITEM_LOBBY:         *frame_w = 640; return SPR_LOBBY_DAY;
    case ITEM_OFFICE:        *frame_w = 288; return SPR_OFFICE_BASE;
    case ITEM_CONDO:         *frame_w = 128; return SPR_CONDO_BASE;
    case ITEM_HOTEL_SINGLE:  *frame_w = 128; return SPR_HOTEL_S_BASE;
    case ITEM_RESTAURANT:    *frame_w = 384; return SPR_RESTAURANT_BASE;
    case ITEM_FAST_FOOD:     *frame_w = 288; return SPR_FASTFOOD_BASE;
    case ITEM_STAIRS:        *frame_w = 200; return SPR_STAIRS_BASE;
    case ITEM_ESCALATOR:     *frame_w = 200; return SPR_ESCALATOR_BASE;
    case ITEM_ELEVATOR_SHAFT:*frame_w = 32;  return SPR_ELEVATOR_BASE;
    case ITEM_FLOOR:         *frame_w = 336; return SPR_FLOOR_WALK;
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
    
    /* Try to use sky tile sprites (32×360 each, tile horizontally) */
    /* Use sprite 0x8352 which has the blue sky appearance */
    Sprite *sky = sprites_find(&game.sprites, 0x8352);
    
    if (sky) {
        /* Tile sky strips across the top of the screen */
        for (int x = 0; x < game.screen_w; x += sky->w) {
            /* Position sky so bottom aligns with top of lobby */
            int sy = lobby_sy - sky->h;
            SDL_Rect dst = { x, sy, sky->w, sky->h };
            SDL_RenderCopy(game.renderer, sky->texture, NULL, &dst);
            /* If sky doesn't reach top, tile upward */
            for (int y2 = sy - sky->h; y2 > -sky->h; y2 -= sky->h) {
                SDL_Rect dst2 = { x, y2, sky->w, sky->h };
                SDL_RenderCopy(game.renderer, sky->texture, NULL, &dst2);
            }
        }
    } else {
        /* Fallback: blue gradient */
        for (int y = 0; y < lobby_sy; y++) {
            int t = (y * 255) / (lobby_sy > 0 ? lobby_sy : 1);
            SDL_SetRenderDrawColor(game.renderer, 
                80 + t/3,    /* R: darker at top */
                150 + t/3,   /* G */
                220 + t/8,   /* B */
                255);
            SDL_RenderDrawLine(game.renderer, 0, y, game.screen_w, y);
        }
    }
    
    /* Underground: brown gradient below lobby */
    Sprite *underground = sprites_find(&game.sprites, SPR_UNDERGROUND);
    if (underground) {
        for (int x = 0; x < game.screen_w; x += underground->w) {
            for (int y = lobby_sy + CELL_H; y < game.screen_h; y += underground->h) {
                SDL_Rect dst = { x, y, underground->w, underground->h };
                SDL_RenderCopy(game.renderer, underground->texture, NULL, &dst);
            }
        }
    } else {
        /* Fallback brown underground */
        for (int y = lobby_sy + CELL_H; y < game.screen_h; y++) {
            int depth = y - lobby_sy;
            int shade = 139 - depth / 8;
            if (shade < 40) shade = 40;
            SDL_SetRenderDrawColor(game.renderer, shade, shade - 20, shade - 38, 255);
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
    top_floor += 2;  /* Some margin */
    bot_floor -= 2;
    
    if (top_floor > TOWER_MAX_FLOOR) top_floor = TOWER_MAX_FLOOR;
    if (bot_floor < TOWER_MIN_FLOOR) bot_floor = TOWER_MIN_FLOOR;
    
    /* Render each visible floor */
    for (int floor = bot_floor; floor <= top_floor; floor++) {
        int fidx = floor_to_index(floor);
        if (fidx < 0 || fidx >= TOWER_FLOOR_COUNT) continue;
        
        int sx_base, sy_base;
        grid_to_screen(floor, 0, &sx_base, &sy_base);
        
        for (int x = 0; x < TOWER_WIDTH; ) {
            TowerCell *cell = &game.tower.grid[fidx][x];
            
            if (cell->type == ITEM_NONE) {
                x++;
                continue;
            }
            
            /* Only render from the leftmost cell of each tenant */
            if (cell->cell_index != 0) {
                x++;
                continue;
            }
            
            Tenant *tenant = tower_tenant(&game.tower, cell->tenant_id);
            if (!tenant) { x++; continue; }
            
            int frame_w_hint = 0;
            uint16_t spr_id = item_sprite_id(tenant->type, &frame_w_hint);
            Sprite *spr = spr_id ? sprites_find(&game.sprites, spr_id) : NULL;
            
            int tx, ty;
            grid_to_screen(floor, tenant->x, &tx, &ty);
            int tw = tenant->width * CELL_W;
            int th = CELL_H;
            
            if (spr) {
                if (tenant->type == ITEM_LOBBY) {
                    /* Lobby: tile the full sprite across the lobby width */
                    int lobby_pw = TOWER_WIDTH * CELL_W;
                    for (int lx = 0; lx < lobby_pw; lx += spr->w) {
                        int lsx = sx_base + lx;
                        if (lsx + spr->w < 0 || lsx > game.screen_w) continue;
                        /* Use full sprite height (which may differ from CELL_H) */
                        SDL_Rect dst = { lsx, sy_base, spr->w, spr->h };
                        SDL_RenderCopy(game.renderer, spr->texture, NULL, &dst);
                    }
                } else {
                    /* For other items: extract one frame from the sprite sheet.
                     * Sprite sheets have multiple frames packed horizontally.
                     * We take just the first tw pixels as one frame. */
                    SDL_Rect src = { 0, 0, tw, spr->h };
                    if (src.w > spr->w) src.w = spr->w;
                    SDL_Rect dst = { tx, ty, tw, spr->h };
                    SDL_RenderCopy(game.renderer, spr->texture, &src, &dst);
                }
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
                SDL_Rect rect = { tx, ty, tw, th };
                SDL_RenderFillRect(game.renderer, &rect);
                /* Border */
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
    
    /* Initialize tower */
    tower_init(&game.tower);
    
    /* Pre-build a sample tower for visual testing */
    /* Add some floors of offices above the lobby */
    for (int f = 1; f <= 5; f++) {
        for (int x = 8; x <= 48; x += ITEM_WIDTH[ITEM_OFFICE]) {
            tower_place(&game.tower, ITEM_OFFICE, f, x);
        }
    }
    /* Add some condos */
    for (int x = 8; x <= 48; x += ITEM_WIDTH[ITEM_CONDO]) {
        tower_place(&game.tower, ITEM_CONDO, 6, x);
        tower_place(&game.tower, ITEM_CONDO, 7, x);
    }
    /* A restaurant on floor 3 */
    tower_place(&game.tower, ITEM_RESTAURANT, 1, 0);
    /* Fast food */
    tower_place(&game.tower, ITEM_FAST_FOOD, 1, 52);
    
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
