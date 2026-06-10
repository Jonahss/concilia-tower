/* sprites.h - Sprite atlas for SimTower assets
 *
 * Loads all game sprites from SIMTOWER.EXE resources and provides
 * lookup by resource ID. Handles both DIB and raw bitmap formats.
 */
#ifndef SPRITES_H
#define SPRITES_H

#include <SDL.h>
#include "ne_resource.h"

/* Maximum sprites we'll ever need */
#define MAX_SPRITES 512

typedef struct {
    uint16_t     id;        /* Resource ID (e.g., 0x8568 = lobby) */
    uint16_t     type;      /* Resource type (0x8002 = DIB, 0xFF02 = raw) */
    SDL_Texture *texture;   /* GPU texture for rendering */
    int          w, h;      /* Dimensions in pixels */
} Sprite;

typedef struct {
    Sprite   sprites[MAX_SPRITES];
    int      count;
    
    /* Palette for raw bitmap decoding */
    uint8_t  palette[256][3];  /* [index][R,G,B] */
    int      palette_loaded;
} SpriteAtlas;

/* Initialize atlas and load all sprites from the NE resource table.
 * renderer is needed to create GPU textures. */
int sprites_init(SpriteAtlas *atlas, NEResourceTable *exe, SDL_Renderer *renderer);

/* Find a sprite by resource ID. Returns NULL if not found. */
Sprite *sprites_find(SpriteAtlas *atlas, uint16_t id);

/* Re-decode a DIB resource to an SDL_Surface (caller must free).
 * Useful for applying color keys before texture creation. */
SDL_Surface *sprites_dib_to_surface(SpriteAtlas *atlas, NEResource *res);

/* Render a sprite at screen position (x,y).
 * If src_rect is NULL, draws the entire sprite. */
void sprites_draw(SDL_Renderer *renderer, Sprite *sprite, int x, int y, SDL_Rect *src_rect);

/* Render a sprite scaled */
void sprites_draw_scaled(SDL_Renderer *renderer, Sprite *sprite, 
                         int x, int y, int w, int h, SDL_Rect *src_rect);

/* Recreate a sprite's texture with a color as transparent (color key).
 * Returns 0 on success. */
int sprites_apply_color_key(SpriteAtlas *atlas, SDL_Renderer *renderer,
                            uint16_t id, uint8_t r, uint8_t g, uint8_t b);
int sprites_apply_white_key(SpriteAtlas *atlas, SDL_Renderer *renderer,
                            uint16_t id);

/* Compose a new sprite by joining two existing sprites horizontally.
 * The new sprite is stored with new_id. Returns 0 on success. */
int sprites_compose_h(SpriteAtlas *atlas, SDL_Renderer *renderer,
                      uint16_t id_left, uint16_t id_right, uint16_t new_id);

/* Compose a new sprite by joining two existing sprites vertically.
 * The new sprite is stored with new_id. Returns 0 on success. */
int sprites_compose_v(SpriteAtlas *atlas, SDL_Renderer *renderer,
                      uint16_t id_top, uint16_t id_bottom, uint16_t new_id);

/* Free all textures */
void sprites_free(SpriteAtlas *atlas);

#endif /* SPRITES_H */
