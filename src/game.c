#include "game.h"
#include "config.h"

/* --- palette (Sweetie-16 inspired, a nod to the 16-bit target) --- */
static const SDL_Color C_BG = { 0x0f, 0x10, 0x19, 255 };
static const SDL_Color C_FLOOR_A = { 0x20, 0x24, 0x38, 255 };
static const SDL_Color C_FLOOR_B = { 0x24, 0x28, 0x3e, 255 };
static const SDL_Color C_WALL = { 0x5a, 0x6b, 0x86, 255 };
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
        player_spawn(&g->players[0], 40.0f, (PD_VIEW_H - PD_PLAYER_SIZE) / 2.0f);
        player_spawn(&g->players[1], PD_VIEW_W - 40.0f - PD_PLAYER_SIZE,
                     (PD_VIEW_H - PD_PLAYER_SIZE) / 2.0f);
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

        const SDL_FRect bounds = { PD_WALL, PD_WALL, PD_VIEW_W - 2.0f * PD_WALL,
                                   PD_VIEW_H - 2.0f * PD_WALL };
        for (int i = 0; i < 2; i++) {
                player_update(&g->players[i], in[i], dt, &bounds);
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

        /* background + checkerboard floor */
        fill(r, (SDL_FRect){ 0.0f, 0.0f, PD_VIEW_W, PD_VIEW_H }, C_BG);
        fill(r, (SDL_FRect){ PD_WALL, PD_WALL, PD_VIEW_W - 2.0f * PD_WALL,
                             PD_VIEW_H - 2.0f * PD_WALL },
             C_FLOOR_B);
        for (int y = PD_WALL; y < PD_VIEW_H - PD_WALL; y += 16) {
                for (int x = PD_WALL; x < PD_VIEW_W - PD_WALL; x += 16) {
                        if (((x / 16) + (y / 16)) % 2 == 0) {
                                fill(r, (SDL_FRect){ (float)x, (float)y, 16.0f, 16.0f },
                                     C_FLOOR_A);
                        }
                }
        }

        /* arena walls, drawn over the floor edges */
        fill(r, (SDL_FRect){ 0.0f, 0.0f, PD_VIEW_W, PD_WALL }, C_WALL);
        fill(r, (SDL_FRect){ 0.0f, PD_VIEW_H - PD_WALL, PD_VIEW_W, PD_WALL }, C_WALL);
        fill(r, (SDL_FRect){ 0.0f, PD_WALL, PD_WALL, PD_VIEW_H - 2.0f * PD_WALL }, C_WALL);
        fill(r, (SDL_FRect){ PD_VIEW_W - PD_WALL, PD_WALL, PD_WALL,
                             PD_VIEW_H - 2.0f * PD_WALL },
             C_WALL);

        /* the fighters */
        for (int i = 0; i < 2; i++) {
                draw_player(r, &g->players[i], (i == 0) ? C_P1 : C_P2,
                            (i == 0) ? C_P1_DARK : C_P2_DARK);
        }

        /* HUD hearts */
        draw_hearts(r, 0, g->players[0].hp);
        draw_hearts(r, 1, g->players[1].hp);

        /* round-over banner */
        if (g->state == ST_ROUND_OVER) {
                fill(r, (SDL_FRect){ 0.0f, 0.0f, PD_VIEW_W, PD_VIEW_H }, C_OVERLAY);
                char buf[64];
                SDL_snprintf(buf, sizeof(buf), "PLAYER %d WINS!", g->winner + 1);
                text(r, (PD_VIEW_W - 8.0f * (float)SDL_strlen(buf)) / 2.0f, 78.0f, buf, true);
                text(r, (PD_VIEW_W - 8.0f * 19.0f) / 2.0f, 94.0f, "PRESS R FOR REMATCH", true);
        }
}
