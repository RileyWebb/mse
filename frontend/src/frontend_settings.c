#define DEBUG_LOG_SOURCE "frontend"
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend_ui.h"
#include "frontend_imgui.h"
#include "frontend_cimgui.h"
#include "frontend_widgets.h"
#include "frontend_icons.h"
#include "frontend_app.h"
#include "libmse/libmse_cvar.h"
#include "libmse/libmse_debug.h"
#include "frontend_profiler.h"
#include "frontend_chrome.h"

#define TOK (mse_frontend_theme())

static const ImVec2 ZERO = {0.0f, 0.0f};

static const char *mse_frontend_ui_settings_tab_name(mse_frontend_settings_tab_t tab)
{
	switch (tab) {
	case MSE_FRONTEND_SETTINGS_TAB_VIDEO:      return "Video";
	case MSE_FRONTEND_SETTINGS_TAB_AUDIO:      return "Audio";
	case MSE_FRONTEND_SETTINGS_TAB_CONTROLS:   return "Controls";
	case MSE_FRONTEND_SETTINGS_TAB_APPEARANCE: return "Appearance";
	case MSE_FRONTEND_SETTINGS_TAB_PERFORMANCE: return "Performance";
	case MSE_FRONTEND_SETTINGS_TAB_BEHAVIOR:   return "Behaviour";
	case MSE_FRONTEND_SETTINGS_TAB_ADVANCED:   return "Advanced";
	case MSE_FRONTEND_SETTINGS_TAB_GENERAL:
	default:                                   return "General";
	}
}

static const char *mse_frontend_ui_settings_tab_blurb(mse_frontend_settings_tab_t tab)
{
	switch (tab) {
	case MSE_FRONTEND_SETTINGS_TAB_VIDEO:      return "Output resolution, scaling and the presentation pipeline.";
	case MSE_FRONTEND_SETTINGS_TAB_AUDIO:      return "Playback levels and the device sound is sent to.";
	case MSE_FRONTEND_SETTINGS_TAB_CONTROLS:   return "Map keyboards and gamepads onto backend inputs.";
	case MSE_FRONTEND_SETTINGS_TAB_APPEARANCE: return "How the frontend itself looks.";
	case MSE_FRONTEND_SETTINGS_TAB_PERFORMANCE:
		return "The frame counter, and the profiler behind it.";
	case MSE_FRONTEND_SETTINGS_TAB_BEHAVIOR:   return "What happens on focus loss, launch and exit.";
	case MSE_FRONTEND_SETTINGS_TAB_ADVANCED:   return "Diagnostics and the tools used to build this.";
	case MSE_FRONTEND_SETTINGS_TAB_GENERAL:
	default:                                   return "Core management and updates.";
	}
}

// One row of the settings list: label and help on the left, the control the
// caller draws pinned to the right so every row lines up down the column.
static void mse_frontend_settings_row_begin(const char *label, const char *help, float control_width)
{
	igPushID_Str(label);
	const float row_right = igGetCursorPosX() + igGetContentRegionAvail().x;

	igBeginGroup();
	mse_frontend_ui_setting_row(label, help);
	igEndGroup();

	igSameLine(0.0f, 0.0f);
	igSetCursorPosX(fmaxf(row_right - control_width, igGetCursorPosX() + mse_frontend_ui_px(12.0f)));
	igSetCursorPosY(igGetCursorPosY() + mse_frontend_ui_px(2.0f));
	igSetNextItemWidth(control_width);
}

static void mse_frontend_settings_row_end(void)
{
	igPopID();
	mse_frontend_ui_gap(6.0f);
	igSeparator();
	mse_frontend_ui_gap(6.0f);
}

static void mse_frontend_settings_toggle_row(const char *label, const char *help, bool *value)
{
	mse_frontend_settings_row_begin(label, help, mse_frontend_ui_px(46.0f));
	if (value != NULL) {
		mse_frontend_ui_toggle("##toggle", value);
	} else {
		igBeginDisabled(true);
		bool unused = false;
		mse_frontend_ui_toggle("##toggle", &unused);
		igEndDisabled();
	}
	mse_frontend_settings_row_end();
}

static void mse_frontend_settings_theme_picker(mse_frontend_ui_state_t *state)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	mse_frontend_ui_setting_row("Colour scheme", "Applies immediately, and to every backend panel too.");
	mse_frontend_ui_gap(10.0f);

	const float swatch = mse_frontend_ui_px(56.0f);
	const float gap    = mse_frontend_ui_px(10.0f);

	// Read from disk each frame the settings are open. It is a handful of small
	// files, it is not a hot path, and it means a theme dropped into the folder
	// is there without restarting.
	mse_frontend_theme_entry_t themes[32];
	const size_t               theme_count = mse_frontend_theme_list(themes, 32);

	if (theme_count == 0) {
		igTextColored(t->text_muted, "No themes found. Put a .cfg in themes/ next to the executable.");
		mse_frontend_ui_gap(10.0f);
		igSeparator();
		mse_frontend_ui_gap(6.0f);
		return;
	}

	const char *current = mse_frontend_theme_current();

	for (size_t i = 0; i < theme_count; ++i) {
		const mse_frontend_theme_entry_t *entry = &themes[i];
		const bool selected = current[0] != '\0' && strcmp(current, entry->path) == 0;

		if (i > 0) {
			igSameLine(0.0f, gap);
		}

		const ImVec2 pos = igGetCursorScreenPos();
		igPushID_Int((int)i);
		const bool clicked = igInvisibleButton("##theme", (ImVec2){swatch, swatch + mse_frontend_ui_px(20.0f)}, 0);
		const bool hovered = igIsItemHovered(0);
		const float hot    = mse_frontend_ui_anim("hot", hovered || selected, 14.0f);
		igPopID();

		if (clicked) {
			mse_frontend_theme_load(entry->path);
		}

		// The swatch is painted from the colours read out of the theme's own
		// file, so it previews what choosing it would do without applying it.

		ImDrawList  *dl  = igGetWindowDrawList();
		const ImVec2 max = (ImVec2){pos.x + swatch, pos.y + swatch};

		ImDrawList_AddRectFilled(dl, pos, max, igGetColorU32_Vec4(entry->bg_base), t->rounding_md, 0);
		ImDrawList_AddRectFilled(dl, (ImVec2){pos.x, pos.y + (swatch * 0.62f)},
								 (ImVec2){pos.x + swatch, max.y}, igGetColorU32_Vec4(entry->bg_raised),
								 t->rounding_md, ImDrawFlags_RoundCornersBottom);
		ImDrawList_AddCircleFilled(dl, (ImVec2){pos.x + (swatch * 0.5f), pos.y + (swatch * 0.36f)},
								   swatch * 0.19f, igGetColorU32_Vec4(entry->accent), 24);
		ImDrawList_AddRect(dl, pos, max,
						   selected ? igGetColorU32_Vec4(t->accent)
									: mse_frontend_theme_u32(t->border_strong, 0.5f + (0.5f * hot)),
						   t->rounding_md, 0, selected ? mse_frontend_ui_px(2.0f) : 1.0f);

		ImFont *small_font = mse_frontend_imgui_font_small();
		if (small_font != NULL) {
			const char  *name   = entry->name;
			const float  size   = mse_frontend_imgui_font_size_small();
			const ImVec2 extent = ImFont_CalcTextSizeA(small_font, size, FLT_MAX, 0.0f, name, NULL, NULL);
			ImDrawList_AddText_FontPtr(dl, small_font, size,
									   (ImVec2){pos.x + ((swatch - extent.x) * 0.5f), max.y + mse_frontend_ui_px(5.0f)},
									   igGetColorU32_Vec4(selected ? t->text : t->text_muted), name, NULL, 0.0f, NULL);
		}
	}

	mse_frontend_ui_gap(10.0f);
	igSeparator();
	mse_frontend_ui_gap(6.0f);
}


// Six cells laid out like the viewport itself, each showing a chip where the
// counter would sit. Quicker to read than a list of corner names, and it
// cannot be ambiguous about what "right" means.
static void mse_frontend_settings_framecounter_position(void)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	const float width  = mse_frontend_ui_px(232.0f);
	const float height = width * (9.0f / 16.0f);
	const ImVec2 pos   = igGetCursorScreenPos();
	ImDrawList  *dl    = igGetWindowDrawList();

	ImDrawList_AddRectFilled(dl, pos, (ImVec2){pos.x + width, pos.y + height},
							 igGetColorU32_Vec4(t->bg_sunken), t->rounding_md, 0);
	ImDrawList_AddRect(dl, pos, (ImVec2){pos.x + width, pos.y + height}, igGetColorU32_Vec4(t->border),
					   t->rounding_md, 0, 1.0f);

	const int   current  = mse_frontend_profiler_framecounter_pos();
	const float cell_w   = width / 3.0f;
	const float cell_h   = height / 2.0f;
	const float chip_w   = cell_w * 0.62f;
	const float chip_h   = cell_h * 0.34f;
	const float inset    = mse_frontend_ui_px(7.0f);

	igPushID_Str("framecounter_pos");
	for (int i = 0; i < MSE_FRONTEND_FRAMECOUNTER_POS_COUNT; ++i) {
		const int column = i % 3;
		const int row    = i / 3;

		const ImVec2 cell_min = {pos.x + (cell_w * (float)column), pos.y + (cell_h * (float)row)};

		igSetCursorScreenPos(cell_min);
		igPushID_Int(i);
		const bool clicked = igInvisibleButton("##cell", (ImVec2){cell_w, cell_h}, 0);
		const bool hovered = igIsItemHovered(0);
		igPopID();

		if (clicked) {
			mse_frontend_profiler_set_framecounter_pos(i);
		}

		// The chip sits in the same corner of its cell that the counter would
		// sit in on screen.
		float chip_x = cell_min.x + inset;
		if (column == 1) {
			chip_x = cell_min.x + ((cell_w - chip_w) * 0.5f);
		} else if (column == 2) {
			chip_x = cell_min.x + cell_w - chip_w - inset;
		}
		const float chip_y = (row == 0) ? (cell_min.y + inset) : (cell_min.y + cell_h - chip_h - inset);

		const bool selected = current == i;
		const ImVec4 fill = selected ? t->accent : (hovered ? t->bg_active : t->bg_hover);

		ImDrawList_AddRectFilled(dl, (ImVec2){chip_x, chip_y}, (ImVec2){chip_x + chip_w, chip_y + chip_h},
								 igGetColorU32_Vec4(fill), mse_frontend_ui_px(3.0f), 0);
		if (selected || hovered) {
			ImDrawList_AddRect(dl, (ImVec2){chip_x, chip_y}, (ImVec2){chip_x + chip_w, chip_y + chip_h},
							   igGetColorU32_Vec4(selected ? t->accent_hover : t->accent),
							   mse_frontend_ui_px(3.0f), 0, 1.0f);
		}
	}
	igPopID();

	igSetCursorScreenPos(pos);
	igDummy((ImVec2){width, height});

	mse_frontend_ui_gap(6.0f);
	igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
	igTextColored(t->text_muted, "%s", mse_frontend_profiler_framecounter_pos_name(current));
	igPopFont();
}

static void mse_frontend_settings_tab_body(mse_frontend_ui_state_t *state)
{
	const mse_frontend_theme_tokens_t *t = TOK;
	const float control_w = mse_frontend_ui_px(220.0f);

	switch (state->settings_tab) {
	case MSE_FRONTEND_SETTINGS_TAB_GENERAL:
		mse_frontend_ui_section("CORES");
		mse_frontend_settings_toggle_row("Installed cores only",
										 "Hide backends that are listed but not present on disk.",
										 &state->show_installed_only);
		mse_frontend_ui_gap(8.0f);
		mse_frontend_ui_section("UPDATES");
		mse_frontend_ui_gap(4.0f);
		if (mse_frontend_ui_button("Check for updates", (ImVec2){mse_frontend_ui_px(200.0f), mse_frontend_ui_px(34.0f)},
								   MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
			// TODO: no update channel exists yet.
		}
		break;

	case MSE_FRONTEND_SETTINGS_TAB_VIDEO: {
		mse_frontend_ui_section("DISPLAY");

		bool fullscreen = state->fullscreen != 0;
		mse_frontend_settings_toggle_row("Fullscreen", "Borderless, on the monitor the window is currently on.",
										 &fullscreen);
		state->fullscreen = fullscreen ? 1 : 0;

		bool menu_bar = state->menu_bar_in_game != 0;
		mse_frontend_settings_toggle_row("Menu bar while playing",
										 "Keeps the top bar up over the game, so View and Emulation stay "
										 "reachable without leaving it. The picture sits below it.",
										 &menu_bar);
		state->menu_bar_in_game = menu_bar ? 1 : 0;

		int resolution = 0;
		mse_frontend_settings_row_begin("Resolution", "Only applies when running fullscreen.", control_w);
		igCombo_Str_arr("##resolution", &resolution, g_video_resolutions, g_video_resolution_count, 5);
		mse_frontend_settings_row_end();

		mse_frontend_settings_row_begin("Interface scale", "Starts at the display scale; this overrides it.",
										control_w);
		{
			char preview[16];
			snprintf(preview, sizeof(preview), "%.2fx", state->content_scale);
			if (igBeginCombo("##ui_scale", preview, ImGuiComboFlags_None)) {
				for (float value = 0.5f; value <= 2.0f + 0.001f; value += 0.25f) {
					char item[16];
					snprintf(item, sizeof(item), "%.2fx", value);
					const bool is_selected = fabsf(state->content_scale - value) < 0.001f;
					if (igSelectable_Bool(item, is_selected, ImGuiSelectableFlags_None, ZERO)) {
						state->content_scale = value;
					}
					if (is_selected) {
						igSetItemDefaultFocus();
					}
				}
				igEndCombo();
			}
		}
		mse_frontend_settings_row_end();

		mse_frontend_ui_gap(8.0f);
		mse_frontend_ui_section("PIPELINE");

		static const char *present_modes[] = {"VSync (FIFO)", "Immediate (uncapped)", "Mailbox (fast VSync)"};
		mse_frontend_settings_row_begin("Present mode", "Immediate tears; mailbox does not, and does not wait.",
										control_w);
		// Through the cvar rather than into the field it is bound to. Only the
		// cvar's change callback reconfigures the swapchain, so writing the
		// field directly moved the label and left the mode alone.
		int present_mode = (int)g_app_ctx.presentation_mode;
		if (igCombo_Str_arr("##present_mode", &present_mode, present_modes, 3, 3)) {
			libmse_cvar_set_i("mse_presentation_mode", present_mode);
		}
		mse_frontend_settings_row_end();

		int driver = 0;
		mse_frontend_settings_row_begin("Graphics backend", "Chosen by SDL at startup; needs a restart to change.",
										control_w);
		igBeginDisabled(true);
		igCombo_Str_arr("##gpu_driver", &driver, g_gpu_drivers, g_gpu_driver_count, 3);
		igEndDisabled();
		mse_frontend_settings_row_end();
		break;
	}

	case MSE_FRONTEND_SETTINGS_TAB_AUDIO: {
		mse_frontend_ui_section("PLAYBACK");
		mse_frontend_settings_toggle_row("Mute", "Silences every backend without changing the volume below.",
										 &state->audio_mute);

		mse_frontend_settings_row_begin("Master volume", "Applied after any per-backend mixing.", control_w);
		if (state->audio_mute) igBeginDisabled(true);
		igSliderFloat("##volume", &state->audio_volume, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_None);
		if (state->audio_mute) igEndDisabled();
		mse_frontend_settings_row_end();

		mse_frontend_ui_gap(8.0f);
		mse_frontend_ui_section("HARDWARE");

		static const char *audio_devices[] = {"System default"};
		int device = 0;
		mse_frontend_settings_row_begin("Output device", "Device enumeration is not wired up yet.", control_w);
		igBeginDisabled(true);
		igCombo_Str_arr("##audio_device", &device, audio_devices, 1, 3);
		igEndDisabled();
		mse_frontend_settings_row_end();
		break;
	}

	case MSE_FRONTEND_SETTINGS_TAB_CONTROLS:
		mse_frontend_ui_section("BINDINGS");
		mse_frontend_ui_gap(4.0f);
		mse_frontend_ui_muted("The configurator draws whatever pad the running backend declares, and "
							  "lights each control up as you press it.");
		mse_frontend_ui_gap(10.0f);
		if (mse_frontend_ui_button(MSE_ICON_CONTROLLER "  Open configurator",
								   (ImVec2){mse_frontend_ui_px(260.0f), mse_frontend_ui_px(36.0f)},
								   MSE_FRONTEND_UI_BUTTON_PRIMARY)) {
			state->show_controller_window = true;
		}
		mse_frontend_ui_gap(8.0f);
		mse_frontend_ui_section("FOCUS");
		mse_frontend_settings_toggle_row("Accept input in the background",
										 "Keeps reading gamepads while another window has focus.", NULL);
		break;

	case MSE_FRONTEND_SETTINGS_TAB_APPEARANCE:
		mse_frontend_ui_section("THEME");
		mse_frontend_ui_gap(4.0f);
		mse_frontend_settings_theme_picker(state);
		mse_frontend_ui_gap(4.0f);
		mse_frontend_ui_muted("Or from the console: %s", "exec themes/<name>.cfg");

		mse_frontend_ui_gap(10.0f);
		mse_frontend_ui_section("WINDOW");
		{
			static const char *decorations[] = {"System", "Custom"};

			// Says why when the setting is being overruled, rather than
			// leaving someone to wonder why Custom does nothing.
			char        help[160];
			const char *tiler = mse_frontend_chrome_tiling_wm();
			if (tiler != NULL) {
				snprintf(help, sizeof(help), "%s is a tiling window manager, so the system's is used either way.", tiler);
			} else {
				snprintf(help, sizeof(help), "Custom draws the title bar and window buttons in the frontend's style.");
			}

			mse_frontend_settings_row_begin("Window decoration", help, control_w);
			int decoration = mse_frontend_chrome_mode();
			if (igCombo_Str_arr("##window_decoration", &decoration, decorations, 2, 2)) {
				libmse_cvar_set_i("mse_window_decoration", decoration);
			}
			mse_frontend_settings_row_end();
		}
		break;

	case MSE_FRONTEND_SETTINGS_TAB_PERFORMANCE: {
		mse_frontend_ui_section("FRAME COUNTER");

		bool counter = mse_frontend_profiler_framecounter_visible();
		mse_frontend_settings_toggle_row("Show frame counter",
										 "Drawn over the emulated image, not the whole window.", &counter);
		mse_frontend_profiler_set_framecounter_visible(counter);

		if (!counter) igBeginDisabled(true);

		bool detailed = mse_frontend_profiler_framecounter_detailed();
		mse_frontend_settings_toggle_row("Show frame time", "Prints milliseconds under the rate.", &detailed);
		mse_frontend_profiler_set_framecounter_detailed(detailed);

		bool graph = mse_frontend_profiler_framecounter_graph();
		mse_frontend_settings_toggle_row("Show frame time graph",
										 "A trace of recent frames, scaled against the budget.", &graph);
		mse_frontend_profiler_set_framecounter_graph(graph);

		mse_frontend_ui_setting_row("Position", "Where it sits within the picture.");
		mse_frontend_ui_gap(8.0f);
		mse_frontend_settings_framecounter_position();

		if (!counter) igEndDisabled();

		mse_frontend_ui_gap(14.0f);
		mse_frontend_ui_section("PROFILER");

		bool profiling = mse_frontend_profiler_enabled();
		mse_frontend_settings_toggle_row("Collect zone timings",
										 "Frame pacing is always measured; this adds the per-zone breakdown.",
										 &profiling);
		mse_frontend_profiler_set_enabled(profiling);

		mse_frontend_settings_row_begin("Frame budget",
										"What the counter and the graph colour against.", control_w);
		{
			float budget = mse_frontend_profiler_budget_ms();
			if (igInputFloat("##budget", &budget, 0.0f, 0.0f, "%.2f ms", 0)) {
				mse_frontend_profiler_set_budget_ms(budget);
			}
		}
		mse_frontend_settings_row_end();

		mse_frontend_ui_gap(4.0f);
		if (mse_frontend_ui_button("Open profiler", (ImVec2){mse_frontend_ui_px(180.0f), mse_frontend_ui_px(34.0f)},
								   MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
			state->show_profiler = true;
		}
		igSameLine(0.0f, mse_frontend_ui_px(10.0f));
		igTextColored(t->text_faint, "F9");
		break;
	}

	case MSE_FRONTEND_SETTINGS_TAB_BEHAVIOR:
		mse_frontend_ui_section("SESSION");
		mse_frontend_settings_toggle_row("Pause on focus loss", "Stops the emulation thread when the window is "
																"not in front.", NULL);
		mse_frontend_settings_toggle_row("Save state on exit", "Writes a state next to the ROM as MSE closes.", NULL);
		mse_frontend_settings_toggle_row("Confirm before quitting", "Asks first, rather than closing straight away.",
										 &state->confirm_on_quit);
		break;

	case MSE_FRONTEND_SETTINGS_TAB_ADVANCED:
		mse_frontend_ui_section("DIAGNOSTICS");
		mse_frontend_settings_toggle_row("Write logs to file", "In addition to the console.", NULL);
		mse_frontend_settings_toggle_row("Profiler", "Collects per-zone timings. F9 opens the viewer.",
										 &state->show_profiler);

		mse_frontend_ui_gap(8.0f);
		mse_frontend_ui_section("IMGUI");
		mse_frontend_settings_toggle_row("Demo window", "The upstream widget gallery.", &state->show_demo_window);
		mse_frontend_settings_toggle_row("Metrics", "Draw call and allocation counts.", &state->show_metrics_window);
		mse_frontend_settings_toggle_row("Style editor", "Edit the live style; changes are not saved.",
										 &state->show_style_editor);

		mse_frontend_ui_gap(8.0f);
		mse_frontend_ui_section("CONSOLE");
		mse_frontend_ui_gap(4.0f);
		if (mse_frontend_ui_button(MSE_ICON_TERMINAL "  Open console",
								   (ImVec2){mse_frontend_ui_px(200.0f), mse_frontend_ui_px(34.0f)},
								   MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
			state->show_terminal = 1;
		}
		igSameLine(0.0f, mse_frontend_ui_px(10.0f));
		igTextColored(t->text_faint, "F10");
		break;
	}
}

void mse_frontend_ui_draw_settings_modal(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}

	const mse_frontend_theme_tokens_t *t = TOK;

	ImGuiViewport *viewport = igGetMainViewport();
	if (viewport != NULL) {
		const float width  = fminf(viewport->WorkSize.x - mse_frontend_ui_px(80.0f), mse_frontend_ui_px(880.0f));
		const float height = fminf(viewport->WorkSize.y - mse_frontend_ui_px(80.0f), mse_frontend_ui_px(600.0f));
		igSetNextWindowPos((ImVec2){viewport->WorkPos.x + ((viewport->WorkSize.x - width) * 0.5f),
									viewport->WorkPos.y + ((viewport->WorkSize.y - height) * 0.5f)},
						   ImGuiCond_Appearing, ZERO);
		igSetNextWindowSize((ImVec2){width, height}, ImGuiCond_Appearing);
	}

	if (state->show_settings_window) {
		igOpenPopup_Str("SETTINGS", 0);
	}

	bool open = state->show_settings_window;

	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, ZERO);
	igPushStyleColor_Vec4(ImGuiCol_PopupBg, t->bg_base);

	if (igBeginPopupModal("SETTINGS", &open,
						  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
							  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar)) {
		const ImVec2 total     = igGetContentRegionAvail();
		const float  nav_width = total.x > mse_frontend_ui_px(560.0f) ? mse_frontend_ui_px(208.0f)
																	  : mse_frontend_ui_px(64.0f);
		const float footer_height = mse_frontend_ui_px(58.0f);
		const float body_height   = total.y - footer_height;

		// --- left rail
		igPushStyleColor_Vec4(ImGuiCol_ChildBg, t->bg_raised);
		igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, 0.0f);
		if (igBeginChild_Str("SETTINGS_NAV", (ImVec2){nav_width, body_height}, 0,
							 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			const ImVec2 pos = igGetWindowPos();
			ImDrawList_AddLine(igGetWindowDrawList(), (ImVec2){pos.x + nav_width - 1.0f, pos.y},
							   (ImVec2){pos.x + nav_width - 1.0f, pos.y + body_height},
							   igGetColorU32_Vec4(t->border), 1.0f);

			mse_frontend_ui_gap(16.0f);
			if (nav_width > mse_frontend_ui_px(100.0f)) {
				igIndent(mse_frontend_ui_px(18.0f));
				igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title() * 0.9f);
				igTextColored(t->text, "Settings");
				igPopFont();
				igUnindent(mse_frontend_ui_px(18.0f));
				mse_frontend_ui_gap(12.0f);
			}

			static const struct {
				const char                 *icon;
				mse_frontend_settings_tab_t tab;
			} tabs[] = {
				{MSE_ICON_WRENCH, MSE_FRONTEND_SETTINGS_TAB_GENERAL},
				{MSE_ICON_VIDEO, MSE_FRONTEND_SETTINGS_TAB_VIDEO},
				{MSE_ICON_VOLUME_HIGH, MSE_FRONTEND_SETTINGS_TAB_AUDIO},
				{MSE_ICON_KEYBOARD, MSE_FRONTEND_SETTINGS_TAB_CONTROLS},
				{MSE_ICON_APPEARANCE, MSE_FRONTEND_SETTINGS_TAB_APPEARANCE},
				{MSE_ICON_PERFORMANCE, MSE_FRONTEND_SETTINGS_TAB_PERFORMANCE},
				{MSE_ICON_POWER, MSE_FRONTEND_SETTINGS_TAB_BEHAVIOR},
				{MSE_ICON_ADVANCED, MSE_FRONTEND_SETTINGS_TAB_ADVANCED},
			};

			for (size_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); ++i) {
				if (mse_frontend_ui_sidebar_row(tabs[i].icon, mse_frontend_ui_settings_tab_name(tabs[i].tab),
												state->settings_tab == tabs[i].tab, true)) {
					state->settings_tab = tabs[i].tab;
				}
			}
		}
		igEndChild();
		igPopStyleVar(1);
		igPopStyleColor(1);

		igSameLine(0.0f, 0.0f);

		// --- right pane
		igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,
							(ImVec2){mse_frontend_ui_px(28.0f), mse_frontend_ui_px(22.0f)});
		if (igBeginChild_Str("SETTINGS_BODY", (ImVec2){0.0f, body_height}, ImGuiChildFlags_AlwaysUseWindowPadding,
							 ImGuiWindowFlags_None)) {
			mse_frontend_ui_page_header(NULL, mse_frontend_ui_settings_tab_name(state->settings_tab),
										mse_frontend_ui_settings_tab_blurb(state->settings_tab));
			mse_frontend_settings_tab_body(state);
			mse_frontend_ui_gap(12.0f);
		}
		igEndChild();
		igPopStyleVar(1);

		// --- footer
		igPushStyleColor_Vec4(ImGuiCol_ChildBg, t->bg_raised);
		igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, 0.0f);
		igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,
							(ImVec2){mse_frontend_ui_px(20.0f), mse_frontend_ui_px(11.0f)});
		if (igBeginChild_Str("SETTINGS_FOOTER", ZERO, ImGuiChildFlags_AlwaysUseWindowPadding,
							 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			const ImVec2 pos = igGetWindowPos();
			ImDrawList_AddLine(igGetWindowDrawList(), pos, (ImVec2){pos.x + igGetWindowSize().x, pos.y},
							   igGetColorU32_Vec4(t->border), 1.0f);

			igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
			igTextColored(t->text_faint, "Changes apply as you make them.");
			igPopFont();

			igSameLine(0.0f, 0.0f);
			const float button_w = mse_frontend_ui_px(110.0f);
			igSetCursorPosX(igGetCursorPosX() + igGetContentRegionAvail().x - button_w);
			if (mse_frontend_ui_button("Done", (ImVec2){button_w, mse_frontend_ui_px(32.0f)},
									   MSE_FRONTEND_UI_BUTTON_PRIMARY)) {
				open = false;
			}
		}
		igEndChild();
		igPopStyleVar(2);
		igPopStyleColor(1);

		igEndPopup();
	}

	igPopStyleColor(1);
	igPopStyleVar(1);

	state->show_settings_window = open;
}
