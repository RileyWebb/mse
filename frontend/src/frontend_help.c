#define DEBUG_LOG_SOURCE "frontend"
#include "frontend_ui.h"
#include "frontend_cimgui.h"
#include "frontend_widgets.h"
#include "frontend_imgui.h"
#include "frontend_icons.h"
#include "libmse/libmse_debug.h"
#include "libmse/libmse.h"
#include "libmse/libmse_version.h"
#include "cimgui_markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOK (mse_frontend_theme())

static const ImVec2 ZERO = {0.0f, 0.0f};

static ImGuiMarkdown_Config mdConfig;

// About, Credits and Licence are the same window with a different file in it,
// so they share one implementation. Each keeps its own cache of the text,
// read once on first open.
static char *about_markdown;
static char *credits_markdown;
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

static const char *mse_frontend_help_load(char **cache, const char *path)
{
	if (*cache != NULL) {
		return *cache;
	}

	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	const long length = ftell(file);
	fseek(file, 0, SEEK_SET);

	if (length > 0) {
		char *text = (char *)malloc((size_t)length + 1);
		if (text != NULL) {
			const size_t read = fread(text, 1, (size_t)length, file);
			text[read]        = '\0';
			*cache            = text;
		}
	}
	fclose(file);

	return *cache;
}

static void mse_frontend_help_markdown_config(void)
{
	ImGuiMarkdown_Config_Init(&mdConfig);
	mdConfig.linkCallback      = Frontend_MD_LinkCallback;
	mdConfig.headingFormats[0] = (ImGuiMarkdown_HeadingFormat){.font = mse_frontend_imgui_font_title()};
	mdConfig.headingFormats[1] = (ImGuiMarkdown_HeadingFormat){.font = mse_frontend_imgui_font_body()};
	mdConfig.headingFormats[2] = (ImGuiMarkdown_HeadingFormat){.font = mse_frontend_imgui_font_body()};
}

// `show` is consumed on open, the way the three callers already worked: the
// menu item sets it, and the modal owns its visibility from then on.
static void mse_frontend_help_document_modal(const char *popup_id, const char *icon, const char *title,
											 const char *subtitle, const char *path, char **cache, bool *show)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	if (*show) {
		ImGuiViewport *viewport = igGetMainViewport();
		if (viewport != NULL) {
			const ImVec2 size = (ImVec2){mse_frontend_ui_px(720.0f), mse_frontend_ui_px(520.0f)};
			igSetNextWindowPos((ImVec2){viewport->WorkPos.x + ((viewport->WorkSize.x - size.x) * 0.5f),
										viewport->WorkPos.y + ((viewport->WorkSize.y - size.y) * 0.5f)},
							   ImGuiCond_Appearing, ZERO);
			igSetNextWindowSize(size, ImGuiCond_Appearing);
		}
		igOpenPopup_Str(popup_id, 0);
		*show = false;
	}

	bool open = true;

	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, ZERO);
	igPushStyleColor_Vec4(ImGuiCol_PopupBg, t->bg_base);

	if (igBeginPopupModal(popup_id, &open,
						  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
							  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar)) {
		const ImVec2 total         = igGetContentRegionAvail();
		const float  footer_height = mse_frontend_ui_px(58.0f);
		const float  header_height = mse_frontend_ui_px(84.0f);

		// --- header
		igPushStyleColor_Vec4(ImGuiCol_ChildBg, t->bg_raised);
		igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, 0.0f);
		igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,
							(ImVec2){mse_frontend_ui_px(24.0f), mse_frontend_ui_px(18.0f)});
		if (igBeginChild_Str("DOC_HEADER", (ImVec2){0.0f, header_height}, ImGuiChildFlags_AlwaysUseWindowPadding,
							 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			const ImVec2 pos = igGetWindowPos();
			ImDrawList_AddLine(igGetWindowDrawList(), (ImVec2){pos.x, pos.y + header_height - 1.0f},
							   (ImVec2){pos.x + igGetWindowSize().x, pos.y + header_height - 1.0f},
							   igGetColorU32_Vec4(t->border), 1.0f);

			ImFont *icon_font = mse_frontend_imgui_font_icon();
			if (icon_font != NULL) {
				igPushFont(icon_font, mse_frontend_imgui_font_size_icon());
				igTextColored(t->accent, "%s", icon);
				igPopFont();
				igSameLine(0.0f, mse_frontend_ui_px(14.0f));
			}

			igBeginGroup();
			mse_frontend_ui_title_text("%s", title);
			igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
			igTextColored(t->text_muted, "%s", subtitle);
			igPopFont();
			igEndGroup();
		}
		igEndChild();
		igPopStyleVar(2);
		igPopStyleColor(1);

		// --- body
		igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,
							(ImVec2){mse_frontend_ui_px(26.0f), mse_frontend_ui_px(20.0f)});
		if (igBeginChild_Str("DOC_BODY", (ImVec2){0.0f, total.y - header_height - footer_height},
							 ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None)) {
			const char *text = mse_frontend_help_load(cache, path);
			if (text != NULL) {
				mse_frontend_help_markdown_config();
				ImGuiMarkdown(text, strlen(text), &mdConfig);
			} else {
				mse_frontend_ui_empty_state(MSE_ICON_INFO, "Nothing to show",
											"The document could not be read from the working directory.");
			}
		}
		igEndChild();
		igPopStyleVar(1);

		// --- footer
		igPushStyleColor_Vec4(ImGuiCol_ChildBg, t->bg_raised);
		igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, 0.0f);
		igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,
							(ImVec2){mse_frontend_ui_px(20.0f), mse_frontend_ui_px(11.0f)});
		if (igBeginChild_Str("DOC_FOOTER", ZERO, ImGuiChildFlags_AlwaysUseWindowPadding,
							 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			const ImVec2 pos = igGetWindowPos();
			ImDrawList_AddLine(igGetWindowDrawList(), pos, (ImVec2){pos.x + igGetWindowSize().x, pos.y},
							   igGetColorU32_Vec4(t->border), 1.0f);

			igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
			igTextColored(t->text_faint, "%s", LIBMSE_VERSION_BUILD_STRING);
			igPopFont();

			igSameLine(0.0f, 0.0f);
			const float button_w = mse_frontend_ui_px(110.0f);
			igSetCursorPosX(igGetCursorPosX() + igGetContentRegionAvail().x - button_w);
			if (mse_frontend_ui_button("Close", (ImVec2){button_w, mse_frontend_ui_px(32.0f)},
									   MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
				igCloseCurrentPopup();
			}
		}
		igEndChild();
		igPopStyleVar(2);
		igPopStyleColor(1);

		igEndPopup();
	}

	igPopStyleColor(1);
	igPopStyleVar(1);
}

void mse_frontend_ui_draw_about_modal(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}
	mse_frontend_help_document_modal("ABOUT_MSE", MSE_ICON_INFO, "About MSE", "v" LIBMSE_VERSION_STRING, "ABOUT",
									 &about_markdown, &state->show_about_window);
}

void mse_frontend_ui_draw_credits_modal(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}
	mse_frontend_help_document_modal("CREDITS_MSE", MSE_ICON_CAPS, "Credits", "The people and projects behind MSE",
									 "CREDITS.md", &credits_markdown, &state->show_credits_window);
}

void mse_frontend_ui_draw_licence_modal(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}
	mse_frontend_help_document_modal("LICENCE_MSE", MSE_ICON_BIOS, "Licences",
									 "MSE and everything it is built on", "LICENCE", &licence_markdown,
									 &state->show_licence_window);
}

void mse_frontend_ui_draw_help_modal(mse_frontend_ui_state_t *state)
{
	(void)state;
}
