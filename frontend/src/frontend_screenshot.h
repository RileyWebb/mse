#ifndef MSE_FRONTEND_SCREENSHOT_H
#define MSE_FRONTEND_SCREENSHOT_H

typedef struct mse_backend_s libmse_backend_t;

// The `mse_screenshot [path]` command.
//
// Lives in the frontend rather than in a backend: a backend publishes CPU
// pixels and knows nothing about files or image formats, and written here it
// works for every backend rather than one.

// Registers the command.
void mse_frontend_screenshot_init(void);

// The backend screenshots are taken from. The main loop sets this each frame;
// NULL means nothing is running, and the command says so rather than guessing.
void mse_frontend_screenshot_set_backend(libmse_backend_t *backend);

#endif // MSE_FRONTEND_SCREENSHOT_H
