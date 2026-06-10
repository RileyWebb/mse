#define DEBUG_LOG_SOURCE "input"
#include "frontend_input.h"
#include "frontend_cimgui.h" /* SDL3 is pulled in transitively */
#include "libmse/libmse_debug.h"

#include <SDL3/SDL.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */

typedef struct mse_frontend_input_manager_s {
    /* Backend we are feeding */
    mse_backend_t *backend;

    /* Per-input bindings (parallel to backend->input_descs) */
    mse_input_binding_t bindings[MSE_INPUT_MAX_INPUTS];
    size_t              binding_count; /* mirrors backend->input_count */

    /* Gamepad handle (single controller for now) */
    SDL_Gamepad *gamepad;
    SDL_JoystickID gamepad_id;

    /* Worker thread */
    SDL_Thread *thread;
    atomic_bool running;

    /* Rebind capture */
    atomic_bool capturing;
    atomic_int  capture_index; /* which input slot is being rebound */

    /* Mutex protecting bindings + capture state from concurrent access */
    SDL_Mutex *mutex;
} mse_frontend_input_manager_t;

/* -----------------------------------------------------------------------
 * Default key map
 *
 * We try to give each input a sensible default keyboard binding based
 * on common retro-controller layouts.
 * ---------------------------------------------------------------------- */

static const struct {
    const char *id;
    SDL_Scancode key;
} g_default_keys[] = {
    /* D-Pad */
    { "DPAD_UP",    SDL_SCANCODE_UP    },
    { "DPAD_DOWN",  SDL_SCANCODE_DOWN  },
    { "DPAD_LEFT",  SDL_SCANCODE_LEFT  },
    { "DPAD_RIGHT", SDL_SCANCODE_RIGHT },
    /* Face buttons */
    { "BTN_A",  SDL_SCANCODE_Z    },
    { "BTN_B",  SDL_SCANCODE_X    },
    { "BTN_X",  SDL_SCANCODE_A    },
    { "BTN_Y",  SDL_SCANCODE_S    },
    /* Shoulder */
    { "BTN_L",  SDL_SCANCODE_Q    },
    { "BTN_R",  SDL_SCANCODE_W    },
    { "BTN_L2", SDL_SCANCODE_E    },
    { "BTN_R2", SDL_SCANCODE_R    },
    /* Meta */
    { "BTN_START",  SDL_SCANCODE_RETURN },
    { "BTN_SELECT", SDL_SCANCODE_RSHIFT },
    /* Analog axes fallback to WASD */
    { "AXIS_LX", SDL_SCANCODE_D    },
    { "AXIS_LY", SDL_SCANCODE_W    },
    { "AXIS_RX", SDL_SCANCODE_L    },
    { "AXIS_RY", SDL_SCANCODE_I    },
};
static const size_t g_default_keys_count =
    sizeof(g_default_keys) / sizeof(g_default_keys[0]);

static SDL_Scancode mse_input_default_key(const char *id)
{
    if (id == NULL) {
        return SDL_SCANCODE_UNKNOWN;
    }
    for (size_t i = 0; i < g_default_keys_count; ++i) {
        if (SDL_strcmp(id, g_default_keys[i].id) == 0) {
            return g_default_keys[i].key;
        }
    }
    return SDL_SCANCODE_UNKNOWN;
}

/* -----------------------------------------------------------------------
 * Input thread
 * ---------------------------------------------------------------------- */

#define MSE_INPUT_POLL_INTERVAL_MS 4  /* ~250 Hz */

static int mse_frontend_input_thread(void *user_data)
{
    mse_frontend_input_manager_t *mgr = (mse_frontend_input_manager_t *)user_data;
    if (mgr == NULL) {
        return 1;
    }

    DEBUG_INFO("Input thread started");

    while (atomic_load(&mgr->running)) {

        /* ---- Phase 1: poll raw SDL state (no event pump) ---- */
        int          kb_numkeys = 0;
        const bool  *kb_state  = SDL_GetKeyboardState(&kb_numkeys);

        SDL_LockMutex(mgr->mutex);

        mse_backend_t *backend = mgr->backend;

        if (backend != NULL && backend->input_states != NULL &&
            backend->input_descs != NULL && backend->input_count > 0U) {

            const size_t count = backend->input_count < MSE_INPUT_MAX_INPUTS
                                     ? backend->input_count
                                     : (size_t)MSE_INPUT_MAX_INPUTS;

            for (size_t i = 0; i < count; ++i) {
                const mse_input_binding_t   *b   = &mgr->bindings[i];
                const mse_backend_input_desc_t *desc = &backend->input_descs[i];
                float value = 0.0f;

                switch (b->source_type) {
                case MSE_INPUT_SOURCE_KEYBOARD: {
                    if (kb_state != NULL && b->scancode > SDL_SCANCODE_UNKNOWN &&
                        b->scancode < kb_numkeys) {
                        value = kb_state[b->scancode] ? 1.0f : 0.0f;
                    }
                    break;
                }
                case MSE_INPUT_SOURCE_GAMEPAD_BUTTON: {
                    if (mgr->gamepad != NULL) {
                        value = SDL_GetGamepadButton(mgr->gamepad,
                                    (SDL_GamepadButton)b->gamepad_button) ? 1.0f : 0.0f;
                    }
                    break;
                }
                case MSE_INPUT_SOURCE_GAMEPAD_AXIS: {
                    if (mgr->gamepad != NULL) {
                        Sint16 raw = SDL_GetGamepadAxis(mgr->gamepad,
                                        (SDL_GamepadAxis)b->gamepad_axis);
                        float  normalised = (float)raw / 32767.0f;
                        float  dz = b->axis_deadzone > 0.0f ? b->axis_deadzone : 0.1f;
                        if (normalised > -dz && normalised < dz) {
                            normalised = 0.0f;
                        }
                        if (desc->type == MSE_INPUT_TYPE_BUTTON) {
                            value = (normalised > 0.5f) ? 1.0f : 0.0f;
                        } else {
                            value = normalised;
                        }
                    }
                    break;
                }
                default:
                    value = 0.0f;
                    break;
                }

                backend->input_states[i] = value;
            }
        }

        /* ---- Phase 2: capture mode – detect first key press ---- */
        if (atomic_load(&mgr->capturing)) {
            int capture_idx = atomic_load(&mgr->capture_index);
            if (capture_idx >= 0 && (size_t)capture_idx < mgr->binding_count) {

                /* Check keyboard */
                if (kb_state != NULL) {
                    for (int sc = SDL_SCANCODE_UNKNOWN + 1; sc < kb_numkeys; ++sc) {
                        if (kb_state[sc]) {
                            mse_input_binding_t nb;
                            memset(&nb, 0, sizeof(nb));
                            nb.source_type = MSE_INPUT_SOURCE_KEYBOARD;
                            nb.scancode    = sc;
                            mgr->bindings[capture_idx] = nb;
                            atomic_store(&mgr->capturing, false);
                            DEBUG_INFO("Captured key %d for input %d", sc, capture_idx);
                            break;
                        }
                    }
                }

                /* Check gamepad buttons (only if still capturing) */
                if (atomic_load(&mgr->capturing) && mgr->gamepad != NULL) {
                    for (int btn = 0; btn < SDL_GAMEPAD_BUTTON_COUNT; ++btn) {
                        if (SDL_GetGamepadButton(mgr->gamepad, (SDL_GamepadButton)btn)) {
                            mse_input_binding_t nb;
                            memset(&nb, 0, sizeof(nb));
                            nb.source_type    = MSE_INPUT_SOURCE_GAMEPAD_BUTTON;
                            nb.gamepad_button = btn;
                            mgr->bindings[capture_idx] = nb;
                            atomic_store(&mgr->capturing, false);
                            break;
                        }
                    }
                }

                /* Check gamepad axes (only if still capturing) */
                if (atomic_load(&mgr->capturing) && mgr->gamepad != NULL) {
                    for (int ax = 0; ax < SDL_GAMEPAD_AXIS_COUNT; ++ax) {
                        Sint16 raw = SDL_GetGamepadAxis(mgr->gamepad, (SDL_GamepadAxis)ax);
                        if (raw > 16384 || raw < -16384) {
                            mse_input_binding_t nb;
                            memset(&nb, 0, sizeof(nb));
                            nb.source_type  = MSE_INPUT_SOURCE_GAMEPAD_AXIS;
                            nb.gamepad_axis = ax;
                            nb.axis_deadzone = 0.1f;
                            mgr->bindings[capture_idx] = nb;
                            atomic_store(&mgr->capturing, false);
                            break;
                        }
                    }
                }
            }
        }

        SDL_UnlockMutex(mgr->mutex);

        SDL_Delay(MSE_INPUT_POLL_INTERVAL_MS);
    }

    DEBUG_INFO("Input thread stopped");
    return 0;
}

/* -----------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

mse_frontend_input_manager_t *mse_frontend_input_manager_create(void)
{
    mse_frontend_input_manager_t *mgr =
        (mse_frontend_input_manager_t *)SDL_calloc(1, sizeof(*mgr));
    if (mgr == NULL) {
        return NULL;
    }

    mgr->mutex = SDL_CreateMutex();
    if (mgr->mutex == NULL) {
        SDL_free(mgr);
        return NULL;
    }

    atomic_store(&mgr->running,  false);
    atomic_store(&mgr->capturing, false);
    atomic_store(&mgr->capture_index, -1);
    mgr->gamepad_id = -1;

    return mgr;
}

void mse_frontend_input_manager_destroy(mse_frontend_input_manager_t *mgr)
{
    if (mgr == NULL) {
        return;
    }

    mse_frontend_input_thread_stop(mgr);

    if (mgr->gamepad != NULL) {
        SDL_CloseGamepad(mgr->gamepad);
        mgr->gamepad = NULL;
    }

    SDL_DestroyMutex(mgr->mutex);
    SDL_free(mgr);
}

void mse_frontend_input_manager_set_backend(mse_frontend_input_manager_t *mgr,
                                             mse_backend_t               *backend)
{
    if (mgr == NULL) {
        return;
    }

    SDL_LockMutex(mgr->mutex);

    mgr->backend = backend;
    memset(mgr->bindings, 0, sizeof(mgr->bindings));
    mgr->binding_count = 0U;
    atomic_store(&mgr->capturing, false);

    if (backend != NULL && backend->input_descs != NULL && backend->input_count > 0U) {
        size_t count = backend->input_count < MSE_INPUT_MAX_INPUTS
                           ? backend->input_count
                           : (size_t)MSE_INPUT_MAX_INPUTS;
        mgr->binding_count = count;

        for (size_t i = 0; i < count; ++i) {
            SDL_Scancode def = mse_input_default_key(backend->input_descs[i].id);
            if (def != SDL_SCANCODE_UNKNOWN) {
                mgr->bindings[i].source_type = MSE_INPUT_SOURCE_KEYBOARD;
                mgr->bindings[i].scancode    = (int)def;
            }
            /* else binding stays NONE until user sets it */
        }
    }

    SDL_UnlockMutex(mgr->mutex);
}

bool mse_frontend_input_thread_start(mse_frontend_input_manager_t *mgr)
{
    if (mgr == NULL || atomic_load(&mgr->running)) {
        return false;
    }

    atomic_store(&mgr->running, true);
    mgr->thread = SDL_CreateThread(mse_frontend_input_thread, "mse_input", mgr);
    if (mgr->thread == NULL) {
        atomic_store(&mgr->running, false);
        DEBUG_ERROR("Failed to create input thread: %s", SDL_GetError());
        return false;
    }

    return true;
}

void mse_frontend_input_thread_stop(mse_frontend_input_manager_t *mgr)
{
    if (mgr == NULL || !atomic_load(&mgr->running)) {
        return;
    }

    atomic_store(&mgr->running, false);
    if (mgr->thread != NULL) {
        SDL_WaitThread(mgr->thread, NULL);
        mgr->thread = NULL;
    }
}

void mse_frontend_input_set_binding(mse_frontend_input_manager_t *mgr,
                                    size_t                         index,
                                    const mse_input_binding_t     *binding)
{
    if (mgr == NULL || binding == NULL || index >= MSE_INPUT_MAX_INPUTS) {
        return;
    }

    SDL_LockMutex(mgr->mutex);
    mgr->bindings[index] = *binding;
    SDL_UnlockMutex(mgr->mutex);
}

bool mse_frontend_input_get_binding(const mse_frontend_input_manager_t *mgr,
                                    size_t                               index,
                                    mse_input_binding_t                 *out)
{
    if (mgr == NULL || out == NULL || index >= MSE_INPUT_MAX_INPUTS) {
        return false;
    }

    SDL_LockMutex(mgr->mutex);
    *out = mgr->bindings[index];
    SDL_UnlockMutex(mgr->mutex);
    return true;
}

void mse_frontend_input_start_capture(mse_frontend_input_manager_t *mgr,
                                      size_t                         index)
{
    if (mgr == NULL || index >= MSE_INPUT_MAX_INPUTS) {
        return;
    }

    SDL_LockMutex(mgr->mutex);
    atomic_store(&mgr->capture_index, (int)index);
    atomic_store(&mgr->capturing, true);
    SDL_UnlockMutex(mgr->mutex);
}

void mse_frontend_input_cancel_capture(mse_frontend_input_manager_t *mgr)
{
    if (mgr == NULL) {
        return;
    }

    atomic_store(&mgr->capturing, false);
    atomic_store(&mgr->capture_index, -1);
}

bool mse_frontend_input_is_capturing(const mse_frontend_input_manager_t *mgr,
                                     size_t                              *out_index)
{
    if (mgr == NULL) {
        return false;
    }

    bool cap = atomic_load(&mgr->capturing);
    if (cap && out_index != NULL) {
        int idx = atomic_load(&mgr->capture_index);
        *out_index = (idx >= 0) ? (size_t)idx : 0U;
    }
    return cap;
}

void mse_frontend_input_on_gamepad_added(mse_frontend_input_manager_t *mgr,
                                          int                            joystick_id)
{
    if (mgr == NULL) {
        return;
    }

    SDL_LockMutex(mgr->mutex);
    /* Only open the first connected gamepad for now */
    if (mgr->gamepad == NULL) {
        mgr->gamepad = SDL_OpenGamepad(joystick_id);
        if (mgr->gamepad != NULL) {
            mgr->gamepad_id = joystick_id;
            DEBUG_INFO("Gamepad connected: %s (id %d)",
                       SDL_GetGamepadName(mgr->gamepad), joystick_id);
        }
    }
    SDL_UnlockMutex(mgr->mutex);
}

void mse_frontend_input_on_gamepad_removed(mse_frontend_input_manager_t *mgr,
                                            int                            joystick_id)
{
    if (mgr == NULL) {
        return;
    }

    SDL_LockMutex(mgr->mutex);
    if (mgr->gamepad != NULL && mgr->gamepad_id == joystick_id) {
        SDL_CloseGamepad(mgr->gamepad);
        mgr->gamepad    = NULL;
        mgr->gamepad_id = 0;
        DEBUG_INFO("Gamepad disconnected (id %d)", joystick_id);
    }
    SDL_UnlockMutex(mgr->mutex);
}

const char *mse_frontend_input_binding_label(const mse_input_binding_t *binding,
                                              char *buf, size_t buf_size)
{
    if (binding == NULL || buf == NULL || buf_size == 0) {
        return "---";
    }

    switch (binding->source_type) {
    case MSE_INPUT_SOURCE_KEYBOARD: {
        const char *name = SDL_GetScancodeName((SDL_Scancode)binding->scancode);
        if (name != NULL && name[0] != '\0') {
            snprintf(buf, buf_size, "%s", name);
        } else {
            snprintf(buf, buf_size, "Key %d", binding->scancode);
        }
        break;
    }
    case MSE_INPUT_SOURCE_GAMEPAD_BUTTON: {
        const char *name = SDL_GetGamepadStringForButton(
            (SDL_GamepadButton)binding->gamepad_button);
        snprintf(buf, buf_size, "GP: %s",
                 (name != NULL && name[0] != '\0') ? name : "Btn?");
        break;
    }
    case MSE_INPUT_SOURCE_GAMEPAD_AXIS: {
        const char *name = SDL_GetGamepadStringForAxis(
            (SDL_GamepadAxis)binding->gamepad_axis);
        snprintf(buf, buf_size, "GP Axis: %s",
                 (name != NULL && name[0] != '\0') ? name : "Axis?");
        break;
    }
    default:
        snprintf(buf, buf_size, "---");
        break;
    }

    return buf;
}

mse_backend_t *mse_frontend_input_manager_get_backend(mse_frontend_input_manager_t *mgr)
{
    if (mgr == NULL) {
        return NULL;
    }
    return mgr->backend;
}
