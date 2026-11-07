#include "input.h"

/* P1: WASD + stab (Space or E) + dash (Q or Left Shift)
 * P2: Arrows + stab (Enter or Right Shift) + dash (Right Ctrl or /) */
typedef struct {
        SDL_Scancode up, down, left, right;
        SDL_Scancode attack_a, attack_b;
        SDL_Scancode dash_a, dash_b;
} KeyMap;

static const KeyMap k_maps[2] = {
        { SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D,
          SDL_SCANCODE_SPACE, SDL_SCANCODE_E, SDL_SCANCODE_Q, SDL_SCANCODE_LSHIFT },
        { SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
          SDL_SCANCODE_RETURN, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RCTRL, SDL_SCANCODE_SLASH },
};

/* One open pad per player slot; NULL = that player is keyboard-only. */
static SDL_Gamepad *pads[2] = { NULL, NULL };

static void attach_pad(SDL_JoystickID id)
{
        for (int i = 0; i < 2; i++) {
                if (!pads[i]) {
                        pads[i] = SDL_OpenGamepad(id);
                        if (pads[i]) {
                                SDL_Log("gamepad attached to player %d: %s",
                                        i + 1, SDL_GetGamepadName(pads[i]));
                        }
                        return;
                }
        }
        SDL_Log("gamepad ignored: both players already have one");
}

void input_init(void)
{
        int n = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&n);
        if (!ids) {
                return; /* gamepad subsystem unavailable; keyboard-only */
        }
        for (int i = 0; i < n && (!pads[0] || !pads[1]); i++) {
                attach_pad(ids[i]);
        }
        SDL_free(ids);
}

void input_shutdown(void)
{
        for (int i = 0; i < 2; i++) {
                if (pads[i]) {
                        SDL_CloseGamepad(pads[i]);
                        pads[i] = NULL;
                }
        }
}

void input_handle_event(const SDL_Event *e)
{
        if (e->type == SDL_EVENT_GAMEPAD_ADDED) {
                attach_pad(e->gdevice.which);
        } else if (e->type == SDL_EVENT_GAMEPAD_REMOVED) {
                for (int i = 0; i < 2; i++) {
                        if (pads[i] && SDL_GetGamepadID(pads[i]) == e->gdevice.which) {
                                SDL_CloseGamepad(pads[i]);
                                pads[i] = NULL;
                                SDL_Log("gamepad removed from player %d", i + 1);
                        }
                }
        }
}

void input_poll(int player, InputState *out)
{
        SDL_zerop(out);
        if (player < 0 || player > 1) {
                return;
        }

        const bool *keys = SDL_GetKeyboardState(NULL);
        const KeyMap *k = &k_maps[player];
        out->up = keys[k->up];
        out->down = keys[k->down];
        out->left = keys[k->left];
        out->right = keys[k->right];
        out->attack = keys[k->attack_a] || keys[k->attack_b];
        out->dash = keys[k->dash_a] || keys[k->dash_b];

        SDL_Gamepad *pad = pads[player];
        if (pad && SDL_GamepadConnected(pad)) {
                const Sint16 DZ = 6000; /* stick deadzone, out of 32767 */
                const Sint16 ax = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
                const Sint16 ay = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
                out->left |= ax < -DZ;
                out->right |= ax > DZ;
                out->up |= ay < -DZ;
                out->down |= ay > DZ;

                out->up |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP);
                out->down |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
                out->left |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
                out->right |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

                out->attack |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH); /* Xbox A */
                out->dash |= SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST) ||  /* Xbox B */
                             SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        }
}
