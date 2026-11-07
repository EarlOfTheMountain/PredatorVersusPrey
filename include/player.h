/* pixelduel -- player: movement, facing, stab and dodge roll. */
#ifndef PD_PLAYER_H
#define PD_PLAYER_H

#include <SDL3/SDL.h>
#include "config.h"
#include "input.h"

/* 4-direction facing, Zelda-style */
typedef enum { DIR_DOWN, DIR_UP, DIR_LEFT, DIR_RIGHT } Dir;

typedef struct {
        SDL_FRect pos; /* x,y = top-left in arena pixels; w,h = hitbox size */
        Dir facing;
        int hp;

        /* dodge roll (proto GW2 dodge: i-frames now, energy cost later) */
        float dash_t;  /* seconds of roll left */
        float dash_cd; /* seconds until the roll is ready */
        Dir dash_dir;

        /* sword stab */
        float attack_anim_t; /* seconds of stab animation left */
        float attack_cd;     /* seconds until the stab is ready */
        bool attack_hit;     /* a stab only hits once */
        Dir attack_dir;      /* facing captured when the stab started */

        /* feedback */
        float iframes;   /* invulnerable seconds left (hurt or rolling) */
        float hit_flash; /* white flash seconds left */
        SDL_FPoint kb;   /* knockback velocity, decays over time */
} Player;

void player_spawn(Player *p, float x, float y);
void player_update(Player *p, const InputState *in, float dt, const SDL_FRect *bounds);
/* Unit vector for a facing direction (also used for knockback). */
SDL_FPoint player_dir_vec(Dir d);
/* The stab's hitbox; meaningful while player_attack_active() is true. */
SDL_FRect player_attack_hitbox(const Player *p);
bool player_attack_active(const Player *p);

#endif /* PD_PLAYER_H */
