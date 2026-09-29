#ifndef FRONTEND_THEME_H
#define FRONTEND_THEME_H

#include <stdbool.h>

#include "frontend_cimgui.h"

// Every colour the frontend draws comes from here. Views never write a literal
// of their own.
//
// The values live in cvars (mse_theme_*), and a theme is a file of assignments
// to them -- themes/midnight.cfg and the rest. There is no list of themes in
// the code: whatever .cfg files are in a themes directory are the themes.
typedef struct mse_frontend_theme_tokens_s {
    // Surfaces, from furthest back to nearest the eye. Depth is carried by
    // these steps and by the hairlines, never by drop shadows on everything.
    ImVec4 bg_base;
    ImVec4 bg_sunken;
    ImVec4 bg_raised;
    ImVec4 bg_overlay;
    ImVec4 bg_hover;
    ImVec4 bg_active;

    ImVec4 border;
    ImVec4 border_strong;

    ImVec4 text;
    ImVec4 text_muted;
    ImVec4 text_faint;
    ImVec4 text_on_accent;

    // One accent, used sparingly: selection, focus, and the single primary
    // action on a screen. accent_soft is a tint for filled backgrounds,
    // accent_glow a wash barely above the surface it sits on.
    ImVec4 accent;
    ImVec4 accent_hover;
    ImVec4 accent_active;
    ImVec4 accent_soft;
    ImVec4 accent_glow;

    ImVec4 success;
    ImVec4 warning;
    ImVec4 danger;
    ImVec4 info;

    float rounding_sm;
    float rounding_md;
    float rounding_lg;

    bool is_light;
} mse_frontend_theme_tokens_t;

// The display scale every style metric is multiplied by. Set once, at
// startup, from the monitor's content scale.
void mse_frontend_theme_set_scale(float scale);
float mse_frontend_theme_scale(void);

#define MSE_FRONTEND_THEME_NAME_MAX 64
#define MSE_FRONTEND_THEME_PATH_MAX 320

// One themes/*.cfg on disk. The three colours are read out of the file so the
// picker can draw a swatch of a theme without applying it.
typedef struct mse_frontend_theme_entry_s {
    char   name[MSE_FRONTEND_THEME_NAME_MAX];
    char   path[MSE_FRONTEND_THEME_PATH_MAX];
    ImVec4 bg_base;
    ImVec4 bg_raised;
    ImVec4 accent;
} mse_frontend_theme_entry_t;

// The themes on disk: themes/ next to the executable, then themes/ in the app
// data directory, so a theme of the user's own sits beside the shipped ones and
// a file of the same name replaces one. Returns how many were written.
size_t mse_frontend_theme_list(mse_frontend_theme_entry_t *out, size_t max);

// Runs a theme file. It is a list of cvar assignments, so this is exec and
// nothing more; the next frame's apply picks the new values up.
bool mse_frontend_theme_load(const char *path);

// The theme file last loaded this session, or "" if the colours came from a
// config rather than from choosing a theme.
const char *mse_frontend_theme_current(void);

// Reads the theme cvars into the tokens and pushes them into ImGui's style.
// Called once a frame: a theme is a set of cvars, so changing one -- from the
// console, from a themes/*.cfg, from anywhere -- shows up on the next frame
// with nothing needing to be notified.
void mse_frontend_theme_apply(void);

// The tokens for whatever theme was applied last. Valid before the first
// apply too: it starts on the default theme.
const mse_frontend_theme_tokens_t *mse_frontend_theme(void);

// Same colour at a different opacity, for the one-off washes and hairlines
// that would otherwise need a token each.
ImVec4 mse_frontend_theme_alpha(ImVec4 colour, float alpha);
ImU32 mse_frontend_theme_u32(ImVec4 colour, float alpha);

// Eases a stored value towards a target at a rate independent of frame time,
// for hover and selection transitions.
float mse_frontend_theme_ease(float current, float target, float rate);

#endif // FRONTEND_THEME_H
