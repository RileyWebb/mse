#ifndef MSE_FRONTEND_WIDGETS_H
#define MSE_FRONTEND_WIDGETS_H

#include <stdbool.h>
#include <stddef.h>

#include "frontend_cimgui.h"
#include "frontend_theme.h"

// The shared component vocabulary. Views compose these instead of drawing
// their own rectangles, so a change to the look of a card or a badge lands
// everywhere at once and nothing drifts.

// Multiplies design-time pixel values by the display scale. Every hardcoded
// size in a view goes through it.
extern float g_frontend_ui_scale;

// --- cybercore (test branch) ------------------------------------------------
//
// mse_ui_cyber swaps the widget vocabulary's drawing for a HUD look: corners
// cut rather than rounded, brackets on the corners of cards, a grid behind the
// pages and scanlines over them. Turning it on also loads themes/cybercore.cfg,
// and turning it off loads Midnight back.

bool mse_frontend_ui_cyber(void);

// Loads the cybercore theme if the look is on, and follows the cvar from then
// on. Call once, after the startup configs have run.
void mse_frontend_ui_cyber_init(void);

// A rectangle with its top-left and bottom-right corners cut at 45 degrees.
void mse_frontend_ui_cyber_chamfer(ImDrawList *dl, ImVec2 min, ImVec2 max, float cut, ImU32 colour, bool filled,
                                   float thickness);

// L-shaped marks on the four corners of a rectangle.
void mse_frontend_ui_cyber_brackets(ImDrawList *dl, ImVec2 min, ImVec2 max, ImU32 colour);

// Scanlines over the whole main viewport. Call once per frame, last.
void mse_frontend_ui_cyber_overlay(void);

static inline float mse_frontend_ui_px(float value)
{
	return value * g_frontend_ui_scale;
}

// --- drawing primitives -----------------------------------------------------

// A soft drop shadow, built from concentric rounded rects because ImDrawList
// cannot blur. Cheap enough for the handful of surfaces that want lifting.
void mse_frontend_ui_shadow(ImDrawList *draw_list, ImVec2 min, ImVec2 max, float rounding, float strength);

// Letter-spaced text, for the small uppercase labels. ImGui has no tracking,
// so this places one glyph at a time.
void mse_frontend_ui_text_tracked(ImDrawList *draw_list, ImFont *font, float size, ImVec2 pos, ImU32 colour,
								  const char *text, float tracking);
float mse_frontend_ui_text_tracked_width(ImFont *font, float size, const char *text, float tracking);

// A 0..1 value that eases towards `target`, stored against `key` in the
// current window, so callers animating a hover need no state of their own.
float mse_frontend_ui_anim(const char *key, bool target, float rate);

// --- surfaces ---------------------------------------------------------------

// A padded, rounded, hairline-bordered panel. Pass 0 for either axis to fill.
bool mse_frontend_ui_card_begin(const char *id, ImVec2 size);
void mse_frontend_ui_card_end(void);

// Same surface, but the caller owns the padding and scrolling.
bool mse_frontend_ui_surface_begin(const char *id, ImVec2 size, ImGuiWindowFlags flags);
void mse_frontend_ui_surface_end(void);

// --- headings and text ------------------------------------------------------

void mse_frontend_ui_page_header(const char *eyebrow, const char *title, const char *subtitle);
void mse_frontend_ui_section(const char *label);
void mse_frontend_ui_eyebrow(const char *text);
void mse_frontend_ui_title_text(const char *fmt, ...);
void mse_frontend_ui_muted(const char *fmt, ...);
void mse_frontend_ui_gap(float dp);

// --- controls ---------------------------------------------------------------

typedef enum mse_frontend_ui_button_kind_e {
	MSE_FRONTEND_UI_BUTTON_PRIMARY = 0, // filled accent; at most one per screen
	MSE_FRONTEND_UI_BUTTON_SECONDARY,   // raised surface with a hairline
	MSE_FRONTEND_UI_BUTTON_GHOST,       // no fill until hovered
	MSE_FRONTEND_UI_BUTTON_DANGER
} mse_frontend_ui_button_kind_t;

bool mse_frontend_ui_button(const char *label, ImVec2 size, mse_frontend_ui_button_kind_t kind);

// A square icon button. `icon` is drawn from the icon font.
bool mse_frontend_ui_icon_button(const char *id, const char *icon, const char *tooltip, bool active);

// An animated switch. Reads better than a checkbox in a settings list.
bool mse_frontend_ui_toggle(const char *id, bool *value);

// A search box with a leading glyph, sized to `width` (0 fills the row).
bool mse_frontend_ui_search_field(const char *id, const char *hint, char *buf, size_t buf_size, float width);

// A pill of mutually exclusive options with an indicator that slides.
bool mse_frontend_ui_segmented(const char *id, const char *const *labels, int count, int *value);

// A full-width row for a settings list: label and help text on the left, the
// control the caller draws next on the right. Returns the x to draw it at.
void mse_frontend_ui_setting_row(const char *label, const char *help);

// --- indicators -------------------------------------------------------------

void mse_frontend_ui_badge(const char *text, ImVec4 colour);
void mse_frontend_ui_dot(ImVec4 colour, const char *label);
void mse_frontend_ui_stat_tile(const char *label, const char *value, const char *note, ImVec4 colour, ImVec2 size);
void mse_frontend_ui_kv_row(const char *key, const char *fmt, ...);
void mse_frontend_ui_empty_state(const char *icon, const char *title, const char *body);

// A quiet backdrop for the content area: a wash of accent at the top-left and
// a vignette at the edges. Replaces per-pixel decoration with two gradients.
void mse_frontend_ui_backdrop(ImDrawList *draw_list, ImVec2 min, ImVec2 max);

#endif // MSE_FRONTEND_WIDGETS_H
