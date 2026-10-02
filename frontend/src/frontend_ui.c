#define DEBUG_LOG_SOURCE "frontend"
#include "frontend_ui.h"
#include "frontend_input.h"
#include "frontend_cimgui.h"
#include "frontend_imgui.h"
#include "frontend_widgets.h"
#include "frontend_logo.h"
#include "frontend_chrome.h"
#include "frontend_icons.h"
#include "frontend_app.h"
#include "libmse/libmse_debug.h"

#include "frontend_lua_ui.h"
#include "frontend_profiler.h"
#include "frontend_controller.h"
#include "libmse/libmse.h"
#include "libmse/libmse_version.h"
#include "libmse/libmse_cvar.h"
#include "libmse/libmse_cmd.h"
#include "libmse/libmse_db.h"
#include "libmse/libmse_library.h"
#include "libmse/libmse_lua.h"
#include "cimgui_markdown.h"
#include <SDL3/SDL_dialog.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TOK (mse_frontend_theme())

float g_frontend_ui_scale = 1.0f;

static Uint32 g_frontend_file_dialog_event = 0;

static void mse_frontend_ui_draw_sidebar_footer(mse_frontend_ui_state_t *state);
static void mse_frontend_ui_draw_quit_modal(mse_frontend_ui_state_t *state);
static void mse_frontend_ui_open_rom_file_dialog(mse_frontend_ui_state_t *state);
static void mse_frontend_ui_load_rom_from_path(mse_frontend_ui_state_t *state);
static void SDLCALL mse_frontend_ui_open_rom_callback(void *userdata, const char *const *filelist, int filter);

static const ImVec2 ZERO = {0.0f, 0.0f};

// --- shared chrome ----------------------------------------------------------

static void mse_frontend_ui_icon_text(const char *icon, const char *label)
{
	ImFont *icon_font = mse_frontend_imgui_font_icon();
	if (icon_font != NULL) {
		igPushFont(icon_font, mse_frontend_imgui_font_size_icon() * 0.8f);
		igTextColored(TOK->accent, "%s", icon);
		igPopFont();
		igSameLine(0, mse_frontend_ui_px(9.0f));
	}
	igTextColored(TOK->text, "%s", label);
}

// The number of titles in the library, refreshed about once a second. The
// landing page reads it every frame and the query is not free.
static int mse_frontend_ui_library_count(void)
{
	static int    cached  = -1;
	static double checked = -1.0;

	const double now = igGetTime();
	if (cached >= 0 && (now - checked) < 1.0) {
		return cached;
	}
	checked = now;

	if (g_temp_db == NULL) {
		cached = 0;
		return cached;
	}

	libmse_stmt_t *stmt = libmse_db_stmt_prepare(g_temp_db, "SELECT count(*) FROM games;");
	if (stmt == NULL) {
		cached = 0;
		return cached;
	}
	cached = (libmse_db_stmt_step(stmt) == 1) ? libmse_db_col_int(stmt, 0) : 0;
	libmse_db_stmt_finalize(stmt);
	return cached;
}

// --- sidebar ----------------------------------------------------------------

bool mse_frontend_ui_sidebar_row(const char *icon, const char *label, bool selected, bool accent)
{
	(void)accent;

	const mse_frontend_theme_tokens_t *t = TOK;

	const float  full_width = igGetContentRegionAvail().x;
	const ImVec2 item_size  = {full_width, mse_frontend_ui_px(40.0f)};
	const ImVec2 item_pos   = igGetCursorScreenPos();
	ImDrawList  *draw_list  = igGetWindowDrawList();
	const bool   icons_only = full_width < mse_frontend_ui_px(96.0f);

	igPushID_Str(label);
	const bool clicked = igInvisibleButton("##sidebar_row", item_size, 0);
	const bool hovered = igIsItemHovered(0);
	const bool held    = igIsItemActive();

	// Hover and selection both ease in, so moving down the list reads as one
	// continuous highlight rather than a hard swap per row.
	const float hot = mse_frontend_ui_anim("hot", hovered, 16.0f);
	const float on  = mse_frontend_ui_anim("on", selected, 16.0f);
	igPopID();

	const float  inset = mse_frontend_ui_px(4.0f);
	const ImVec2 row_min = {item_pos.x + inset, item_pos.y + mse_frontend_ui_px(2.0f)};
	const ImVec2 row_max = {item_pos.x + item_size.x - inset, item_pos.y + item_size.y - mse_frontend_ui_px(2.0f)};
	const float  rounding = t->rounding_sm;

	const bool cyber = mse_frontend_ui_cyber();
	const float cut  = mse_frontend_ui_px(8.0f);

	if (hot > 0.01f && on < 0.99f) {
		const ImVec4 fill = mse_frontend_theme_alpha(held ? t->bg_active : t->bg_hover, hot * (1.0f - on));
		if (cyber) {
			mse_frontend_ui_cyber_chamfer(draw_list, row_min, row_max, cut, igGetColorU32_Vec4(fill), true, 0.0f);
		} else {
			ImDrawList_AddRectFilled(draw_list, row_min, row_max, igGetColorU32_Vec4(fill), rounding, 0);
		}
	}

	if (on > 0.01f && cyber) {
		// Selected: a soft fill, a clipped outline, and a solid bar down the
		// whole left edge rather than a marker growing out of the middle.
		mse_frontend_ui_cyber_chamfer(draw_list, row_min, row_max, cut, mse_frontend_theme_u32(t->accent_soft, on), true,
									  0.0f);
		mse_frontend_ui_cyber_chamfer(draw_list, row_min, row_max, cut, mse_frontend_theme_u32(t->accent, 0.7f * on),
									  false, 1.0f);
		ImDrawList_AddRectFilled(draw_list, (ImVec2){item_pos.x, row_min.y + cut},
								 (ImVec2){item_pos.x + mse_frontend_ui_px(3.0f), row_max.y},
								 mse_frontend_theme_u32(t->accent, on), 0.0f, 0);
	} else if (on > 0.01f) {
		ImDrawList_AddRectFilled(draw_list, row_min, row_max, mse_frontend_theme_u32(t->accent_soft, on), rounding, 0);
		ImDrawList_AddRect(draw_list, row_min, row_max, mse_frontend_theme_u32(t->accent, 0.28f * on), rounding, 0, 1.0f);

		// The marker grows out of the middle of the edge as the row is chosen.
		const float half = (row_max.y - row_min.y) * 0.5f;
		const float reach = half * 0.62f * on;
		const float mid   = row_min.y + half;
		ImDrawList_AddRectFilled(draw_list, (ImVec2){item_pos.x, mid - reach},
								 (ImVec2){item_pos.x + mse_frontend_ui_px(3.0f), mid + reach},
								 mse_frontend_theme_u32(t->accent, on), mse_frontend_ui_px(2.0f), 0);
	}

	const ImVec4 text_colour = (ImVec4){
		t->text_muted.x + ((t->text.x - t->text_muted.x) * fmaxf(on, hot)),
		t->text_muted.y + ((t->text.y - t->text_muted.y) * fmaxf(on, hot)),
		t->text_muted.z + ((t->text.z - t->text_muted.z) * fmaxf(on, hot)),
		1.0f};
	const ImVec4 icon_colour = (ImVec4){
		text_colour.x + ((t->accent.x - text_colour.x) * on),
		text_colour.y + ((t->accent.y - text_colour.y) * on),
		text_colour.z + ((t->accent.z - text_colour.z) * on),
		1.0f};

	ImFont     *icon_font = mse_frontend_imgui_font_icon();
	const float icon_size = mse_frontend_imgui_font_size_icon() * 0.8f;
	const float centre_y  = item_pos.y + (item_size.y * 0.5f);

	float icon_x = item_pos.x + mse_frontend_ui_px(16.0f);
	if (icons_only && icon_font != NULL) {
		const ImVec2 extent = ImFont_CalcTextSizeA(icon_font, icon_size, FLT_MAX, 0.0f, icon, NULL, NULL);
		icon_x = item_pos.x + ((item_size.x - extent.x) * 0.5f);
	}

	if (icon_font != NULL) {
		ImDrawList_AddText_FontPtr(draw_list, icon_font, icon_size, (ImVec2){icon_x, centre_y - (icon_size * 0.55f)},
								   igGetColorU32_Vec4(icon_colour), icon, NULL, 0.0f, NULL);
	}

	if (!icons_only) {
		ImFont     *body_font = mse_frontend_imgui_font_body();
		const float body_size = mse_frontend_imgui_font_size_body();
		if (body_font != NULL && cyber) {
			char upper[64];
			size_t n = 0;
			for (; label[n] != '\0' && n < sizeof(upper) - 1; ++n) {
				upper[n] = (label[n] >= 'a' && label[n] <= 'z') ? (char)(label[n] - 32) : label[n];
			}
			upper[n] = '\0';
			const float small = mse_frontend_imgui_font_size_small() * 1.08f;
			mse_frontend_ui_text_tracked(draw_list, body_font, small,
										 (ImVec2){icon_x + icon_size + mse_frontend_ui_px(12.0f),
												  centre_y - (small * 0.56f)},
										 igGetColorU32_Vec4(text_colour), upper, mse_frontend_ui_px(1.6f));
		} else if (body_font != NULL) {
			ImDrawList_AddText_FontPtr(draw_list, body_font, body_size,
									   (ImVec2){icon_x + icon_size + mse_frontend_ui_px(12.0f),
												centre_y - (body_size * 0.56f)},
									   igGetColorU32_Vec4(text_colour), label, NULL, 0.0f, NULL);
		}
	} else if (hovered) {
		igSetTooltip("%s", label);
	}

	return clicked;
}

static bool mse_frontend_ui_sidebar_item(const char *icon, const char *label, bool selected)
{
	return mse_frontend_ui_sidebar_row(icon, label, selected, false);
}

static void mse_frontend_ui_draw_brand(bool compact)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	ImDrawList  *dl  = igGetWindowDrawList();
	const ImVec2 pos = igGetCursorScreenPos();

	ImFont     *title_font = mse_frontend_imgui_font_title();
	ImFont     *small_font = mse_frontend_imgui_font_small();
	const float name_size  = mse_frontend_imgui_font_size_title() * 0.82f;
	const float meta_size  = mse_frontend_imgui_font_size_small();

	// Sized off the two lines of text it sits beside, so the mark and the
	// wordmark stay the same height as the fonts scale.
	const float mark   = compact ? mse_frontend_ui_px(30.0f) : (name_size + meta_size);
	const float height = mark;

	mse_frontend_logo_draw(dl, pos, mark);

	if (!compact) {
		const float text_x = pos.x + mark + mse_frontend_ui_px(12.0f);
		if (title_font != NULL) {
			mse_frontend_ui_text_tracked(dl, title_font, name_size, (ImVec2){text_x, pos.y},
										 igGetColorU32_Vec4(t->text), "MSE", mse_frontend_ui_px(2.0f));
		}
		if (small_font != NULL) {
			ImDrawList_AddText_FontPtr(dl, small_font, meta_size, (ImVec2){text_x, pos.y + name_size},
									   igGetColorU32_Vec4(t->text_faint), "v" LIBMSE_VERSION_STRING, NULL, 0.0f,
									   NULL);
		}
	}

	igDummy((ImVec2){mark, height});
}

static void mse_frontend_ui_draw_sidebar(mse_frontend_ui_state_t *state, bool compact)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	const ImVec2 pos  = igGetWindowPos();
	const ImVec2 size = igGetWindowSize();
	ImDrawList  *dl   = igGetWindowDrawList();

	ImDrawList_AddRectFilled(dl, pos, (ImVec2){pos.x + size.x, pos.y + size.y}, igGetColorU32_Vec4(t->bg_raised),
							 0.0f, 0);
	ImDrawList_AddLine(dl, (ImVec2){pos.x + size.x - 1.0f, pos.y}, (ImVec2){pos.x + size.x - 1.0f, pos.y + size.y},
					   igGetColorU32_Vec4(t->border), 1.0f);

	mse_frontend_ui_gap(4.0f);
	igIndent(mse_frontend_ui_px(compact ? 10.0f : 16.0f));
	mse_frontend_ui_draw_brand(compact);
	igUnindent(mse_frontend_ui_px(compact ? 10.0f : 16.0f));
	mse_frontend_ui_gap(14.0f);

	const float footer_height = (mse_frontend_ui_px(40.0f) * 2.0f) + mse_frontend_ui_px(26.0f);

	igBeginChild_Str("SIDEBAR_NAV", (ImVec2){0.0f, -footer_height}, 0,
					 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (!compact) {
		igIndent(mse_frontend_ui_px(18.0f));
		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
		igTextColored(t->text_faint, "BROWSE");
		igPopFont();
		igUnindent(mse_frontend_ui_px(18.0f));
		mse_frontend_ui_gap(2.0f);
	}

	if (mse_frontend_ui_sidebar_item(MSE_ICON_HOME, "Home", state->current_nav == MSE_FRONTEND_NAV_HOME))
		state->current_nav = MSE_FRONTEND_NAV_HOME;
	if (mse_frontend_ui_sidebar_item(MSE_ICON_LIBRARY, "Library", state->current_nav == MSE_FRONTEND_NAV_LIBRARY))
		state->current_nav = MSE_FRONTEND_NAV_LIBRARY;
	if (mse_frontend_ui_sidebar_item(MSE_ICON_BACKENDS, "Backends", state->current_nav == MSE_FRONTEND_NAV_BACKENDS))
		state->current_nav = MSE_FRONTEND_NAV_BACKENDS;

	mse_frontend_ui_gap(10.0f);
	if (!compact) {
		igIndent(mse_frontend_ui_px(18.0f));
		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
		igTextColored(t->text_faint, "SYSTEM");
		igPopFont();
		igUnindent(mse_frontend_ui_px(18.0f));
		mse_frontend_ui_gap(2.0f);
	}

	if (mse_frontend_ui_sidebar_item(MSE_ICON_BIOS, "BIOS", state->current_nav == MSE_FRONTEND_NAV_BIOS))
		state->current_nav = MSE_FRONTEND_NAV_BIOS;
	if (mse_frontend_ui_sidebar_item(MSE_ICON_MEMVIEW, "MemView", state->current_nav == MSE_FRONTEND_NAV_MEMVIEW))
		state->current_nav = MSE_FRONTEND_NAV_MEMVIEW;
	igEndChild();

	igBeginChild_Str("SIDEBAR_FOOTER", ZERO, 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	mse_frontend_ui_draw_sidebar_footer(state);
	igEndChild();
}

static void mse_frontend_ui_draw_sidebar_footer(mse_frontend_ui_state_t *state)
{
	const ImVec2 pos = igGetWindowPos();
	ImDrawList  *dl  = igGetWindowDrawList();

	ImDrawList_AddLine(dl, (ImVec2){pos.x + mse_frontend_ui_px(12.0f), pos.y},
					   (ImVec2){pos.x + igGetWindowSize().x - mse_frontend_ui_px(12.0f), pos.y},
					   igGetColorU32_Vec4(TOK->border), 1.0f);

	mse_frontend_ui_gap(8.0f);
	if (mse_frontend_ui_sidebar_row(MSE_ICON_SETTINGS, "Settings", false, true)) {
		state->show_settings_window = true;
	}
	if (mse_frontend_ui_sidebar_row(MSE_ICON_POWER, "Quit", false, true)) {
		if (state->confirm_on_quit) {
			state->show_power_confirm = true;
		} else {
			exit(0);
		}
	}
}

// --- home -------------------------------------------------------------------

static void mse_frontend_ui_draw_hero(mse_frontend_ui_state_t *state, float width)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	const ImVec2 pos = igGetCursorScreenPos();
	const float  pad = mse_frontend_ui_px(28.0f);
	ImDrawList  *dl  = igGetWindowDrawList();

	// The card sits behind content whose height is not known until it has been
	// laid out -- the copy wraps to a different number of lines at different
	// widths, and ImGui adds its own spacing between every item. So the two go
	// into separate draw channels and are merged with the card underneath.
	ImDrawList_ChannelsSplit(dl, 2);
	ImDrawList_ChannelsSetCurrent(dl, 1);

	igSetCursorScreenPos((ImVec2){pos.x + pad, pos.y + pad});
	igBeginGroup();

	mse_frontend_ui_eyebrow("MULTI-SYSTEM EMULATOR");

	ImFont *title_font = mse_frontend_imgui_font_title();
	if (title_font != NULL) {
		const float size = mse_frontend_imgui_font_size_title() * 1.5f;
		ImDrawList_AddText_FontPtr(dl, title_font, size, igGetCursorScreenPos(), igGetColorU32_Vec4(t->text),
								   "Pick up where you left off", NULL, 0.0f, NULL);
		igDummy((ImVec2){width - (pad * 2.0f), size});
	}

	igPushTextWrapPos(igGetCursorPosX() + (width * 0.58f));
	mse_frontend_ui_muted("Load a ROM straight from disk, or browse the titles already in your library.");
	igPopTextWrapPos();

	mse_frontend_ui_gap(8.0f);
	if (mse_frontend_ui_button(MSE_ICON_START_CORE "  Open ROM",
							   (ImVec2){mse_frontend_ui_px(170.0f), mse_frontend_ui_px(40.0f)},
							   MSE_FRONTEND_UI_BUTTON_PRIMARY)) {
		mse_frontend_ui_open_rom_file_dialog(state);
	}
	igSameLine(0.0f, mse_frontend_ui_px(10.0f));
	if (mse_frontend_ui_button(MSE_ICON_LIBRARY "  Library",
							   (ImVec2){mse_frontend_ui_px(150.0f), mse_frontend_ui_px(40.0f)},
							   MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
		state->current_nav = MSE_FRONTEND_NAV_LIBRARY;
	}

	igEndGroup();

	const float  height = igGetItemRectSize().y + (pad * 2.0f);
	const ImVec2 max    = (ImVec2){pos.x + width, pos.y + height};

	ImDrawList_ChannelsSetCurrent(dl, 0);

	mse_frontend_ui_shadow(dl, pos, max, t->rounding_lg, 0.55f);
	ImDrawList_AddRectFilled(dl, pos, max, igGetColorU32_Vec4(t->bg_raised), t->rounding_lg, 0);

	// One diagonal accent sweep out of the right edge, clipped to the card so
	// the rounded corners stay clean.
	ImDrawList_PushClipRect(dl, pos, max, true);
	ImDrawList_AddRectFilledMultiColor(dl, (ImVec2){pos.x + (width * 0.35f), pos.y}, max,
									   mse_frontend_theme_u32(t->accent, 0.0f),
									   mse_frontend_theme_u32(t->accent, 0.10f),
									   mse_frontend_theme_u32(t->accent, 0.02f),
									   mse_frontend_theme_u32(t->accent, 0.0f));
	ImDrawList_AddCircleFilled(dl, (ImVec2){max.x - mse_frontend_ui_px(60.0f), pos.y - mse_frontend_ui_px(20.0f)},
							   mse_frontend_ui_px(120.0f), mse_frontend_theme_u32(t->accent, 0.07f), 48);
	ImDrawList_PopClipRect(dl);

	ImDrawList_AddRect(dl, pos, max, igGetColorU32_Vec4(t->border), t->rounding_lg, 0, 1.0f);

	ImDrawList_ChannelsMerge(dl);

	igSetCursorScreenPos(pos);
	igDummy((ImVec2){width, height});
}

static void mse_frontend_ui_draw_home_view(mse_frontend_ui_state_t *state)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	const ImVec2 content_pos  = igGetCursorScreenPos();
	const ImVec2 content_size = igGetContentRegionAvail();
	const float  pad_x        = mse_frontend_ui_px(28.0f);
	const float  pad_y        = mse_frontend_ui_px(24.0f);
	const float  gap          = mse_frontend_ui_px(14.0f);
	const float  width        = content_size.x - (pad_x * 2.0f);

	if (width < mse_frontend_ui_px(120.0f)) {
		return;
	}

	igSetCursorScreenPos((ImVec2){content_pos.x + pad_x, content_pos.y + pad_y});
	igBeginChild_Str("HOME_SCROLL", (ImVec2){width, content_size.y - (pad_y * 2.0f)}, 0,
					 ImGuiWindowFlags_NoBackground);

	mse_frontend_ui_draw_hero(state, igGetContentRegionAvail().x);
	mse_frontend_ui_gap(18.0f);

	// --- at a glance
	mse_frontend_ui_section("AT A GLANCE");
	mse_frontend_ui_gap(4.0f);

	const ImGuiIO *io          = igGetIO_Nil();
	const float    inner_width = igGetContentRegionAvail().x;
	const int      tile_count  = 4;
	const int      tile_cols   = inner_width >= mse_frontend_ui_px(760.0f) ? 4
								 : (inner_width >= mse_frontend_ui_px(420.0f) ? 2 : 1);
	const float    tile_width  = (inner_width - (gap * (float)(tile_cols - 1))) / (float)tile_cols;
	const float    tile_height = mse_frontend_ui_px(104.0f);

	char fps_buf[32], backend_buf[32], library_buf[32];
	snprintf(fps_buf, sizeof(fps_buf), "%.0f", io != NULL ? io->Framerate : 0.0f);
	snprintf(backend_buf, sizeof(backend_buf), "%zu", state->backend_count);
	snprintf(library_buf, sizeof(library_buf), "%d", mse_frontend_ui_library_count());

	const struct {
		const char *label, *value, *note;
		ImVec4      colour;
	} tiles[4] = {
		{"BACKENDS", backend_buf, "emulator cores loaded", t->accent},
		{"LIBRARY", library_buf, "titles catalogued", t->info},
		{"FRAME RATE", fps_buf, "frames per second", t->success},
		{"ACTIVE CORE", state->active_backend_name != NULL ? state->active_backend_name : "None",
		 state->active_backend_name != NULL ? "ready to run" : "load a ROM to begin",
		 state->active_backend_name != NULL ? t->success : t->text_faint},
	};

	for (int i = 0; i < tile_count; ++i) {
		if ((i % tile_cols) != 0) {
			igSameLine(0.0f, gap);
		}
		mse_frontend_ui_stat_tile(tiles[i].label, tiles[i].value, tiles[i].note, tiles[i].colour,
								  (ImVec2){tile_width, tile_height});
	}

	mse_frontend_ui_gap(20.0f);

	// --- shortcuts
	mse_frontend_ui_section("SHORTCUTS");
	mse_frontend_ui_gap(4.0f);

	const struct {
		const char        *icon, *title, *subtitle;
		mse_frontend_nav_t nav;
		bool               opens_rom;
	} cards[] = {
		{MSE_ICON_BACKENDS, "Backends", "Inspect the emulator cores that are loaded, and what each one supports.",
		 MSE_FRONTEND_NAV_BACKENDS, false},
		{MSE_ICON_LIBRARY, "Library", "Browse scraped titles, artwork and metadata from your collection.",
		 MSE_FRONTEND_NAV_LIBRARY, false},
		{MSE_ICON_START_CORE, "Open a ROM", "Load an image straight from disk with the active backend.",
		 MSE_FRONTEND_NAV_NONE, true},
	};
	const int card_count = (int)(sizeof(cards) / sizeof(cards[0]));
	const int card_cols  = inner_width >= mse_frontend_ui_px(880.0f) ? 3
						   : (inner_width >= mse_frontend_ui_px(560.0f) ? 2 : 1);
	const float card_width  = (inner_width - (gap * (float)(card_cols - 1))) / (float)card_cols;
	const float card_height = mse_frontend_ui_px(148.0f);

	for (int i = 0; i < card_count; ++i) {
		if ((i % card_cols) != 0) {
			igSameLine(0.0f, gap);
		}

		const ImVec2 card_pos = igGetCursorScreenPos();
		igPushID_Int(i);
		const bool clicked = igInvisibleButton("##shortcut", (ImVec2){card_width, card_height}, 0);
		const bool hovered = igIsItemHovered(0);
		const bool held    = igIsItemActive();
		const float hot    = mse_frontend_ui_anim("hot", hovered, 14.0f);
		igPopID();

		if (clicked) {
			if (cards[i].opens_rom) {
				mse_frontend_ui_open_rom_file_dialog(state);
			} else {
				state->current_nav = cards[i].nav;
			}
		}

		ImDrawList  *dl  = igGetWindowDrawList();
		const ImVec2 max = (ImVec2){card_pos.x + card_width, card_pos.y + card_height};
		// A couple of pixels of lift on hover, pressed back down on click.
		const float  rise = held ? 0.0f : (hot * mse_frontend_ui_px(2.0f));
		const ImVec2 rmin = (ImVec2){card_pos.x, card_pos.y - rise};
		const ImVec2 rmax = (ImVec2){max.x, max.y - rise};

		if (hot > 0.01f) {
			mse_frontend_ui_shadow(dl, rmin, rmax, t->rounding_md, hot * 0.5f);
		}
		ImDrawList_AddRectFilled(dl, rmin, rmax, igGetColorU32_Vec4(t->bg_raised), t->rounding_md, 0);
		if (hot > 0.01f) {
			ImDrawList_AddRectFilled(dl, rmin, rmax, mse_frontend_theme_u32(t->accent, 0.05f * hot), t->rounding_md, 0);
		}
		ImDrawList_AddRect(dl, rmin, rmax,
						   igGetColorU32_Vec4(hot > 0.01f
												  ? mse_frontend_theme_alpha(t->accent, 0.18f + (0.35f * hot))
												  : t->border),
						   t->rounding_md, 0, 1.0f);

		const float pad  = mse_frontend_ui_px(18.0f);
		const float tile = mse_frontend_ui_px(38.0f);
		ImDrawList_AddRectFilled(dl, (ImVec2){rmin.x + pad, rmin.y + pad},
								 (ImVec2){rmin.x + pad + tile, rmin.y + pad + tile},
								 mse_frontend_theme_u32(t->accent, 0.14f + (0.08f * hot)), t->rounding_sm, 0);

		ImFont *icon_font = mse_frontend_imgui_font_icon();
		if (icon_font != NULL) {
			const float  isize  = mse_frontend_imgui_font_size_icon() * 0.8f;
			const ImVec2 extent = ImFont_CalcTextSizeA(icon_font, isize, FLT_MAX, 0.0f, cards[i].icon, NULL, NULL);
			ImDrawList_AddText_FontPtr(dl, icon_font, isize,
									   (ImVec2){rmin.x + pad + ((tile - extent.x) * 0.5f),
												rmin.y + pad + ((tile - extent.y) * 0.5f)},
									   igGetColorU32_Vec4(t->accent), cards[i].icon, NULL, 0.0f, NULL);
		}

		ImFont *title_font = mse_frontend_imgui_font_title();
		if (title_font != NULL) {
			const float tsize = mse_frontend_imgui_font_size_title() * 0.82f;
			ImDrawList_AddText_FontPtr(dl, title_font, tsize,
									   (ImVec2){rmin.x + pad, rmin.y + pad + tile + mse_frontend_ui_px(14.0f)},
									   igGetColorU32_Vec4(t->text), cards[i].title, NULL, 0.0f, NULL);
		}

		ImFont *small_font = mse_frontend_imgui_font_small();
		if (small_font != NULL) {
			const ImVec2 sub_pos = (ImVec2){rmin.x + pad, rmin.y + pad + tile + mse_frontend_ui_px(40.0f)};
			const ImVec4 clip    = {sub_pos.x, sub_pos.y, rmax.x - pad, rmax.y - mse_frontend_ui_px(6.0f)};
			ImFont_RenderText(small_font, dl, mse_frontend_imgui_font_size_small(), sub_pos,
							  igGetColorU32_Vec4(t->text_muted), clip, cards[i].subtitle, NULL,
							  card_width - (pad * 2.0f), 0);
		}
	}

	mse_frontend_ui_gap(24.0f);
	igEndChild();
}

// --- backends ---------------------------------------------------------------

static void mse_frontend_ui_draw_backend_list(mse_frontend_ui_state_t *state)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	mse_frontend_ui_search_field("backend_search", "Filter backends...", state->search_filter,
								 sizeof(state->search_filter), 0.0f);
	mse_frontend_ui_gap(8.0f);

	const int num_backends = (int)state->backend_count;
	if (num_backends == 0) {
		mse_frontend_ui_empty_state(MSE_ICON_BACKENDS, "No backends loaded",
									"Drop a backend plugin next to the executable and restart to see it here.");
		return;
	}

	const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
								  ImGuiTableFlags_BordersInnerV;

	igPushStyleVar_Vec2(ImGuiStyleVar_CellPadding, (ImVec2){mse_frontend_ui_px(10.0f), mse_frontend_ui_px(8.0f)});
	if (igBeginTable("cores_table", 4, flags, ZERO, 0)) {
		igTableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(22.0f), 0);
		igTableSetupColumn("NAME", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
		igTableSetupColumn("VERSION", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(86.0f), 0);
		igTableSetupColumn("AUTHOR", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
		igTableSetupScrollFreeze(0, 1);

		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
		igTableHeadersRow();
		igPopFont();

		int shown = 0;
		for (int i = 0; i < num_backends; ++i) {
			libmse_backend_t *b = state->backends[i];
			if (b == NULL) {
				continue;
			}

			const char *name    = b->info.name ? b->info.name : "(unnamed)";
			const char *version = b->info.version ? b->info.version : "--";
			const char *author  = b->info.author ? b->info.author : "--";

			if (state->search_filter[0] != '\0' && strstr(name, state->search_filter) == NULL) {
				continue;
			}
			++shown;

			igTableNextRow(0, 0);

			igTableSetColumnIndex(1);
			igPushID_Int(i);
			if (igSelectable_Bool("##row", state->selected_core_index == i,
								  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ZERO)) {
				state->selected_core_index = i;
			}
			igPopID();
			igSameLine(0.0f, 0.0f);
			igTextColored(t->text, "%s", name);

			igTableSetColumnIndex(0);
			mse_frontend_ui_dot(t->success, NULL);

			igTableSetColumnIndex(2);
			igTextColored(t->text_muted, "%s", version);

			igTableSetColumnIndex(3);
			igTextColored(t->text_muted, "%s", author);
		}
		igEndTable();

		if (shown == 0) {
			mse_frontend_ui_gap(12.0f);
			mse_frontend_ui_muted("Nothing matches \"%s\".", state->search_filter);
		}
	}
	igPopStyleVar(1);
}

bool mse_frontend_ui_load_rom(mse_frontend_ui_state_t *state, const char *path)
{
	if (state == NULL || path == NULL || path[0] == '\0') {
		return false;
	}

	strncpy(state->rom_path, path, sizeof(state->rom_path) - 1);
	state->rom_path[sizeof(state->rom_path) - 1] = '\0';

	state->core_view_requested = false;
	mse_frontend_ui_load_rom_from_path(state);
	return state->core_view_requested;
}

static void mse_frontend_ui_load_rom_from_path(mse_frontend_ui_state_t *state)
{
	libmse_backend_t *active = mse_frontend_input_manager_get_backend(state->input_manager);
	if (active == NULL || state->rom_path[0] == '\0') {
		return;
	}

	FILE *f = fopen(state->rom_path, "rb");
	if (f == NULL) {
		DEBUG_ERROR("Could not open ROM '%s'", state->rom_path);
		return;
	}

	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size > 0) {
		uint8_t *data = (uint8_t *)malloc((size_t)size);
		if (data != NULL) {
			if (fread(data, 1, (size_t)size, f) == (size_t)size &&
				mse_backend_load_rom(active, data, (size_t)size)) {
				state->core_view_requested = true;
			}
			free(data);
		}
	}
	fclose(f);
}

static void mse_frontend_ui_draw_inspector_view(mse_frontend_ui_state_t *state)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	const int num_backends = (int)state->backend_count;
	libmse_backend_t *b =
		(state->selected_core_index >= 0 && state->selected_core_index < num_backends)
			? state->backends[state->selected_core_index]
			: NULL;

	if (b == NULL) {
		if (mse_frontend_ui_card_begin("INSPECTOR_EMPTY", ZERO)) {
			mse_frontend_ui_empty_state(MSE_ICON_INFO, "Nothing selected",
										"Pick a backend from the list to see its metadata and load a ROM with it.");
		}
		mse_frontend_ui_card_end();
		return;
	}

	const char *name    = b->info.name ? b->info.name : "(unnamed)";
	const char *version = b->info.version ? b->info.version : "--";
	const char *author  = b->info.author ? b->info.author : "--";
	const char *licence = b->info.licence ? b->info.licence : "--";
	const char *desc    = b->info.description ? b->info.description : "No description provided.";
	const char *built   = b->info.build_date ? b->info.build_date : "--";

	const float gap = mse_frontend_ui_px(12.0f);

	// --- identity
	if (mse_frontend_ui_card_begin("INSPECTOR_HEADER", (ImVec2){0.0f, mse_frontend_ui_px(148.0f)})) {
		ImDrawList *dl = igGetWindowDrawList();

		const float tile = mse_frontend_ui_px(52.0f);
		const ImVec2 tile_pos = igGetCursorScreenPos();
		ImDrawList_AddRectFilled(dl, tile_pos, (ImVec2){tile_pos.x + tile, tile_pos.y + tile},
								 mse_frontend_theme_u32(t->accent, 0.15f), t->rounding_md, 0);
		ImDrawList_AddRect(dl, tile_pos, (ImVec2){tile_pos.x + tile, tile_pos.y + tile},
						   mse_frontend_theme_u32(t->accent, 0.35f), t->rounding_md, 0, 1.0f);

		ImFont *icon_font = mse_frontend_imgui_font_icon();
		if (icon_font != NULL) {
			const float  isize  = mse_frontend_imgui_font_size_icon();
			const ImVec2 extent = ImFont_CalcTextSizeA(icon_font, isize, FLT_MAX, 0.0f, MSE_ICON_BACKENDS, NULL, NULL);
			ImDrawList_AddText_FontPtr(dl, icon_font, isize,
									   (ImVec2){tile_pos.x + ((tile - extent.x) * 0.5f),
												tile_pos.y + ((tile - extent.y) * 0.5f)},
									   igGetColorU32_Vec4(t->accent), MSE_ICON_BACKENDS, NULL, 0.0f, NULL);
		}

		igSetCursorScreenPos((ImVec2){tile_pos.x + tile + mse_frontend_ui_px(14.0f), tile_pos.y});
		igBeginGroup();
		mse_frontend_ui_title_text("%s", name);
		mse_frontend_ui_badge("LOADED", t->success);
		igSameLine(0.0f, mse_frontend_ui_px(6.0f));
		mse_frontend_ui_badge(version, t->accent);
		igEndGroup();

		igSetCursorScreenPos((ImVec2){tile_pos.x, tile_pos.y + tile + mse_frontend_ui_px(14.0f)});
		igPushTextWrapPos(0.0f);
		mse_frontend_ui_muted("%s", desc);
		igPopTextWrapPos();
	}
	mse_frontend_ui_card_end();

	igDummy((ImVec2){0.0f, gap});

	// --- launch
	if (mse_frontend_ui_card_begin("INSPECTOR_LAUNCH", (ImVec2){0.0f, mse_frontend_ui_px(170.0f)})) {
		mse_frontend_ui_icon_text(MSE_ICON_START_CORE, "Load a ROM");
		mse_frontend_ui_gap(8.0f);

		igPushStyleColor_Vec4(ImGuiCol_FrameBg, t->bg_sunken);
		igSetNextItemWidth(-FLT_MIN);
		igInputTextWithHint("##rom_path", "Path to a ROM image...", state->rom_path, sizeof(state->rom_path), 0, NULL,
							NULL);
		igPopStyleColor(1);

		mse_frontend_ui_gap(6.0f);
		if (mse_frontend_ui_button("Browse...", (ImVec2){mse_frontend_ui_px(110.0f), mse_frontend_ui_px(34.0f)},
								   MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
			mse_frontend_ui_open_rom_file_dialog(state);
		}
		igSameLine(0.0f, mse_frontend_ui_px(8.0f));
		if (mse_frontend_ui_button(MSE_ICON_START_CORE "  Launch", (ImVec2){-FLT_MIN, mse_frontend_ui_px(34.0f)},
								   MSE_FRONTEND_UI_BUTTON_PRIMARY)) {
			mse_frontend_ui_load_rom_from_path(state);
		}
	}
	mse_frontend_ui_card_end();

	igDummy((ImVec2){0.0f, gap});

	// --- metadata
	if (mse_frontend_ui_card_begin("INSPECTOR_DETAILS", ZERO)) {
		mse_frontend_ui_icon_text(MSE_ICON_INFO, "Details");
		mse_frontend_ui_gap(8.0f);

		if (igBeginTable("core_info_table", 2, ImGuiTableFlags_SizingStretchProp, ZERO, 0)) {
			igTableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(104.0f), 0);
			igTableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);

			mse_frontend_ui_kv_row("Author", "%s", author);
			mse_frontend_ui_kv_row("Version", "%s", version);
			mse_frontend_ui_kv_row("Licence", "%s", licence);
			mse_frontend_ui_kv_row("Built", "%s", built);
			mse_frontend_ui_kv_row("Inputs", "%zu", b->input_count);

			igEndTable();
		}
	}
	mse_frontend_ui_card_end();
}

static void mse_frontend_ui_draw_backends_view(mse_frontend_ui_state_t *state)
{
	mse_frontend_ui_page_header("CORES", "Backends",
								"Emulator plugins discovered at startup, and what each one can run.");

	const float avail_w = igGetContentRegionAvail().x;
	const float avail_h = igGetContentRegionAvail().y;
	const bool  split   = avail_w > mse_frontend_ui_px(820.0f);
	const float gap     = mse_frontend_ui_px(14.0f);
	const float list_w  = split ? (avail_w * 0.56f) : avail_w;

	if (mse_frontend_ui_card_begin("BACKEND_LIST", (ImVec2){list_w, avail_h})) {
		mse_frontend_ui_draw_backend_list(state);
	}
	mse_frontend_ui_card_end();

	if (split) {
		igSameLine(0.0f, gap);
		if (igBeginChild_Str("BACKEND_INSPECTOR", (ImVec2){0.0f, avail_h}, 0, ImGuiWindowFlags_NoBackground)) {
			mse_frontend_ui_draw_inspector_view(state);
		}
		igEndChild();
	}
}

// --- placeholder destinations ----------------------------------------------

static void mse_frontend_ui_draw_stub_view(const char *eyebrow, const char *title, const char *subtitle,
										   const char *icon, const char *body)
{
	mse_frontend_ui_page_header(eyebrow, title, subtitle);
	if (mse_frontend_ui_card_begin("STUB_CARD", ZERO)) {
		mse_frontend_ui_empty_state(icon, "Not wired up yet", body);
	}
	mse_frontend_ui_card_end();
}

// --- centre -----------------------------------------------------------------

static bool    g_frontend_dock_layout_built = false;
static ImGuiID g_frontend_dock_center_id    = 0;

static void mse_frontend_ui_draw_center_window(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}

	const float avail_width = igGetContentRegionAvail().x;
	const bool  compact     = avail_width < mse_frontend_ui_px(680.0f);
	const float sidebar_w   = compact ? mse_frontend_ui_px(68.0f) : mse_frontend_ui_px(232.0f);

	if (igBeginChild_Str("SIDEBAR_WRAPPER", (ImVec2){sidebar_w, 0.0f}, 0,
						 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
							 ImGuiWindowFlags_NoScrollWithMouse)) {
		mse_frontend_ui_draw_sidebar(state, compact);
	}
	igEndChild();

	igSameLine(0.0f, 0.0f);

	if (igBeginChild_Str("CONTENT_WRAPPER", ZERO, 0,
						 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
							 ImGuiWindowFlags_NoScrollWithMouse)) {
		const ImVec2 content_pos  = igGetCursorScreenPos();
		const ImVec2 content_size = igGetContentRegionAvail();

		mse_frontend_ui_backdrop(igGetWindowDrawList(), content_pos,
								 (ImVec2){content_pos.x + content_size.x, content_pos.y + content_size.y});

		if (state->current_nav == MSE_FRONTEND_NAV_HOME) {
			// The landing page owns its own padding: the hero runs to the card
			// edges and would look inset twice.
			mse_frontend_ui_draw_home_view(state);
		} else {
			const float pad_x = mse_frontend_ui_px(28.0f);
			const float pad_y = mse_frontend_ui_px(24.0f);
			igSetCursorScreenPos((ImVec2){content_pos.x + pad_x, content_pos.y + pad_y});

			if (igBeginChild_Str("VIEW_BODY",
								 (ImVec2){content_size.x - (pad_x * 2.0f), content_size.y - (pad_y * 2.0f)}, 0,
								 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
									 ImGuiWindowFlags_NoScrollWithMouse)) {
				switch (state->current_nav) {
				case MSE_FRONTEND_NAV_LIBRARY:
					mse_frontend_library_view_draw(state);
					break;
				case MSE_FRONTEND_NAV_BACKENDS:
					mse_frontend_ui_draw_backends_view(state);
					break;
				case MSE_FRONTEND_NAV_BIOS:
					mse_frontend_ui_draw_stub_view("SYSTEM", "BIOS", "System images backends can boot from.",
												   MSE_ICON_BIOS,
												   "BIOS management has not been built yet. Backends that need a "
												   "system image still read it from their own data directory.");
					break;
				case MSE_FRONTEND_NAV_MEMVIEW:
					mse_frontend_ui_draw_stub_view("SYSTEM", "MemView", "Raw memory of the running machine.",
												   MSE_ICON_MEMVIEW,
												   "Use the backend debug panels under Tools for now; cNES ships a "
												   "memory viewer, a disassembler and a PPU inspector.");
					break;
				default:
					break;
				}
			}
			igEndChild();
		}
	}
	igEndChild();
}

static void mse_frontend_ui_ensure_dock_layout(ImGuiViewport *viewport)
{
	if (viewport == NULL || g_frontend_dock_layout_built) {
		return;
	}

	const ImGuiID dockspace_id = igGetID_Str("MSE_DOCKSPACE");
	igDockBuilderRemoveNode(dockspace_id);
	igDockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
	igDockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

	g_frontend_dock_center_id = dockspace_id;
	ImGuiDockNode *center_node = igDockBuilderGetNode(g_frontend_dock_center_id);
	if (center_node != NULL) {
		center_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
	}

	igDockBuilderDockWindow("CENTER_DOCK", g_frontend_dock_center_id);
	igDockBuilderFinish(dockspace_id);
	igSetWindowFocus_Str("CENTER_DOCK");

	g_frontend_dock_layout_built = true;
}

// --- cvars ------------------------------------------------------------------

static void mse_cvar_fullscreen_cb(libmse_cvar_t *cvar, void *user_data)
{
	if (cvar == NULL) return;

	mse_frontend_ui_state_t *state = (mse_frontend_ui_state_t *)user_data;
	if (state == NULL || state->window == NULL) return;

	SDL_SetWindowFullscreen(state->window, *cvar->data.i);
}

static void mse_cvar_presentation_mode_cb(libmse_cvar_t *cvar, void *user_data)
{
	if (cvar == NULL) return;

	if (!SDL_WindowSupportsGPUPresentMode(g_app_ctx.gpu_device, g_app_ctx.window, *cvar->data.i)) {
		DEBUG_ERROR("Presentation mode %d is not supported on this platform, falling back to VSync", *cvar->data.i);
		g_app_ctx.presentation_mode = SDL_GPU_PRESENTMODE_VSYNC;
		// Falls through rather than returning: the swapchain is still on
		// whatever mode was in force, so the fallback has to be applied too or
		// the cvar reads VSync while the swapchain is not.
	}

	if (!SDL_SetGPUSwapchainParameters(g_app_ctx.gpu_device, g_app_ctx.window, g_app_ctx.swapchain_composition,
									   g_app_ctx.presentation_mode))
		DEBUG_ERROR("Failed to update swapchain parameters: %s", SDL_GetError());
}

// These are bound rather than declared because the values are fields of the UI
// state, which the rest of the frontend reads directly and constantly. Bound
// through LIBMSE_CVAR_BIND_*, whose _Generic rejects a pointer of the wrong
// type: an int cvar bound to a bool writes four bytes into a one-byte field and
// silently corrupts whatever is declared after it, which this file has already
// paid for once -- see the int show_terminal in frontend_ui.h.
static void mse_frontend_register_cvars(mse_frontend_ui_state_t *state)
{
	LIBMSE_CVAR_BIND_INT("mse_fullscreen", &state->fullscreen,
						 "Current fullscreen state (0 = windowed, 1 = fullscreen)");
	libmse_cvar_register_change_cb("mse_fullscreen", mse_cvar_fullscreen_cb, (void *)state);

	// Two of these are enums, not ints, and the cvar system only has int. The
	// cast is safe exactly as long as the enum is int-sized, so that is checked
	// rather than assumed -- this is the corruption the bind macros exist to
	// stop, and a cast that hides it would be worse than no macro at all.
	_Static_assert(sizeof(g_app_ctx.presentation_mode) == sizeof(int),
				   "mse_presentation_mode is bound as an int cvar");
	LIBMSE_CVAR_BIND_INT("mse_presentation_mode", (int *)&g_app_ctx.presentation_mode,
						 "Presentation mode (0 = VSync, 1 = Immediate, 2 = Mailbox)");
	libmse_cvar_register_change_cb("mse_presentation_mode", mse_cvar_presentation_mode_cb, NULL);

	LIBMSE_CVAR_BIND_FLOAT("mse_content_scale", &state->content_scale, "UI content scale factor");

	LIBMSE_CVAR_BIND_INT("mse_show_terminal", &state->show_terminal,
						 "Show terminal window (0 = No, 1 = Yes)");


	LIBMSE_CVAR_BIND_INT("mse_menu_bar_in_game", &state->menu_bar_in_game,
						 "Keep the menu bar up while a game is running (0 = No, 1 = Yes)");
}

// --- status bar -------------------------------------------------------------

static float mse_frontend_ui_status_bar_height(void)
{
	return mse_frontend_imgui_font_size_small() + mse_frontend_ui_px(18.0f);
}

static void mse_frontend_ui_draw_bottom_bar(const mse_frontend_ui_state_t *state)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	const float height = mse_frontend_ui_status_bar_height();

	igPushStyleColor_Vec4(ImGuiCol_ChildBg, t->bg_raised);
	igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, 0.0f);
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){mse_frontend_ui_px(14.0f), mse_frontend_ui_px(4.0f)});

	if (igBeginChild_Str("STATUS_BAR", (ImVec2){0.0f, height}, ImGuiChildFlags_AlwaysUseWindowPadding,
						 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		const ImVec2 pos = igGetWindowPos();
		ImDrawList_AddLine(igGetWindowDrawList(), pos, (ImVec2){pos.x + igGetWindowSize().x, pos.y},
						   igGetColorU32_Vec4(t->border), 1.0f);

		const ImGuiIO *io = igGetIO_Nil();
		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());

		const libmse_backend_state_t run_state =
			mse_backend_get_state(state != NULL ? mse_frontend_input_manager_get_backend(state->input_manager) : NULL);

		ImVec4 state_colour = t->text_faint;
		const char *state_label = "Stopped";
		switch (run_state) {
		case LIBMSE_BACKEND_RUNNING: state_colour = t->success; state_label = "Running"; break;
		case LIBMSE_BACKEND_PAUSED:  state_colour = t->warning; state_label = "Paused";  break;
		default: break;
		}
		mse_frontend_ui_dot(state_colour, state_label);

		igSameLine(0.0f, mse_frontend_ui_px(14.0f));
		igTextColored(t->border_strong, "|");
		igSameLine(0.0f, mse_frontend_ui_px(14.0f));
		igTextColored(t->text_faint, "Core");
		igSameLine(0.0f, mse_frontend_ui_px(6.0f));
		igTextColored(t->text_muted, "%s",
					  (state != NULL && state->active_backend_name != NULL) ? state->active_backend_name : "none");

		igSameLine(0.0f, mse_frontend_ui_px(14.0f));
		igTextColored(t->border_strong, "|");
		igSameLine(0.0f, mse_frontend_ui_px(14.0f));

		// The frame rate is the one number here that can go wrong, so it is
		// the one thing allowed to change colour.
		const float fps = io != NULL ? io->Framerate : 0.0f;
		const ImVec4 fps_colour = fps >= 55.0f ? t->success : (fps >= 30.0f ? t->warning : t->danger);
		igTextColored(t->text_faint, "FPS");
		igSameLine(0.0f, mse_frontend_ui_px(6.0f));
		igTextColored(fps_colour, "%.0f", fps);

		const char *version = LIBMSE_VERSION_BUILD_STRING;
		const ImVec2 extent = igCalcTextSize(version, NULL, false, 0.0f);
		const float  right  = igGetCursorPosX() + igGetContentRegionAvail().x - extent.x;
		if (right > igGetCursorPosX()) {
			igSameLine(0.0f, 0.0f);
			igSetCursorPosX(right);
		}
		igTextColored(t->text_faint, "%s", version);

		igPopFont();
	}
	igEndChild();

	igPopStyleVar(2);
	igPopStyleColor(1);
}

// --- entry points -----------------------------------------------------------

int g_frontend_log_callback_registered = 0;

void mse_frontend_ui_init(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}

	// The caller holds this as a plain local, so anything not written here is
	// stack garbage -- which is how the settings modal used to open by itself
	// on launch.
	memset(state, 0, sizeof(*state));

	state->settings_tab = MSE_FRONTEND_SETTINGS_TAB_GENERAL;
	state->current_nav  = MSE_FRONTEND_NAV_HOME;

	state->content_scale       = 1.0f;
	state->selected_core_index = -1;
	state->selected_lua_worker_idx = -1;
	state->confirm_on_quit     = true;
	state->audio_volume        = 100.0f;

	if (g_frontend_file_dialog_event == 0) {
		const Uint32 event_id = SDL_RegisterEvents(1);
		if (event_id != (Uint32)-1) {
			g_frontend_file_dialog_event = event_id;
		}
	}

	mse_frontend_library_view_init();
	mse_frontend_register_cvars(state);
}

static void mse_frontend_ui_draw_menu_bar(mse_frontend_ui_state_t *state)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, (ImVec2){mse_frontend_ui_px(11.0f), mse_frontend_ui_px(7.0f)});
	igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, (ImVec2){mse_frontend_ui_px(4.0f), mse_frontend_ui_px(4.0f)});

	if (igBeginMainMenuBar()) {
		const ImVec2 bar_pos  = igGetWindowPos();
		const ImVec2 bar_size = igGetWindowSize();
		ImDrawList_AddLine(igGetWindowDrawList(), (ImVec2){bar_pos.x, bar_pos.y + bar_size.y - 1.0f},
						   (ImVec2){bar_pos.x + bar_size.x, bar_pos.y + bar_size.y - 1.0f},
						   igGetColorU32_Vec4(t->border), 1.0f);

		mse_frontend_chrome_menu_bar_begin();

		if (igBeginMenu("File", true)) {
			if (igMenuItem_Bool("Open ROM...", "Ctrl+O", false, true)) {
				mse_frontend_ui_open_rom_file_dialog(state);
			}
			if (igMenuItem_Bool("Screenshot", NULL, false, true)) {
				libmse_cmd_execute("mse_screenshot", 0, NULL);
			}
			igSeparator();
			if (igMenuItem_Bool("Exit", "Alt+F4", false, true)) {
				if (state->confirm_on_quit) {
					state->show_power_confirm = true;
				} else {
					exit(0);
				}
			}
			igEndMenu();
		}

		{
			libmse_backend_t *active = mse_frontend_input_manager_get_backend(state->input_manager);
			const libmse_backend_state_t run_state = mse_backend_get_state(active);
			const bool has_content = run_state != LIBMSE_BACKEND_STOPPED;

			if (igBeginMenu("Emulation", true)) {
				// One item that flips, rather than two of which one is always
				// dead: the state it reports is the state to leave.
				if (run_state == LIBMSE_BACKEND_PAUSED) {
					if (igMenuItem_Bool("Resume", "F5", false, true)) {
						mse_backend_resume(active);
					}
				} else {
					if (igMenuItem_Bool("Pause", "F5", false, has_content)) {
						mse_backend_pause(active);
					}
				}

				if (igMenuItem_Bool("Stop", NULL, false, has_content)) {
					mse_backend_stop(active);
				}

				igSeparator();
				igTextDisabled("State: %s", mse_backend_state_name(run_state));
				igEndMenu();
			}
		}

		if (igBeginMenu("View", true)) {
			if (igMenuItem_Bool("Fullscreen", "F11", state->fullscreen != 0, true)) {
				state->fullscreen = !state->fullscreen;
				SDL_SetWindowFullscreen(state->window, state->fullscreen != 0);
			}
			igSeparator();
			if (igBeginMenu("Theme", true)) {
				// Whatever .cfg files are in a themes directory; there is no
				// list of themes in the code to keep in step with them.
				mse_frontend_theme_entry_t themes[32];
				const size_t               count   = mse_frontend_theme_list(themes, 32);
				const char                *current = mse_frontend_theme_current();

				if (count == 0) {
					igTextDisabled("No themes in themes/");
				}
				for (size_t i = 0; i < count; ++i) {
					const bool selected = current[0] != '\0' && strcmp(current, themes[i].path) == 0;
					if (igMenuItem_Bool(themes[i].name, NULL, selected, true)) {
						mse_frontend_theme_load(themes[i].path);
					}
				}
				igEndMenu();
			}
			igEndMenu();
		}

		if (igBeginMenu("Tools", true)) {
			igMenuItem_BoolPtr("Profiler", "F9", &state->show_profiler, true);
			if (igMenuItem_Bool("Frame counter", NULL, mse_frontend_profiler_framecounter_visible(), true)) {
				mse_frontend_profiler_set_framecounter_visible(!mse_frontend_profiler_framecounter_visible());
			}
			igSeparator();
			if (igMenuItem_Bool("Controller", NULL, state->show_controller_window, true)) {
				state->show_controller_window = !state->show_controller_window;
			}
			if (igMenuItem_Bool("Settings", NULL, false, true)) {
				state->show_settings_window = true;
			}
			if (igBeginMenu("Lua", true)) {
				if (igMenuItem_Bool("Terminal", "F10", state->show_terminal, true))
					state->show_terminal = !state->show_terminal;
				if (igMenuItem_Bool("Inspector", NULL, state->show_lua_debugger_window, true))
					state->show_lua_debugger_window = !state->show_lua_debugger_window;
				igEndMenu();
			}
			if (igBeginMenu("ImGui", true)) {
				igMenuItem_BoolPtr("Demo", NULL, &state->show_demo_window, true);
				igMenuItem_BoolPtr("Metrics", NULL, &state->show_metrics_window, true);
				igMenuItem_BoolPtr("Style editor", NULL, &state->show_style_editor, true);
				igEndMenu();
			}

			// Panels a backend defines for itself, in Lua. Contributes nothing
			// when no loaded backend ships any.
			if (mse_frontend_lua_ui_panel_count() > 0) {
				igSeparator();
				mse_frontend_lua_ui_draw_menu();
			}

			// Overlays are the other half of the same idea: scripts that draw
			// on the picture instead of beside it, so they get their own
			// submenu rather than being mixed in with the windows.
			if (mse_frontend_lua_ui_overlay_count() > 0) {
				igSeparator();
				if (igBeginMenu("Overlays", true)) {
					mse_frontend_lua_ui_draw_overlay_menu();
					igEndMenu();
				}
			}

			igEndMenu();
		}

		if (igBeginMenu("Help", true)) {
			if (igMenuItem_Bool("About", NULL, false, true)) state->show_about_window = true;
			if (igMenuItem_Bool("Licences", NULL, false, true)) state->show_licence_window = true;
			if (igMenuItem_Bool("Credits", NULL, false, true)) state->show_credits_window = true;
			igEndMenu();
		}

		mse_frontend_chrome_menu_bar_end();

		igEndMainMenuBar();
	}

	igPopStyleVar(2);
}

void mse_frontend_ui_draw_menu_bar_only(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}

	// Set here as well as in mse_frontend_ui_draw, because in the core view
	// that one is never called and every px() below would scale by whatever
	// the last frame left behind.
	g_frontend_ui_scale = state->content_scale > 0.0f ? state->content_scale : 1.0f;

	mse_frontend_ui_draw_menu_bar(state);

	// Everything the bar can open, drawn here too. A menu whose Settings item
	// does nothing is worse than no menu, and in this view mse_frontend_ui_draw
	// -- which is where these normally live -- never runs.
	//
	// Listed again rather than shared with that function: there they are drawn
	// partly inside the frontend's host window, and lifting them out to share
	// one list would change their ImGui ids and lose every saved window
	// position with it. The cost of the duplication is this comment.
	mse_frontend_ui_draw_about_modal(state);
	mse_frontend_ui_draw_licence_modal(state);
	mse_frontend_ui_draw_credits_modal(state);
	mse_frontend_ui_draw_terminal(state);
	mse_frontend_ui_draw_settings_modal(state);
	mse_frontend_ui_draw_quit_modal(state);

	if (state->show_demo_window) igShowDemoWindow(&state->show_demo_window);
	if (state->show_metrics_window) igShowMetricsWindow(&state->show_metrics_window);
	if (state->show_style_editor) igShowStyleEditor(NULL);
	if (state->show_lua_debugger_window) mse_frontend_ui_draw_lua_debugger(state);
	mse_frontend_controller_draw(state);
}

static void mse_frontend_ui_draw_quit_modal(mse_frontend_ui_state_t *state)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	if (state->show_power_confirm) {
		ImGuiViewport *viewport = igGetMainViewport();
		if (viewport != NULL) {
			const ImVec2 size = (ImVec2){mse_frontend_ui_px(400.0f), mse_frontend_ui_px(184.0f)};
			igSetNextWindowPos((ImVec2){viewport->WorkPos.x + ((viewport->WorkSize.x - size.x) * 0.5f),
										viewport->WorkPos.y + ((viewport->WorkSize.y - size.y) * 0.5f)},
							   ImGuiCond_Appearing, ZERO);
			igSetNextWindowSize(size, ImGuiCond_Appearing);
		}
		igOpenPopup_Str("EXIT_CONFIRM", 0);
	}

	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){mse_frontend_ui_px(24.0f), mse_frontend_ui_px(22.0f)});
	igPushStyleColor_Vec4(ImGuiCol_PopupBg, t->bg_overlay);

	if (igBeginPopupModal("EXIT_CONFIRM", &state->show_power_confirm,
						  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar |
							  ImGuiWindowFlags_NoSavedSettings)) {
		mse_frontend_ui_title_text("Quit MSE?");
		mse_frontend_ui_gap(4.0f);
		igPushTextWrapPos(0.0f);
		mse_frontend_ui_muted("Anything not saved by the running backend will be lost.");
		igPopTextWrapPos();

		mse_frontend_ui_gap(18.0f);
		const float button_w = (igGetContentRegionAvail().x - mse_frontend_ui_px(10.0f)) * 0.5f;
		if (mse_frontend_ui_button("Cancel", (ImVec2){button_w, mse_frontend_ui_px(36.0f)},
								   MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
			state->show_power_confirm = false;
			igCloseCurrentPopup();
		}
		igSameLine(0.0f, mse_frontend_ui_px(10.0f));
		if (mse_frontend_ui_button("Quit", (ImVec2){button_w, mse_frontend_ui_px(36.0f)},
								   MSE_FRONTEND_UI_BUTTON_DANGER)) {
			igEndPopup();
			igPopStyleColor(1);
			igPopStyleVar(1);
			exit(0);
		}
		igEndPopup();
	}

	igPopStyleColor(1);
	igPopStyleVar(1);
}

void mse_frontend_ui_draw(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}

	// content_scale is the whole interface scale, seeded from the display
	// scale at startup. The theme scales its own metrics by the same number,
	// so this must not fold the display scale in a second time.
	g_frontend_ui_scale = state->content_scale > 0.0f ? state->content_scale : 1.0f;

	mse_frontend_ui_draw_menu_bar(state);

	ImGuiViewport *viewport = igGetMainViewport();
	igSetNextWindowPos(viewport->WorkPos, ImGuiCond_Always, ZERO);
	igSetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
	igSetNextWindowViewport(viewport->ID);

	igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 0.0f);
	igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0.0f);
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, ZERO);

	const ImGuiWindowFlags host_flags =
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

	igBegin("MSE_MAIN_LAYOUT", NULL, host_flags);
	igPopStyleVar(3);

	igBeginChild_Str("MAIN_DOCKSPACE", (ImVec2){0.0f, -mse_frontend_ui_status_bar_height()}, 0,
					 ImGuiWindowFlags_NoBackground);
	mse_frontend_ui_ensure_dock_layout(viewport);
	(void)igDockSpace(igGetID_Str("MSE_DOCKSPACE"), ZERO, ImGuiDockNodeFlags_PassthruCentralNode, NULL);

	const ImGuiWindowFlags pane_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground |
										ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
										ImGuiWindowFlags_NoNavFocus;

	// No padding on the dock window: it left a dead strip of background down
	// the left of the sidebar and doubled up with the content padding on the
	// right, so the two margins did not match. The sidebar wants to sit flush
	// against the window edge, and the content area applies its own.
	igSetNextWindowDockID(g_frontend_dock_center_id, ImGuiCond_FirstUseEver);
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, ZERO);
	const bool center_open = igBegin("CENTER_DOCK", NULL, pane_flags);
	igPopStyleVar(1);
	if (center_open) {
		mse_frontend_ui_draw_center_window(state);
	}
	igEnd();

	igEndChild();

	mse_frontend_ui_draw_about_modal(state);
	mse_frontend_ui_draw_licence_modal(state);
	mse_frontend_ui_draw_credits_modal(state);

	mse_frontend_ui_draw_terminal(state);

	mse_frontend_ui_draw_bottom_bar(state);
	igEnd();

	mse_frontend_ui_draw_settings_modal(state);
	mse_frontend_ui_draw_quit_modal(state);

	if (state->show_demo_window) igShowDemoWindow(&state->show_demo_window);
	if (state->show_metrics_window) igShowMetricsWindow(&state->show_metrics_window);
	if (state->show_style_editor) igShowStyleEditor(NULL);
	if (state->show_lua_debugger_window) mse_frontend_ui_draw_lua_debugger(state);
	mse_frontend_controller_draw(state);
}

// --- file dialog ------------------------------------------------------------

static void SDLCALL mse_frontend_ui_open_rom_callback(void *userdata, const char *const *filelist, int filter)
{
	(void)userdata;
	(void)filter;

	if (g_frontend_file_dialog_event == 0) return;

	char *selected_path = NULL;
	if (filelist != NULL && filelist[0] != NULL && filelist[0][0] != '\0') {
		const size_t path_length = strlen(filelist[0]);
		selected_path            = (char *)malloc(path_length + 1);
		if (selected_path != NULL) {
			memcpy(selected_path, filelist[0], path_length + 1);
		}
	}

	SDL_Event event;
	memset(&event, 0, sizeof(event));
	event.type       = g_frontend_file_dialog_event;
	event.user.type  = g_frontend_file_dialog_event;
	event.user.code  = 0;
	event.user.data1 = selected_path;
	event.user.data2 = NULL;
	SDL_PushEvent(&event);
}

static void mse_frontend_ui_open_rom_file_dialog(mse_frontend_ui_state_t *state)
{
	static const SDL_DialogFileFilter filters[] = {{"ROM files", "nes;tnes;zip"}, {"All files", "*"}};

	if (state == NULL) return;

	const char *default_location = state->rom_path[0] != '\0' ? state->rom_path : NULL;
	SDL_ShowOpenFileDialog(mse_frontend_ui_open_rom_callback, state, state->window, filters, 2, default_location,
						   false);
}

bool mse_frontend_ui_handle_event(mse_frontend_ui_state_t *state, const SDL_Event *event)
{
	if (state == NULL || event == NULL || g_frontend_file_dialog_event == 0 ||
		event->type != g_frontend_file_dialog_event) {
		return false;
	}

	if (event->user.data1 != NULL) {
		const char *selected_path = (const char *)event->user.data1;
		strncpy(state->rom_path, selected_path, sizeof(state->rom_path) - 1);
		state->rom_path[sizeof(state->rom_path) - 1] = '\0';
		free(event->user.data1);

		// Picking a file from the dialog is the request to run it; making the
		// user press Launch afterwards was the old behaviour and read as a bug.
		mse_frontend_ui_load_rom_from_path(state);
	}

	return true;
}
