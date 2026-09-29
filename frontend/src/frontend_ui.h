#ifndef MSE_FRONTEND_UI_H
#define MSE_FRONTEND_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_gpu.h>

#include "frontend_theme.h"
#include "frontend_widgets.h"

typedef struct mse_backend_s libmse_backend_t;

typedef struct ImVec2_c ImVec2;

typedef enum mse_frontend_nav_e {
    MSE_FRONTEND_NAV_NONE = -1,
    MSE_FRONTEND_NAV_HOME = 0,
    MSE_FRONTEND_NAV_LIBRARY,
    MSE_FRONTEND_NAV_BACKENDS,
    MSE_FRONTEND_NAV_BIOS,
    MSE_FRONTEND_NAV_MEMVIEW
} mse_frontend_nav_t;

typedef enum mse_frontend_settings_tab_e {
    MSE_FRONTEND_SETTINGS_TAB_GENERAL = 0,
    MSE_FRONTEND_SETTINGS_TAB_VIDEO,
    MSE_FRONTEND_SETTINGS_TAB_AUDIO,
    MSE_FRONTEND_SETTINGS_TAB_CONTROLS,
    MSE_FRONTEND_SETTINGS_TAB_APPEARANCE,
    MSE_FRONTEND_SETTINGS_TAB_PERFORMANCE,
    MSE_FRONTEND_SETTINGS_TAB_BEHAVIOR,
    MSE_FRONTEND_SETTINGS_TAB_ADVANCED
} mse_frontend_settings_tab_t;

typedef struct mse_frontend_ui_state_s {
    SDL_Window *window;
    mse_frontend_nav_t current_nav;
    mse_frontend_settings_tab_t settings_tab;
    struct mse_frontend_input_manager_s *input_manager;

    libmse_backend_t **backends;
    size_t          backend_count;

    bool show_demo_window;
    bool show_metrics_window;
    bool show_style_editor;
    bool show_settings_window;
    bool show_about_window;
    bool show_licence_window;
    bool show_credits_window;
    bool show_lua_debugger_window;
    bool show_controller_window;
    // int, not bool: these two are bound to cvars, and the cvar system writes
    // four bytes through the pointer it is given. As bools they clobbered the
    // flags declared after them -- "set mse_show_terminal 256" in config.cfg
    // was this bug reading show_profiler as byte 1.
    int  show_terminal;
    // Whether the menu bar stays up once a game is running. int for the same
    // reason as the flag above: it is bound to a cvar.
    int  menu_bar_in_game;
    bool show_profiler;

    bool show_power_confirm; // the modal is on screen right now
    bool confirm_on_quit;    // the preference that decides whether it appears
    bool show_installed_only;
    bool core_view_requested;
    int  fullscreen;
    int selected_lua_worker_idx;

    const char *active_backend_name;
    float content_scale;
    float sidebar_width;
    char search_filter[256];
    int selected_core_index;

    float audio_volume;
    bool audio_mute;

    char rom_path[PATH_MAX];
} mse_frontend_ui_state_t;

void mse_frontend_ui_init(mse_frontend_ui_state_t *state);
void mse_frontend_ui_draw(mse_frontend_ui_state_t *state);

// Just the menu bar, for the core view, where the rest of the frontend's
// chrome is deliberately gone. Without it there is no way to reach the View
// menu -- and so no way to turn a debug panel or a game overlay on -- without
// leaving the game first.
void mse_frontend_ui_draw_menu_bar_only(mse_frontend_ui_state_t *state);
void mse_frontend_ui_draw_settings_modal(mse_frontend_ui_state_t *state);
bool mse_frontend_ui_handle_event(mse_frontend_ui_state_t *state, const SDL_Event *event);

// Reads `path` and hands it to the active backend, switching to the core
// view on success. Returns false when there is no backend or the file
// could not be read.
bool mse_frontend_ui_load_rom(mse_frontend_ui_state_t *state, const char *path);

bool mse_frontend_ui_sidebar_row(const char *icon, const char *label, bool selected, bool accent);

// log
typedef struct libmse_debug_log_s libmse_debug_log_t;
void mse_frontend_ui_capture_log(const libmse_debug_log_t *log, void *user_data);
void mse_frontend_ui_draw_logs_view(void);

// Help/About
void mse_frontend_ui_draw_help_modal(mse_frontend_ui_state_t *state);
void mse_frontend_ui_draw_about_modal(mse_frontend_ui_state_t *state);
void mse_frontend_ui_draw_licence_modal(mse_frontend_ui_state_t *state);
void mse_frontend_ui_draw_credits_modal(mse_frontend_ui_state_t *state);

// Terminal
void mse_frontend_terminal_init(void);
void mse_frontend_terminal_log_callback(const char *message);
void mse_frontend_ui_draw_terminal(mse_frontend_ui_state_t *state);

// Lua
void mse_frontend_ui_draw_lua_debugger(mse_frontend_ui_state_t *state);

// Library
void mse_frontend_library_view_draw(mse_frontend_ui_state_t *state);
void mse_frontend_library_view_init(void);

#endif // MSE_FRONTEND_UI_H