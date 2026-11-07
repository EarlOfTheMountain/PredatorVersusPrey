/* pixelduel -- main: window, fixed-timestep loop, headless selftest.
 *
 * Plain main() works as-is on FreeBSD/Linux; when this grows toward a
 * Windows build we will revisit SDL_main. The loop is a classic
 * fixed-timestep accumulator: simulation always steps at PD_TICK_HZ,
 * rendering happens once per frame, and we yield the CPU when ahead of
 * schedule (a real display's vsync also throttles us; this keeps us
 * polite when it doesn't).
 *
 * ./pixelduel --selftest runs headless (SDL dummy video driver),
 * fast-forwards a scripted fight through the real update pipeline and
 * checks that a stab actually lands. It returns in well under a second
 * and is meant as a build smoke test on machines without a display.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h> /* setenv, for the selftest's dummy video driver */

#include "config.h"
#include "game.h"
#include "input.h"

#define PD_SELFTEST_TICKS 320

static InputState st_in[2];

/* Scripted fight: P1 walks and stabs (whiff), P2 walks and dodge-rolls,
 * P2 is then teleported next to P1 twice so exactly two stabs connect. */
static void selftest_script(Game *g, Uint64 t)
{
        SDL_zerop(&st_in[0]);
        SDL_zerop(&st_in[1]);

        if (t >= 20 && t < 80) {
                st_in[0].right = true; /* P1 walks right */
        }
        if (t >= 60 && t < 95) {
                st_in[0].attack = true; /* P1 stab #1 (far away, whiffs) */
        }
        if (t >= 100 && t < 130) {
                st_in[1].left = true; /* P2 walks left */
        }
        if (t == 100) {
                st_in[1].dash = true; /* P2 dodge roll (i-frames) */
        }
        if (t == 150 || t == 185) {
                /* teleport P2 next to P1 so the next stab lands for sure */
                Player *p1 = &g->players[0];
                Player *p2 = &g->players[1];
                p2->pos.x = p1->pos.x + p1->pos.w + 4.0f;
                p2->pos.y = p1->pos.y;
                p2->iframes = 0.0f;
        }
        if (t >= 155 && t < 200) {
                st_in[0].attack = true; /* P1 stabs twice into P2 */
        }
}

int main(int argc, char **argv)
{
        bool selftest = false;
        for (int i = 1; i < argc; i++) {
                if (SDL_strcmp(argv[i], "--selftest") == 0) {
                        selftest = true;
                }
        }

        if (selftest && setenv("SDL_VIDEODRIVER", "dummy", 1) != 0) {
                SDL_Log("warning: could not force the dummy video driver");
        }

        if (!SDL_Init(SDL_INIT_VIDEO)) {
                SDL_Log("SDL_Init failed: %s", SDL_GetError());
                return 1;
        }
        /* gamepad support is optional: keyboard-only must always work */
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
                SDL_Log("no gamepad support (%s) - keyboard only", SDL_GetError());
        }

        SDL_Window *win = NULL;
        SDL_Renderer *ren = NULL;
        if (!SDL_CreateWindowAndRenderer("pixelduel", 960, 540, SDL_WINDOW_RESIZABLE,
                                         &win, &ren)) {
                SDL_Log("could not create window/renderer: %s", SDL_GetError());
                return 1;
        }
        SDL_SetWindowMinimumSize(win, PD_VIEW_W, PD_VIEW_H);
        (void)SDL_SetRenderVSync(ren, 1);
        if (!SDL_SetRenderLogicalPresentation(ren, PD_VIEW_W, PD_VIEW_H,
                                              SDL_LOGICAL_PRESENTATION_INTEGER_SCALE)) {
                SDL_Log("could not set logical presentation: %s", SDL_GetError());
                return 1;
        }
        /* alpha blending for the round-over dim overlay */
        (void)SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

        /* one line of startup chatter: it tells us instantly which backend
         * we got, which is the first thing to know when a frame looks wrong */
        SDL_Log("pixelduel: video driver '%s', renderer '%s'",
                SDL_GetCurrentVideoDriver(), SDL_GetRendererName(ren));

        input_init();

        Game game;
        game_reset(&game);

        const Uint64 NS_PER_TICK = SDL_NS_PER_SECOND / PD_TICK_HZ;
        Uint64 prev = SDL_GetTicksNS();
        Uint64 acc = 0;
        Uint64 tick = 0;
        int pre_reset_p2_hp = -1;
        bool quit = false;

        while (!quit) {
                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                        switch (e.type) {
                        case SDL_EVENT_QUIT:
                                quit = true;
                                break;
                        case SDL_EVENT_KEY_DOWN:
                                if (!e.key.repeat) {
                                        if (e.key.key == SDLK_ESCAPE) {
                                                quit = true;
                                        } else if (e.key.key == SDLK_R) {
                                                game_reset(&game); /* rematch */
                                        }
                                }
                                break;
                        case SDL_EVENT_GAMEPAD_ADDED:
                        case SDL_EVENT_GAMEPAD_REMOVED:
                                input_handle_event(&e);
                                break;
                        default:
                                break;
                        }
                }

                if (selftest) {
                        /* fast-forward: burst out the whole scripted fight */
                        for (int burst = 0; burst < 60 && tick < PD_SELFTEST_TICKS; burst++) {
                                selftest_script(&game, tick);
                                if (tick == 260) {
                                        pre_reset_p2_hp = game.players[1].hp;
                                        game_reset(&game); /* exercise rematch path */
                                }
                                game_update(&game, st_in, PD_TICK_DT);
                                tick++;
                        }
                        game_render(&game, ren); /* exercise the render path too */
                        SDL_RenderPresent(ren);
                        if (tick >= PD_SELFTEST_TICKS) {
                                quit = true;
                        }
                        continue;
                }

                Uint64 frame_start = SDL_GetTicksNS();
                acc += frame_start - prev;
                prev = frame_start;
                acc = SDL_min(acc, NS_PER_TICK * 8); /* no spiral after a stall */

                while (acc >= NS_PER_TICK) {
                        game_update(&game, NULL, PD_TICK_DT);
                        acc -= NS_PER_TICK;
                        tick++;
                }

                game_render(&game, ren);
                SDL_RenderPresent(ren); /* nothing is visible until we flip */

                /* Yield until the next tick. Measured from frame_start, not
                 * from acc: with vsync on, SDL_RenderPresent already waits
                 * out most of the frame, and sleeping on the accumulator's
                 * leftover residue on top of that can drop us to half the
                 * frame rate. */
                Uint64 spent = SDL_GetTicksNS() - frame_start;
                if (spent < NS_PER_TICK) {
                        SDL_DelayNS(NS_PER_TICK - spent);
                }
        }

        int rc = 0;
        if (selftest) {
                printf("pixelduel selftest: %llu ticks simulated\n",
                       (unsigned long long)tick);
                printf("p2 hp before the scripted rematch: %d (expected 3)\n",
                       pre_reset_p2_hp);
                if (pre_reset_p2_hp != 3) {
                        fprintf(stderr,
                                "selftest FAIL: two stabs should have landed\n");
                        rc = 1;
                } else {
                        printf("selftest OK\n");
                }
        }

        input_shutdown();
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return rc;
}
