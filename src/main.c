/* pvp (Predator Versus Prey) -- main: window, fixed-timestep loop, selftest.
 *
 * Plain main() works as-is on FreeBSD/Linux; when this grows toward a
 * Windows build we will revisit SDL_main. The loop is a classic
 * fixed-timestep accumulator: simulation always steps at PD_TICK_HZ,
 * rendering happens once per frame, and we yield the CPU when ahead of
 * schedule (a real display's vsync also throttles us; this keeps us
 * polite when it doesn't).
 *
 * Usage:
 *   ./build/pvp                       play with the built-in ring arena
 *   ./build/pvp maps/duel.tmj         play a Tiled map (Export As -> .tmj)
 *   ./build/pvp --selftest            headless scripted fight + assertions
 *   ./build/pvp [map.tmj] --screenshot
 *                                     fast-forward the script, save a frame
 *                                     as screenshot.bmp (dummy video driver)
 *
 * The selftest runs headless, fast-forwards a scripted fight through the
 * real update pipeline and checks that stabs actually land, plus a
 * Tiled-map parsing check. It is meant as a build smoke test on machines
 * without a display and runs in well under a second.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h> /* setenv, for the headless video driver */

#include "arena.h"
#include "config.h"
#include "game.h"
#include "input.h"

#define PD_SELFTEST_TICKS 320
#define PD_SCREENSHOT_TICK 162 /* just after hitstop: flash on, blink visible */

static InputState st_in[2];

/* Scripted fight: P1 walks right and presses stab once (whiff), P2 walks
 * left and dodge-rolls, P2 is then teleported next to P1 twice so the two
 * stab PRESSES land. Presses are single ticks: they also exercise the
 * input buffers (a queued press fires as soon as the cooldown allows). */
static void selftest_script(Game *g, Uint64 t)
{
        SDL_zerop(&st_in[0]);
        SDL_zerop(&st_in[1]);

        if (t >= 20 && t < 80) {
                st_in[0].right = true; /* P1 walks right */
        }
        if (t == 60) {
                st_in[0].attack = true; /* P1 stab press #1 (far away, whiffs) */
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
        if (t == 155 || t == 186) {
                st_in[0].attack = true; /* P1 stab presses #2 and #3 */
        }
}

/* A tiny Tiled map, exercised straight from memory so the selftest never
 * depends on files or the working directory: 8x6 tiles of 16px, a solid
 * border, a 2x1 pillar in the middle, two spawn points. */
static const char *TMJ_TEST_MAP =
        "{\"width\":8,\"height\":6,\"tilewidth\":16,\"tileheight\":16,"
        "\"type\":\"map\",\"version\":\"1.10\",\"orientation\":\"orthogonal\","
        "\"renderorder\":\"right-down\",\"infinite\":false,"
        "\"layers\":["
        "{\"data\":[1,1,1,1,1,1,1,1,"
        "1,0,0,0,0,0,0,1,"
        "1,0,0,0,0,0,0,1,"
        "1,0,0,1,1,0,0,1,"
        "1,0,0,0,0,0,0,1,"
        "1,1,1,1,1,1,1,1],"
        "\"height\":6,\"width\":8,\"id\":1,\"name\":\"walls\",\"opacity\":1,"
        "\"type\":\"tilelayer\",\"visible\":true,\"x\":0,\"y\":0},"
        "{\"draworder\":\"topdown\",\"id\":2,\"name\":\"spawns\",\"objects\":["
        "{\"height\":0,\"id\":1,\"name\":\"p1\",\"point\":true,\"type\":\"\","
        "\"visibility\":true,\"width\":0,\"x\":64,\"y\":48},"
        "{\"height\":0,\"id\":2,\"name\":\"p2\",\"point\":true,\"type\":\"\","
        "\"visibility\":true,\"width\":0,\"x\":64,\"y\":48}"
        "],\"opacity\":1,\"type\":\"objectgroup\",\"visible\":true,\"x\":0,\"y\":0}"
        "],\"nextlayerid\":3,\"nextobjectid\":3,\"tilesets\":[]}";

static bool arena_tmj_selftest(void)
{
        Arena a;
        char err[128];
        if (!arena_load_tmj_buf(&a, TMJ_TEST_MAP, SDL_strlen(TMJ_TEST_MAP),
                                err, sizeof(err))) {
                fprintf(stderr, "arena tmj test FAIL: %s\n", err);
                return false;
        }

        /* 8x6 tiles of 16px = 128x96, centered in 320x180 -> off (96, 42) */
        bool ok = true;
        ok = ok && a.w == 8 && a.h == 6 && a.tw == 16 && a.th == 16;
        ok = ok && SDL_fabsf(a.offx - 96.0f) < 0.01f;
        ok = ok && SDL_fabsf(a.offy - 42.0f) < 0.01f;

        const SDL_FRect pillar = { a.offx + 3.0f * 16.0f, a.offy + 3.0f * 16.0f,
                                   16.0f, 16.0f };
        const SDL_FRect open = { a.offx + 1.0f * 16.0f, a.offy + 1.0f * 16.0f,
                                 16.0f, 16.0f };
        ok = ok && arena_rect_solid(&a, pillar);
        ok = ok && !arena_rect_solid(&a, open);
        ok = ok && arena_rect_solid(&a, (SDL_FRect){ -50.0f, -50.0f, 10.0f, 10.0f });

        /* p1 point object at map (64,48) -> view (160,90) center */
        ok = ok && SDL_fabsf(a.p1_spawn.x - (160.0f - PD_PLAYER_SIZE * 0.5f)) < 0.01f;
        ok = ok && SDL_fabsf(a.p1_spawn.y - (90.0f - PD_PLAYER_SIZE * 0.5f)) < 0.01f;

        if (ok) {
                printf("arena tmj test OK\n");
        } else {
                fprintf(stderr,
                        "arena tmj test FAIL: w=%d h=%d tw=%d th=%d "
                        "off=(%g,%g) p1=(%g,%g)\n",
                        a.w, a.h, a.tw, a.th, a.offx, a.offy,
                        a.p1_spawn.x, a.p1_spawn.y);
        }
        arena_free(&a);
        return ok;
}

/* grab the current back buffer and write it as a BMP (for README shots).
 * SDL 3.4 made SDL_RenderReadPixels return a surface; 3.2 still wants a
 * caller-provided buffer, so support both while Debian ships 3.2.x. */
static bool save_screenshot(SDL_Renderer *ren, const char *path)
{
#if SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION) >= \
        SDL_VERSIONNUM(3, 4, 0)
        SDL_Surface *s = SDL_RenderReadPixels(ren, NULL);
        if (!s) {
                SDL_Log("screenshot: read failed: %s", SDL_GetError());
                return false;
        }
#else
        int ow = 0, oh = 0;
        if (!SDL_GetCurrentRenderOutputSize(ren, &ow, &oh) || ow < 1 || oh < 1) {
                SDL_Log("screenshot: no output size: %s", SDL_GetError());
                return false;
        }
        const int pitch = ow * 3; /* RGB24 */
        void *px = SDL_malloc((size_t)pitch * (size_t)oh);
        if (!px) {
                return false;
        }
        if (!SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGB24, px, pitch)) {
                SDL_Log("screenshot: read failed: %s", SDL_GetError());
                SDL_free(px);
                return false;
        }
        SDL_Surface *s = SDL_CreateSurfaceFrom(ow, oh, SDL_PIXELFORMAT_RGB24,
                                               px, pitch);
        if (!s) {
                SDL_free(px);
                return false;
        }
#endif
        const bool ok = SDL_SaveBMP(s, path);
        if (!ok) {
                SDL_Log("screenshot: save failed: %s", SDL_GetError());
        }
        SDL_DestroySurface(s);
#if SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION) < \
        SDL_VERSIONNUM(3, 4, 0)
        SDL_free(px);
#endif
        if (ok) {
                SDL_Log("screenshot written: %s", path);
        }
        return ok;
}

int main(int argc, char **argv)
{
        bool selftest = false;
        bool screenshot = false;
        const char *arena_path = NULL;
        for (int i = 1; i < argc; i++) {
                if (SDL_strcmp(argv[i], "--selftest") == 0) {
                        selftest = true;
                } else if (SDL_strcmp(argv[i], "--screenshot") == 0) {
                        screenshot = true;
                } else {
                        arena_path = argv[i]; /* first non-flag = map path */
                }
        }

        if ((selftest || screenshot) &&
            setenv("SDL_VIDEODRIVER", "dummy", 1) != 0) {
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
        if (!SDL_CreateWindowAndRenderer("Predator Versus Prey", 960, 540,
                                         SDL_WINDOW_RESIZABLE, &win, &ren)) {
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
        SDL_Log("pvp: video driver '%s', renderer '%s'",
                SDL_GetCurrentVideoDriver(), SDL_GetRendererName(ren));

        input_init();

        Game game;
        if (selftest) {
                arena_init_default(&game.arena);
        } else if (arena_path) {
                char err[128];
                if (!arena_load_tmj_file(&game.arena, arena_path,
                                         err, sizeof(err))) {
                        SDL_Log("arena '%s' failed: %s", arena_path, err);
                        SDL_Log("falling back to the built-in ring");
                        arena_init_default(&game.arena);
                } else {
                        SDL_Log("arena: %dx%d tiles of %dx%d px from %s",
                                game.arena.w, game.arena.h,
                                game.arena.tw, game.arena.th, arena_path);
                }
        } else {
                arena_init_default(&game.arena);
        }
        game_reset(&game);

        const Uint64 NS_PER_TICK = SDL_NS_PER_SECOND / PD_TICK_HZ;
        Uint64 prev = SDL_GetTicksNS();
        Uint64 acc = 0;
        Uint64 tick = 0;
        int pre_reset_p2_hp = -1;
        bool shot_taken = false;
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

                if (selftest || screenshot) {
                        /* fast-forward: burst out the whole scripted fight */
                        for (int burst = 0; burst < 60 && tick < PD_SELFTEST_TICKS;
                             burst++) {
                                selftest_script(&game, tick);
                                if (tick == 260) {
                                        pre_reset_p2_hp = game.players[1].hp;
                                        game_reset(&game); /* exercise rematch path */
                                }
                                game_update(&game, st_in, PD_TICK_DT);
                                tick++;
                                if (screenshot && tick == PD_SCREENSHOT_TICK) {
                                        game_render(&game, ren);
                                        shot_taken = save_screenshot(ren,
                                                                     "screenshot.bmp");
                                        quit = true;
                                        break;
                                }
                        }
                        if (screenshot) {
                                if (tick >= PD_SELFTEST_TICKS && !shot_taken) {
                                        /* script ended before the tick: shoot now */
                                        game_render(&game, ren);
                                        shot_taken = save_screenshot(ren,
                                                                     "screenshot.bmp");
                                }
                                if (shot_taken) {
                                        quit = true;
                                }
                                continue;
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
        if (screenshot) {
                if (!shot_taken) {
                        fprintf(stderr, "screenshot FAIL: never captured a frame\n");
                        rc = 1;
                }
        } else if (selftest) {
                printf("pvp selftest: %llu ticks simulated\n",
                       (unsigned long long)tick);
                printf("p2 hp before the scripted rematch: %d (expected 3)\n",
                       pre_reset_p2_hp);
                bool ok = pre_reset_p2_hp == 3;
                ok = arena_tmj_selftest() && ok;
                if (!ok) {
                        fprintf(stderr, "selftest FAIL\n");
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
