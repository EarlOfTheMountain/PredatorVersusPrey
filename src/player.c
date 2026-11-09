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
        p->stamina = PD_STAMINA_MAX;
}

void player_update(Player *p, const InputState *in, float dt, const Arena *arena)
{
        /* 1) tick timers down, never below zero */
        p->dash_cd = SDL_max(0.0f, p->dash_cd - dt);
        p->attack_cd = SDL_max(0.0f, p->attack_cd - dt);
        p->attack_anim_t = SDL_max(0.0f, p->attack_anim_t - dt);
        p->iframes = SDL_max(0.0f, p->iframes - dt);
        p->hit_flash = SDL_max(0.0f, p->hit_flash - dt);
        p->attack_buf = SDL_max(0.0f, p->attack_buf - dt);
        p->dash_buf = SDL_max(0.0f, p->dash_buf - dt);
        p->stamina_cd = SDL_max(0.0f, p->stamina_cd - dt);
        if (p->stamina_cd <= 0.0f) {
                p->stamina = SDL_min(PD_STAMINA_MAX,
                                     p->stamina + PD_STAMINA_REGEN * dt);
        }

        /* 2) press edges feed the action buffers: a stab or roll pressed a
         * few frames early stays queued (PD_INPUT_BUFFER_TIME) and fires
         * the instant the action is possible */
        if (in->attack && !p->last_in.attack) {
                p->attack_buf = PD_INPUT_BUFFER_TIME;
        }
        if (in->dash && !p->last_in.dash) {
                p->dash_buf = PD_INPUT_BUFFER_TIME;
        }
        p->last_in = *in;

        /* 3) desired direction from input */
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

        /* 4) dodge roll trigger: buffered press + enough stamina */
        if (p->dash_buf > 0.0f && p->stamina >= PD_STAMINA_DASH &&
            p->dash_cd <= 0.0f && p->dash_t <= 0.0f && p->attack_anim_t <= 0.0f) {
                p->dash_t = PD_DASH_TIME;
                p->dash_cd = PD_DASH_COOLDOWN;
                p->dash_dir = p->facing; /* facing already reflects held input */
                p->iframes = SDL_max(p->iframes, PD_DASH_IFRAMES);
                p->attack_anim_t = 0.0f; /* a roll cancels a stab wind-up */
                p->stamina -= PD_STAMINA_DASH;
                p->stamina_cd = PD_STAMINA_REGEN_DELAY;
                p->dash_buf = 0.0f;
        }

        /* 5) stab trigger */
        if (p->attack_buf > 0.0f && p->stamina >= PD_STAMINA_ATTACK &&
            p->attack_cd <= 0.0f && p->dash_t <= 0.0f) {
                p->attack_anim_t = PD_ATTACK_ANIM;
                p->attack_cd = PD_ATTACK_COOLDOWN;
                p->attack_hit = false;
                p->attack_dir = p->facing;
                p->stamina -= PD_STAMINA_ATTACK;
                p->stamina_cd = PD_STAMINA_REGEN_DELAY;
                p->attack_buf = 0.0f;
        }

        /* 6) velocity: an active roll overrides walking */
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

        /* 7) integrate position + knockback; knockback decays each tick.
         * Axis-by-axis with revert-on-block so bodies slide along walls. */
        const float old_x = p->pos.x;
        const float old_y = p->pos.y;
        p->pos.x += (vel.x + p->kb.x) * dt;
        if (arena_rect_solid(arena, p->pos)) {
                p->pos.x = old_x;
        }
        p->pos.y += (vel.y + p->kb.y) * dt;
        if (arena_rect_solid(arena, p->pos)) {
                p->pos.y = old_y;
        }
        const float decay = 1.0f / (1.0f + PD_KNOCKBACK_FRICTION * dt);
        p->kb.x *= decay;
        p->kb.y *= decay;
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
