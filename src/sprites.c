/* sprites.c - Sprite atlas implementation */
#include "sprites.h"
#include <stdio.h>
#include <string.h>

/* ---------- Palette loading ---------- */
static int load_palette(SpriteAtlas *atlas, NEResourceTable *exe)
{
    NEResource *pal = ne_find(exe, 0xFF03, 0x83e8);
    if (!pal) return -1;
    
    int count = pal->length / 8;
    if (count > 256) count = 256;
    
    for (int i = 0; i < count; i++) {
        uint8_t *p = pal->data + i * 8;
        atlas->palette[i][0] = p[2]; /* R */
        atlas->palette[i][1] = p[4]; /* G */
        atlas->palette[i][2] = p[6]; /* B */
    }
    
    atlas->palette_loaded = 1;
    return 0;
}

/* ---------- DIB → SDL_Surface ---------- */
SDL_Surface *dib_to_surface(NEResource *res, SpriteAtlas *atlas)
{
    if (!res || res->length < 40) return NULL;
    
    uint8_t *data = res->data;
    uint32_t hdr_size = *(uint32_t *)(data + 0);
    int32_t  width    = *(int32_t  *)(data + 4);
    int32_t  height   = *(int32_t  *)(data + 8);
    uint16_t bpp      = *(uint16_t *)(data + 14);
    
    if (hdr_size < 40 || bpp != 8) return NULL;
    
    int abs_height = height < 0 ? -height : height;
    int top_down = height < 0;
    
    uint8_t *color_table = data + hdr_size;
    int row_bytes = (width + 3) & ~3;
    uint8_t *pixels = color_table + 256 * 4;
    
    /* Check if we have enough data for local palette + pixels */
    int has_local_palette = (res->length >= (int)(hdr_size + 256 * 4 + row_bytes * abs_height));
    if (!has_local_palette) {
        pixels = color_table; /* No room for local palette */
    }
    
    SDL_Surface *surf = SDL_CreateRGBSurface(0, width, abs_height, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surf) return NULL;
    
    SDL_LockSurface(surf);
    for (int y = 0; y < abs_height; y++) {
        int src_y = top_down ? y : (abs_height - 1 - y);
        uint8_t *src_row = pixels + src_y * row_bytes;
        uint32_t *dst_row = (uint32_t *)((uint8_t *)surf->pixels + y * surf->pitch);
        
        for (int x = 0; x < width; x++) {
            uint8_t idx = src_row[x];
            if (has_local_palette) {
                uint8_t *ct = color_table + idx * 4;
                dst_row[x] = (0xFF << 24) | (ct[2] << 16) | (ct[1] << 8) | ct[0];
            } else if (atlas->palette_loaded) {
                dst_row[x] = (0xFF << 24) | 
                    (atlas->palette[idx][0] << 16) | 
                    (atlas->palette[idx][1] << 8) | 
                    atlas->palette[idx][2];
            } else {
                dst_row[x] = (0xFF << 24) | (idx << 16) | (idx << 8) | idx;
            }
        }
    }
    SDL_UnlockSurface(surf);
    return surf;
}

/* ---------- Raw bitmap → SDL_Surface ---------- */
static SDL_Surface *raw_to_surface(NEResource *res, SpriteAtlas *atlas)
{
    if (!res || !atlas->palette_loaded) return NULL;
    
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
        int dsty = srcy % 36;  /* No flip — SDL surfaces are top-down */
        
        if (dstx < width && dsty < height) {
            uint8_t *c = atlas->palette[src[i]];
            uint32_t *row = (uint32_t *)((uint8_t *)surf->pixels + dsty * surf->pitch);
            row[dstx] = (0xFF << 24) | (c[0] << 16) | (c[1] << 8) | c[2];
        }
    }
    SDL_UnlockSurface(surf);
    return surf;
}

/* ---------- Public API ---------- */

int sprites_init(SpriteAtlas *atlas, NEResourceTable *exe, SDL_Renderer *renderer)
{
    memset(atlas, 0, sizeof(*atlas));
    
    if (load_palette(atlas, exe) != 0) {
        fprintf(stderr, "sprites: warning: no palette found\n");
    }
    
    int loaded = 0;
    
    /* Load DIB bitmaps (type 0x8002) */
    NEResourceList *dibs = ne_find_type(exe, 0x8002);
    if (dibs) {
        for (int i = 0; i < dibs->count && atlas->count < MAX_SPRITES; i++) {
            SDL_Surface *surf = dib_to_surface(&dibs->items[i], atlas);
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                if (tex) {
                    Sprite *s = &atlas->sprites[atlas->count++];
                    s->id = dibs->items[i].id;
                    s->type = 0x8002;
                    s->texture = tex;
                    s->w = surf->w;
                    s->h = surf->h;
                    loaded++;
                }
                SDL_FreeSurface(surf);
            }
        }
    }
    
    /* Load raw bitmaps (type 0xFF02) */
    NEResourceList *raws = ne_find_type(exe, 0xFF02);
    if (raws) {
        for (int i = 0; i < raws->count && atlas->count < MAX_SPRITES; i++) {
            SDL_Surface *surf = raw_to_surface(&raws->items[i], atlas);
            if (surf) {
                SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
                if (tex) {
                    Sprite *s = &atlas->sprites[atlas->count++];
                    s->id = raws->items[i].id;
                    s->type = 0xFF02;
                    s->texture = tex;
                    s->w = surf->w;
                    s->h = surf->h;
                    loaded++;
                }
                SDL_FreeSurface(surf);
            }
        }
    }
    
    printf("Sprites: loaded %d textures (%d DIB + %d raw)\n", 
           loaded, dibs ? dibs->count : 0, raws ? raws->count : 0);
    return 0;
}

Sprite *sprites_find(SpriteAtlas *atlas, uint16_t id)
{
    for (int i = 0; i < atlas->count; i++) {
        if (atlas->sprites[i].id == id)
            return &atlas->sprites[i];
    }
    return NULL;
}

SDL_Surface *sprites_dib_to_surface(SpriteAtlas *atlas, NEResource *res)
{
    return dib_to_surface(res, atlas);
}

void sprites_draw(SDL_Renderer *renderer, Sprite *sprite, int x, int y, SDL_Rect *src_rect)
{
    if (!sprite || !sprite->texture) return;
    
    int w = src_rect ? src_rect->w : sprite->w;
    int h = src_rect ? src_rect->h : sprite->h;
    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(renderer, sprite->texture, src_rect, &dst);
}

void sprites_draw_scaled(SDL_Renderer *renderer, Sprite *sprite, 
                         int x, int y, int w, int h, SDL_Rect *src_rect)
{
    if (!sprite || !sprite->texture) return;
    SDL_Rect dst = { x, y, w, h };
    SDL_RenderCopy(renderer, sprite->texture, src_rect, &dst);
}

/* ---------- Sprite composition ---------- */

/* Helper: read a texture back into a surface */
static SDL_Surface *texture_to_surface(SDL_Renderer *renderer, SDL_Texture *tex, int w, int h)
{
    SDL_Surface *surf = SDL_CreateRGBSurface(0, w, h, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!surf) return NULL;
    
    SDL_Texture *target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET, w, h);
    if (!target) { SDL_FreeSurface(surf); return NULL; }
    
    SDL_SetRenderTarget(renderer, target);
    SDL_RenderCopy(renderer, tex, NULL, NULL);
    SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch);
    SDL_SetRenderTarget(renderer, NULL);
    SDL_DestroyTexture(target);
    return surf;
}

int sprites_apply_color_key(SpriteAtlas *atlas, SDL_Renderer *renderer,
                            uint16_t id, uint8_t r, uint8_t g, uint8_t b)
{
    Sprite *s = sprites_find(atlas, id);
    if (!s) return -1;
    SDL_Surface *surf = texture_to_surface(renderer, s->texture, s->w, s->h);
    if (!surf) return -1;
    SDL_SetColorKey(surf, SDL_TRUE, SDL_MapRGB(surf->format, r, g, b));
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) return -1;
    SDL_DestroyTexture(s->texture);
    s->texture = tex;
    return 0;
}

int sprites_apply_white_key(SpriteAtlas *atlas, SDL_Renderer *renderer,
                            uint16_t id)
{
    return sprites_apply_color_key(atlas, renderer, id, 0xFF, 0xFF, 0xFF);
}

int sprites_compose_h(SpriteAtlas *atlas, SDL_Renderer *renderer,
                      uint16_t id_left, uint16_t id_right, uint16_t new_id)
{
    Sprite *left = sprites_find(atlas, id_left);
    Sprite *right = sprites_find(atlas, id_right);
    if (!left || !right || atlas->count >= MAX_SPRITES) return -1;
    
    int w = left->w + right->w;
    int h = left->h > right->h ? left->h : right->h;
    
    SDL_Surface *lsurf = texture_to_surface(renderer, left->texture, left->w, left->h);
    SDL_Surface *rsurf = texture_to_surface(renderer, right->texture, right->w, right->h);
    if (!lsurf || !rsurf) {
        if (lsurf) SDL_FreeSurface(lsurf);
        if (rsurf) SDL_FreeSurface(rsurf);
        return -1;
    }
    
    SDL_Surface *combined = SDL_CreateRGBSurface(0, w, h, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    SDL_Rect ldst = { 0, 0, left->w, left->h };
    SDL_Rect rdst = { left->w, 0, right->w, right->h };
    SDL_BlitSurface(lsurf, NULL, combined, &ldst);
    SDL_BlitSurface(rsurf, NULL, combined, &rdst);
    SDL_FreeSurface(lsurf);
    SDL_FreeSurface(rsurf);
    
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, combined);
    SDL_FreeSurface(combined);
    if (!tex) return -1;
    
    Sprite *s = &atlas->sprites[atlas->count++];
    s->id = new_id;
    s->type = 0x0001; /* composite */
    s->texture = tex;
    s->w = w;
    s->h = h;
    
    return 0;
}

int sprites_compose_v(SpriteAtlas *atlas, SDL_Renderer *renderer,
                      uint16_t id_top, uint16_t id_bottom, uint16_t new_id)
{
    Sprite *top = sprites_find(atlas, id_top);
    Sprite *bot = sprites_find(atlas, id_bottom);
    if (!top || !bot || atlas->count >= MAX_SPRITES) return -1;
    
    int w = top->w > bot->w ? top->w : bot->w;
    int h = top->h + bot->h;
    
    SDL_Surface *tsurf = texture_to_surface(renderer, top->texture, top->w, top->h);
    SDL_Surface *bsurf = texture_to_surface(renderer, bot->texture, bot->w, bot->h);
    if (!tsurf || !bsurf) {
        if (tsurf) SDL_FreeSurface(tsurf);
        if (bsurf) SDL_FreeSurface(bsurf);
        return -1;
    }
    
    SDL_Surface *combined = SDL_CreateRGBSurface(0, w, h, 32,
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    SDL_Rect tdst = { 0, 0, top->w, top->h };
    SDL_Rect bdst = { 0, top->h, bot->w, bot->h };
    SDL_BlitSurface(tsurf, NULL, combined, &tdst);
    SDL_BlitSurface(bsurf, NULL, combined, &bdst);
    SDL_FreeSurface(tsurf);
    SDL_FreeSurface(bsurf);
    
    /* Apply white transparency mask (stairs, escalators use white as transparent) */
    SDL_SetColorKey(combined, SDL_TRUE, SDL_MapRGB(combined->format, 0xFF, 0xFF, 0xFF));
    
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, combined);
    SDL_FreeSurface(combined);
    if (!tex) return -1;
    
    Sprite *s = &atlas->sprites[atlas->count++];
    s->id = new_id;
    s->type = 0x0001; /* composite */
    s->texture = tex;
    s->w = w;
    s->h = h;
    
    return 0;
}

void sprites_free(SpriteAtlas *atlas)
{
    for (int i = 0; i < atlas->count; i++) {
        if (atlas->sprites[i].texture)
            SDL_DestroyTexture(atlas->sprites[i].texture);
    }
    memset(atlas, 0, sizeof(*atlas));
}
