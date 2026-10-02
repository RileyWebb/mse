#include "frontend_theme.h"

#include "libmse/libmse_cmd.h"
#include "libmse/libmse_cvar.h"
#include "libmse/libmse_debug.h"
#include "libmse/libmse_resource.h"

#include <SDL3/SDL.h>

#include <math.h>

static float g_theme_scale = 1.0f;

// The tokens everything draws from. Filled from the colour cvars on every
// apply; nothing else writes them.
static mse_frontend_theme_tokens_t g_tokens;
static bool                        g_tokens_ready = false;

void mse_frontend_theme_set_scale(float scale) {
    g_theme_scale = (scale > 0.0f) ? scale : 1.0f;
}

float mse_frontend_theme_scale(void) {
    return g_theme_scale;
}

const mse_frontend_theme_tokens_t *mse_frontend_theme(void) {
    if (!g_tokens_ready) {
        // The cvars hold their declared defaults until a theme file is read,
        // and those are a complete theme, so there is nothing else to fall
        // back to and no reason to keep a copy of one palette in here.
        mse_frontend_theme_apply();
    }
    return &g_tokens;
}

// --- one-off colour and animation helpers ------------------------------------

ImVec4 mse_frontend_theme_alpha(ImVec4 colour, float alpha) {
    // Scales the opacity the colour already has. Callers pass a fade -- an
    // animation's 0..1, a hover strength -- and a token that carries its own
    // opacity, accent_soft at 0.14 say, has to keep it: setting the alpha
    // instead turned the selected sidebar row into a slab of solid accent the
    // moment its fade reached 1. For every opaque token the two are the same.
    return (ImVec4){colour.x, colour.y, colour.z, colour.w * alpha};
}

ImU32 mse_frontend_theme_u32(ImVec4 colour, float alpha) {
    return igGetColorU32_Vec4(mse_frontend_theme_alpha(colour, alpha));
}

float mse_frontend_theme_ease(float current, float target, float rate) {
    // Exponential rather than a fixed step per frame, so a transition takes the
    // same time at 30fps as at 240.
    const ImGuiIO *io = igGetIO_Nil();
    const float    dt = (io != NULL && io->DeltaTime > 0.0f) ? io->DeltaTime : (1.0f / 60.0f);

    const float blend = 1.0f - expf(-rate * dt);
    return current + ((target - current) * blend);
}

// --- style metrics, as cvars -------------------------------------------------
//
// Unlike the colours these are not per-theme -- a theme is a palette, not a set
// of paddings -- so they are plain declarations holding the values that used to
// be literals below. Every one is in unscaled pixels; the display scale is
// applied where they are read, so a setting keeps its meaning on another
// monitor.

LIBMSE_CVAR_DEFINE_VEC2(g_cv_window_padding, "mse_style_window_padding", 18.0f, 16.0f,
                        "Padding inside a window, in unscaled pixels");
LIBMSE_CVAR_DEFINE_VEC2(g_cv_frame_padding, "mse_style_frame_padding", 11.0f, 6.0f,
                        "Padding inside a framed widget");
LIBMSE_CVAR_DEFINE_VEC2(g_cv_item_spacing, "mse_style_item_spacing", 9.0f, 8.0f,
                        "Space between widgets");
LIBMSE_CVAR_DEFINE_VEC2(g_cv_item_inner_spacing, "mse_style_item_inner_spacing", 7.0f, 5.0f,
                        "Space between a widget and its label");
LIBMSE_CVAR_DEFINE_VEC2(g_cv_cell_padding, "mse_style_cell_padding", 9.0f, 6.0f,
                        "Padding inside a table cell");
LIBMSE_CVAR_DEFINE_VEC2(g_cv_separator_text_padding, "mse_style_separator_text_padding", 0.0f, 8.0f,
                        "Padding around a separator's label");

LIBMSE_CVAR_DEFINE_VEC2(g_cv_window_title_align, "mse_style_window_title_align", 0.0f, 0.5f,
                        "Title alignment in a title bar, 0 to 1 on each axis");
LIBMSE_CVAR_DEFINE_VEC2(g_cv_button_text_align, "mse_style_button_text_align", 0.5f, 0.5f,
                        "Label alignment inside a button");
LIBMSE_CVAR_DEFINE_VEC2(g_cv_selectable_text_align, "mse_style_selectable_text_align", 0.0f, 0.5f,
                        "Label alignment inside a selectable");
LIBMSE_CVAR_DEFINE_VEC2(g_cv_separator_text_align, "mse_style_separator_text_align", 0.0f, 0.5f,
                        "Label alignment on a separator");

LIBMSE_CVAR_DEFINE_FLOAT(g_cv_indent_spacing, "mse_style_indent_spacing", 20.0f, "Width of one indent level");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_scrollbar_size, "mse_style_scrollbar_size", 11.0f, "Scrollbar thickness");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_grab_min_size, "mse_style_grab_min_size", 10.0f, "Smallest a slider grab gets");

LIBMSE_CVAR_DEFINE_FLOAT(g_cv_window_border, "mse_style_window_border", 1.0f, "Window border thickness");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_child_border, "mse_style_child_border", 1.0f, "Child border thickness");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_popup_border, "mse_style_popup_border", 1.0f, "Popup border thickness");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_frame_border, "mse_style_frame_border", 1.0f, "Frame border thickness");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_tab_bar_border, "mse_style_tab_bar_border", 1.0f, "Tab bar border thickness");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_tab_bar_overline, "mse_style_tab_bar_overline", 2.0f, "Line over the selected tab");

LIBMSE_CVAR_DEFINE_FLOAT(g_cv_window_rounding, "mse_style_window_rounding", 10.0f, "Window corner radius");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_child_rounding, "mse_style_child_rounding", 10.0f, "Child corner radius");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_frame_rounding, "mse_style_frame_rounding", 7.0f, "Framed widget corner radius");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_popup_rounding, "mse_style_popup_rounding", 10.0f, "Popup corner radius");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_scrollbar_rounding, "mse_style_scrollbar_rounding", 8.0f, "Scrollbar corner radius");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_grab_rounding, "mse_style_grab_rounding", 6.0f, "Slider grab corner radius");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_tab_rounding, "mse_style_tab_rounding", 8.0f, "Tab corner radius");

// The token roundings the widget library draws with, which the theme used to
// derive from the same constants as the style ones above.
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_rounding_sm, "mse_style_rounding_sm", 7.0f, "Small corner radius, for controls");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_rounding_md, "mse_style_rounding_md", 10.0f, "Medium corner radius, for cards");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_rounding_lg, "mse_style_rounding_lg", 16.0f, "Large corner radius, for panels");

LIBMSE_CVAR_DEFINE_FLOAT(g_cv_disabled_alpha, "mse_style_disabled_alpha", 0.42f, "Opacity of a disabled widget");
LIBMSE_CVAR_DEFINE_FLOAT(g_cv_alpha, "mse_style_alpha", 1.0f, "Opacity of everything");

LIBMSE_CVAR_DEFINE_INT(g_cv_antialiased_lines, "mse_style_antialiased_lines", 1,
                       "Anti-alias lines (0 = No, 1 = Yes)");
LIBMSE_CVAR_DEFINE_INT(g_cv_antialiased_fill, "mse_style_antialiased_fill", 1,
                       "Anti-alias filled shapes (0 = No, 1 = Yes)");

// --- theme colours, as cvars -------------------------------------------------
//
// Every token is a vec4 cvar, so a colour can be changed from the console or a
// config without rebuilding: "set mse_theme_accent 0.35 0.62 1 1".
//
// Unlike the metrics above these belong to a theme, so they cannot simply hold
// their values: picking a theme has to put that palette's colours back. The
// cvars are therefore a view of the theme in force -- written from the palette
// when the theme changes, and read every frame after that, so an edit sticks
// until the theme is switched.
//
// Bound by offset rather than declared one at a time because twenty-one
// near-identical declarations and two more lists to keep in step with them is
// how a token ends up missing from one of the three.

typedef struct theme_colour_binding_s {
    const char *name;
    size_t      offset;
    const char *description;
    // What the cvar is created with when no theme file has been read. These
    // are Midnight, taken from themes/midnight.cfg, so a build with the themes
    // folder missing still looks like the frontend.
    float default_value[4];
} theme_colour_binding_t;

// ImVec4 is four floats and nothing else, which is what lets a token be handed
// to the cvar system as a vector. Checked rather than assumed.
_Static_assert(sizeof(ImVec4) == 4 * sizeof(float), "theme colours are bound as vec4 cvars");

static const theme_colour_binding_t k_theme_colours[] = {
    {"mse_theme_bg_base", offsetof(mse_frontend_theme_tokens_t, bg_base), "The furthest-back surface, as r g b a", {0.0392f, 0.0392f, 0.0549f, 1.0f}},
    {"mse_theme_bg_sunken", offsetof(mse_frontend_theme_tokens_t, bg_sunken), "A surface below the base, for wells and inputs, as r g b a", {0.0235f, 0.0235f, 0.0353f, 1.0f}},
    {"mse_theme_bg_raised", offsetof(mse_frontend_theme_tokens_t, bg_raised), "A surface above the base, for cards, as r g b a", {0.0667f, 0.0667f, 0.0902f, 1.0f}},
    {"mse_theme_bg_overlay", offsetof(mse_frontend_theme_tokens_t, bg_overlay), "Popups and menus, as r g b a", {0.0902f, 0.0902f, 0.1216f, 1.0f}},
    {"mse_theme_bg_hover", offsetof(mse_frontend_theme_tokens_t, bg_hover), "A surface under the pointer, as r g b a", {0.1098f, 0.1098f, 0.1451f, 1.0f}},
    {"mse_theme_bg_active", offsetof(mse_frontend_theme_tokens_t, bg_active), "A surface being pressed, as r g b a", {0.1451f, 0.1451f, 0.1882f, 1.0f}},
    {"mse_theme_border", offsetof(mse_frontend_theme_tokens_t, border), "Hairlines, as r g b a", {0.1333f, 0.1333f, 0.1725f, 1.0f}},
    {"mse_theme_border_strong", offsetof(mse_frontend_theme_tokens_t, border_strong), "Hairlines that need to be seen, as r g b a", {0.2039f, 0.2039f, 0.2588f, 1.0f}},
    {"mse_theme_text", offsetof(mse_frontend_theme_tokens_t, text), "Body text, as r g b a", {0.9176f, 0.9176f, 0.949f, 1.0f}},
    {"mse_theme_text_muted", offsetof(mse_frontend_theme_tokens_t, text_muted), "Secondary text, as r g b a", {0.6039f, 0.6039f, 0.6745f, 1.0f}},
    {"mse_theme_text_faint", offsetof(mse_frontend_theme_tokens_t, text_faint), "Text that is barely there, as r g b a", {0.3608f, 0.3608f, 0.4196f, 1.0f}},
    {"mse_theme_text_on_accent", offsetof(mse_frontend_theme_tokens_t, text_on_accent), "Text drawn on the accent, as r g b a", {0.0431f, 0.0235f, 0.0863f, 1.0f}},
    {"mse_theme_accent", offsetof(mse_frontend_theme_tokens_t, accent), "The one accent: selection, focus, the primary action, as r g b a", {0.6392f, 0.4431f, 0.9686f, 1.0f}},
    {"mse_theme_accent_hover", offsetof(mse_frontend_theme_tokens_t, accent_hover), "The accent under the pointer, as r g b a", {0.7216f, 0.5529f, 1.0f, 1.0f}},
    {"mse_theme_accent_active", offsetof(mse_frontend_theme_tokens_t, accent_active), "The accent being pressed, as r g b a", {0.5412f, 0.3333f, 0.8784f, 1.0f}},
    {"mse_theme_accent_soft", offsetof(mse_frontend_theme_tokens_t, accent_soft), "A tint of the accent, for filled backgrounds, as r g b a", {0.6392f, 0.4431f, 0.9686f, 0.18f}},
    {"mse_theme_accent_glow", offsetof(mse_frontend_theme_tokens_t, accent_glow), "A wash of the accent, barely above its surface, as r g b a", {0.6392f, 0.4431f, 0.9686f, 0.07f}},
    {"mse_theme_success", offsetof(mse_frontend_theme_tokens_t, success), "Something worked, as r g b a", {0.2471f, 0.7255f, 0.3137f, 1.0f}},
    {"mse_theme_warning", offsetof(mse_frontend_theme_tokens_t, warning), "Something needs attention, as r g b a", {0.8235f, 0.6f, 0.1333f, 1.0f}},
    {"mse_theme_danger", offsetof(mse_frontend_theme_tokens_t, danger), "Something is wrong, or is about to be, as r g b a", {0.9725f, 0.3176f, 0.2863f, 1.0f}},
    {"mse_theme_info", offsetof(mse_frontend_theme_tokens_t, info), "Something worth saying, as r g b a", {0.3451f, 0.651f, 1.0f, 1.0f}},
};

#define THEME_COLOUR_COUNT (sizeof(k_theme_colours) / sizeof(k_theme_colours[0]))

// Resolved once. The registry is a linear scan, and looking twenty-one names up
// by string every frame would put the whole palette on the hot path.
static float *g_theme_colour_cvars[THEME_COLOUR_COUNT];

// The declared defaults below are Midnight, so a build with no theme files at
// all still looks like the frontend rather than like ImGui.
LIBMSE_CVAR_DEFINE_INT(g_cv_is_light, "mse_theme_is_light", 0,
                       "Treat the palette as light, which changes the dims derived from it");

static float *theme_colour_field(mse_frontend_theme_tokens_t *tokens, size_t offset) {
    return (float *)((char *)tokens + offset);
}

// Resolves each colour cvar's storage, once. The registry is a linear scan and
// this would otherwise be twenty-one string lookups a frame.
static void mse_frontend_theme_bind_colours(void) {
    if (g_theme_colour_cvars[0] != NULL) {
        return;
    }
    for (size_t i = 0; i < THEME_COLOUR_COUNT; ++i) {
        const theme_colour_binding_t *binding = &k_theme_colours[i];
        libmse_cvar_define_vec(binding->name, 4, binding->default_value, binding->description);
        g_theme_colour_cvars[i] = libmse_cvar_get_v(binding->name);
    }
}

// cvars -> the tokens everything draws from. One direction only: the cvars are
// the theme, and a themes/*.cfg is the only thing that writes them.
static void mse_frontend_theme_pull_colours(mse_frontend_theme_tokens_t *tokens) {
    for (size_t i = 0; i < THEME_COLOUR_COUNT; ++i) {
        if (g_theme_colour_cvars[i] != NULL) {
            memcpy(theme_colour_field(tokens, k_theme_colours[i].offset), g_theme_colour_cvars[i],
                   4 * sizeof(float));
        }
    }
    tokens->is_light = *g_cv_is_light != 0;
}

static void mse_frontend_theme_apply_metrics(ImGuiStyle *style) {
    const float s = g_theme_scale;

    style->WindowPadding     = (ImVec2){g_cv_window_padding[0] * s, g_cv_window_padding[1] * s};
    style->FramePadding      = (ImVec2){g_cv_frame_padding[0] * s, g_cv_frame_padding[1] * s};
    style->ItemSpacing       = (ImVec2){g_cv_item_spacing[0] * s, g_cv_item_spacing[1] * s};
    style->ItemInnerSpacing  = (ImVec2){g_cv_item_inner_spacing[0] * s, g_cv_item_inner_spacing[1] * s};
    style->CellPadding       = (ImVec2){g_cv_cell_padding[0] * s, g_cv_cell_padding[1] * s};
    style->TouchExtraPadding = (ImVec2){0.0f, 0.0f};
    style->IndentSpacing     = *g_cv_indent_spacing * s;
    style->ScrollbarSize     = *g_cv_scrollbar_size * s;
    style->GrabMinSize       = *g_cv_grab_min_size * s;

    // Border thicknesses stay unscaled, as they always have: a hairline is one
    // pixel on every monitor, and multiplying it turns the frontend's hairlines
    // into strokes at 2x.
    style->WindowBorderSize   = *g_cv_window_border;
    style->ChildBorderSize    = *g_cv_child_border;
    style->PopupBorderSize    = *g_cv_popup_border;
    style->FrameBorderSize    = *g_cv_frame_border;
    style->TabBarBorderSize   = *g_cv_tab_bar_border;
    style->TabBarOverlineSize = *g_cv_tab_bar_overline * s;

    style->WindowRounding    = *g_cv_window_rounding * s;
    style->ChildRounding     = *g_cv_child_rounding * s;
    style->FrameRounding     = *g_cv_frame_rounding * s;
    style->PopupRounding     = *g_cv_popup_rounding * s;
    style->ScrollbarRounding = *g_cv_scrollbar_rounding * s;
    style->GrabRounding      = *g_cv_grab_rounding * s;
    style->TabRounding       = *g_cv_tab_rounding * s;

    style->WindowTitleAlign         = (ImVec2){g_cv_window_title_align[0], g_cv_window_title_align[1]};
    style->WindowMenuButtonPosition = ImGuiDir_None;
    style->ButtonTextAlign          = (ImVec2){g_cv_button_text_align[0], g_cv_button_text_align[1]};
    style->SelectableTextAlign      = (ImVec2){g_cv_selectable_text_align[0], g_cv_selectable_text_align[1]};
    style->SeparatorTextBorderSize  = 1.0f;
    style->SeparatorTextAlign       = (ImVec2){g_cv_separator_text_align[0], g_cv_separator_text_align[1]};
    style->SeparatorTextPadding =
        (ImVec2){g_cv_separator_text_padding[0] * s, g_cv_separator_text_padding[1] * s};

    style->DisabledAlpha = *g_cv_disabled_alpha;
    style->Alpha         = *g_cv_alpha;

    style->AntiAliasedLines       = *g_cv_antialiased_lines != 0;
    style->AntiAliasedLinesUseTex = *g_cv_antialiased_lines != 0;
    style->AntiAliasedFill        = *g_cv_antialiased_fill != 0;
}

static void mse_frontend_theme_apply_colours(ImGuiStyle *style, const mse_frontend_theme_tokens_t *t) {
    ImVec4 *c = style->Colors;

    c[ImGuiCol_Text]         = t->text;
    c[ImGuiCol_TextDisabled] = t->text_faint;
    c[ImGuiCol_TextLink]     = t->accent;

    c[ImGuiCol_WindowBg] = t->bg_base;
    c[ImGuiCol_ChildBg]  = (ImVec4){0.0f, 0.0f, 0.0f, 0.0f};
    c[ImGuiCol_PopupBg]  = t->bg_overlay;

    c[ImGuiCol_Border]       = t->border;
    c[ImGuiCol_BorderShadow] = (ImVec4){0.0f, 0.0f, 0.0f, 0.0f};

    c[ImGuiCol_FrameBg]        = t->bg_sunken;
    c[ImGuiCol_FrameBgHovered] = t->bg_hover;
    c[ImGuiCol_FrameBgActive]  = t->bg_active;

    c[ImGuiCol_TitleBg]          = t->bg_raised;
    c[ImGuiCol_TitleBgActive]    = t->bg_raised;
    c[ImGuiCol_TitleBgCollapsed] = t->bg_raised;
    c[ImGuiCol_MenuBarBg]        = t->bg_raised;

    c[ImGuiCol_ScrollbarBg]          = (ImVec4){0.0f, 0.0f, 0.0f, 0.0f};
    c[ImGuiCol_ScrollbarGrab]        = mse_frontend_theme_alpha(t->border_strong, 0.85f);
    c[ImGuiCol_ScrollbarGrabHovered] = t->border_strong;
    c[ImGuiCol_ScrollbarGrabActive]  = t->accent;

    c[ImGuiCol_CheckMark]          = t->text_on_accent;
    c[ImGuiCol_CheckboxSelectedBg] = t->accent;
    c[ImGuiCol_SliderGrab]         = t->accent;
    c[ImGuiCol_SliderGrabActive]   = t->accent_hover;

    // Buttons stay quiet. The one loud action on a screen is drawn by the
    // widget layer, not by this colour.
    c[ImGuiCol_Button]        = t->bg_raised;
    c[ImGuiCol_ButtonHovered] = t->bg_hover;
    c[ImGuiCol_ButtonActive]  = t->bg_active;

    c[ImGuiCol_Header]        = t->accent_soft;
    c[ImGuiCol_HeaderHovered] = mse_frontend_theme_alpha(t->accent, 0.24f);
    c[ImGuiCol_HeaderActive]  = mse_frontend_theme_alpha(t->accent, 0.34f);

    c[ImGuiCol_Separator]        = t->border;
    c[ImGuiCol_SeparatorHovered] = t->border_strong;
    c[ImGuiCol_SeparatorActive]  = t->accent;

    c[ImGuiCol_ResizeGrip]        = (ImVec4){0.0f, 0.0f, 0.0f, 0.0f};
    c[ImGuiCol_ResizeGripHovered] = mse_frontend_theme_alpha(t->accent, 0.35f);
    c[ImGuiCol_ResizeGripActive]  = t->accent;

    c[ImGuiCol_InputTextCursor] = t->accent;

    c[ImGuiCol_Tab]                       = (ImVec4){0.0f, 0.0f, 0.0f, 0.0f};
    c[ImGuiCol_TabHovered]                = t->bg_hover;
    c[ImGuiCol_TabSelected]               = t->bg_raised;
    c[ImGuiCol_TabSelectedOverline]       = t->accent;
    c[ImGuiCol_TabDimmed]                 = (ImVec4){0.0f, 0.0f, 0.0f, 0.0f};
    c[ImGuiCol_TabDimmedSelected]         = t->bg_raised;
    c[ImGuiCol_TabDimmedSelectedOverline] = mse_frontend_theme_alpha(t->accent, 0.4f);

    c[ImGuiCol_DockingPreview] = mse_frontend_theme_alpha(t->accent, 0.35f);
    c[ImGuiCol_DockingEmptyBg] = t->bg_base;

    c[ImGuiCol_PlotLines]            = t->accent;
    c[ImGuiCol_PlotLinesHovered]     = t->accent_hover;
    c[ImGuiCol_PlotHistogram]        = t->accent;
    c[ImGuiCol_PlotHistogramHovered] = t->accent_hover;

    c[ImGuiCol_TableHeaderBg]     = t->bg_raised;
    c[ImGuiCol_TableBorderStrong] = t->border_strong;
    c[ImGuiCol_TableBorderLight]  = t->border;
    c[ImGuiCol_TableRowBg]        = (ImVec4){0.0f, 0.0f, 0.0f, 0.0f};
    c[ImGuiCol_TableRowBgAlt]     = mse_frontend_theme_alpha(t->is_light ? t->border : t->bg_raised, 0.45f);

    c[ImGuiCol_TextSelectedBg] = mse_frontend_theme_alpha(t->accent, 0.32f);
    c[ImGuiCol_TreeLines]      = t->border;

    c[ImGuiCol_DragDropTarget]   = t->accent;
    c[ImGuiCol_DragDropTargetBg] = mse_frontend_theme_alpha(t->accent, 0.2f);
    c[ImGuiCol_UnsavedMarker]    = t->warning;

    c[ImGuiCol_NavCursor]             = t->accent;
    c[ImGuiCol_NavWindowingHighlight] = mse_frontend_theme_alpha(t->accent, 0.7f);
    c[ImGuiCol_NavWindowingDimBg]     = (ImVec4){0.0f, 0.0f, 0.0f, 0.55f};
    c[ImGuiCol_ModalWindowDimBg]      = (ImVec4){0.0f, 0.0f, 0.0f, t->is_light ? 0.35f : 0.68f};
}

void mse_frontend_theme_apply(void) {
    ImGuiStyle *style = igGetStyle();
    if (style == NULL) {
        return;
    }

    mse_frontend_theme_bind_colours();
    mse_frontend_theme_pull_colours(&g_tokens);

    g_tokens.rounding_sm = *g_cv_rounding_sm * g_theme_scale;
    g_tokens.rounding_md = *g_cv_rounding_md * g_theme_scale;
    g_tokens.rounding_lg = *g_cv_rounding_lg * g_theme_scale;
    g_tokens_ready       = true;

    mse_frontend_theme_apply_metrics(style);
    mse_frontend_theme_apply_colours(style, &g_tokens);
}

// --- themes on disk ----------------------------------------------------------

static char g_current_theme[MSE_FRONTEND_THEME_PATH_MAX] = "";

// Pulls one colour out of a theme file. The files are generated and hand-edited
// lists of "set <name> \"r g b a\"", so this looks for the assignment rather
// than parsing the console's grammar a second time.
static bool theme_file_colour(const char *text, const char *cvar, ImVec4 *out) {
    char needle[96];
    snprintf(needle, sizeof(needle), "set %s", cvar);

    const char *at = strstr(text, needle);
    if (at == NULL) {
        return false;
    }

    at += strlen(needle);
    while (*at == ' ' || *at == '\t' || *at == '"') {
        at++;
    }

    float v[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (!libmse_cvar_parse_vec(at, 4, v)) {
        return false;
    }

    *out = (ImVec4){v[0], v[1], v[2], v[3]};
    return true;
}

// "midnight.cfg" -> "Midnight". The file name is the theme's name; there is no
// display name inside the file to get out of step with it.
static void theme_display_name(const char *file, char *out, size_t out_size) {
    snprintf(out, out_size, "%s", file);

    char *dot = strrchr(out, '.');
    if (dot != NULL) {
        *dot = '\0';
    }

    bool start = true;
    for (char *c = out; *c != '\0'; ++c) {
        if (*c == '_' || *c == '-') {
            *c    = ' ';
            start = true;
        } else if (start) {
            if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
            start = false;
        }
    }
}

static size_t theme_scan_directory(const char *dir, mse_frontend_theme_entry_t *out, size_t max, size_t written) {
    int    count = 0;
    char **names = SDL_GlobDirectory(dir, "*.cfg", SDL_GLOB_CASEINSENSITIVE, &count);
    if (names == NULL) {
        return written;
    }

    for (int i = 0; i < count && written < max; ++i) {
        mse_frontend_theme_entry_t entry;
        memset(&entry, 0, sizeof(entry));

        theme_display_name(names[i], entry.name, sizeof(entry.name));
        snprintf(entry.path, sizeof(entry.path), "%s/%s", dir, names[i]);

        // A file of the user's own with the same name replaces the shipped one
        // rather than appearing twice.
        bool replaced = false;
        for (size_t j = 0; j < written; ++j) {
            if (strcmp(out[j].name, entry.name) == 0) {
                replaced = true;
                snprintf(out[j].path, sizeof(out[j].path), "%s", entry.path);
                break;
            }
        }

        entry.bg_base   = (ImVec4){0.05f, 0.05f, 0.06f, 1.0f};
        entry.bg_raised = (ImVec4){0.09f, 0.09f, 0.11f, 1.0f};
        entry.accent    = (ImVec4){0.55f, 0.55f, 0.60f, 1.0f};

        FILE *file = fopen(entry.path, "rb");
        if (file != NULL) {
            char   contents[8192];
            size_t got = fread(contents, 1, sizeof(contents) - 1, file);
            contents[got] = '\0';
            fclose(file);

            theme_file_colour(contents, "mse_theme_bg_base", &entry.bg_base);
            theme_file_colour(contents, "mse_theme_bg_raised", &entry.bg_raised);
            theme_file_colour(contents, "mse_theme_accent", &entry.accent);
        }

        if (!replaced) {
            out[written++] = entry;
        }
    }

    SDL_free(names);
    return written;
}

size_t mse_frontend_theme_list(mse_frontend_theme_entry_t *out, size_t max) {
    if (out == NULL || max == 0) {
        return 0;
    }

    size_t written = theme_scan_directory("themes", out, max, 0);

    const char *appdata = libmse_resource_get_appdata_path();
    if (appdata != NULL) {
        char user_dir[MSE_FRONTEND_THEME_PATH_MAX];
        snprintf(user_dir, sizeof(user_dir), "%s/themes", appdata);
        written = theme_scan_directory(user_dir, out, max, written);
    }

    return written;
}

bool mse_frontend_theme_load(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    if (!libmse_cmd_execute("exec", 1, &path)) {
        DEBUG_ERROR("Could not read theme '%s'", path);
        return false;
    }

    snprintf(g_current_theme, sizeof(g_current_theme), "%s", path);
    return true;
}

const char *mse_frontend_theme_current(void) {
    return g_current_theme;
}
