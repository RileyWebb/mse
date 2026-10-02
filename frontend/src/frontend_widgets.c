#include "frontend_widgets.h"

#include "frontend_imgui.h"

#include "libmse/libmse_cvar.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TOK (mse_frontend_theme())

// --- drawing primitives -----------------------------------------------------

void mse_frontend_ui_shadow(ImDrawList *draw_list, ImVec2 min, ImVec2 max, float rounding, float strength)
{
	if (draw_list == NULL || strength <= 0.0f) {
		return;
	}

	const int   steps = 6;
	const float spread = mse_frontend_ui_px(10.0f);

	for (int i = steps; i >= 1; --i) {
		const float t      = (float)i / (float)steps;
		const float grow   = spread * t;
		const float alpha  = strength * (1.0f - t) * (1.0f - t) * 0.5f;
		const ImU32 colour = igGetColorU32_Vec4((ImVec4){0.0f, 0.0f, 0.0f, alpha});
		ImDrawList_AddRectFilled(draw_list, (ImVec2){min.x - grow, min.y - grow + (grow * 0.35f)},
								 (ImVec2){max.x + grow, max.y + grow + (grow * 0.35f)}, colour, rounding + grow, 0);
	}
}

// --- cybercore ---------------------------------------------------------------

LIBMSE_CVAR_DEFINE_INT(g_cv_cyber, "mse_ui_cyber", 1, "Cybercore UI test look (0 = off, 1 = on)");

bool mse_frontend_ui_cyber(void)
{
	return g_cv_cyber != NULL && *g_cv_cyber != 0;
}

static void mse_frontend_ui_cyber_changed(libmse_cvar_t *cvar, void *user_data)
{
	(void)cvar;
	(void)user_data;
	mse_frontend_theme_load(mse_frontend_ui_cyber() ? "themes/cybercore.cfg" : "themes/midnight.cfg");
}

void mse_frontend_ui_cyber_init(void)
{
	if (mse_frontend_ui_cyber()) {
		mse_frontend_theme_load("themes/cybercore.cfg");
	}
	libmse_cvar_register_change_cb("mse_ui_cyber", mse_frontend_ui_cyber_changed, NULL);
}

static void mse_frontend_ui_cyber_path(ImDrawList *dl, ImVec2 min, ImVec2 max, float cut)
{
	// Clockwise from the cut at the top left, which is the winding ImGui's
	// anti-aliased convex fill expects.
	ImDrawList_PathLineTo(dl, (ImVec2){min.x + cut, min.y});
	ImDrawList_PathLineTo(dl, (ImVec2){max.x, min.y});
	ImDrawList_PathLineTo(dl, (ImVec2){max.x, max.y - cut});
	ImDrawList_PathLineTo(dl, (ImVec2){max.x - cut, max.y});
	ImDrawList_PathLineTo(dl, (ImVec2){min.x, max.y});
	ImDrawList_PathLineTo(dl, (ImVec2){min.x, min.y + cut});
}

void mse_frontend_ui_cyber_chamfer(ImDrawList *dl, ImVec2 min, ImVec2 max, float cut, ImU32 colour, bool filled,
								   float thickness)
{
	if (dl == NULL) {
		return;
	}
	cut = fminf(cut, fminf(max.x - min.x, max.y - min.y) * 0.5f);
	mse_frontend_ui_cyber_path(dl, min, max, cut);
	if (filled) {
		ImDrawList_PathFillConvex(dl, colour);
	} else {
		ImDrawList_PathStroke(dl, colour, thickness, ImDrawFlags_Closed);
	}
}

void mse_frontend_ui_cyber_brackets(ImDrawList *dl, ImVec2 min, ImVec2 max, ImU32 colour)
{
	if (dl == NULL) {
		return;
	}

	const float len   = fminf(mse_frontend_ui_px(12.0f), fminf(max.x - min.x, max.y - min.y) * 0.25f);
	const float thick = fmaxf(1.0f, floorf(mse_frontend_ui_px(2.0f)));
	const float h     = thick * 0.5f;

	// Inset by half the stroke so the marks sit on the edge, not across it.
	const float x0 = min.x + h, y0 = min.y + h, x1 = max.x - h, y1 = max.y - h;

	ImDrawList_AddLine(dl, (ImVec2){x0, y0 + len}, (ImVec2){x0, y0}, colour, thick);
	ImDrawList_AddLine(dl, (ImVec2){x0, y0}, (ImVec2){x0 + len, y0}, colour, thick);
	ImDrawList_AddLine(dl, (ImVec2){x1 - len, y0}, (ImVec2){x1, y0}, colour, thick);
	ImDrawList_AddLine(dl, (ImVec2){x1, y0}, (ImVec2){x1, y0 + len}, colour, thick);
	ImDrawList_AddLine(dl, (ImVec2){x0, y1 - len}, (ImVec2){x0, y1}, colour, thick);
	ImDrawList_AddLine(dl, (ImVec2){x0, y1}, (ImVec2){x0 + len, y1}, colour, thick);
	ImDrawList_AddLine(dl, (ImVec2){x1 - len, y1}, (ImVec2){x1, y1}, colour, thick);
	ImDrawList_AddLine(dl, (ImVec2){x1, y1}, (ImVec2){x1, y1 - len}, colour, thick);
}

// Brackets on the current child window's own rectangle, drawn with clipping
// widened to it -- the child's clip rectangle stops short of its border.
static void mse_frontend_ui_cyber_decorate_window(ImVec4 colour)
{
	const ImVec2 pos  = igGetWindowPos();
	const ImVec2 size = igGetWindowSize();
	const ImVec2 max  = {pos.x + size.x, pos.y + size.y};

	ImDrawList *dl = igGetWindowDrawList();
	ImDrawList_PushClipRect(dl, pos, max, false);
	mse_frontend_ui_cyber_brackets(dl, pos, max, mse_frontend_theme_u32(colour, 0.9f));
	ImDrawList_PopClipRect(dl);
}

void mse_frontend_ui_cyber_overlay(void)
{
	if (!mse_frontend_ui_cyber()) {
		return;
	}

	const ImGuiViewport *vp = igGetMainViewport();
	ImDrawList          *dl = igGetForegroundDrawList_ViewportPtr((ImGuiViewport *)vp);

	// One dark line in every three: enough to read as a screen, faint enough
	// that text stays sharp underneath it.
	const float step = fmaxf(3.0f, floorf(mse_frontend_ui_px(3.0f)));
	const ImU32 line = igGetColorU32_Vec4((ImVec4){0.0f, 0.0f, 0.0f, 0.10f});
	for (float y = vp->Pos.y; y < vp->Pos.y + vp->Size.y; y += step) {
		ImDrawList_AddRectFilled(dl, (ImVec2){vp->Pos.x, y}, (ImVec2){vp->Pos.x + vp->Size.x, y + 1.0f}, line, 0.0f, 0);
	}
}

// Steps one UTF-8 sequence. The labels this is used for are ASCII in practice,
// but a stray multi-byte glyph must not split into mojibake.
static const char *mse_frontend_ui_utf8_next(const char *p)
{
	const unsigned char c = (unsigned char)*p;
	if (c < 0x80) return p + 1;
	if ((c & 0xE0) == 0xC0) return p + 2;
	if ((c & 0xF0) == 0xE0) return p + 3;
	if ((c & 0xF8) == 0xF0) return p + 4;
	return p + 1;
}

void mse_frontend_ui_text_tracked(ImDrawList *draw_list, ImFont *font, float size, ImVec2 pos, ImU32 colour,
								  const char *text, float tracking)
{
	if (draw_list == NULL || font == NULL || text == NULL) {
		return;
	}

	float x = pos.x;
	for (const char *p = text; *p != '\0';) {
		const char *next = mse_frontend_ui_utf8_next(p);
		ImDrawList_AddText_FontPtr(draw_list, font, size, (ImVec2){x, pos.y}, colour, p, next, 0.0f, NULL);
		x += ImFont_CalcTextSizeA(font, size, FLT_MAX, 0.0f, p, next, NULL).x + tracking;
		p = next;
	}
}

float mse_frontend_ui_text_tracked_width(ImFont *font, float size, const char *text, float tracking)
{
	if (font == NULL || text == NULL || *text == '\0') {
		return 0.0f;
	}

	float width = -tracking;
	for (const char *p = text; *p != '\0';) {
		const char *next = mse_frontend_ui_utf8_next(p);
		width += ImFont_CalcTextSizeA(font, size, FLT_MAX, 0.0f, p, next, NULL).x + tracking;
		p = next;
	}
	return width;
}

float mse_frontend_ui_anim(const char *key, bool target, float rate)
{
	ImGuiStorage *storage = igGetStateStorage();
	if (storage == NULL) {
		return target ? 1.0f : 0.0f;
	}

	const ImGuiID id      = igGetID_Str(key);
	float        *current = ImGuiStorage_GetFloatRef(storage, id, target ? 1.0f : 0.0f);
	*current              = mse_frontend_theme_ease(*current, target ? 1.0f : 0.0f, rate);
	if (fabsf(*current - (target ? 1.0f : 0.0f)) < 0.002f) {
		*current = target ? 1.0f : 0.0f;
	}
	return *current;
}

static ImVec4 mse_frontend_ui_mix(ImVec4 a, ImVec4 b, float t)
{
	return (ImVec4){a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t), a.z + ((b.z - a.z) * t),
					a.w + ((b.w - a.w) * t)};
}

// --- surfaces ---------------------------------------------------------------

bool mse_frontend_ui_surface_begin(const char *id, ImVec2 size, ImGuiWindowFlags flags)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, t->rounding_md);
	igPushStyleVar_Float(ImGuiStyleVar_ChildBorderSize, 1.0f);
	igPushStyleColor_Vec4(ImGuiCol_ChildBg, t->bg_raised);
	igPushStyleColor_Vec4(ImGuiCol_Border, t->border);
	const bool visible = igBeginChild_Str(id, size, ImGuiChildFlags_Borders, flags);
	if (mse_frontend_ui_cyber()) {
		mse_frontend_ui_cyber_decorate_window(t->accent);
	}
	return visible;
}

void mse_frontend_ui_surface_end(void)
{
	igEndChild();
	igPopStyleColor(2);
	igPopStyleVar(2);
}

bool mse_frontend_ui_card_begin(const char *id, ImVec2 size)
{
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,
						(ImVec2){mse_frontend_ui_px(18.0f), mse_frontend_ui_px(16.0f)});
	// Returns false when the card is clipped, but BeginChild pushed a window
	// regardless: card_end must still be called on that branch.
	return mse_frontend_ui_surface_begin(id, size, ImGuiWindowFlags_None);
}

void mse_frontend_ui_card_end(void)
{
	mse_frontend_ui_surface_end();
	igPopStyleVar(1);
}

// --- headings and text ------------------------------------------------------

void mse_frontend_ui_gap(float dp)
{
	igDummy((ImVec2){0.0f, mse_frontend_ui_px(dp)});
}

void mse_frontend_ui_eyebrow(const char *text)
{
	const mse_frontend_theme_tokens_t *t = TOK;
	ImFont *font = mse_frontend_imgui_font_small();
	if (font == NULL || text == NULL) {
		return;
	}

	const float  size  = mse_frontend_imgui_font_size_small();
	const float  track = mse_frontend_ui_px(1.6f);
	const ImVec2 pos   = igGetCursorScreenPos();

	char bracketed[256];
	if (mse_frontend_ui_cyber()) {
		snprintf(bracketed, sizeof(bracketed), "[ %s ]", text);
		text = bracketed;
	}

	mse_frontend_ui_text_tracked(igGetWindowDrawList(), font, size, pos, igGetColorU32_Vec4(t->accent), text, track);
	igDummy((ImVec2){mse_frontend_ui_text_tracked_width(font, size, text, track), size});
}

void mse_frontend_ui_title_text(const char *fmt, ...)
{
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
	igTextColored(TOK->text, "%s", buf);
	igPopFont();
}

void mse_frontend_ui_muted(const char *fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	igTextColored(TOK->text_muted, "%s", buf);
}

void mse_frontend_ui_page_header(const char *eyebrow, const char *title, const char *subtitle)
{
	if (eyebrow != NULL) {
		mse_frontend_ui_eyebrow(eyebrow);
		mse_frontend_ui_gap(6.0f);
	}
	if (title != NULL) {
		mse_frontend_ui_title_text("%s", title);
	}
	if (subtitle != NULL) {
		mse_frontend_ui_muted("%s", subtitle);
	}
	mse_frontend_ui_gap(10.0f);
}

void mse_frontend_ui_section(const char *label)
{
	const mse_frontend_theme_tokens_t *t = TOK;
	ImFont *font = mse_frontend_imgui_font_small();
	if (font == NULL || label == NULL) {
		return;
	}

	const float  size  = mse_frontend_imgui_font_size_small();
	const float  track = mse_frontend_ui_px(1.4f);
	const ImVec2 pos   = igGetCursorScreenPos();
	ImDrawList  *dl    = igGetWindowDrawList();

	// The cybercore look prefixes the label with a "//" in the accent.
	float prefix = 0.0f;
	if (mse_frontend_ui_cyber()) {
		const char *slash = "// ";
		mse_frontend_ui_text_tracked(dl, font, size, pos, igGetColorU32_Vec4(t->accent), slash, track);
		prefix = mse_frontend_ui_text_tracked_width(font, size, slash, track) + track;
	}

	const float width = prefix + mse_frontend_ui_text_tracked_width(font, size, label, track);
	mse_frontend_ui_text_tracked(dl, font, size, (ImVec2){pos.x + prefix, pos.y}, igGetColorU32_Vec4(t->text_muted),
								 label, track);

	// A hairline running from the label to the right edge ties the heading to
	// the block it introduces without a full-width rule above it.
	const float line_x = pos.x + width + mse_frontend_ui_px(12.0f);
	const float line_y = pos.y + (size * 0.5f);
	const float right  = pos.x + igGetContentRegionAvail().x;
	if (right > line_x) {
		ImDrawList_AddLine(dl, (ImVec2){line_x, line_y}, (ImVec2){right, line_y},
						   igGetColorU32_Vec4(t->border), 1.0f);
		if (mse_frontend_ui_cyber()) {
			const float s = floorf(mse_frontend_ui_px(3.0f));
			ImDrawList_AddRectFilled(dl, (ImVec2){right - s * 2.0f, line_y - s}, (ImVec2){right, line_y + s},
									 igGetColorU32_Vec4(t->accent), 0.0f, 0);
		}
	}

	igDummy((ImVec2){width, size});
	mse_frontend_ui_gap(4.0f);
}

// --- controls ---------------------------------------------------------------

static bool mse_frontend_ui_cyber_button(const char *label, ImVec2 size, mse_frontend_ui_button_kind_t kind)
{
	const mse_frontend_theme_tokens_t *t = TOK;
	const ImGuiStyle                  *st = igGetStyle();

	const char  *shown_end = strstr(label, "##");
	const ImVec2 text      = igCalcTextSize(label, NULL, true, 0.0f);
	if (size.x <= 0.0f) size.x = text.x + (st->FramePadding.x * 2.0f);
	if (size.y <= 0.0f) size.y = text.y + (st->FramePadding.y * 2.0f);

	const ImVec2 min     = igGetCursorScreenPos();
	const bool   pressed = igInvisibleButton(label, size, 0);
	const bool   hovered = igIsItemHovered(0);
	const bool   held    = igIsItemActive();
	const float  hot     = mse_frontend_ui_anim(label, hovered, 18.0f);

	const ImVec2 max = {min.x + size.x, min.y + size.y};
	const float  cut = fminf(mse_frontend_ui_px(9.0f), size.y * 0.32f);
	ImDrawList  *dl  = igGetWindowDrawList();

	ImVec4 fill, edge, ink;
	switch (kind) {
	case MSE_FRONTEND_UI_BUTTON_PRIMARY:
		fill = held ? t->accent_active : mse_frontend_ui_mix(t->accent, t->accent_hover, hot);
		edge = t->accent_hover;
		ink  = t->text_on_accent;
		break;
	case MSE_FRONTEND_UI_BUTTON_DANGER:
		fill = mse_frontend_theme_alpha(t->danger, 0.12f + (0.14f * hot));
		edge = t->danger;
		ink  = t->danger;
		break;
	case MSE_FRONTEND_UI_BUTTON_GHOST:
		fill = mse_frontend_theme_alpha(t->bg_hover, hot);
		edge = mse_frontend_theme_alpha(t->accent, 0.0f);
		ink  = mse_frontend_ui_mix(t->text_muted, t->accent, hot);
		break;
	case MSE_FRONTEND_UI_BUTTON_SECONDARY:
	default:
		fill = held ? t->bg_active : mse_frontend_ui_mix(t->bg_raised, t->bg_hover, hot);
		edge = mse_frontend_theme_alpha(t->accent, 0.45f + (0.55f * hot));
		ink  = mse_frontend_ui_mix(t->text, t->accent, hot);
		break;
	}

	// The glow: two wider outlines, fading out, only while hovered.
	if (hot > 0.01f && kind != MSE_FRONTEND_UI_BUTTON_GHOST) {
		for (int i = 2; i >= 1; --i) {
			const float g = mse_frontend_ui_px(2.0f) * (float)i;
			mse_frontend_ui_cyber_chamfer(dl, (ImVec2){min.x - g, min.y - g}, (ImVec2){max.x + g, max.y + g}, cut + g,
										  mse_frontend_theme_u32(edge, 0.16f * hot / (float)i), false,
										  mse_frontend_ui_px(2.0f));
		}
	}

	mse_frontend_ui_cyber_chamfer(dl, min, max, cut, igGetColorU32_Vec4(fill), true, 0.0f);
	if (edge.w > 0.0f) {
		mse_frontend_ui_cyber_chamfer(dl, min, max, cut, igGetColorU32_Vec4(edge), false, 1.0f);
	}

	const ImVec2 at = {floorf(min.x + ((size.x - text.x) * st->ButtonTextAlign.x)),
					   floorf(min.y + ((size.y - text.y) * st->ButtonTextAlign.y))};
	ImDrawList_AddText_Vec2(dl, at, igGetColorU32_Vec4(ink), label, shown_end);

	return pressed;
}

bool mse_frontend_ui_button(const char *label, ImVec2 size, mse_frontend_ui_button_kind_t kind)
{
	if (mse_frontend_ui_cyber()) {
		return mse_frontend_ui_cyber_button(label, size, kind);
	}

	const mse_frontend_theme_tokens_t *t = TOK;

	ImVec4 fill, fill_hover, fill_active, text, border;

	switch (kind) {
	case MSE_FRONTEND_UI_BUTTON_PRIMARY:
		fill = t->accent; fill_hover = t->accent_hover; fill_active = t->accent_active;
		text = t->text_on_accent; border = mse_frontend_theme_alpha(t->accent_hover, 0.0f);
		break;
	case MSE_FRONTEND_UI_BUTTON_DANGER:
		fill = mse_frontend_theme_alpha(t->danger, 0.16f);
		fill_hover = mse_frontend_theme_alpha(t->danger, 0.28f);
		fill_active = mse_frontend_theme_alpha(t->danger, 0.40f);
		text = t->danger; border = mse_frontend_theme_alpha(t->danger, 0.45f);
		break;
	case MSE_FRONTEND_UI_BUTTON_GHOST:
		fill = (ImVec4){0.0f, 0.0f, 0.0f, 0.0f}; fill_hover = t->bg_hover; fill_active = t->bg_active;
		text = t->text_muted; border = (ImVec4){0.0f, 0.0f, 0.0f, 0.0f};
		break;
	case MSE_FRONTEND_UI_BUTTON_SECONDARY:
	default:
		fill = t->bg_raised; fill_hover = t->bg_hover; fill_active = t->bg_active;
		text = t->text; border = t->border;
		break;
	}

	igPushStyleColor_Vec4(ImGuiCol_Button, fill);
	igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, fill_hover);
	igPushStyleColor_Vec4(ImGuiCol_ButtonActive, fill_active);
	igPushStyleColor_Vec4(ImGuiCol_Text, text);
	igPushStyleColor_Vec4(ImGuiCol_Border, border);
	igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, t->rounding_sm);

	const bool pressed = igButton(label, size);

	igPopStyleVar(1);
	igPopStyleColor(5);
	return pressed;
}

bool mse_frontend_ui_icon_button(const char *id, const char *icon, const char *tooltip, bool active)
{
	const mse_frontend_theme_tokens_t *t = TOK;
	ImFont *icon_font = mse_frontend_imgui_font_icon();

	const float side = mse_frontend_imgui_font_size_body() + (mse_frontend_ui_px(6.0f) * 2.0f);
	const ImVec2 pos = igGetCursorScreenPos();

	igPushID_Str(id);
	const bool pressed = igInvisibleButton("##hit", (ImVec2){side, side}, 0);
	igPopID();

	const bool hovered = igIsItemHovered(0);
	const bool held    = igIsItemActive();
	const float glow   = mse_frontend_ui_anim(id, hovered || active, 14.0f);

	ImDrawList *dl = igGetWindowDrawList();
	const ImVec2 max = (ImVec2){pos.x + side, pos.y + side};

	if (glow > 0.01f || active) {
		const ImVec4 base = active ? t->accent_soft : mse_frontend_theme_alpha(t->bg_hover, glow);
		ImDrawList_AddRectFilled(dl, pos, max, igGetColorU32_Vec4(held ? t->bg_active : base), t->rounding_sm, 0);
	}
	if (active) {
		ImDrawList_AddRect(dl, pos, max, mse_frontend_theme_u32(t->accent, 0.55f), t->rounding_sm, 0, 1.0f);
	}

	if (icon_font != NULL && icon != NULL) {
		const float  size  = mse_frontend_imgui_font_size_icon() * 0.72f;
		const ImVec2 extent = ImFont_CalcTextSizeA(icon_font, size, FLT_MAX, 0.0f, icon, NULL, NULL);
		const ImVec4 colour = active ? t->accent : mse_frontend_ui_mix(t->text_muted, t->text, glow);
		ImDrawList_AddText_FontPtr(dl, icon_font, size,
								   (ImVec2){pos.x + ((side - extent.x) * 0.5f), pos.y + ((side - extent.y) * 0.5f)},
								   igGetColorU32_Vec4(colour), icon, NULL, 0.0f, NULL);
	}

	if (hovered && tooltip != NULL) {
		igSetTooltip("%s", tooltip);
	}
	return pressed;
}

bool mse_frontend_ui_toggle(const char *id, bool *value)
{
	if (value == NULL) {
		return false;
	}

	const mse_frontend_theme_tokens_t *t = TOK;

	const float height = mse_frontend_imgui_font_size_body() + mse_frontend_ui_px(4.0f);
	const float width  = height * 1.85f;
	const float radius = height * 0.5f;
	const ImVec2 pos   = igGetCursorScreenPos();

	igPushID_Str(id);
	const bool pressed = igInvisibleButton("##toggle", (ImVec2){width, height}, 0);
	igPopID();

	if (pressed) {
		*value = !*value;
	}

	const bool  hovered = igIsItemHovered(0);
	const float on      = mse_frontend_ui_anim(id, *value, 18.0f);

	ImDrawList *dl  = igGetWindowDrawList();
	const ImVec2 max = (ImVec2){pos.x + width, pos.y + height};

	const ImVec4 track_off = hovered ? t->bg_active : t->bg_sunken;
	const ImVec4 track     = mse_frontend_ui_mix(track_off, t->accent, on);
	ImDrawList_AddRectFilled(dl, pos, max, igGetColorU32_Vec4(track), radius, 0);
	if (on < 0.99f) {
		ImDrawList_AddRect(dl, pos, max, mse_frontend_theme_u32(t->border_strong, 1.0f - on), radius, 0, 1.0f);
	}

	const float inset  = mse_frontend_ui_px(2.5f);
	const float knob_r = radius - inset;
	const float knob_x = pos.x + radius + ((width - (radius * 2.0f)) * on);
	ImDrawList_AddCircleFilled(dl, (ImVec2){knob_x, pos.y + radius}, knob_r,
							   igGetColorU32_Vec4(mse_frontend_ui_mix(t->text_muted, t->text_on_accent, on)), 20);

	return pressed;
}

bool mse_frontend_ui_search_field(const char *id, const char *hint, char *buf, size_t buf_size, float width)
{
	const mse_frontend_theme_tokens_t *t = TOK;
	ImFont *icon_font = mse_frontend_imgui_font_icon();

	const float  glyph_size = mse_frontend_imgui_font_size_icon() * 0.62f;
	const float  inset      = mse_frontend_ui_px(12.0f) + glyph_size;
	const ImVec2 pos        = igGetCursorScreenPos();

	igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding,
						(ImVec2){inset, igGetStyle()->FramePadding.y});
	igPushStyleColor_Vec4(ImGuiCol_FrameBg, t->bg_sunken);

	igSetNextItemWidth(width > 0.0f ? width : -FLT_MIN);
	igPushID_Str(id);
	const bool changed = igInputTextWithHint("##field", hint, buf, buf_size, 0, NULL, NULL);
	igPopID();

	igPopStyleColor(1);
	igPopStyleVar(1);

	if (icon_font != NULL) {
		const float height = igGetItemRectMax().y - igGetItemRectMin().y;
		ImDrawList_AddText_FontPtr(igGetWindowDrawList(), icon_font, glyph_size,
								   (ImVec2){pos.x + mse_frontend_ui_px(9.0f),
											pos.y + ((height - glyph_size) * 0.5f)},
								   igGetColorU32_Vec4(t->text_faint), "\xEF\x80\x82", NULL, 0.0f, NULL);
	}

	return changed;
}

bool mse_frontend_ui_segmented(const char *id, const char *const *labels, int count, int *value)
{
	if (labels == NULL || value == NULL || count <= 0) {
		return false;
	}

	const mse_frontend_theme_tokens_t *t = TOK;
	const ImGuiStyle *style = igGetStyle();

	const float pad_x  = mse_frontend_ui_px(14.0f);
	const float height = mse_frontend_imgui_font_size_body() + (style->FramePadding.y * 2.0f);
	const float inset  = mse_frontend_ui_px(3.0f);

	float seg_width = 0.0f;
	for (int i = 0; i < count; ++i) {
		const ImVec2 size = igCalcTextSize(labels[i], NULL, false, 0.0f);
		if (size.x > seg_width) {
			seg_width = size.x;
		}
	}
	seg_width += pad_x * 2.0f;

	const float  total = (seg_width * (float)count) + (inset * 2.0f);
	const ImVec2 pos   = igGetCursorScreenPos();
	ImDrawList  *dl    = igGetWindowDrawList();

	ImDrawList_AddRectFilled(dl, pos, (ImVec2){pos.x + total, pos.y + height + (inset * 2.0f)},
							 igGetColorU32_Vec4(t->bg_sunken), t->rounding_sm + inset, 0);
	ImDrawList_AddRect(dl, pos, (ImVec2){pos.x + total, pos.y + height + (inset * 2.0f)},
					   igGetColorU32_Vec4(t->border), t->rounding_sm + inset, 0, 1.0f);

	// The indicator slides rather than jumping, so the eye can follow which
	// option it came from.
	char anim_key[128];
	snprintf(anim_key, sizeof(anim_key), "%s##seg_pos", id);
	ImGuiStorage *storage = igGetStateStorage();
	float *slot = ImGuiStorage_GetFloatRef(storage, igGetID_Str(anim_key), (float)*value);
	*slot = mse_frontend_theme_ease(*slot, (float)*value, 20.0f);

	const float ind_x = pos.x + inset + (seg_width * (*slot));
	ImDrawList_AddRectFilled(dl, (ImVec2){ind_x, pos.y + inset},
							 (ImVec2){ind_x + seg_width, pos.y + inset + height},
							 igGetColorU32_Vec4(t->accent_soft), t->rounding_sm, 0);
	ImDrawList_AddRect(dl, (ImVec2){ind_x, pos.y + inset}, (ImVec2){ind_x + seg_width, pos.y + inset + height},
					   mse_frontend_theme_u32(t->accent, 0.5f), t->rounding_sm, 0, 1.0f);

	bool changed = false;
	igPushID_Str(id);
	for (int i = 0; i < count; ++i) {
		const ImVec2 seg_pos = (ImVec2){pos.x + inset + (seg_width * (float)i), pos.y + inset};
		igSetCursorScreenPos(seg_pos);
		igPushID_Int(i);
		if (igInvisibleButton("##seg", (ImVec2){seg_width, height}, 0)) {
			*value  = i;
			changed = true;
		}
		const bool hovered = igIsItemHovered(0);
		igPopID();

		const ImVec4 colour = (i == *value) ? t->text : (hovered ? t->text : t->text_muted);
		const ImVec2 extent = igCalcTextSize(labels[i], NULL, false, 0.0f);
		ImDrawList_AddText_Vec2(dl,
								(ImVec2){seg_pos.x + ((seg_width - extent.x) * 0.5f),
										 seg_pos.y + ((height - extent.y) * 0.5f)},
								igGetColorU32_Vec4(colour), labels[i], NULL);
	}
	igPopID();

	igSetCursorScreenPos(pos);
	igDummy((ImVec2){total, height + (inset * 2.0f)});
	return changed;
}

void mse_frontend_ui_setting_row(const char *label, const char *help)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	igTextColored(t->text, "%s", label != NULL ? label : "");
	if (help != NULL && help[0] != '\0') {
		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
		igTextColored(t->text_faint, "%s", help);
		igPopFont();
	}
}

// --- indicators -------------------------------------------------------------

void mse_frontend_ui_badge(const char *text, ImVec4 colour)
{
	if (text == NULL) {
		return;
	}

	ImFont *font = mse_frontend_imgui_font_small();
	const float size = mse_frontend_imgui_font_size_small();
	const float pad_x = mse_frontend_ui_px(8.0f);
	const float pad_y = mse_frontend_ui_px(3.0f);

	const ImVec2 extent = font != NULL ? ImFont_CalcTextSizeA(font, size, FLT_MAX, 0.0f, text, NULL, NULL)
									   : igCalcTextSize(text, NULL, false, 0.0f);
	const ImVec2 pos = igGetCursorScreenPos();
	const ImVec2 max = (ImVec2){pos.x + extent.x + (pad_x * 2.0f), pos.y + extent.y + (pad_y * 2.0f)};

	ImDrawList *dl = igGetWindowDrawList();
	ImDrawList_AddRectFilled(dl, pos, max, mse_frontend_theme_u32(colour, 0.15f), (max.y - pos.y) * 0.5f, 0);
	ImDrawList_AddRect(dl, pos, max, mse_frontend_theme_u32(colour, 0.38f), (max.y - pos.y) * 0.5f, 0, 1.0f);
	if (font != NULL) {
		ImDrawList_AddText_FontPtr(dl, font, size, (ImVec2){pos.x + pad_x, pos.y + pad_y},
								   igGetColorU32_Vec4(colour), text, NULL, 0.0f, NULL);
	}

	igDummy((ImVec2){max.x - pos.x, max.y - pos.y});
}

void mse_frontend_ui_dot(ImVec4 colour, const char *label)
{
	const float radius = mse_frontend_ui_px(3.5f);
	const float line   = igGetTextLineHeight();
	const ImVec2 pos   = igGetCursorScreenPos();
	ImDrawList  *dl    = igGetWindowDrawList();

	// A soft halo sells the "live" reading better than the dot alone.
	ImDrawList_AddCircleFilled(dl, (ImVec2){pos.x + radius, pos.y + (line * 0.5f)}, radius * 2.4f,
							   mse_frontend_theme_u32(colour, 0.18f), 16);
	ImDrawList_AddCircleFilled(dl, (ImVec2){pos.x + radius, pos.y + (line * 0.5f)}, radius,
							   igGetColorU32_Vec4(colour), 16);

	igDummy((ImVec2){radius * 2.0f, line});
	if (label != NULL) {
		igSameLine(0.0f, mse_frontend_ui_px(7.0f));
		igTextColored(TOK->text_muted, "%s", label);
	}
}

void mse_frontend_ui_stat_tile(const char *label, const char *value, const char *note, ImVec4 colour, ImVec2 size)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	igPushID_Str(label != NULL ? label : "stat");
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,
						(ImVec2){mse_frontend_ui_px(16.0f), mse_frontend_ui_px(14.0f)});
	igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, t->rounding_md);
	igPushStyleColor_Vec4(ImGuiCol_ChildBg, t->bg_raised);
	igPushStyleColor_Vec4(ImGuiCol_Border, t->border);

	if (igBeginChild_Str("##stat", size, ImGuiChildFlags_Borders,
						 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		const ImVec2 win_pos  = igGetWindowPos();
		const ImVec2 win_size = igGetWindowSize();

		if (mse_frontend_ui_cyber()) {
			mse_frontend_ui_cyber_decorate_window(colour);
		}

		// A short accent rule at the left edge keys the tile to its metric.
		ImDrawList_AddRectFilled(igGetWindowDrawList(),
								 (ImVec2){win_pos.x, win_pos.y + mse_frontend_ui_px(14.0f)},
								 (ImVec2){win_pos.x + mse_frontend_ui_px(3.0f),
										  win_pos.y + win_size.y - mse_frontend_ui_px(14.0f)},
								 igGetColorU32_Vec4(colour), mse_frontend_ui_px(2.0f), 0);

		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
		igTextColored(t->text_faint, "%s", label != NULL ? label : "");
		igPopFont();

		igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
		igTextColored(colour, "%s", value != NULL ? value : "--");
		igPopFont();

		if (note != NULL) {
			igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
			igPushTextWrapPos(0.0f);
			igTextColored(t->text_muted, "%s", note);
			igPopTextWrapPos();
			igPopFont();
		}
	}
	igEndChild();

	igPopStyleColor(2);
	igPopStyleVar(2);
	igPopID();
}

void mse_frontend_ui_kv_row(const char *key, const char *fmt, ...)
{
	char buf[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	const mse_frontend_theme_tokens_t *t = TOK;

	igTableNextRow(0, 0);
	igTableSetColumnIndex(0);
	igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
	igTextColored(t->text_faint, "%s", key != NULL ? key : "");
	igPopFont();

	igTableSetColumnIndex(1);
	igPushTextWrapPos(0.0f);
	igTextColored(t->text, "%s", buf);
	igPopTextWrapPos();
}

void mse_frontend_ui_empty_state(const char *icon, const char *title, const char *body)
{
	const mse_frontend_theme_tokens_t *t = TOK;
	const ImVec2 avail = igGetContentRegionAvail();
	const ImVec2 pos   = igGetCursorScreenPos();

	const float block_height = mse_frontend_ui_px(150.0f);
	const float top          = pos.y + fmaxf((avail.y - block_height) * 0.4f, 0.0f);
	const float centre_x     = pos.x + (avail.x * 0.5f);

	ImDrawList *dl        = igGetWindowDrawList();
	ImFont     *icon_font = mse_frontend_imgui_font_icon();

	if (icon_font != NULL && icon != NULL) {
		const float  size   = mse_frontend_imgui_font_size_icon() * 1.9f;
		const ImVec2 extent = ImFont_CalcTextSizeA(icon_font, size, FLT_MAX, 0.0f, icon, NULL, NULL);
		const float  ring   = size * 0.92f;

		ImDrawList_AddCircleFilled(dl, (ImVec2){centre_x, top + (size * 0.5f)}, ring,
								   igGetColorU32_Vec4(t->bg_raised), 40);
		ImDrawList_AddCircle(dl, (ImVec2){centre_x, top + (size * 0.5f)}, ring,
							 igGetColorU32_Vec4(t->border), 40, 1.0f);
		ImDrawList_AddText_FontPtr(dl, icon_font, size,
								   (ImVec2){centre_x - (extent.x * 0.5f), top}, igGetColorU32_Vec4(t->text_faint),
								   icon, NULL, 0.0f, NULL);
	}

	igSetCursorScreenPos((ImVec2){pos.x, top + mse_frontend_ui_px(62.0f)});

	if (title != NULL) {
		igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
		const ImVec2 extent = igCalcTextSize(title, NULL, false, 0.0f);
		igSetCursorPosX(igGetCursorPosX() + ((avail.x - extent.x) * 0.5f));
		igTextColored(t->text, "%s", title);
		igPopFont();
	}

	if (body != NULL) {
		const float wrap = fminf(avail.x, mse_frontend_ui_px(360.0f));
		igSetCursorPosX(igGetCursorPosX() + ((avail.x - wrap) * 0.5f));
		igPushTextWrapPos(igGetCursorPosX() + wrap);
		igPushStyleColor_Vec4(ImGuiCol_Text, t->text_muted);
		igTextWrapped("%s", body);
		igPopStyleColor(1);
		igPopTextWrapPos();
	}
}

void mse_frontend_ui_backdrop(ImDrawList *draw_list, ImVec2 min, ImVec2 max)
{
	if (draw_list == NULL || max.x <= min.x || max.y <= min.y) {
		return;
	}

	const mse_frontend_theme_tokens_t *t = TOK;

	ImDrawList_AddRectFilled(draw_list, min, max, igGetColorU32_Vec4(t->bg_base), 0.0f, 0);

	if (mse_frontend_ui_cyber()) {
		// A grid in the accent, with every fourth line brighter, and the
		// accent rising off the bottom edge like a horizon.
		const float step  = floorf(mse_frontend_ui_px(28.0f));
		const ImU32 minor = mse_frontend_theme_u32(t->accent, 0.035f);
		const ImU32 major = mse_frontend_theme_u32(t->accent, 0.075f);
		int         i     = 0;
		for (float x = min.x; x < max.x; x += step, ++i) {
			ImDrawList_AddLine(draw_list, (ImVec2){x, min.y}, (ImVec2){x, max.y}, (i % 4) ? minor : major, 1.0f);
		}
		i = 0;
		for (float y = min.y; y < max.y; y += step, ++i) {
			ImDrawList_AddLine(draw_list, (ImVec2){min.x, y}, (ImVec2){max.x, y}, (i % 4) ? minor : major, 1.0f);
		}

		const ImU32 clear = mse_frontend_theme_u32(t->accent, 0.0f);
		const ImU32 rise  = mse_frontend_theme_u32(t->accent, 0.10f);
		ImDrawList_AddRectFilledMultiColor(draw_list, (ImVec2){min.x, max.y - ((max.y - min.y) * 0.35f)}, max, clear,
										   clear, rise, rise);
		return;
	}

	// One wash of accent bleeding out of the top-left corner, and a darkening
	// towards the bottom. Two gradients instead of a grid of animated dots:
	// the old version drew a couple of thousand circles every frame.
	const float  reach  = fminf((max.x - min.x) * 0.55f, mse_frontend_ui_px(560.0f));
	const ImVec2 wash   = (ImVec2){min.x + reach, min.y + reach};
	const ImU32  hot    = igGetColorU32_Vec4(t->accent_glow);
	const ImU32  clear  = mse_frontend_theme_u32(t->accent_glow, 0.0f);

	ImDrawList_AddRectFilledMultiColor(draw_list, min, wash, hot, clear, clear, clear);

	const ImU32 shade_top = igGetColorU32_Vec4((ImVec4){0.0f, 0.0f, 0.0f, 0.0f});
	const ImU32 shade_bot = igGetColorU32_Vec4((ImVec4){0.0f, 0.0f, 0.0f, t->is_light ? 0.03f : 0.22f});
	ImDrawList_AddRectFilledMultiColor(draw_list, (ImVec2){min.x, min.y + ((max.y - min.y) * 0.45f)}, max,
									   shade_top, shade_top, shade_bot, shade_bot);
}
