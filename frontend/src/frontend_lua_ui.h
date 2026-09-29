#ifndef MSE_FRONTEND_LUA_UI_H
#define MSE_FRONTEND_LUA_UI_H

#include <stdbool.h>
#include <stddef.h>

typedef struct mse_backend_s libmse_backend_t;

// Where the picture is on screen, and how big it is in its own pixels, so an
// overlay can draw in game coordinates whatever the window is doing.
typedef struct mse_frontend_overlay_view_s {
	float x, y;			 // top-left of the picture, in screen space
	float width, height; // its size on screen, letterboxing already removed
	int	  pixels_x;		 // the frame's own resolution
	int	  pixels_y;
} mse_frontend_overlay_view_t;

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

// Runs every .lua in a directory, in name order. This is what makes overlays
// drop-in: a script in data/lua/overlays is loaded without anything having to
// list it. Missing directories are not an error.
size_t mse_frontend_lua_ui_load_directory(const char *dir);

// Runs one script directly, for reloading during development.
bool mse_frontend_lua_ui_load_script(const char *path);

// Throws the UI Lua state away and builds it again from what is on disk: the
// backend's scripts and every drop-in directory that has been loaded from.
// Panels and overlays that were open are reopened. Backs the "ui_reload"
// console command, which is how a panel is edited without restarting.
bool mse_frontend_lua_ui_reload(void);

// Draws every visible panel. Must be called inside an ImGui frame.
void mse_frontend_lua_ui_draw(void);

// Draws the panel toggles; call inside a menu.
void mse_frontend_lua_ui_draw_menu(void);

// How many panels are registered, for deciding whether to show the menu at all.
size_t mse_frontend_lua_ui_panel_count(void);

// Overlays: scripts that draw onto the picture rather than beside it. Called
// from inside the emulation window, after the frame image.
void   mse_frontend_lua_ui_draw_overlays(const mse_frontend_overlay_view_t *view);
void   mse_frontend_lua_ui_draw_overlay_menu(void);
size_t mse_frontend_lua_ui_overlay_count(void);
bool   mse_frontend_lua_ui_show_overlay(const char *id, bool visible);

// Opens or closes a panel by id, the same thing its menu entry does. Backs the
// "panel" console command, so a config or a -e script can lay out a debugging
// session without anyone reaching for the View menu.
bool mse_frontend_lua_ui_show_panel(const char *id, bool visible);

#endif // MSE_FRONTEND_LUA_UI_H
