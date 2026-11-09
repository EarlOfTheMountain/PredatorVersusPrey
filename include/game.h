/* pvp -- game: arena state, combat resolution, rendering. */
#ifndef PD_GAME_H
#define PD_GAME_H

#include <SDL3/SDL.h>
#include "arena.h"
#include "input.h"
#include "player.h"

typedef enum { ST_PLAYING, ST_ROUND_OVER } GameState;

typedef struct {
        Arena arena;     /* loaded before the first game_reset() */
        Player players[2];
        GameState state;
        int winner;      /* 0 or 1 when state == ST_ROUND_OVER */
        float hitstop_t; /* global freeze seconds left (makes hits feel heavy) */
        float shake_t;   /* screen shake seconds left */
} Game;

void game_reset(Game *g);
/* Pass override_inputs for scripted input (selftest); NULL polls devices. */
void game_update(Game *g, const InputState *override_inputs, float dt);
void game_render(Game *g, SDL_Renderer *r);

#endif /* PD_GAME_H */
