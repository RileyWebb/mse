#ifndef MSE_FRONTEND_UI_H
#define MSE_FRONTEND_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include "frontend_theme.h"

typedef struct mse_backend_s mse_backend_t;

typedef struct ImVec2_c ImVec2;

typedef enum mse_frontend_nav_e {
    MSE_FRONTEND_NAV_NONE = -1,
    MSE_FRONTEND_NAV_HOME = 0,
    MSE_FRONTEND_NAV_LIBRARY,
    MSE_FRONTEND_NAV_BACKENDS,
    MSE_FRONTEND_NAV_BIOS,
    MSE_FRONTEND_NAV_MEMVIEW,
    MSE_FRONTEND_NAV_LOGS
} mse_frontend_nav_t;

typedef enum mse_frontend_settings_tab_e {
    MSE_FRONTEND_SETTINGS_TAB_GENERAL = 0,
    MSE_FRONTEND_SETTINGS_TAB_VIDEO,
    MSE_FRONTEND_SETTINGS_TAB_AUDIO,
    MSE_FRONTEND_SETTINGS_TAB_CONTROLS,
    MSE_FRONTEND_SETTINGS_TAB_APPEARANCE,
    MSE_FRONTEND_SETTINGS_TAB_BEHAVIOR,
    MSE_FRONTEND_SETTINGS_TAB_ADVANCED
} mse_frontend_settings_tab_t;

typedef struct mse_frontend_ui_state_s {
    SDL_Window *window;
    mse_frontend_theme_t theme;
    mse_frontend_nav_t current_nav;
    mse_frontend_settings_tab_t settings_tab;
    struct mse_frontend_input_manager_s *input_manager;

    mse_backend_t **backends;
    size_t          backend_count;

    bool show_demo_window;
    bool show_metrics_window;
    bool show_style_editor;
    bool show_settings_window;
    bool show_about_window;
    bool show_licence_window;
    bool show_credits_window;
    bool show_terminal;

    bool show_power_confirm;
    bool show_installed_only;
    bool core_view_requested;
    bool fullscreen;

    const char *active_backend_name;
    float content_scale;
    float sidebar_width;
    char search_filter[256];
    int selected_core_index;

    float audio_volume;
    bool audio_mute;

    int video_present_mode;

    char rom_path[PATH_MAX];
} mse_frontend_ui_state_t;

void mse_frontend_ui_init(mse_frontend_ui_state_t *state);
void mse_frontend_ui_draw(mse_frontend_ui_state_t *state);
void mse_frontend_ui_draw_settings_modal(mse_frontend_ui_state_t *state);
bool mse_frontend_ui_handle_event(mse_frontend_ui_state_t *state, const SDL_Event *event);

bool mse_frontend_ui_begin_child_window(const char *id, ImVec2 size, bool border);
void mse_frontend_ui_end_child_window(void);
bool mse_frontend_ui_sidebar_row(const char *icon, const char *label, bool selected, bool accent);

// log
typedef struct debug_log_s debug_log;
void mse_frontend_ui_capture_log(const debug_log *log, void *user_data);
void mse_frontend_ui_clear_logs(void);
void mse_frontend_ui_draw_logs_view(void);

// Help/About
void mse_frontend_ui_draw_help_modal(mse_frontend_ui_state_t *state);
void mse_frontend_ui_draw_about_modal(mse_frontend_ui_state_t *state);
void mse_frontend_ui_draw_licence_modal(mse_frontend_ui_state_t *state);
void mse_frontend_ui_draw_credits_modal(mse_frontend_ui_state_t *state);

// Terminal
void mse_frontend_terminal_init(void);
void mse_frontend_ui_draw_terminal(mse_frontend_ui_state_t *state);
void mse_frontend_terminal_log_callback(const char *message);

extern float g_frontend_ui_scale;

static inline float mse_frontend_ui_px(float value)
{
	return value * g_frontend_ui_scale;
}

#endif // MSE_FRONTEND_UI_H