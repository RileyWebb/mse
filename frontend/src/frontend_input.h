#ifndef MSE_FRONTEND_INPUT_H
#define MSE_FRONTEND_INPUT_H

#include <stdbool.h>
#include <stddef.h>

#include "libmse/libmse.h"

/* -----------------------------------------------------------------------
 * Input manager
 *
 * The input thread polls SDL_GetKeyboardState / SDL_GetGamepadAxis on its
 * own thread.  The main thread only handles device-connection events
 * (SDL_EVENT_GAMEPAD_ADDED / REMOVED) through the normal SDL_PollEvent
 * loop in frontend_app.c.
 * ---------------------------------------------------------------------- */

/* Maximum number of inputs a single backend can declare. */
#define MSE_INPUT_MAX_INPUTS 64

/* Opaque manager handle. */
typedef struct mse_frontend_input_manager_s mse_frontend_input_manager_t;

/* Create/destroy -------------------------------------------------------- */
mse_frontend_input_manager_t *mse_frontend_input_manager_create(void);
void                          mse_frontend_input_manager_destroy(mse_frontend_input_manager_t *mgr);

/* Backend attachment ---------------------------------------------------- */
/* Attach the active backend.  Clears all current bindings and loads
 * defaults based on backend->input_descs. Pass NULL to detach. */
void mse_frontend_input_manager_set_backend(mse_frontend_input_manager_t *mgr,
                                             mse_backend_t               *backend);

/* Thread lifecycle ------------------------------------------------------ */
bool mse_frontend_input_thread_start(mse_frontend_input_manager_t *mgr);
void mse_frontend_input_thread_stop(mse_frontend_input_manager_t *mgr);

/* Binding manipulation -------------------------------------------------- */
/* Overwrite the binding for input slot [index]. */
void mse_frontend_input_set_binding(mse_frontend_input_manager_t *mgr,
                                    size_t                         index,
                                    const mse_input_binding_t     *binding);

/* Get a copy of the binding for input slot [index]. Returns false if out-of-range. */
bool mse_frontend_input_get_binding(const mse_frontend_input_manager_t *mgr,
                                    size_t                               index,
                                    mse_input_binding_t                 *out);

/* Rebind capture -------------------------------------------------------- */
/* Begin listening for the next key/button press.
 * When a press is detected the binding at [index] is updated automatically
 * and mse_frontend_input_is_capturing() returns false. */
void mse_frontend_input_start_capture(mse_frontend_input_manager_t *mgr,
                                      size_t                         index);
void mse_frontend_input_cancel_capture(mse_frontend_input_manager_t *mgr);
bool mse_frontend_input_is_capturing(const mse_frontend_input_manager_t *mgr,
                                     size_t                              *out_index);

/* Gamepad hot-plug (call from main-thread SDL event loop) --------------- */
void mse_frontend_input_on_gamepad_added(mse_frontend_input_manager_t *mgr,
                                          int                            joystick_id);
void mse_frontend_input_on_gamepad_removed(mse_frontend_input_manager_t *mgr,
                                            int                            joystick_id);

/* Utilities ------------------------------------------------------------- */
const char *mse_frontend_input_binding_label(const mse_input_binding_t *binding,
                                              char *buf, size_t buf_size);

/* Accessor --------------------------------------------------------------- */
mse_backend_t *mse_frontend_input_manager_get_backend(mse_frontend_input_manager_t *mgr);

#endif // MSE_FRONTEND_INPUT_H