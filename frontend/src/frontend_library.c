#define DEBUG_LOG_SOURCE "frontend_library"
#include "frontend_ui.h"
#include "frontend_cimgui.h"
#include "frontend_imgui.h"
#include "frontend_widgets.h"
#include "frontend_icons.h"
#include "frontend_covers.h"
#include "libmse/libmse.h"
#include "libmse/libmse_db.h"
#include "libmse/libmse_library.h"
#include "libmse/libmse_cmd.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>

#include <float.h>
#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TOK (mse_frontend_theme())

static const ImVec2 ZERO = {0.0f, 0.0f};

typedef enum {
	LIB_VIEW_GRID = 0,
	LIB_VIEW_LIST
} lib_view_mode_t;

typedef struct {
	int  view_mode; // lib_view_mode_t; an int because the segmented control writes one
	char search_query[256];
	char add_rom_path[512];

	int64_t selected_game_id;
	char    selected_rom_path[1024];
} library_view_state_t;

static library_view_state_t s_lib_state = {
	.view_mode         = LIB_VIEW_GRID,
	.search_query      = "",
	.add_rom_path      = "",
	.selected_game_id  = -1,
	.selected_rom_path = "",
};

// Every listing selects the same shape, so the column indices below hold no
// matter which view is drawing.
#define LIB_SELECT_COLUMNS                                                       \
	"SELECT g.id, g.name, g.release_year, p.name, c.name, f.rom_path, "          \
	"length(g.artwork_blob), f.status "                                                    \
	"FROM games g "                                                              \
	"LEFT JOIN platforms p ON g.platform_id = p.id "                             \
	"LEFT JOIN companies c ON p.company_id = c.id "                              \
	"JOIN game_files f ON g.id = f.game_id "

void mse_frontend_library_view_init(void)
{
	s_lib_state.selected_game_id = -1;
	memset(s_lib_state.search_query, 0, sizeof(s_lib_state.search_query));
}

// --- toolbar ----------------------------------------------------------------

// --- adding content ---------------------------------------------------------

// Set from the SDL dialog thread, read on the UI thread. Only ever a short
// status line, so a torn read would show a garbled message and nothing worse.
static char s_lib_status[192];

// Set by the toolbar buttons, acted on by the view once it is back outside the
// card's child window. OpenPopup and BeginPopup have to agree on the id stack
// they are called from, and the card is a window of its own.
static bool s_open_add_popup    = false;
static bool s_open_manage_popup = false;

static void library_set_status(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vsnprintf(s_lib_status, sizeof(s_lib_status), fmt, args);
	va_end(args);
}

static void library_commit_add(void)
{
	if (g_temp_lib == NULL || s_lib_state.add_rom_path[0] == '\0') {
		return;
	}

	if (libmse_library_add_game(g_temp_lib, s_lib_state.add_rom_path)) {
		library_set_status("Queued %s", s_lib_state.add_rom_path);
	} else {
		library_set_status("Could not add %s", s_lib_state.add_rom_path);
	}
	s_lib_state.add_rom_path[0] = '\0';
}

// SDL runs these on its own thread. Everything they touch is either the
// library (which takes its own locks) or the status line above.
static void SDLCALL library_pick_rom_cb(void *userdata, const char *const *filelist, int filter)
{
	(void)userdata;
	(void)filter;

	if (filelist == NULL || filelist[0] == NULL) {
		return;
	}

	size_t added = 0;
	for (const char *const *path = filelist; *path != NULL; ++path) {
		if (g_temp_lib != NULL && libmse_library_add_game(g_temp_lib, *path)) {
			++added;
		}
	}
	library_set_status("Queued %zu ROM%s", added, added == 1 ? "" : "s");
}

static void SDLCALL library_pick_folder_cb(void *userdata, const char *const *filelist, int filter)
{
	(void)userdata;
	(void)filter;

	if (filelist == NULL || filelist[0] == NULL || g_temp_lib == NULL) {
		return;
	}

	const size_t added = libmse_library_add_folder(g_temp_lib, filelist[0], true);
	library_set_status("Queued %zu ROM%s from %s", added, added == 1 ? "" : "s", filelist[0]);
}

static void library_browse_rom(SDL_Window *window)
{
	static const SDL_DialogFileFilter filters[] = {
		{"ROM files", "nes;fds;sfc;smc;gb;gbc;gba;n64;z64;md;gen;sms;gg;pce;a26;a78;lnx;zip;7z"},
		{"All files", "*"},
	};
	SDL_ShowOpenFileDialog(library_pick_rom_cb, NULL, window, filters, 2, NULL, true);
}

static void library_browse_folder(SDL_Window *window)
{
	SDL_ShowOpenFolderDialog(library_pick_folder_cb, NULL, window, NULL, false);
}

// --- toolbar ----------------------------------------------------------------

static void draw_add_popup(mse_frontend_ui_state_t *state)
{
	if (!igBeginPopup("lib_add", 0)) {
		return;
	}

	mse_frontend_ui_setting_row("Add to library", "Scanned, hashed and matched against the metadata service.");
	mse_frontend_ui_gap(8.0f);

	const ImVec2 wide = {mse_frontend_ui_px(260.0f), mse_frontend_ui_px(34.0f)};

	if (mse_frontend_ui_button(MSE_ICON_LIBRARY "  Add ROM files...", wide, MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
		library_browse_rom(state->window);
		igCloseCurrentPopup();
	}
	mse_frontend_ui_gap(2.0f);
	if (mse_frontend_ui_button(MSE_ICON_BACKENDS "  Add a folder...", wide, MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
		library_browse_folder(state->window);
		igCloseCurrentPopup();
	}
	mse_frontend_ui_gap(4.0f);
	igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
	igTextColored(TOK->text_faint, "Folders are searched recursively.");
	igPopFont();

	mse_frontend_ui_gap(10.0f);
	mse_frontend_ui_section("BY PATH");
	mse_frontend_ui_gap(6.0f);

	igPushStyleColor_Vec4(ImGuiCol_FrameBg, TOK->bg_sunken);
	igSetNextItemWidth(mse_frontend_ui_px(186.0f));
	const bool submitted =
		igInputTextWithHint("##lib_add_path", "C:\\roms\\game.nes", s_lib_state.add_rom_path,
							sizeof(s_lib_state.add_rom_path), ImGuiInputTextFlags_EnterReturnsTrue, NULL, NULL);
	igPopStyleColor(1);

	igSameLine(0.0f, mse_frontend_ui_px(6.0f));
	const bool add = mse_frontend_ui_button("Add", (ImVec2){mse_frontend_ui_px(68.0f), 0.0f},
										   MSE_FRONTEND_UI_BUTTON_PRIMARY);
	if (submitted || add) {
		library_commit_add();
		igCloseCurrentPopup();
	}

	igEndPopup();
}

static void draw_manage_popup(void)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	if (!igBeginPopup("lib_manage", 0)) {
		return;
	}

	size_t games = 0, files = 0, with_art = 0;
	libmse_library_stats(g_temp_lib, &games, &files, &with_art);

	mse_frontend_ui_setting_row("Library", NULL);
	mse_frontend_ui_gap(6.0f);

	if (igBeginTable("lib_stats", 2, ImGuiTableFlags_SizingStretchProp, ZERO, 0.0f)) {
		igTableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(96.0f), 0);
		igTableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
		mse_frontend_ui_kv_row("Titles", "%zu", games);
		mse_frontend_ui_kv_row("Files", "%zu", files);
		mse_frontend_ui_kv_row("Covers", "%zu of %zu", with_art, games);
		igEndTable();
	}

	mse_frontend_ui_gap(10.0f);
	mse_frontend_ui_section("MAINTENANCE");
	mse_frontend_ui_gap(6.0f);

	const ImVec2 wide = {mse_frontend_ui_px(260.0f), mse_frontend_ui_px(32.0f)};

	if (mse_frontend_ui_button("Rescrape everything", wide, MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
		libmse_cmd_execute("libmse_library_rescrape", 0, NULL);
		library_set_status("Rescraping %zu title%s", files, files == 1 ? "" : "s");
		igCloseCurrentPopup();
	}
	if (igIsItemHovered(0)) {
		igSetTooltip("Re-runs metadata and cover art for every title. Needs a network connection.");
	}

	mse_frontend_ui_gap(2.0f);
	if (mse_frontend_ui_button("Forget missing files", wide, MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
		const size_t gone = libmse_library_forget_missing(g_temp_lib);
		library_set_status("Forgot %zu missing file%s", gone, gone == 1 ? "" : "s");
		s_lib_state.selected_game_id = -1;
		igCloseCurrentPopup();
	}
	if (igIsItemHovered(0)) {
		igSetTooltip("Drops entries whose ROM is no longer where the library recorded it.");
	}

	mse_frontend_ui_gap(10.0f);
	mse_frontend_ui_section("DATABASE");
	mse_frontend_ui_gap(6.0f);

	if (g_temp_db != NULL && g_temp_db->db_path != NULL) {
		SDL_PathInfo info;
		const bool   known = SDL_GetPathInfo(g_temp_db->db_path, &info);

		if (igBeginTable("lib_db", 2, ImGuiTableFlags_SizingStretchProp, ZERO, 0.0f)) {
			igTableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(96.0f), 0);
			igTableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
			mse_frontend_ui_kv_row("File", "%s", g_temp_db->db_path);
			if (known) {
				mse_frontend_ui_kv_row("Size", "%.1f MB", (double)info.size / (1024.0 * 1024.0));
			}
			igEndTable();
		}

		mse_frontend_ui_gap(8.0f);
		if (mse_frontend_ui_button("Compact", (ImVec2){mse_frontend_ui_px(126.0f), mse_frontend_ui_px(32.0f)},
								   MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
			libmse_db_lock(g_temp_db);
			libmse_db_exec("VACUUM;", g_temp_db);
			libmse_db_unlock(g_temp_db);
			library_set_status("Database compacted");
		}
		if (igIsItemHovered(0)) {
			igSetTooltip("Rewrites the database to reclaim space left by deleted rows.");
		}

		igSameLine(0.0f, mse_frontend_ui_px(8.0f));
		if (mse_frontend_ui_button("Empty library", (ImVec2){mse_frontend_ui_px(126.0f), mse_frontend_ui_px(32.0f)},
								   MSE_FRONTEND_UI_BUTTON_DANGER)) {
			igOpenPopup_Str("lib_clear_confirm", 0);
		}

		// Nested inside the manage popup so dismissing that dismisses this too.
		if (igBeginPopupModal("lib_clear_confirm", NULL,
							  ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
								  ImGuiWindowFlags_NoSavedSettings)) {
			mse_frontend_ui_title_text("Empty the library?");
			mse_frontend_ui_gap(4.0f);
			igTextColored(t->text_muted, "Every title, cover and match is removed. The ROM files");
			igTextColored(t->text_muted, "themselves are not touched.");
			mse_frontend_ui_gap(14.0f);

			if (mse_frontend_ui_button("Cancel", (ImVec2){mse_frontend_ui_px(120.0f), mse_frontend_ui_px(34.0f)},
									   MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
				igCloseCurrentPopup();
			}
			igSameLine(0.0f, mse_frontend_ui_px(8.0f));
			if (mse_frontend_ui_button("Empty it", (ImVec2){mse_frontend_ui_px(120.0f), mse_frontend_ui_px(34.0f)},
									   MSE_FRONTEND_UI_BUTTON_DANGER)) {
				libmse_library_clear(g_temp_lib);
				s_lib_state.selected_game_id = -1;
				library_set_status("Library emptied");
				igCloseCurrentPopup();
			}
			igEndPopup();
		}
	}

	igEndPopup();
}

static void draw_toolbar(void)
{
	// Sized off what is actually there: at the widths this pane collapses to a
	// fixed layout pushed the view toggle straight off the edge.
	const float avail     = igGetContentRegionAvail().x;
	const float gap       = mse_frontend_ui_px(8.0f);
	const float toggle_w  = mse_frontend_ui_px(150.0f);
	const float add_w     = mse_frontend_ui_px(92.0f);
	const float manage_w  = mse_frontend_ui_px(104.0f);

	float search_w = avail - toggle_w - add_w - manage_w - (gap * 3.0f);
	if (search_w < mse_frontend_ui_px(120.0f)) {
		search_w = mse_frontend_ui_px(120.0f);
	}

	mse_frontend_ui_search_field("lib_search", "Search library...", s_lib_state.search_query,
								 sizeof(s_lib_state.search_query), search_w);

	igSameLine(0.0f, gap);
	if (mse_frontend_ui_button(MSE_ICON_ADD "  Add", (ImVec2){add_w, 0.0f}, MSE_FRONTEND_UI_BUTTON_PRIMARY)) {
		s_open_add_popup = true;
	}

	igSameLine(0.0f, gap);
	if (mse_frontend_ui_button(MSE_ICON_SETTINGS "  Manage", (ImVec2){manage_w, 0.0f},
							   MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
		s_open_manage_popup = true;
	}

	// The view toggle sits hard right, where it stays put as the pane resizes.
	static const char *modes[] = {"Grid", "List"};
	const float        right   = igGetCursorPosX() + igGetContentRegionAvail().x - toggle_w;
	igSameLine(0.0f, 0.0f);
	if (right > igGetCursorPosX()) {
		igSetCursorPosX(right);
	}
	mse_frontend_ui_segmented("lib_view_mode", modes, 2, &s_lib_state.view_mode);

	if (s_lib_status[0] != '\0') {
		mse_frontend_ui_gap(8.0f);
		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
		igTextColored(TOK->accent, "%s", s_lib_status);
		igPopFont();
	}

	mse_frontend_ui_gap(10.0f);
	igSeparator();
	mse_frontend_ui_gap(10.0f);
}

// --- artwork placeholder ----------------------------------------------------

// Drawn under every cover, and left showing for titles that have none: a tint
// derived from the id plus the initials, which reads as deliberate where a grey
// box reads as broken.
static void draw_cover_placeholder(ImDrawList *dl, ImVec2 min, ImVec2 max, int64_t id, const char *name,
								   bool has_art, float rounding, ImDrawFlags corners)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	const float hue   = (float)((uint64_t)id * 2654435761u % 360u) / 360.0f;
	float       r = 0.0f, g = 0.0f, b = 0.0f;
	igColorConvertHSVtoRGB(hue, 0.42f, t->is_light ? 0.82f : 0.34f, &r, &g, &b);

	ImDrawList_AddRectFilled(dl, min, max, igGetColorU32_Vec4((ImVec4){r * 0.5f, g * 0.5f, b * 0.5f, 1.0f}),
							 rounding, corners);
	ImDrawList_AddRectFilledMultiColor(dl, min, max, igGetColorU32_Vec4((ImVec4){r, g, b, 1.0f}),
									   igGetColorU32_Vec4((ImVec4){r, g, b, 1.0f}),
									   igGetColorU32_Vec4((ImVec4){r * 0.45f, g * 0.45f, b * 0.45f, 1.0f}),
									   igGetColorU32_Vec4((ImVec4){r * 0.45f, g * 0.45f, b * 0.45f, 1.0f}));

	char initials[3] = {0};
	if (name != NULL) {
		int written = 0;
		bool at_word_start = true;
		for (const char *p = name; *p != '\0' && written < 2; ++p) {
			if (*p == ' ' || *p == '-' || *p == '_') {
				at_word_start = true;
				continue;
			}
			if (at_word_start) {
				initials[written++] = (char)((*p >= 'a' && *p <= 'z') ? (*p - 32) : *p);
				at_word_start       = false;
			}
		}
	}
	if (initials[0] == '\0') {
		initials[0] = '?';
	}

	ImFont *font = mse_frontend_imgui_font_title();
	if (font != NULL) {
		const float  size   = fminf((max.y - min.y) * 0.34f, mse_frontend_ui_px(44.0f));
		const ImVec2 extent = ImFont_CalcTextSizeA(font, size, FLT_MAX, 0.0f, initials, NULL, NULL);
		ImDrawList_AddText_FontPtr(dl, font, size,
								   (ImVec2){min.x + (((max.x - min.x) - extent.x) * 0.5f),
											min.y + (((max.y - min.y) - extent.y) * 0.5f)},
								   igGetColorU32_Vec4((ImVec4){1.0f, 1.0f, 1.0f, 0.85f}), initials, NULL, 0.0f, NULL);
	}

	(void)has_art;
}

static void draw_cover(ImDrawList *dl, ImVec2 min, ImVec2 max, int64_t id, const char *name, bool has_art,
					   mse_frontend_cover_fit_t fit, float rounding, ImDrawFlags corners)
{
	// A contained cover leaves bars either side, and the generated placeholder
	// showing through them looks like two pictures fighting. Flat surface
	// instead; a cropped one covers the rect so it needs no backing at all.
	if (fit == MSE_FRONTEND_COVER_FIT_CONTAIN && mse_frontend_cover_get(id) != NULL) {
		ImDrawList_AddRectFilled(dl, min, max, igGetColorU32_Vec4(TOK->bg_sunken), rounding, corners);
	} else {
		// Drawn first so the card never flashes empty while the texture is
		// still being uploaded, and left showing for titles with no cover.
		draw_cover_placeholder(dl, min, max, id, name, has_art, rounding, corners);
	}

	mse_frontend_cover_draw(dl, min, max, id, fit, rounding, corners);
}

// Shortens `text` until it fits `max_width`, ending in an ellipsis. A hard clip
// cuts glyphs in half, which reads as a rendering fault rather than as text
// that did not fit.
static void library_fit_text(ImFont *font, float size, const char *text, float max_width, char *out,
							 size_t out_size)
{
	snprintf(out, out_size, "%s", text != NULL ? text : "");
	if (font == NULL || ImFont_CalcTextSizeA(font, size, FLT_MAX, 0.0f, out, NULL, NULL).x <= max_width) {
		return;
	}

	size_t length = strlen(out);
	while (length > 1) {
		--length;
		out[length] = (char)0;
		char probe[256];
		snprintf(probe, sizeof(probe), "%s...", out);
		if (ImFont_CalcTextSizeA(font, size, FLT_MAX, 0.0f, probe, NULL, NULL).x <= max_width) {
			snprintf(out, out_size, "%s", probe);
			return;
		}
	}
}

// --- grid -------------------------------------------------------------------

static void draw_grid_view(libmse_stmt_t *stmt)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	const float avail = igGetContentRegionAvail().x;
	const float gap   = mse_frontend_ui_px(16.0f);

	// Columns are chosen from a minimum width and then stretched to fill the
	// row exactly, so the grid has no ragged gutter down its right-hand side.
	const float min_card = mse_frontend_ui_px(152.0f);
	int         cols     = (int)floorf((avail + gap) / (min_card + gap));
	if (cols < 1) cols = 1;

	const float card_width = (avail - (gap * (float)(cols - 1))) / (float)cols;

	// Console box art sits near 1:1.4. Sizing the art to that and fitting the
	// picture inside it means the whole box shows, whatever shape it is.
	const float art_h     = card_width / 0.72f;
	const float caption_h = (mse_frontend_imgui_font_size_body() * 2.2f) +
							mse_frontend_imgui_font_size_small() + mse_frontend_ui_px(26.0f);
	const float card_height = art_h + caption_h;

	int index = 0;
	while (libmse_db_stmt_step(stmt) == 1) {
		const int64_t id       = libmse_db_col_int64(stmt, 0);
		const char   *name     = libmse_db_col_text(stmt, 1);
		const int     year     = libmse_db_col_int(stmt, 2);
		const char   *platform = libmse_db_col_text(stmt, 3);
		const int     has_art  = libmse_db_col_int(stmt, 6) > 0;
		const char   *status   = libmse_db_col_text(stmt, 7);

		if ((index % cols) != 0) {
			igSameLine(0.0f, gap);
		}
		++index;

		const bool   selected = s_lib_state.selected_game_id == id;
		const ImVec2 pos      = igGetCursorScreenPos();

		igPushID_Int((int)id);
		const bool  clicked = igInvisibleButton("##card", (ImVec2){card_width, card_height}, 0);
		const bool  hovered = igIsItemHovered(0);
		const bool  held    = igIsItemActive();
		const float hot     = mse_frontend_ui_anim("hot", hovered, 14.0f);
		const float on      = mse_frontend_ui_anim("on", selected, 16.0f);
		igPopID();

		if (clicked) {
			s_lib_state.selected_game_id = id;
		}

		ImDrawList  *dl   = igGetWindowDrawList();
		const float  rise = held ? 0.0f : (hot * mse_frontend_ui_px(3.0f));
		const ImVec2 cmin = (ImVec2){pos.x, pos.y - rise};
		const ImVec2 cmax = (ImVec2){pos.x + card_width, pos.y + card_height - rise};
		const float  round = t->rounding_md;

		if (hot > 0.01f) {
			mse_frontend_ui_shadow(dl, cmin, cmax, round, hot * 0.6f);
		}

		ImDrawList_AddRectFilled(dl, cmin, cmax, igGetColorU32_Vec4(t->bg_raised), round, 0);

		draw_cover(dl, cmin, (ImVec2){cmax.x, cmin.y + art_h}, id, name, has_art != 0,
				   MSE_FRONTEND_COVER_FIT_CONTAIN, round, ImDrawFlags_RoundCornersTop);

		// A scrim under the caption so the tint behind it never fights the text.
		ImDrawList_AddRectFilledMultiColor(dl, (ImVec2){cmin.x, cmin.y + art_h - mse_frontend_ui_px(26.0f)},
										   (ImVec2){cmax.x, cmin.y + art_h},
										   igGetColorU32_Vec4((ImVec4){0.0f, 0.0f, 0.0f, 0.0f}),
										   igGetColorU32_Vec4((ImVec4){0.0f, 0.0f, 0.0f, 0.0f}),
										   igGetColorU32_Vec4((ImVec4){0.0f, 0.0f, 0.0f, 0.45f}),
										   igGetColorU32_Vec4((ImVec4){0.0f, 0.0f, 0.0f, 0.45f}));

		ImDrawList_AddRect(dl, cmin, cmax,
						   on > 0.01f ? mse_frontend_theme_u32(t->accent, 0.35f + (0.65f * on))
									  : mse_frontend_theme_u32(hot > 0.01f ? t->border_strong : t->border, 1.0f),
						   round, 0, selected ? mse_frontend_ui_px(2.0f) : 1.0f);

		const float pad  = mse_frontend_ui_px(11.0f);
		ImFont     *body = mse_frontend_imgui_font_body();
		if (body != NULL) {
			// Two lines of title, hard-clipped: long names are common and a
			// third line would run into the caption below it.
			const float  line = mse_frontend_imgui_font_size_body();
			const ImVec2 tpos = (ImVec2){cmin.x + pad, cmin.y + art_h + mse_frontend_ui_px(9.0f)};
			const ImVec4 clip = {tpos.x, tpos.y, cmax.x - pad, tpos.y + (line * 2.1f)};
			ImFont_RenderText(body, dl, line, tpos, igGetColorU32_Vec4(t->text), clip,
							  name != NULL ? name : "Unknown", NULL, card_width - (pad * 2.0f), true);
		}

		ImFont *small_font = mse_frontend_imgui_font_small();
		if (small_font != NULL) {
			char meta[96];
			if (year > 0 && platform != NULL) {
				snprintf(meta, sizeof(meta), "%s  %d", platform, year);
			} else if (platform != NULL) {
				snprintf(meta, sizeof(meta), "%s", platform);
			} else if (year > 0) {
				snprintf(meta, sizeof(meta), "%d", year);
			} else if (status != NULL && strcmp(status, "Unmatched") == 0) {
				// Said plainly: the file is in the library, the scraper just
				// could not work out what it is.
				snprintf(meta, sizeof(meta), "Not identified");
			} else {
				snprintf(meta, sizeof(meta), "Unknown");
			}

			const float msize = mse_frontend_imgui_font_size_small();

			// One line, shortened to fit: a second would land past the bottom
			// of the card.
			char fitted[128];
			library_fit_text(small_font, msize, meta, card_width - (pad * 2.0f), fitted, sizeof(fitted));

			const ImVec2 mpos = (ImVec2){cmin.x + pad, cmax.y - msize - mse_frontend_ui_px(10.0f)};
			const ImVec4 clip = {mpos.x, mpos.y, cmax.x - pad, cmax.y};
			ImFont_RenderText(small_font, dl, msize, mpos, igGetColorU32_Vec4(t->text_faint), clip, fitted, NULL,
							  0.0f, true);
		}
	}

	if (index == 0) {
		mse_frontend_ui_empty_state(MSE_ICON_LIBRARY, "Nothing here yet",
									"Add a ROM with the field above and it will be scanned into the library.");
	}
}

// --- list -------------------------------------------------------------------

static void draw_list_view(libmse_stmt_t *stmt)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
								  ImGuiTableFlags_BordersInnerV;

	igPushStyleVar_Vec2(ImGuiStyleVar_CellPadding, (ImVec2){mse_frontend_ui_px(10.0f), mse_frontend_ui_px(7.0f)});
	if (igBeginTable("lib_list_table", 5, flags, ZERO, 0.0f)) {
		igTableSetupScrollFreeze(0, 1);
		igTableSetupColumn("TITLE", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
		igTableSetupColumn("PLATFORM", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(140.0f), 0);
		igTableSetupColumn("YEAR", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(60.0f), 0);
		igTableSetupColumn("PATH", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
		igTableSetupColumn("ART", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(44.0f), 0);

		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
		igTableHeadersRow();
		igPopFont();

		int rows = 0;
		while (libmse_db_stmt_step(stmt) == 1) {
			const int64_t id       = libmse_db_col_int64(stmt, 0);
			const char   *name     = libmse_db_col_text(stmt, 1);
			const int     year     = libmse_db_col_int(stmt, 2);
			const char   *platform = libmse_db_col_text(stmt, 3);
			const char   *path     = libmse_db_col_text(stmt, 5);
			const int     art_size = libmse_db_col_int(stmt, 6);
			++rows;

			igTableNextRow(0, 0.0f);
			igTableSetColumnIndex(0);

			igPushID_Int((int)id);
			if (igSelectable_Bool("##row", s_lib_state.selected_game_id == id,
								  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ZERO)) {
				s_lib_state.selected_game_id = id;
			}
			igPopID();
			igSameLine(0.0f, 0.0f);
			igTextColored(t->text, "%s", name != NULL ? name : "Unknown");

			igTableSetColumnIndex(1);
			igTextColored(t->text_muted, "%s", platform != NULL ? platform : "--");

			igTableSetColumnIndex(2);
			if (year > 0) {
				igTextColored(t->text_muted, "%d", year);
			} else {
				igTextColored(t->text_faint, "--");
			}

			igTableSetColumnIndex(3);
			igTextColored(t->text_faint, "%s", path != NULL ? path : "");

			igTableSetColumnIndex(4);
			if (art_size > 0) {
				igTextColored(t->success, MSE_ICON_INSTALLED);
			} else {
				igTextColored(t->text_faint, "--");
			}
		}
		igEndTable();

		if (rows == 0) {
			mse_frontend_ui_empty_state(MSE_ICON_LIBRARY, "Nothing here yet",
										"Add a ROM with the field above and it will be scanned into the library.");
		}
	}
	igPopStyleVar(1);
}

// --- details ----------------------------------------------------------------

static void draw_details_pane(mse_frontend_ui_state_t *state, ImVec2 size)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	if (!mse_frontend_ui_card_begin("LIB_DETAILS_PANE", size)) {
		mse_frontend_ui_card_end();
		return;
	}

	if (g_temp_db == NULL) {
		mse_frontend_ui_card_end();
		return;
	}

	libmse_stmt_t *stmt = libmse_db_stmt_prepare(
		g_temp_db, "SELECT g.name, g.release_year, g.description, p.name, c.name, f.rom_path, "
				   "length(g.artwork_blob) FROM games g "
				   "LEFT JOIN platforms p ON g.platform_id = p.id "
				   "LEFT JOIN companies c ON p.company_id = c.id "
				   "JOIN game_files f ON g.id = f.game_id WHERE g.id = ?1 LIMIT 1;");
	if (stmt == NULL) {
		mse_frontend_ui_card_end();
		return;
	}

	libmse_db_bind_int64(stmt, 1, s_lib_state.selected_game_id);
	if (libmse_db_stmt_step(stmt) == 1) {
		const char *name      = libmse_db_col_text(stmt, 0);
		const int   year      = libmse_db_col_int(stmt, 1);
		const char *desc      = libmse_db_col_text(stmt, 2);
		const char *platform  = libmse_db_col_text(stmt, 3);
		const char *developer = libmse_db_col_text(stmt, 4);
		const char *path      = libmse_db_col_text(stmt, 5);
		const int   art_size  = libmse_db_col_int(stmt, 6);

		if (path != NULL) {
			strncpy(s_lib_state.selected_rom_path, path, sizeof(s_lib_state.selected_rom_path) - 1);
			s_lib_state.selected_rom_path[sizeof(s_lib_state.selected_rom_path) - 1] = '\0';
		}

		const float  art_h = mse_frontend_ui_px(300.0f);
		const ImVec2 apos  = igGetCursorScreenPos();
		const ImVec2 amax  = (ImVec2){apos.x + igGetContentRegionAvail().x, apos.y + art_h};
		// Contained rather than cropped: this pane is wide and short, and a
		// centre crop of a portrait cover would show only its midriff.
		draw_cover(igGetWindowDrawList(), apos, amax, s_lib_state.selected_game_id, name, art_size > 0,
				   MSE_FRONTEND_COVER_FIT_CONTAIN, t->rounding_md, 0);
		igDummy((ImVec2){amax.x - apos.x, art_h});

		mse_frontend_ui_gap(12.0f);

		igPushTextWrapPos(0.0f);
		mse_frontend_ui_title_text("%s", name != NULL ? name : "Unknown title");
		igPopTextWrapPos();

		if (platform != NULL) {
			mse_frontend_ui_badge(platform, t->accent);
			igSameLine(0.0f, mse_frontend_ui_px(6.0f));
		}
		if (year > 0) {
			char year_text[16];
			snprintf(year_text, sizeof(year_text), "%d", year);
			mse_frontend_ui_badge(year_text, t->info);
		}
		if (platform == NULL && year <= 0) {
			mse_frontend_ui_muted("No metadata scraped.");
		}

		mse_frontend_ui_gap(14.0f);

		if (mse_frontend_ui_button(MSE_ICON_START_CORE "  Launch",
								   (ImVec2){-FLT_MIN, mse_frontend_ui_px(40.0f)},
								   MSE_FRONTEND_UI_BUTTON_PRIMARY)) {
			// This used to set the path and switch to the core view without ever
			// handing the file to the backend, so Launch showed a black screen.
			if (path != NULL && !mse_frontend_ui_load_rom(state, path)) {
				library_set_status("Could not load %s", path);
			}
		}

		mse_frontend_ui_gap(16.0f);
		mse_frontend_ui_section("ABOUT");
		mse_frontend_ui_gap(4.0f);
		igPushTextWrapPos(0.0f);
		if (desc != NULL && desc[0] != '\0') {
			mse_frontend_ui_muted("%s", desc);
		} else {
			igTextColored(t->text_faint, "No description was scraped for this title.");
		}
		igPopTextWrapPos();

		mse_frontend_ui_gap(16.0f);
		mse_frontend_ui_section("FILE");
		mse_frontend_ui_gap(4.0f);
		if (igBeginTable("lib_details_meta", 2, ImGuiTableFlags_SizingStretchProp, ZERO, 0.0f)) {
			igTableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(78.0f), 0);
			igTableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
			mse_frontend_ui_kv_row("Developer", "%s", developer != NULL ? developer : "Unknown");
			mse_frontend_ui_kv_row("Artwork", "%s", art_size > 0 ? "Stored" : "None");
			mse_frontend_ui_kv_row("Path", "%s", path != NULL ? path : "--");
			igEndTable();
		}
	}
	libmse_db_stmt_finalize(stmt);

	mse_frontend_ui_card_end();
}

// --- view -------------------------------------------------------------------

void mse_frontend_library_view_draw(mse_frontend_ui_state_t *state)
{
	mse_frontend_ui_page_header("COLLECTION", "Library",
								"Scanned ROMs, with whatever metadata and artwork could be matched.");

	const float avail_w   = igGetContentRegionAvail().x;
	const float avail_h   = igGetContentRegionAvail().y;
	const float gap       = mse_frontend_ui_px(14.0f);
	const float details_w = mse_frontend_ui_px(330.0f);
	const bool  split     = s_lib_state.selected_game_id != -1 && avail_w > mse_frontend_ui_px(760.0f);
	const float list_w    = split ? (avail_w - details_w - gap) : avail_w;

	if (mse_frontend_ui_card_begin("LIB_MAIN", (ImVec2){list_w, avail_h})) {
		draw_toolbar();

		if (g_temp_db == NULL) {
			mse_frontend_ui_empty_state(MSE_ICON_LIBRARY, "No database",
										"The library database could not be opened, so there is nothing to list.");
		} else {
			const bool  filtered = s_lib_state.search_query[0] != '\0';
			const char *sql      = filtered ? LIB_SELECT_COLUMNS "WHERE g.name LIKE ?1 ORDER BY g.name ASC;"
											: LIB_SELECT_COLUMNS "ORDER BY g.name ASC;";

			libmse_stmt_t *stmt = libmse_db_stmt_prepare(g_temp_db, sql);
			if (stmt != NULL) {
				if (filtered) {
					// Bound, not interpolated: a quote in the search box used
					// to break the statement outright.
					char pattern[sizeof(s_lib_state.search_query) + 2];
					snprintf(pattern, sizeof(pattern), "%%%s%%", s_lib_state.search_query);
					libmse_db_bind_text(stmt, 1, pattern);
				}

				if (s_lib_state.view_mode == LIB_VIEW_GRID) {
					draw_grid_view(stmt);
				} else {
					draw_list_view(stmt);
				}
				libmse_db_stmt_finalize(stmt);
			} else {
				igTextColored(TOK->danger, "The library query failed.");
			}
		}
	}
	mse_frontend_ui_card_end();

	if (split) {
		igSameLine(0.0f, gap);
		draw_details_pane(state, (ImVec2){details_w, avail_h});
	}

	if (s_open_add_popup) {
		igOpenPopup_Str("lib_add", 0);
		s_open_add_popup = false;
	}
	if (s_open_manage_popup) {
		igOpenPopup_Str("lib_manage", 0);
		s_open_manage_popup = false;
	}
	draw_add_popup(state);
	draw_manage_popup();
}
