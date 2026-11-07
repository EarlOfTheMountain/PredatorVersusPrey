/* pixelduel -- input.
 *
 * Merges keyboard and gamepad into one InputState per player:
 *   - keyboard always works: P1 on WASD, P2 on arrows
 *   - gamepads are assigned to players in plug-in order (first pad -> P1)
 *   - pad input is OR-merged with that player's keyboard input
 */
#ifndef PD_INPUT_H
#define PD_INPUT_H

#include <SDL3/SDL.h>

typedef struct {
        bool up, down, left, right;
        bool attack; /* sword stab */
        bool dash;   /* dodge roll */
} InputState;

/* Attach any already-plugged gamepads to P1/P2. */
void input_init(void);
void input_shutdown(void);
/* Hot-plug support: feed SDL_EVENT_GAMEPAD_ADDED / _REMOVED events. */
void input_handle_event(const SDL_Event *e);
/* Fill *out for `player` (0 or 1) from keyboard + that player's pad. */
void input_poll(int player, InputState *out);

#endif /* PD_INPUT_H */
