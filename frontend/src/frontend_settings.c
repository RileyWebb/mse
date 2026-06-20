#define DEBUG_LOG_SOURCE "frontend"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h> // Required for roundf()

#include "frontend_ui.h"
#include "frontend_imgui.h"
#include "frontend_cimgui.h"
#include "frontend_icons.h"
#include "frontend_app.h"
#include "libmse/libmse_cvar.h"
#include "libmse/libmse_debug.h"

// Theme color used for section headers
#define SETTINGS_HEADER_COLOR (ImVec4){0.62f, 0.52f, 0.96f, 1.0f}

static const char *mse_frontend_ui_settings_tab_name(mse_frontend_settings_tab_t tab)
{
	switch (tab) {
	case MSE_FRONTEND_SETTINGS_TAB_GENERAL:
		return "General";
	case MSE_FRONTEND_SETTINGS_TAB_VIDEO:
		return "Video";
	case MSE_FRONTEND_SETTINGS_TAB_AUDIO:
		return "Audio";
	case MSE_FRONTEND_SETTINGS_TAB_CONTROLS:
		return "Controls";
	case MSE_FRONTEND_SETTINGS_TAB_APPEARANCE:
		return "Appearance";
	case MSE_FRONTEND_SETTINGS_TAB_BEHAVIOR:
		return "Behavior";
	case MSE_FRONTEND_SETTINGS_TAB_ADVANCED:
		return "Advanced";
	default:
		return "General";
	}
}

void mse_frontend_ui_draw_settings_modal(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}

	ImGuiViewport *viewport = igGetMainViewport();
	if (viewport != NULL) {
		const ImVec2 margin = (ImVec2){mse_frontend_ui_px(24.0f), mse_frontend_ui_px(24.0f)};
		const float	 modal_width =
			viewport->WorkSize.x > margin.x * 2.0f ? viewport->WorkSize.x - (margin.x * 2.0f) : viewport->WorkSize.x;
		const float modal_height =
			viewport->WorkSize.y > margin.y * 2.0f ? viewport->WorkSize.y - (margin.y * 2.0f) : viewport->WorkSize.y;
		const ImVec2 modal_size = (ImVec2){modal_width, modal_height};
		const ImVec2 modal_pos	= (ImVec2){viewport->WorkPos.x + (viewport->WorkSize.x - modal_size.x) * 0.5f,
										   viewport->WorkPos.y + (viewport->WorkSize.y - modal_size.y) * 0.5f};
		igSetNextWindowPos(modal_pos, ImGuiCond_Appearing, (ImVec2){0.0f, 0.0f});
		igSetNextWindowSize(modal_size, ImGuiCond_Appearing);
	}

	if (state->show_settings_window) {
		igOpenPopup_Str("SETTINGS", 0);
	}

	bool open = state->show_settings_window;
	if (igBeginPopupModal("SETTINGS", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
		const ImVec2 content_size = igGetContentRegionAvail();
		const float	 tab_width =
			content_size.x > mse_frontend_ui_px(320.0f) ? mse_frontend_ui_px(240.0f) : mse_frontend_ui_px(180.0f);
		const float footer_height = mse_frontend_ui_px(54.0f);
		const float body_height	  = content_size.y > footer_height ? content_size.y - footer_height : content_size.y;
		const float right_width	  = content_size.x > tab_width + mse_frontend_ui_px(12.0f)
										? content_size.x - tab_width - mse_frontend_ui_px(12.0f)
										: 0.0f;

		igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
		igTextColored((ImVec4){0.90f, 0.90f, 0.95f, 1.0f}, "SETTINGS");
		igPopFont();
		igTextDisabled("Application Configuration");
		igSeparator();

		// LEFT SIDEBAR TABS
		igBeginChild_Str("SETTINGS_LEFT", (ImVec2){tab_width, body_height}, ImGuiChildFlags_Borders,
						 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		if (mse_frontend_ui_sidebar_row(MSE_ICON_WRENCH, "General", state->settings_tab == MSE_FRONTEND_SETTINGS_TAB_GENERAL, true)) {
			state->settings_tab = MSE_FRONTEND_SETTINGS_TAB_GENERAL;
		}
		if (mse_frontend_ui_sidebar_row(MSE_ICON_VIDEO, "Video", state->settings_tab == MSE_FRONTEND_SETTINGS_TAB_VIDEO, true)) {
			state->settings_tab = MSE_FRONTEND_SETTINGS_TAB_VIDEO;
		}
		if (mse_frontend_ui_sidebar_row(MSE_ICON_VOLUME_HIGH, "Audio", state->settings_tab == MSE_FRONTEND_SETTINGS_TAB_AUDIO, true)) {
			state->settings_tab = MSE_FRONTEND_SETTINGS_TAB_AUDIO;
		}
		if (mse_frontend_ui_sidebar_row(MSE_ICON_KEYBOARD, "Controls", state->settings_tab == MSE_FRONTEND_SETTINGS_TAB_CONTROLS, true)) {
			state->settings_tab = MSE_FRONTEND_SETTINGS_TAB_CONTROLS;
		}
		if (mse_frontend_ui_sidebar_row(MSE_ICON_APPEARANCE, "Appearance", state->settings_tab == MSE_FRONTEND_SETTINGS_TAB_APPEARANCE, true)) {
			state->settings_tab = MSE_FRONTEND_SETTINGS_TAB_APPEARANCE;
		}
		if (mse_frontend_ui_sidebar_row(MSE_ICON_POWER, "Behavior", state->settings_tab == MSE_FRONTEND_SETTINGS_TAB_BEHAVIOR, true)) {
			state->settings_tab = MSE_FRONTEND_SETTINGS_TAB_BEHAVIOR;
		}
		if (mse_frontend_ui_sidebar_row(MSE_ICON_CAPS, "Advanced", state->settings_tab == MSE_FRONTEND_SETTINGS_TAB_ADVANCED, true)) {
			state->settings_tab = MSE_FRONTEND_SETTINGS_TAB_ADVANCED;
		}
		igEndChild();

		igSameLine(0.0f, mse_frontend_ui_px(12.0f));

		// RIGHT CONTENT AREA
		igBeginChild_Str("SETTINGS_RIGHT", (ImVec2){right_width, body_height}, ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
		
		igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
		igText("%s", mse_frontend_ui_settings_tab_name(state->settings_tab));
		igPopFont();
		igSeparator();
		igSpacing();

		igPushItemWidth(mse_frontend_ui_px(280.0f)); // Give input fields a nice uniform width

		switch (state->settings_tab) {
		case MSE_FRONTEND_SETTINGS_TAB_GENERAL:
			igTextColored(SETTINGS_HEADER_COLOR, "Core Management");
			igCheckbox("Show installed cores only", &state->show_installed_only);
			igSpacing();
			igTextColored(SETTINGS_HEADER_COLOR, "Updates");
			if (igButton("Check for Updates...", (ImVec2){mse_frontend_ui_px(200.0f), 0})) {
				// Stub
			}
			break;

		case MSE_FRONTEND_SETTINGS_TAB_VIDEO:
		{
			igTextColored(SETTINGS_HEADER_COLOR, "Display");
			igCheckbox("Enable Fullscreen", &state->fullscreen);
			
			// Dynamically populated resolution list specific to the current monitor
            int temp = 0;
			igCombo_Str_arr("Resolution", /*&state->video_resolution_index*/ &temp, g_video_resolutions, g_video_resolution_count, 5);

			// Text Scale Slider with 0.25 snapping
			// Define the range parameters
            const float scale_min = 0.5f;
            const float scale_max = 2.0f;
            const float scale_step = 0.25f;

            // Format a string for the preview preview box (e.g., "1.25x")
            char preview_buf[16];
            snprintf(preview_buf, sizeof(preview_buf), "%.2fx", state->content_scale);

            if (igBeginCombo("UI Text Scale", preview_buf, ImGuiComboFlags_None)) {
                for (float val = scale_min; val <= scale_max; val += scale_step) {
                    char item_buf[16];
                    snprintf(item_buf, sizeof(item_buf), "%.2fx", val);
                    
                    bool is_selected = (state->content_scale == val);
                    
                    if (igSelectable_Bool(item_buf, is_selected, ImGuiSelectableFlags_None, (ImVec2){0, 0})) {
                        state->content_scale = val;
                        // Trigger your update logic here if needed since the scale changed
                    }
                    
                    // Set the initial focus when opening the combo
                    if (is_selected) {
                        igSetItemDefaultFocus();
                    }
                }
                igEndCombo();
            }
			
			igSpacing();
			igSpacing();
			
			igTextColored(SETTINGS_HEADER_COLOR, "Graphics Pipeline");
			
			// SDL_GPU specifically maps to these 3 swapchain present modes
			const char* present_modes[] = { "VSync (FIFO)", "Immediate (Uncapped)", "Mailbox (Fast VSync)" };
			igCombo_Str_arr("Present Mode", (int*)&g_app_ctx.presentation_mode, present_modes, 3, 3);
			
			// Uses the dynamically cached driver backends compiled into SDL3
			igCombo_Str_arr("Graphics Backend", /*&state->video_graphics_device*/ &temp, g_gpu_drivers, g_gpu_driver_count, 3);
			break;
		}

		case MSE_FRONTEND_SETTINGS_TAB_AUDIO:
		{
			igTextColored(SETTINGS_HEADER_COLOR, "Playback");
			igCheckbox("Mute all audio", &state->audio_mute);
			
			if (state->audio_mute) igBeginDisabled(true); // Dim the volume slider if muted
			igSliderFloat("Master Volume", &state->audio_volume, 0.0f, 100.0f, "%.0f %%", ImGuiSliderFlags_None);
			if (state->audio_mute) igEndDisabled();

			igSpacing();
			igSpacing();
			
			igTextColored(SETTINGS_HEADER_COLOR, "Hardware");
			const char* audio_devices[] = { "System Default", "Speakers (High Definition Audio)", "Headphones (USB Audio Device)" };
            int temp = 0; // Placeholder index for selected audio device
			igCombo_Str_arr("Output Device", &temp, audio_devices, 3, 3); //&state->audio_device_index
			break;
		}

		case MSE_FRONTEND_SETTINGS_TAB_CONTROLS:
			igTextColored(SETTINGS_HEADER_COLOR, "Input Device Configuration");
			igTextDisabled("Select a player slot to map physical hardware.");
			igSpacing();
			if (igButton("Configure Player 1", (ImVec2){mse_frontend_ui_px(280.0f), 0.0f})) {}
			if (igButton("Configure Player 2", (ImVec2){mse_frontend_ui_px(280.0f), 0.0f})) {}
			if (igButton("Configure Player 3", (ImVec2){mse_frontend_ui_px(280.0f), 0.0f})) {}
			if (igButton("Configure Player 4", (ImVec2){mse_frontend_ui_px(280.0f), 0.0f})) {}
			igSpacing();
			igCheckbox("Allow input while window is in background", NULL);//&state->pause_on_focus_loss); // Reusing bool creatively
			break;

		case MSE_FRONTEND_SETTINGS_TAB_APPEARANCE:
		{
			igTextColored(SETTINGS_HEADER_COLOR, "Theme");
			const char* theme_names[] = { "Dark Theme", "Light Theme", "Midnight Blue" };
			// Cast enum directly for UI prototype. Will need mapping in production.
			igCombo_Str_arr("Color Scheme", (int*)&state->theme, theme_names, 3, 3);
			break;
		}

		case MSE_FRONTEND_SETTINGS_TAB_BEHAVIOR:
			igTextColored(SETTINGS_HEADER_COLOR, "System");
			igCheckbox("Pause emulation on focus loss", NULL);//&state->pause_on_focus_loss);
			igCheckbox("Automatically save state on exit", NULL);//&state->save_on_exit);
			igCheckbox("Ask for confirmation before quitting", &state->show_power_confirm);
			break;

		case MSE_FRONTEND_SETTINGS_TAB_ADVANCED:
			igTextColored(SETTINGS_HEADER_COLOR, "Developer Tools");
			igCheckbox("Write application logs to file", NULL);//&state->enable_debug_logging);
			
			igSpacing();
			igTextColored(SETTINGS_HEADER_COLOR, "ImGui Debugging");
			igCheckbox("Show ImGui Demo Window", &state->show_demo_window);
			igCheckbox("Show ImGui Metrics Window", &state->show_metrics_window);
			igCheckbox("Show ImGui Style Editor", &state->show_style_editor);
			
			igSpacing();
			if (igButton("Open Developer Console", (ImVec2){mse_frontend_ui_px(200.0f), 0.0f})) {
				state->show_terminal = true;
				open = false; // Optionally close settings when opening terminal
			}
			break;
		}

		igPopItemWidth();
		igEndChild();

		igSpacing();
		igSeparator();
		
		// Modal Footer Actions
		if (igButton("Close", (ImVec2){mse_frontend_ui_px(100.0f), 0.0f})) {
			open = false;
		}
		igSameLine(0.0f, mse_frontend_ui_px(8.0f));
		igTextDisabled("Settings are saved automatically.");

		igEndPopup();
	}

	state->show_settings_window = open;
}