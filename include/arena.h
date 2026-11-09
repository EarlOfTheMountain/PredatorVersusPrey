/* pvp -- arena: Tiled (.tmj) maps, collision probes, placeholder art.
 *
 * A map is authored in Tiled (https://www.mapeditor.org) and exported as
 * JSON: File -> Export As -> "Tiled JSON map files (*.tmj)". Conventions:
 *
 *   - the FIRST tile layer is the collision layer: every painted tile is
 *     a solid wall, every empty cell is walkable floor
 *   - an object layer carries two objects named "p1" and "p2" (points or
 *     rectangles); their CENTER is where the player spawns
 *   - any tile size works, the map is centered inside the 320x180 view;
 *     16 px tiles on a 20x11 grid (320x176) is the sweet spot
 *   - anything outside the map counts as solid, so border your arena
 *
 * The JSON reader lives in arena.c so the game keeps zero dependencies.
 * Tiles are placeholder rects for now; when sprite sheets land, only
 * arena_render() changes.
 */
#ifndef PD_ARENA_H
#define PD_ARENA_H

#include <SDL3/SDL.h>
#include "config.h"

typedef struct {
        bool tiled;              /* false = built-in legacy wall ring */
        int tw, th;              /* tile size in px */
        int w, h;                /* map size in tiles */
        float offx, offy;        /* map top-left inside the 320x180 view */
        bool *solid;             /* w*h cells, NULL when !tiled */
        SDL_FRect p1_spawn;      /* view-space rects, player-sized */
        SDL_FRect p2_spawn;
        SDL_FRect legacy_bounds; /* used when !tiled */
} Arena;

/* Built-in arena: the classic 8px wall ring, no file needed. */
bool arena_init_default(Arena *a);

/* Load a Tiled JSON map from a file (or straight from memory, which the
 * selftest uses). On failure the reason is written into err. */
bool arena_load_tmj_file(Arena *a, const char *path, char *err, size_t errlen);
bool arena_load_tmj_buf(Arena *a, const char *json, size_t len,
                        char *err, size_t errlen);

void arena_free(Arena *a);

/* True if this rect overlaps a wall (or leaves the map). */
bool arena_rect_solid(const Arena *a, SDL_FRect r);

void arena_render(const Arena *a, SDL_Renderer *r);

#endif /* PD_ARENA_H */
