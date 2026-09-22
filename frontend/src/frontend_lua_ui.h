#ifndef MSE_FRONTEND_LUA_UI_H
#define MSE_FRONTEND_LUA_UI_H

#include <stdbool.h>
#include <stddef.h>

typedef struct mse_backend_s libmse_backend_t;

// Lua-defined UI, for panels a backend ships rather than the frontend.
//
// This state is deliberately not one of the threaded Lua workers: ImGui is
// single-threaded and these scripts draw inside the frame the frontend has
// already begun, so everything here runs on the UI thread between NewFrame and
// Render.

// Creates the UI Lua state and loads the panel registry. Safe to call twice.
bool mse_frontend_lua_ui_init(void);
void mse_frontend_lua_ui_shutdown(void);

// Loads the UI scripts a backend declares through its lua_libraries export.
// Paths are relative to the working directory, as the backend's data is.
void mse_frontend_lua_ui_load_backend(libmse_backend_t *backend);

// Runs one script directly, for reloading during development.
bool mse_frontend_lua_ui_load_script(const char *path);

// Draws every visible panel. Must be called inside an ImGui frame.
void mse_frontend_lua_ui_draw(void);

// Draws the panel toggles; call inside a menu.
void mse_frontend_lua_ui_draw_menu(void);

// How many panels are registered, for deciding whether to show the menu at all.
size_t mse_frontend_lua_ui_panel_count(void);

#endif // MSE_FRONTEND_LUA_UI_H
