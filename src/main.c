/* SimTower for Linux - main.c
 *
 * A native Linux port of SimTower, using game mechanics extracted from
 * decompilation of SIMTOWER.EXE and guided by the YootTower code map.
 *
 * Platform: SDL2 (graphics, sound, input)
 * Assets: loaded from original SIMTOWER.EXE (NE format)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "ne_resource.h"

/* ---------- Constants from the original game ---------- */
#define TOWER_MAX_FLOORS_ABOVE  15   /* Ground + 14 above (up to 100 in later stars) */
#define TOWER_MAX_FLOORS_BELOW  9    /* B1 through B9 */
#define TOWER_MAX_WIDTH         63   /* Units (each unit = 8 pixels) */
#define CELL_WIDTH              8    /* Pixels per horizontal cell */
#define CELL_HEIGHT             36   /* Pixels per floor */
#define LOBBY_FLOOR             0

/* Window dimensions */
#define WINDOW_W    800
#define WINDOW_H    600

/* ---------- Resource IDs for key bitmaps ---------- */
/* These come from OpenSkyscraper's SimTowerLoader analysis */
#define BMP_LOBBY       0x8568   /* Lobby bitmap */
#define BMP_SKY         0x8200   /* Sky background */
#define BMP_UNDERGROUND 0x8201   /* Underground background */

/* ---------- Palette ---------- */
/* SimTower uses a 256-color indexed palette, stored in resource 0x83e8 
 * of type 0xFF03. Each entry is 8 bytes: 
 *   [2 bytes index] [2 bytes R] [2 bytes G] [2 bytes B]
 * where only the low byte of each color matters. */

typedef struct {
    uint8_t r, g, b;
} PaletteEntry;

static PaletteEntry palette[256];
static int palette_loaded = 0;

static int load_palette(NEResourceTable *exe)
{
    NEResource *pal = ne_find(exe, 0xFF03, 0x83e8);
    if (!pal) {
        fprintf(stderr, "Palette resource 0x83e8 not found\n");
        return -1;
    }
    
    /* Each entry is 8 bytes in the resource */
    int entry_count = pal->length / 8;
    if (entry_count > 256) entry_count = 256;
    
    for (int i = 0; i < entry_count; i++) {
        uint8_t *p = pal->data + i * 8;
        /* Byte layout per OpenSkyscraper: [idx_lo idx_hi] [r_lo r_hi] [g_lo g_hi] [b_lo b_hi]
         * But the BMP palette assembly in OpenSkyscraper reads:
         *   B = data[i*8+6], G = data[i*8+4], R = data[i*8+2]
         * So the bytes at +2,+4,+6 ARE R,G,B respectively */
        palette[i].r = p[2];
        palette[i].g = p[4];
        palette[i].b = p[6];
    }
    
    /* Debug: check if the palette looks sane (entry 0 should be near-black for SimTower) */
    printf("  Entry 0: R=%d G=%d B=%d\n", palette[0].r, palette[0].g, palette[0].b);
    printf("  Entry 255: R=%d G=%d B=%d\n", palette[255].r, palette[255].g, palette[255].b);
    
    palette_loaded = 1;
    printf("Loaded palette: %d entries\n", entry_count);
    return 0;
}

/* ---------- Bitmap conversion ---------- */
/* Convert a DIB resource (type 0x8002) to an SDL surface.
 * The NE resource contains a BITMAPINFOHEADER + palette + pixel data. */

static SDL_Surface *dib_to_surface(NEResource *res)
{
    if (!res || res->length < 40) return NULL;
    
    uint8_t *data = res->data;
    
    /* Read BITMAPINFOHEADER */
    uint32_t hdr_size  = *(uint32_t *)(data + 0);
    int32_t  width     = *(int32_t  *)(data + 4);
    int32_t  height    = *(int32_t  *)(data + 8);
    uint16_t bpp       = *(uint16_t *)(data + 14);
    
    if (hdr_size < 40 || bpp != 8) {
        fprintf(stderr, "Unsupported bitmap: hdr=%u bpp=%u\n", hdr_size, bpp);
        return NULL;
    }
    
    int abs_height = height < 0 ? -height : height;
    int top_down = height < 0;
    
    /* Color table follows the header */
    uint8_t *color_table = data + hdr_size;
    
    /* Pixel data follows the color table (256 entries × 4 bytes) */
    int row_bytes = (width + 3) & ~3;  /* Rows are DWORD-aligned */
    uint8_t *pixels = color_table + 256 * 4;
    
    /* Check we have enough data */
    int expected = (int)(hdr_size + 256 * 4 + row_bytes * abs_height);
    if (res->length < expected) {
        /* Try with palette from the global palette instead */
        pixels = color_table;  /* No local palette */
    }
    
    /* Create SDL surface */
    SDL_Surface *surf = SDL_CreateRGBSurface(0, width, abs_height, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surf) return NULL;
    
    SDL_LockSurface(surf);
    
    for (int y = 0; y < abs_height; y++) {
        /* DIB rows are stored bottom-up unless height is negative */
        int src_y = top_down ? y : (abs_height - 1 - y);
        uint8_t *src_row = pixels + src_y * row_bytes;
        uint32_t *dst_row = (uint32_t *)((uint8_t *)surf->pixels + y * surf->pitch);
        
        for (int x = 0; x < width; x++) {
            uint8_t idx = src_row[x];
            PaletteEntry *c;
            
            /* Use local color table if available, otherwise global palette */
            if (pixels != color_table) {
                uint8_t *ct = color_table + idx * 4;
                /* RGBQUAD is actually BGRA */
                dst_row[x] = (0xFF << 24) | (ct[2] << 16) | (ct[1] << 8) | ct[0];
            } else if (palette_loaded) {
                c = &palette[idx];
                dst_row[x] = (0xFF << 24) | (c->r << 16) | (c->g << 8) | c->b;
            } else {
                dst_row[x] = (0xFF << 24) | (idx << 16) | (idx << 8) | idx;
            }
        }
    }
    
    SDL_UnlockSurface(surf);
    return surf;
}

/* ---------- Raw bitmap conversion (type 0xFF02) ---------- */
/* These are raw 8-bit pixel arrays, 8 pixels wide, with cells stacked vertically.
 * Each cell is 8×36 pixels. */
static SDL_Surface *raw_bitmap_to_surface(NEResource *res)
{
    if (!res || !palette_loaded) return NULL;
    
    int cell_pixels = 36 * 8;
    int cell_count = res->length / cell_pixels;
    if (cell_count <= 0) return NULL;
    
    int width = cell_count * 8;
    int height = 36;
    
    SDL_Surface *surf = SDL_CreateRGBSurface(0, width, height, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surf) return NULL;
    
    SDL_LockSurface(surf);
    
    uint8_t *src = res->data;
    for (int i = 0; i < res->length; i++) {
        int srcx = i % 8;
        int srcy = i / 8;
        int dstx = srcx + (srcy / 36) * 8;
        int dsty = 35 - (srcy % 36);  /* Flip vertically */
        
        if (dstx < width && dsty < height) {
            PaletteEntry *c = &palette[src[i]];
            uint32_t *row = (uint32_t *)((uint8_t *)surf->pixels + dsty * surf->pitch);
            row[dstx] = (0xFF << 24) | (c->r << 16) | (c->g << 8) | c->b;
        }
    }
    
    SDL_UnlockSurface(surf);
    return surf;
}

/* ---------- Game state ---------- */
typedef struct {
    NEResourceTable exe;
    SDL_Window     *window;
    SDL_Renderer   *renderer;
    int             running;
    int             scroll_x;
    int             scroll_y;
    
    /* Resource catalog */
    int   bitmap_count;
    int   sound_count;
    int   raw_bitmap_count;
} GameState;

static GameState game;

/* ---------- Dump mode ---------- */
/* Save first N bitmaps as BMP files for visual verification */
static int dump_bitmaps(NEResourceTable *exe, const char *outdir, int max)
{
    char path[256];
    int saved = 0;
    
    NEResourceList *bitmaps = ne_find_type(exe, NE_RT_BITMAP);
    if (bitmaps) {
        for (int i = 0; i < bitmaps->count && saved < max; i++) {
            SDL_Surface *surf = dib_to_surface(&bitmaps->items[i]);
            if (surf) {
                snprintf(path, sizeof(path), "%s/bmp_%04x.bmp", outdir, bitmaps->items[i].id);
                SDL_SaveBMP(surf, path);
                printf("  Saved %s (%dx%d)\n", path, surf->w, surf->h);
                SDL_FreeSurface(surf);
                saved++;
            }
        }
    }
    
    NEResourceList *raw = ne_find_type(exe, NE_RT_RAWBITMAP);
    if (raw) {
        for (int i = 0; i < raw->count && saved < max; i++) {
            SDL_Surface *surf = raw_bitmap_to_surface(&raw->items[i]);
            if (surf) {
                snprintf(path, sizeof(path), "%s/raw_%04x.bmp", outdir, raw->items[i].id);
                SDL_SaveBMP(surf, path);
                printf("  Saved %s (%dx%d)\n", path, surf->w, surf->h);
                SDL_FreeSurface(surf);
                saved++;
            }
        }
    }
    
    return saved;
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
        "../simtower-decomp/../OpenSkyscraper/data/SIMTOWER.EXE",
        NULL
    };
    
    if (argc > 1) {
        exe_path = argv[1];
    } else {
        for (int i = 0; search_paths[i]; i++) {
            FILE *test = fopen(search_paths[i], "rb");
            if (test) {
                fclose(test);
                exe_path = search_paths[i];
                break;
            }
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
    
    /* Print resource summary */
    printf("\nResources loaded:\n");
    for (int i = 0; i < game.exe.type_count; i++) {
        printf("  Type 0x%04x: %d resources\n", 
               game.exe.type_ids[i], game.exe.types[i].count);
    }
    
    /* Load palette */
    load_palette(&game.exe);
    
    /* Count resources */
    NEResourceList *bitmaps = ne_find_type(&game.exe, NE_RT_BITMAP);
    NEResourceList *sounds = ne_find_type(&game.exe, NE_RT_SOUND);
    NEResourceList *raw_bitmaps = ne_find_type(&game.exe, NE_RT_RAWBITMAP);
    
    printf("\nAsset summary:\n");
    printf("  Bitmaps (DIB):  %d\n", bitmaps ? bitmaps->count : 0);
    printf("  Bitmaps (raw):  %d\n", raw_bitmaps ? raw_bitmaps->count : 0);
    printf("  Sounds:         %d\n", sounds ? sounds->count : 0);
    printf("  Palette:        %s\n", palette_loaded ? "yes" : "no");
    
    /* Dump mode: --dump <dir> saves bitmaps and exits */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
            if (SDL_Init(SDL_INIT_VIDEO) != 0) {
                fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
                return 1;
            }
            printf("\nDumping bitmaps to %s...\n", argv[i+1]);
            int n = dump_bitmaps(&game.exe, argv[i+1], 999);
            printf("Saved %d bitmaps.\n", n);
            ne_free(&game.exe);
            SDL_Quit();
            return 0;
        }
    }
    
    /* Initialize SDL */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        ne_free(&game.exe);
        return 1;
    }
    
    game.window = SDL_CreateWindow("SimTower for Linux",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!game.window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    
    game.renderer = SDL_CreateRenderer(game.window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!game.renderer) {
        /* Fallback to software */
        game.renderer = SDL_CreateRenderer(game.window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!game.renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(game.window);
        SDL_Quit();
        return 1;
    }
    
    /* Try to render a bitmap as proof of life */
    SDL_Texture *test_tex = NULL;
    if (bitmaps && bitmaps->count > 0) {
        /* Find the lobby bitmap or just use the first available */
        NEResource *lobby = ne_find(&game.exe, NE_RT_BITMAP, 0x8568);
        NEResource *test_res = lobby ? lobby : &bitmaps->items[0];
        
        printf("\nRendering test bitmap: type=0x%x id=0x%x (%d bytes)\n",
               test_res->type, test_res->id, test_res->length);
        
        SDL_Surface *surf = dib_to_surface(test_res);
        if (surf) {
            printf("  → %dx%d surface created\n", surf->w, surf->h);
            test_tex = SDL_CreateTextureFromSurface(game.renderer, surf);
            SDL_FreeSurface(surf);
        }
    }
    
    /* Also try a raw bitmap */
    SDL_Texture *raw_tex = NULL;
    if (raw_bitmaps && raw_bitmaps->count > 0) {
        NEResource *test_raw = &raw_bitmaps->items[0];
        printf("Rendering test raw bitmap: id=0x%x (%d bytes)\n",
               test_raw->id, test_raw->length);
        
        SDL_Surface *surf = raw_bitmap_to_surface(test_raw);
        if (surf) {
            printf("  → %dx%d surface created\n", surf->w, surf->h);
            raw_tex = SDL_CreateTextureFromSurface(game.renderer, surf);
            SDL_FreeSurface(surf);
        }
    }
    
    /* Main loop */
    printf("\n=== SimTower for Linux running ===\n");
    printf("Controls: Arrow keys to scroll, Q/Escape to quit\n\n");
    
    game.running = 1;
    game.scroll_x = WINDOW_W / 2;
    game.scroll_y = WINDOW_H / 2;
    
    while (game.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                game.running = 0;
                break;
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_q:
                case SDLK_ESCAPE:
                    game.running = 0;
                    break;
                case SDLK_LEFT:  game.scroll_x -= 20; break;
                case SDLK_RIGHT: game.scroll_x += 20; break;
                case SDLK_UP:    game.scroll_y -= 20; break;
                case SDLK_DOWN:  game.scroll_y += 20; break;
                default: break;
                }
                break;
            }
        }
        
        /* Clear to sky blue */
        SDL_SetRenderDrawColor(game.renderer, 135, 206, 235, 255);
        SDL_RenderClear(game.renderer);
        
        /* Draw ground (brown below lobby level) */
        SDL_Rect ground = { 0, WINDOW_H / 2, WINDOW_W, WINDOW_H / 2 };
        SDL_SetRenderDrawColor(game.renderer, 139, 119, 101, 255);
        SDL_RenderFillRect(game.renderer, &ground);
        
        /* Draw lobby line */
        SDL_SetRenderDrawColor(game.renderer, 200, 200, 200, 255);
        SDL_RenderDrawLine(game.renderer, 0, WINDOW_H / 2, WINDOW_W, WINDOW_H / 2);
        
        /* Draw test bitmap if we have one */
        if (test_tex) {
            int w, h;
            SDL_QueryTexture(test_tex, NULL, NULL, &w, &h);
            SDL_Rect dst = { 
                WINDOW_W / 2 - w / 2 - game.scroll_x + WINDOW_W / 2,
                WINDOW_H / 2 - h,
                w, h 
            };
            SDL_RenderCopy(game.renderer, test_tex, NULL, &dst);
        }
        
        /* Draw raw bitmap below */
        if (raw_tex) {
            int w, h;
            SDL_QueryTexture(raw_tex, NULL, NULL, &w, &h);
            SDL_Rect dst = { 10, 10, w * 2, h * 2 };  /* 2x scale */
            SDL_RenderCopy(game.renderer, raw_tex, NULL, &dst);
        }
        
        /* Draw info text overlay */
        /* (We'll add SDL_ttf later, for now just render colored rects as placeholders) */
        
        SDL_RenderPresent(game.renderer);
    }
    
    /* Cleanup */
    if (test_tex) SDL_DestroyTexture(test_tex);
    if (raw_tex) SDL_DestroyTexture(raw_tex);
    SDL_DestroyRenderer(game.renderer);
    SDL_DestroyWindow(game.window);
    SDL_Quit();
    ne_free(&game.exe);
    
    printf("SimTower for Linux exited cleanly.\n");
    return 0;
}
