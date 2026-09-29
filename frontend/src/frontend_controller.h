#ifndef MSE_FRONTEND_CONTROLLER_H
#define MSE_FRONTEND_CONTROLLER_H

#include <stdbool.h>

typedef struct mse_frontend_ui_state_s mse_frontend_ui_state_t;

// The input configurator.
//
// Backends describe their own pad (see libmse_input.h), so this draws a NES
// controller for cNES and whatever the next backend declares for itself. One
// that describes nothing still gets a working list of its inputs.
void mse_frontend_controller_draw(mse_frontend_ui_state_t *state);

#endif // MSE_FRONTEND_CONTROLLER_H
