#include "player.h"

SDL_FPoint player_dir_vec(Dir d)
{
        switch (d) {
        case DIR_UP:
                return (SDL_FPoint){ 0.0f, -1.0f };
        case DIR_DOWN:
                return (SDL_FPoint){ 0.0f, 1.0f };
        case DIR_LEFT:
                return (SDL_FPoint){ -1.0f, 0.0f };
        case DIR_RIGHT:
                return (SDL_FPoint){ 1.0f, 0.0f };
        }
        return (SDL_FPoint){ 0.0f, 0.0f };
}

void player_spawn(Player *p, float x, float y)
{
        SDL_zerop(p);
        p->pos.x = x;
        p->pos.y = y;
        p->pos.w = PD_PLAYER_SIZE;
        p->pos.h = PD_PLAYER_SIZE;
        p->facing = DIR_DOWN;
        p->hp = PD_PLAYER_MAX_HP;
}

void player_update(Player *p, const InputState *in, float dt, const SDL_FRect *bounds)
{
        /* 1) tick timers down, never below zero */
        p->dash_cd = SDL_max(0.0f, p->dash_cd - dt);
        p->attack_cd = SDL_max(0.0f, p->attack_cd - dt);
        p->attack_anim_t = SDL_max(0.0f, p->attack_anim_t - dt);
        p->iframes = SDL_max(0.0f, p->iframes - dt);
        p->hit_flash = SDL_max(0.0f, p->hit_flash - dt);

        /* 2) desired direction from input */
        const int dx = (in->right ? 1 : 0) - (in->left ? 1 : 0);
        const int dy = (in->down ? 1 : 0) - (in->up ? 1 : 0);

        /* facing: horizontal wins ties, so you aim where you last pressed */
        if (dx > 0) {
                p->facing = DIR_RIGHT;
        } else if (dx < 0) {
                p->facing = DIR_LEFT;
        } else if (dy > 0) {
                p->facing = DIR_DOWN;
        } else if (dy < 0) {
                p->facing = DIR_UP;
        }

        /* 3) dodge roll trigger (TODO later: require energy, GW2-style) */
        if (in->dash && p->dash_cd <= 0.0f && p->dash_t <= 0.0f && p->attack_anim_t <= 0.0f) {
                p->dash_t = PD_DASH_TIME;
                p->dash_cd = PD_DASH_COOLDOWN;
                p->dash_dir = p->facing; /* facing already reflects held input */
                p->iframes = SDL_max(p->iframes, PD_DASH_IFRAMES);
                p->attack_anim_t = 0.0f; /* a roll cancels a stab wind-up */
        }

        /* 4) stab trigger */
        if (in->attack && p->attack_cd <= 0.0f && p->dash_t <= 0.0f) {
                p->attack_anim_t = PD_ATTACK_ANIM;
                p->attack_cd = PD_ATTACK_COOLDOWN;
                p->attack_hit = false;
                p->attack_dir = p->facing;
        }

        /* 5) velocity: an active roll overrides walking */
        SDL_FPoint vel = { 0.0f, 0.0f };
        if (p->dash_t > 0.0f) {
                p->dash_t = SDL_max(0.0f, p->dash_t - dt);
                const SDL_FPoint dv = player_dir_vec(p->dash_dir);
                vel.x = dv.x * PD_DASH_SPEED;
                vel.y = dv.y * PD_DASH_SPEED;
        } else if (dx != 0 || dy != 0) {
                const float len = SDL_sqrtf((float)(dx * dx + dy * dy));
                vel.x = (float)dx / len * PD_PLAYER_SPEED;
                vel.y = (float)dy / len * PD_PLAYER_SPEED;
        }

        /* 6) integrate position + knockback; knockback decays each tick */
        p->pos.x += (vel.x + p->kb.x) * dt;
        p->pos.y += (vel.y + p->kb.y) * dt;
        const float decay = 1.0f / (1.0f + PD_KNOCKBACK_FRICTION * dt);
        p->kb.x *= decay;
        p->kb.y *= decay;

        /* 7) stay inside the arena */
        p->pos.x = SDL_clamp(p->pos.x, bounds->x, bounds->x + bounds->w - p->pos.w);
        p->pos.y = SDL_clamp(p->pos.y, bounds->y, bounds->y + bounds->h - p->pos.h);
}

SDL_FRect player_attack_hitbox(const Player *p)
{
        SDL_FRect hb = { p->pos.x, p->pos.y, 0.0f, 0.0f };
        switch (p->attack_dir) {
        case DIR_RIGHT:
                hb.x = p->pos.x + p->pos.w;
                hb.y = p->pos.y;
                hb.w = PD_ATTACK_REACH;
                hb.h = p->pos.h;
                break;
        case DIR_LEFT:
                hb.x = p->pos.x - PD_ATTACK_REACH;
                hb.y = p->pos.y;
                hb.w = PD_ATTACK_REACH;
                hb.h = p->pos.h;
                break;
        case DIR_UP:
                hb.x = p->pos.x;
                hb.y = p->pos.y - PD_ATTACK_REACH;
                hb.w = p->pos.w;
                hb.h = PD_ATTACK_REACH;
                break;
        case DIR_DOWN:
                hb.x = p->pos.x;
                hb.y = p->pos.y + p->pos.h;
                hb.w = p->pos.w;
                hb.h = PD_ATTACK_REACH;
                break;
        }
        return hb;
}

bool player_attack_active(const Player *p)
{
        /* the blade only bites during the first PD_ATTACK_ACTIVE seconds */
        return p->attack_anim_t > (PD_ATTACK_ANIM - PD_ATTACK_ACTIVE);
}
