#include <stdio.h>
#include <SDL.h>
#include "src/ne_resource.h"
#include "src/sprites.h"

int main(int argc, char *argv[]) {
    if (argc < 3) { fprintf(stderr, "Usage: dump_sprite EXE ID [output.bmp]\n"); return 1; }
    
    NEResourceTable exe;
    if (ne_load(&exe, argv[1]) != 0) { fprintf(stderr, "Failed to load %s\n", argv[1]); return 1; }
    
    uint16_t id = (uint16_t)strtol(argv[2], NULL, 0);
    const char *out = argc > 3 ? argv[3] : "/tmp/sprite_dump.bmp";
    
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *win = SDL_CreateWindow("dump", 0, 0, 32, 32, SDL_WINDOW_HIDDEN);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    
    SpriteAtlas atlas;
    sprites_init(&atlas, &exe, ren);
    
    Sprite *s = sprites_find(&atlas, id);
    if (!s) { fprintf(stderr, "Sprite 0x%04x not found\n", id); return 1; }
    
    printf("Sprite 0x%04x: %dx%d\n", id, s->w, s->h);
    
    /* Render to a surface scaled 4x */
    int scale = 4;
    SDL_Texture *target = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, 
                                             SDL_TEXTUREACCESS_TARGET, s->w * scale, s->h * scale);
    SDL_SetRenderTarget(ren, target);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_Rect dst = {0, 0, s->w * scale, s->h * scale};
    SDL_RenderCopy(ren, s->texture, NULL, &dst);
    
    SDL_Surface *surf = SDL_CreateRGBSurface(0, s->w * scale, s->h * scale, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch);
    SDL_SaveBMP(surf, out);
    SDL_FreeSurface(surf);
    
    printf("Saved to %s (%dx%d @ %dx)\n", out, s->w, s->h, scale);
    
    sprites_free(&atlas);
    SDL_DestroyTexture(target);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    ne_free(&exe);
    return 0;
}
