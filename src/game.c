#include "game.h"
#include "config.h"

/* --- palette (Sweetie-16 inspired, a nod to the 16-bit target) ---
 * background / floor / wall colors live in arena.c now */
static const SDL_Color C_P1 = { 0xe4, 0x3b, 0x44, 255 };
static const SDL_Color C_P1_DARK = { 0x8f, 0x20, 0x28, 255 };
static const SDL_Color C_P2 = { 0x63, 0xc7, 0x4d, 255 };
static const SDL_Color C_P2_DARK = { 0x37, 0x83, 0x2c, 255 };
static const SDL_Color C_BLADE = { 0xb8, 0xa0, 0x5c, 255 };     /* wind-up / late */
static const SDL_Color C_BLADE_HOT = { 0xff, 0xcd, 0x75, 255 }; /* active frames */
static const SDL_Color C_HEART_OFF = { 0x3b, 0x40, 0x58, 255 };
static const SDL_Color C_TEXT = { 0xf4, 0xf4, 0xf4, 255 };
static const SDL_Color C_SHADOW = { 0x0f, 0x10, 0x19, 255 };
static const SDL_Color C_WHITE = { 0xff, 0xff, 0xff, 255 };
static const SDL_Color C_OVERLAY = { 0x0f, 0x10, 0x19, 160 };

/* Screen-shake offset for this frame; fill() applies it to everything. */
static float g_ox, g_oy;

static void fill(SDL_Renderer *r, SDL_FRect q, SDL_Color c)
{
        q.x += g_ox;
        q.y += g_oy;
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        (void)SDL_RenderFillRect(r, &q);
}

static void text(SDL_Renderer *r, float x, float y, const char *s, bool shadow)
{
        if (shadow) {
                SDL_SetRenderDrawColor(r, C_SHADOW.r, C_SHADOW.g, C_SHADOW.b, C_SHADOW.a);
                (void)SDL_RenderDebugText(r, x + 1.0f, y + 1.0f, s);
        }
        SDL_SetRenderDrawColor(r, C_TEXT.r, C_TEXT.g, C_TEXT.b, C_TEXT.a);
        (void)SDL_RenderDebugText(r, x, y, s);
}

static void draw_player(SDL_Renderer *r, const Player *p, SDL_Color body, SDL_Color dark)
{
        /* blink while invulnerable (hurt i-frames / dodge roll) */
        if (p->iframes > 0.0f && ((int)(p->iframes * 20.0f)) % 2 == 0) {
                return;
        }

        /* blade: bright while it can actually bite */
        if (p->attack_anim_t > 0.0f) {
                const SDL_FRect hb = player_attack_hitbox(p);
                const bool biting = player_attack_active(p) && !p->attack_hit;
                fill(r, hb, biting ? C_BLADE_HOT : C_BLADE);
        }

        fill(r, p->pos, body);

        /* facing notch so you can read where someone aims */
        SDL_FRect n = { p->pos.x, p->pos.y, 4.0f, 4.0f };
        switch (p->facing) {
        case DIR_UP:
                n.x = p->pos.x + (p->pos.w - 4.0f) / 2.0f;
                n.y = p->pos.y + 2.0f;
                break;
        case DIR_DOWN:
                n.x = p->pos.x + (p->pos.w - 4.0f) / 2.0f;
                n.y = p->pos.y + p->pos.h - 6.0f;
                break;
        case DIR_LEFT:
                n.x = p->pos.x + 2.0f;
                n.y = p->pos.y + (p->pos.h - 4.0f) / 2.0f;
                break;
        case DIR_RIGHT:
                n.x = p->pos.x + p->pos.w - 6.0f;
                n.y = p->pos.y + (p->pos.h - 4.0f) / 2.0f;
                break;
        }
        fill(r, n, dark);

        if (p->hit_flash > 0.0f) {
                fill(r, p->pos, C_WHITE);
        }
}

/* slim energy bar under each player's hearts, GW2-style */
static void draw_stamina(SDL_Renderer *r, int player, float stamina)
{
        const SDL_Color c = (player == 0) ? C_P1 : C_P2;
        const float w = 36.0f;
        const float x = (player == 0) ? 12.0f : PD_VIEW_W - 12.0f - w;
        const float frac = SDL_clamp(stamina / PD_STAMINA_MAX, 0.0f, 1.0f);
        fill(r, (SDL_FRect){ x, 17.0f, w, 2.0f }, C_HEART_OFF);
        if (frac > 0.0f) {
                fill(r, (SDL_FRect){ x, 17.0f, w * frac, 2.0f }, c);
        }
}

static void draw_hearts(SDL_Renderer *r, int player, int hp)
{
        const SDL_Color c = (player == 0) ? C_P1 : C_P2;
        for (int i = 0; i < PD_PLAYER_MAX_HP; i++) {
                const float x = (player == 0) ? 12.0f + i * 9.0f
                                              : PD_VIEW_W - 12.0f - (float)(i + 1) * 9.0f;
                const SDL_Color col = (i < hp) ? c : C_HEART_OFF;
                /* tiny 8x7 heart built from three rects */
                fill(r, (SDL_FRect){ x, 6.0f, 3.0f, 3.0f }, col);
                fill(r, (SDL_FRect){ x + 5.0f, 6.0f, 3.0f, 3.0f }, col);
                fill(r, (SDL_FRect){ x, 8.0f, 8.0f, 5.0f }, col);
        }
}

void game_reset(Game *g)
{
        /* spawns come from the arena (Tiled p1/p2 objects or the legacy ring) */
        player_spawn(&g->players[0], g->arena.p1_spawn.x, g->arena.p1_spawn.y);
        player_spawn(&g->players[1], g->arena.p2_spawn.x, g->arena.p2_spawn.y);
        g->state = ST_PLAYING;
        g->winner = -1;
        g->hitstop_t = 0.0f;
        g->shake_t = 0.0f;
}

static void try_hits(Game *g)
{
        for (int i = 0; i < 2; i++) {
                Player *att = &g->players[i];
                Player *def = &g->players[1 - i];

                if (!player_attack_active(att) || att->attack_hit) {
                        continue;
                }
                const SDL_FRect hb = player_attack_hitbox(att);
                if (!SDL_HasRectIntersectionFloat(&hb, &def->pos)) {
                        continue;
                }
                if (def->iframes > 0.0f) {
                        continue; /* dodge roll / hurt i-frames: the stab whiffs */
                }

                att->attack_hit = true;
                def->hp -= PD_ATTACK_DAMAGE;
                def->iframes = PD_IFRAME_TIME;
                def->hit_flash = 0.12f;

                const SDL_FPoint dv = player_dir_vec(att->attack_dir);
                def->kb.x = dv.x * PD_KNOCKBACK_SPEED;
                def->kb.y = dv.y * PD_KNOCKBACK_SPEED;

                g->hitstop_t = PD_HITSTOP_TIME;
                g->shake_t = PD_SHAKE_TIME;

                if (def->hp <= 0) {
                        def->hp = 0;
                        g->state = ST_ROUND_OVER;
                        g->winner = i;
                        g->shake_t = PD_SHAKE_TIME * 2.0f;
                }
        }
}

/* keep the two bodies from stacking into one box */
static void separate_bodies(Player *a, Player *b)
{
        const float ox = SDL_min(a->pos.x + a->pos.w, b->pos.x + b->pos.w) -
                         SDL_max(a->pos.x, b->pos.x);
        const float oy = SDL_min(a->pos.y + a->pos.h, b->pos.y + b->pos.h) -
                         SDL_max(a->pos.y, b->pos.y);
        if (ox <= 0.0f || oy <= 0.0f) {
                return;
        }
        if (ox < oy) {
                const float half = ox * 0.5f;
                if (a->pos.x < b->pos.x) {
                        a->pos.x -= half;
                        b->pos.x += half;
                } else {
                        a->pos.x += half;
                        b->pos.x -= half;
                }
        } else {
                const float half = oy * 0.5f;
                if (a->pos.y < b->pos.y) {
                        a->pos.y -= half;
                        b->pos.y += half;
                } else {
                        a->pos.y += half;
                        b->pos.y -= half;
                }
        }
}

void game_update(Game *g, const InputState *override_inputs, float dt)
{
        g->shake_t = SDL_max(0.0f, g->shake_t - dt);

        if (g->state == ST_ROUND_OVER) {
                return;
        }

        /* hitstop: freeze the whole world for a few frames on a clean hit */
        if (g->hitstop_t > 0.0f) {
                g->hitstop_t = SDL_max(0.0f, g->hitstop_t - dt);
                return;
        }

        InputState live[2];
        const InputState *in[2] = { &live[0], &live[1] };
        if (override_inputs) {
                in[0] = &override_inputs[0];
                in[1] = &override_inputs[1];
        } else {
                input_poll(0, &live[0]);
                input_poll(1, &live[1]);
        }

        for (int i = 0; i < 2; i++) {
                player_update(&g->players[i], in[i], dt, &g->arena);
        }

        separate_bodies(&g->players[0], &g->players[1]);
        try_hits(g);
}

void game_render(Game *g, SDL_Renderer *r)
{
        /* screen shake: random offset, magnitude eases back to zero */
        g_ox = g_oy = 0.0f;
        if (g->shake_t > 0.0f) {
                const float m = PD_SHAKE_MAG * (g->shake_t / PD_SHAKE_TIME);
                g_ox = (float)(SDL_rand(2001) - 1000) / 1000.0f * m;
                g_oy = (float)(SDL_rand(2001) - 1000) / 1000.0f * m;
        }

        /* floor + walls come from the arena (Tiled map or legacy ring) */
        arena_render(&g->arena, r);

        /* the fighters */
        for (int i = 0; i < 2; i++) {
                draw_player(r, &g->players[i], (i == 0) ? C_P1 : C_P2,
                            (i == 0) ? C_P1_DARK : C_P2_DARK);
        }

        /* HUD hearts + stamina */
        draw_hearts(r, 0, g->players[0].hp);
        draw_hearts(r, 1, g->players[1].hp);
        draw_stamina(r, 0, g->players[0].stamina);
        draw_stamina(r, 1, g->players[1].stamina);

        /* round-over banner */
        if (g->state == ST_ROUND_OVER) {
                fill(r, (SDL_FRect){ 0.0f, 0.0f, PD_VIEW_W, PD_VIEW_H }, C_OVERLAY);
                char buf[64];
                SDL_snprintf(buf, sizeof(buf), "PLAYER %d WINS!", g->winner + 1);
                text(r, (PD_VIEW_W - 8.0f * (float)SDL_strlen(buf)) / 2.0f, 78.0f, buf, true);
                text(r, (PD_VIEW_W - 8.0f * 19.0f) / 2.0f, 94.0f, "PRESS R FOR REMATCH", true);
        }
}
