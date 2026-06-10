#define DEBUG_LOG_SOURCE "frontend"
#include "frontend_ui.h"
#include "frontend_cimgui.h"
#include "frontend_imgui.h"
#include "libmse/debug.h"
#include "libmse/libmse.h"
#include "libmse/libmse_version.h"
#include "cimgui_markdown.h"
#include <stdio.h>
#include <stdlib.h>

static ImGuiMarkdown_Config mdConfig;

static char *credits_markdown;
static char *about_markdown;
static char *licence_markdown;

static void Frontend_MD_LinkCallback(ImGuiMarkdown_LinkCallbackData link)
{
	if (link.link && link.linkLength > 0) {
		char truncated_link[link.linkLength + 1];
		strncpy(truncated_link, link.link, link.linkLength);
		truncated_link[link.linkLength] = '\0';
		SDL_OpenURL(truncated_link);
	}
}

void mse_frontend_ui_draw_about_modal(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}

	if (state->show_about_window) {
		ImGuiViewport *viewport = igGetMainViewport();
		if (viewport != NULL) {
			const ImVec2 modal_size = (ImVec2){mse_frontend_ui_px(720.0f), mse_frontend_ui_px(480.0f)};
			const ImVec2 modal_pos	= (ImVec2){viewport->WorkPos.x + (viewport->WorkSize.x - modal_size.x) * 0.5f,
											   viewport->WorkPos.y + (viewport->WorkSize.y - modal_size.y) * 0.5f};
			igSetNextWindowPos(modal_pos, ImGuiCond_Appearing, (ImVec2){0.0f, 0.0f});
			igSetNextWindowSize(modal_size, ImGuiCond_Appearing);
		}
		igOpenPopup_Str("ABOUT_MSE", 0);
		state->show_about_window = false;
	}

	bool open = true;
	if (igBeginPopupModal("ABOUT_MSE", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
		const ImVec2 available_size = igGetContentRegionAvail();

		igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
		igTextColored((ImVec4){0.96f, 0.96f, 0.98f, 1.0f}, "About MSE Emulator");
		igPopFont();

		igSeparator();

		ImGuiMarkdown_Config_Init(&mdConfig);
		mdConfig.linkCallback	   = Frontend_MD_LinkCallback;
		mdConfig.headingFormats[0] = (ImGuiMarkdown_HeadingFormat){.font = mse_frontend_imgui_font_title()};
		mdConfig.headingFormats[1] = (ImGuiMarkdown_HeadingFormat){.font = mse_frontend_imgui_font_body()};
		mdConfig.headingFormats[2] = (ImGuiMarkdown_HeadingFormat){.font = mse_frontend_imgui_font_body()};

		if (about_markdown) {
			ImGuiMarkdown(about_markdown, strlen(about_markdown), &mdConfig);
		} else {
			FILE *file = fopen("ABOUT", "r");
			if (file) {
				fseek(file, 0, SEEK_END);
				long length = ftell(file);
				fseek(file, 0, SEEK_SET);
				if (length > 0) {
					about_markdown = (char *)malloc((size_t)length + 1);
					if (about_markdown) {
						fread(about_markdown, 1, (size_t)length, file);
						about_markdown[length] = '\0';
					}
				}
				fclose(file);
			} else {
				igText("Error loading about file.");
			}
		}
		igEndPopup();
	}
}

void mse_frontend_ui_draw_credits_modal(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}

	if (state->show_credits_window) {
		ImGuiViewport *viewport = igGetMainViewport();
		if (viewport != NULL) {
			const ImVec2 modal_size = (ImVec2){mse_frontend_ui_px(720.0f), mse_frontend_ui_px(480.0f)};
			const ImVec2 modal_pos	= (ImVec2){viewport->WorkPos.x + (viewport->WorkSize.x - modal_size.x) * 0.5f,
											   viewport->WorkPos.y + (viewport->WorkSize.y - modal_size.y) * 0.5f};
			igSetNextWindowPos(modal_pos, ImGuiCond_Appearing, (ImVec2){0.0f, 0.0f});
			igSetNextWindowSize(modal_size, ImGuiCond_Appearing);
		}
		igOpenPopup_Str("CREDITS_MSE", 0);
		state->show_credits_window = false;
	}

	bool open = true;
	if (igBeginPopupModal("CREDITS_MSE", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
		const ImVec2 available_size = igGetContentRegionAvail();

		igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
		igTextColored((ImVec4){0.96f, 0.96f, 0.98f, 1.0f}, "Credits");
		igPopFont();

		igSeparator();

		ImGuiMarkdown_Config_Init(&mdConfig);
		mdConfig.linkCallback	   = Frontend_MD_LinkCallback;
		mdConfig.headingFormats[0] = (ImGuiMarkdown_HeadingFormat){.font = mse_frontend_imgui_font_title()};
		mdConfig.headingFormats[1] = (ImGuiMarkdown_HeadingFormat){.font = mse_frontend_imgui_font_body()};
		mdConfig.headingFormats[2] = (ImGuiMarkdown_HeadingFormat){.font = mse_frontend_imgui_font_body()};

		if (credits_markdown) {
			ImGuiMarkdown(credits_markdown, strlen(credits_markdown), &mdConfig);
		} else {
			FILE *file = fopen("CREDITS", "r");
			if (file) {
				fseek(file, 0, SEEK_END);
				long length = ftell(file);
				fseek(file, 0, SEEK_SET);
				if (length > 0) {
					credits_markdown = (char *)malloc((size_t)length + 1);
					if (credits_markdown) {
						fread(credits_markdown, 1, (size_t)length, file);
						credits_markdown[length] = '\0';
					}
				}
				fclose(file);
			} else {
				igText("Error loading credits file.");
			}
		}
		igEndPopup();
	}
}

void mse_frontend_ui_draw_licence_modal(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}

	if (state->show_licence_window) {
		ImGuiViewport *viewport = igGetMainViewport();
		if (viewport != NULL) {
			const ImVec2 modal_size = (ImVec2){mse_frontend_ui_px(720.0f), mse_frontend_ui_px(480.0f)};
			const ImVec2 modal_pos	= (ImVec2){viewport->WorkPos.x + (viewport->WorkSize.x - modal_size.x) * 0.5f,
											   viewport->WorkPos.y + (viewport->WorkSize.y - modal_size.y) * 0.5f};
			igSetNextWindowPos(modal_pos, ImGuiCond_Appearing, (ImVec2){0.0f, 0.0f});
			igSetNextWindowSize(modal_size, ImGuiCond_Appearing);
		}
		igOpenPopup_Str("LICENCE_MSE", 0);
		state->show_licence_window = false;
	}

	bool open = true;
	if (igBeginPopupModal("LICENCE_MSE", &open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
		const ImVec2 available_size = igGetContentRegionAvail();

		igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
		igTextColored((ImVec4){0.96f, 0.96f, 0.98f, 1.0f}, "License");
		igPopFont();

		igSeparator();

		ImGuiMarkdown_Config_Init(&mdConfig);
		mdConfig.linkCallback	   = Frontend_MD_LinkCallback;
		mdConfig.headingFormats[0] = (ImGuiMarkdown_HeadingFormat){.font = mse_frontend_imgui_font_title()};
		mdConfig.headingFormats[1] = (ImGuiMarkdown_HeadingFormat){.font = mse_frontend_imgui_font_body()};
		mdConfig.headingFormats[2] = (ImGuiMarkdown_HeadingFormat){.font = mse_frontend_imgui_font_body()};

		if (licence_markdown) {
			ImGuiMarkdown(licence_markdown, strlen(licence_markdown), &mdConfig);
		} else {
			FILE *file = fopen("LICENCE", "r");
			if (file) {
				fseek(file, 0, SEEK_END);
				long length = ftell(file);
				fseek(file, 0, SEEK_SET);
				if (length > 0) {
					licence_markdown = (char *)malloc((size_t)length + 1);
					if (licence_markdown) {
						fread(licence_markdown, 1, (size_t)length, file);
						licence_markdown[length] = '\0';
					}
				}
				fclose(file);
			} else {
				igText("Error loading licence file.");
			}
		}
		igEndPopup();
	}
}

void mse_frontend_ui_draw_help_modal(mse_frontend_ui_state_t *state) {}