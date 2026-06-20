#define DEBUG_LOG_SOURCE "frontend"
#include "frontend_ui.h"
#include "frontend_input.h"
#include "frontend_cimgui.h"
#include "frontend_imgui.h"
#include "frontend_icons.h"
#include "frontend_app.h"
#include "libmse/libmse_debug.h"
#include "libmse/libmse.h"
#include "libmse/libmse_version.h"
#include "libmse/libmse_cvar.h"
#include "cimgui_markdown.h"
#include <SDL3/SDL_dialog.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include <time.h>

float g_frontend_ui_scale = 1.0f;

static Uint32 g_frontend_file_dialog_event = 0;

static void mse_frontend_ui_draw_sidebar_footer(mse_frontend_ui_state_t *state);
static void mse_frontend_ui_open_rom_file_dialog(mse_frontend_ui_state_t *state);

bool mse_frontend_ui_begin_child_window(const char *id, ImVec2 size, bool border);
void mse_frontend_ui_end_child_window(void);

static void SDLCALL mse_frontend_ui_open_rom_callback(void *userdata, const char *const *filelist, int filter);

static void mse_frontend_ui_icon_text(const char *icon, const char *label)
{
	ImFont *icon_font = mse_frontend_imgui_font_icon();
	if (icon_font != NULL) {
		igPushFont(icon_font, mse_frontend_imgui_font_size_icon());
	}
	igText("%s", icon);
	if (icon_font != NULL) {
		igPopFont();
	}
	igSameLine(0, 6.0f);
	igText("%s", label);
}

bool mse_frontend_ui_begin_child_window(const char *id, ImVec2 size, bool border)
{
	igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, mse_frontend_ui_px(14.0f));
	igPushStyleVar_Float(ImGuiStyleVar_ChildBorderSize, border ? mse_frontend_ui_px(1.0f) : 0.0f);
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){mse_frontend_ui_px(16.0f), mse_frontend_ui_px(16.0f)});
	igPushStyleColor_Vec4(ImGuiCol_ChildBg, (ImVec4){0.10f, 0.10f, 0.14f, 0.88f});
	igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0.42f, 0.30f, 0.68f, 0.42f});
	return igBeginChild_Str(id, size, border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None,
							ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void mse_frontend_ui_end_child_window(void)
{
	igEndChild();
	igPopStyleColor(2);
	igPopStyleVar(3);
}

static void mse_frontend_ui_draw_sidebar_hover_mouse_effect(ImDrawList *draw_list, ImVec2 item_pos, ImVec2 item_size,
															float press_offset)
{
	if (draw_list == NULL) {
		return;
	}

	const ImGuiIO *io = igGetIO_Nil();
	if (io == NULL) {
		return;
	}

	const ImVec2 mouse = io->MousePos;
	const ImVec2 effect_min =
		(ImVec2){item_pos.x + mse_frontend_ui_px(4.0f), item_pos.y + mse_frontend_ui_px(4.0f) + press_offset};
	const ImVec2 effect_max	   = (ImVec2){item_pos.x + item_size.x - mse_frontend_ui_px(4.0f),
										  item_pos.y + item_size.y - mse_frontend_ui_px(4.0f) + press_offset};
	const ImVec2 effect_center = (ImVec2){mouse.x, mouse.y + press_offset * 0.25f};
	const float	 time_value	   = (float)igGetTime();
	const float	 pulse		   = 0.5f + (0.5f * sinf(time_value * 3.6f));
	const float	 radius_glow   = mse_frontend_ui_px(72.0f) + (pulse * mse_frontend_ui_px(6.0f));
	const float	 radius_soft   = mse_frontend_ui_px(42.0f) + (pulse * mse_frontend_ui_px(3.0f));
	const float	 radius_core   = mse_frontend_ui_px(14.0f) + (pulse * mse_frontend_ui_px(1.5f));
	const ImVec2 sheen_min	   = (ImVec2){effect_min.x, effect_center.y - mse_frontend_ui_px(14.0f)};
	const ImVec2 sheen_max	   = (ImVec2){effect_max.x, effect_center.y + mse_frontend_ui_px(14.0f)};
	const ImU32	 glow_col	   = igGetColorU32_Vec4((ImVec4){0.62f, 0.42f, 0.96f, 0.10f});
	const ImU32	 soft_col	   = igGetColorU32_Vec4((ImVec4){0.78f, 0.66f, 1.0f, 0.12f});
	const ImU32	 core_col	   = igGetColorU32_Vec4((ImVec4){0.92f, 0.84f, 1.0f, 0.18f});
	const ImU32	 ring_col	   = igGetColorU32_Vec4((ImVec4){0.90f, 0.80f, 1.0f, 0.16f});
	const ImU32	 sheen_col	   = igGetColorU32_Vec4((ImVec4){0.84f, 0.76f, 1.0f, 0.06f});

	ImDrawList_PushClipRect(draw_list, effect_min, effect_max, true);
	ImDrawList_AddRectFilledMultiColor(draw_list, sheen_min, sheen_max, sheen_col, sheen_col,
									   igGetColorU32_Vec4((ImVec4){0.84f, 0.76f, 1.0f, 0.0f}),
									   igGetColorU32_Vec4((ImVec4){0.84f, 0.76f, 1.0f, 0.0f}));
	ImDrawList_AddCircleFilled(draw_list, effect_center, radius_glow, glow_col, 32);
	ImDrawList_AddCircleFilled(draw_list, effect_center, radius_soft, soft_col, 28);
	ImDrawList_AddCircle(draw_list, effect_center, radius_soft + mse_frontend_ui_px(6.0f), ring_col, 28,
						 mse_frontend_ui_px(1.0f));
	ImDrawList_AddCircleFilled(draw_list, effect_center, radius_core, core_col, 20);
	ImDrawList_PopClipRect(draw_list);
}

bool mse_frontend_ui_sidebar_row(const char *icon, const char *label, bool selected, bool accent)
{
	const float	 full_width = igGetContentRegionAvail().x;
	const ImVec2 item_size	= {full_width, mse_frontend_ui_px(42.0f)};
	const ImVec2 item_pos	= igGetCursorScreenPos();
	ImDrawList	*draw_list	= igGetWindowDrawList();

	igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, mse_frontend_ui_px(6.0f));
	igPushStyleVar_Float(ImGuiStyleVar_FrameBorderSize, 0.0f);

	if (selected) {
		/* Disable ImGui built-in selection color — we draw our own background */
		igPushStyleColor_Vec4(ImGuiCol_Header, (ImVec4){0.0f, 0.0f, 0.0f, 0.0f});
		igPushStyleColor_Vec4(ImGuiCol_HeaderHovered, (ImVec4){0.0f, 0.0f, 0.0f, 0.0f});
		igPushStyleColor_Vec4(ImGuiCol_HeaderActive, (ImVec4){0.0f, 0.0f, 0.0f, 0.0f});
	} else if (accent) {
		igPushStyleColor_Vec4(ImGuiCol_Header, (ImVec4){0.16f, 0.16f, 0.16f, 0.0f});
		igPushStyleColor_Vec4(ImGuiCol_HeaderHovered, (ImVec4){0.20f, 0.20f, 0.20f, 1.0f});
		igPushStyleColor_Vec4(ImGuiCol_HeaderActive, (ImVec4){0.18f, 0.18f, 0.18f, 1.0f});
	} else {
		igPushStyleColor_Vec4(ImGuiCol_Header, (ImVec4){0.14f, 0.14f, 0.14f, 0.0f});
		igPushStyleColor_Vec4(ImGuiCol_HeaderHovered, (ImVec4){0.20f, 0.20f, 0.20f, 1.0f});
		igPushStyleColor_Vec4(ImGuiCol_HeaderActive, (ImVec4){0.18f, 0.18f, 0.18f, 1.0f});
	}

	igPushID_Str(label);
	bool clicked = igSelectable_Bool("##sidebar_row", selected, ImGuiSelectableFlags_SpanAvailWidth, item_size);
	igPopID();
	const bool	row_hovered	 = igIsItemHovered(ImGuiHoveredFlags_RectOnly);
	const bool	row_active	 = igIsItemActive();
	const float press_offset = row_active ? mse_frontend_ui_px(1.2f) : 0.0f;

	const ImVec2 row_min =
		(ImVec2){item_pos.x + mse_frontend_ui_px(4.0f), item_pos.y + mse_frontend_ui_px(4.0f) + press_offset};
	const ImVec2 row_max	  = (ImVec2){item_pos.x + item_size.x - mse_frontend_ui_px(4.0f),
										 item_pos.y + item_size.y - mse_frontend_ui_px(4.0f) + press_offset};
	const float	 row_rounding = mse_frontend_ui_px(10.0f);

	/* draw a subtle rounded background and accent treatment */
	if (selected) {
		ImU32  bg_col	  = igGetColorU32_Vec4((ImVec4){0.11f, 0.08f, 0.22f, 0.94f});
		ImU32  bg_glow	  = igGetColorU32_Vec4((ImVec4){0.38f, 0.20f, 0.72f, 0.18f});
		ImU32  accent_col = igGetColorU32_Vec4((ImVec4){0.75f, 0.58f, 1.0f, 0.98f});
		ImU32  edge_col	  = igGetColorU32_Vec4((ImVec4){0.68f, 0.52f, 0.98f, 0.34f});
		ImVec2 a_min =
			(ImVec2){item_pos.x + mse_frontend_ui_px(2.0f), item_pos.y + mse_frontend_ui_px(8.0f) + press_offset};
		ImVec2 a_max = (ImVec2){item_pos.x + mse_frontend_ui_px(6.0f),
								item_pos.y + item_size.y - mse_frontend_ui_px(8.0f) + press_offset};
		ImDrawList_AddRectFilled(draw_list, row_min, row_max, bg_col, row_rounding, 0);
		ImDrawList_AddRectFilledMultiColor(draw_list, (ImVec2){row_min.x, row_min.y},
										   (ImVec2){row_max.x, row_min.y + (row_max.y - row_min.y) * 0.5f}, bg_glow,
										   igGetColorU32_Vec4((ImVec4){0.16f, 0.10f, 0.28f, 0.08f}),
										   igGetColorU32_Vec4((ImVec4){0.16f, 0.10f, 0.28f, 0.02f}), bg_glow);
		ImDrawList_AddRect(draw_list, row_min, row_max, edge_col, row_rounding, mse_frontend_ui_px(1.0f), 0);
		ImDrawList_AddRectFilled(draw_list, a_min, a_max, accent_col, mse_frontend_ui_px(3.0f), 0);
	} else if (row_hovered) {
		ImU32 hcol	= igGetColorU32_Vec4((ImVec4){0.19f, 0.16f, 0.27f, 0.72f});
		ImU32 hedge = igGetColorU32_Vec4((ImVec4){0.70f, 0.56f, 0.98f, 0.18f});
		ImDrawList_AddRectFilled(draw_list, row_min, row_max, hcol, row_rounding, 0);
		ImDrawList_AddRect(draw_list, row_min, row_max, hedge, row_rounding, mse_frontend_ui_px(1.0f), 0);
		mse_frontend_ui_draw_sidebar_hover_mouse_effect(draw_list, item_pos, item_size, press_offset);
	}

	if (row_active) {

		ImU32 pfill = igGetColorU32_Vec4((ImVec4){0.70f, 0.52f, 1.0f, 0.16f});
		ImU32 pedge = igGetColorU32_Vec4((ImVec4){0.84f, 0.70f, 1.0f, 0.42f});
		ImDrawList_AddRectFilled(draw_list, row_min, row_max, pfill, row_rounding, 0);
		ImDrawList_AddRect(draw_list, row_min, row_max, pedge, row_rounding, mse_frontend_ui_px(1.2f), 0);
	}

	const ImGuiStyle *style = igGetStyle();
	const ImVec4	  text_color =
		selected ? (ImVec4){0.96f, 0.94f, 1.0f, 1.0f}
				 : (row_hovered ? (ImVec4){0.88f, 0.86f, 0.96f, 1.0f} : (ImVec4){0.74f, 0.74f, 0.78f, 1.0f});

	const ImU32 icon_color	 = igGetColorU32_Vec4(text_color);
	const ImU32 label_color	 = icon_color;
	const float row_center_y = item_pos.y + (item_size.y * 0.5f);
	const float icon_size	 = mse_frontend_imgui_font_size_icon() * 1.6f; /* larger sidebar icon */
	const float body_size	 = mse_frontend_imgui_font_size_body() + mse_frontend_ui_px(3.0f);
	/* If the sidebar is narrow, snap to icons-only layout centered. */
	const float icon_y	   = row_center_y - (icon_size * 0.5f) - mse_frontend_ui_px(1.0f) + press_offset;
	bool		icons_only = full_width < mse_frontend_ui_px(64.0f);
	float		icon_x;
	float		label_x = 0.0f;
	float		label_y = row_center_y - (body_size * 0.5f) - 1.0f;
	if (icons_only) {
		icon_x = item_pos.x + (item_size.x * 0.5f) - (icon_size * 0.5f) + style->FramePadding.x +
				 (selected ? mse_frontend_ui_px(10.0f) : mse_frontend_ui_px(6.0f));
	} else {
		icon_x	= item_pos.x + style->FramePadding.x +
				  (selected ? mse_frontend_ui_px(10.0f) : mse_frontend_ui_px(6.0f)); /* nudge icon to the right */
		label_x = icon_x + icon_size + 0.0f;
	}
	icon_x += press_offset;
	label_y += press_offset;

	ImFont *icon_font = mse_frontend_imgui_font_icon();
	if (icon_font != NULL) {
		ImDrawList_AddText_FontPtr(draw_list, icon_font, icon_size, (ImVec2){icon_x, icon_y}, icon_color, icon, NULL,
								   0.0f, NULL);
	} else {
		ImDrawList_AddText_Vec2(draw_list, (ImVec2){icon_x, icon_y}, icon_color, icon, NULL);
	}

	if (!icons_only) {
		ImFont *body_font = mse_frontend_imgui_font_body();
		if (body_font != NULL) {
			ImDrawList_AddText_FontPtr(draw_list, body_font, body_size, (ImVec2){label_x, label_y}, label_color, label,
									   NULL, 0.0f, NULL);
		} else {
			ImDrawList_AddText_Vec2(draw_list, (ImVec2){label_x, label_y}, label_color, label, NULL);
		}
	}

	igPopStyleColor(3);
	igPopStyleVar(2);
	return clicked;
}

static bool mse_frontend_ui_sidebar_item(const char *icon, const char *label, bool selected)
{
	return mse_frontend_ui_sidebar_row(icon, label, selected, false);
}

static bool mse_frontend_ui_sidebar_action(const char *icon, const char *label)
{
	return mse_frontend_ui_sidebar_row(icon, label, false, true);
}

static bool	   g_frontend_dock_layout_built = false;
static ImGuiID g_frontend_dock_left_id		= 0;
static ImGuiID g_frontend_dock_center_id	= 0;
static ImGuiID g_frontend_dock_right_id		= 0;
static float   g_last_systems_width			= 0.0f;

static void mse_frontend_ui_draw_home_view(void)
{
	const ImVec2 content_pos  = igGetCursorScreenPos();
	const ImVec2 content_size = igGetContentRegionAvail();
	ImDrawList	*draw_list	  = igGetWindowDrawList();
	const float	 pad_x		  = mse_frontend_ui_px(24.0f);
	const float	 pad_y		  = mse_frontend_ui_px(20.0f);
	const float	 gap		  = mse_frontend_ui_px(14.0f);
	const float	 hero_height  = mse_frontend_ui_px(172.0f);

	igSetCursorScreenPos((ImVec2){content_pos.x + pad_x, content_pos.y + pad_y});

	const ImVec2 hero_pos  = igGetCursorScreenPos();
	const ImVec2 hero_size = (ImVec2){content_size.x - pad_x * 2.0f, hero_height};
	if (hero_size.x > mse_frontend_ui_px(120.0f)) {
		ImU32		 hero_bg		 = igGetColorU32_Vec4((ImVec4){0.10f, 0.08f, 0.16f, 0.92f});
		ImU32		 hero_edge		 = igGetColorU32_Vec4((ImVec4){0.62f, 0.44f, 0.96f, 0.35f});
		ImU32		 hero_accent	 = igGetColorU32_Vec4((ImVec4){0.78f, 0.66f, 1.00f, 0.92f});
		const float	 rounding		 = mse_frontend_ui_px(14.0f);
		const float	 time_value		 = (float)igGetTime();
		const ImVec2 hero_max		 = (ImVec2){hero_pos.x + hero_size.x, hero_pos.y + hero_size.y};
		const ImVec2 circle_center_a = (ImVec2){
			hero_pos.x + hero_size.x - mse_frontend_ui_px(92.0f) + sinf(time_value * 1.15f) * mse_frontend_ui_px(10.0f),
			hero_pos.y + mse_frontend_ui_px(56.0f) + cosf(time_value * 0.92f) * mse_frontend_ui_px(8.0f)};
		const ImVec2 circle_center_b = (ImVec2){hero_pos.x + hero_size.x - mse_frontend_ui_px(34.0f) +
													cosf(time_value * 0.84f + 1.2f) * mse_frontend_ui_px(12.0f),
												hero_pos.y + hero_size.y - mse_frontend_ui_px(22.0f) +
													sinf(time_value * 1.05f + 0.7f) * mse_frontend_ui_px(9.0f)};
		const ImVec2 circle_center_c = (ImVec2){hero_pos.x + hero_size.x - mse_frontend_ui_px(146.0f) +
													sinf(time_value * 0.73f + 2.1f) * mse_frontend_ui_px(14.0f),
												hero_pos.y + hero_size.y - mse_frontend_ui_px(56.0f) +
													cosf(time_value * 1.18f + 0.35f) * mse_frontend_ui_px(11.0f)};
		const float	 circle_radius_a = mse_frontend_ui_px(74.0f) + sinf(time_value * 1.35f) * mse_frontend_ui_px(7.0f);
		const float	 circle_radius_b =
			mse_frontend_ui_px(92.0f) + cosf(time_value * 1.08f + 0.45f) * mse_frontend_ui_px(9.0f);
		const float circle_radius_c =
			mse_frontend_ui_px(54.0f) + sinf(time_value * 1.52f + 1.1f) * mse_frontend_ui_px(6.0f);
		const float glow_alpha_a = 0.12f + (0.06f * (0.5f + 0.5f * sinf(time_value * 1.25f)));
		const float glow_alpha_b = 0.10f + (0.07f * (0.5f + 0.5f * cosf(time_value * 0.98f + 0.9f)));
		const float glow_alpha_c = 0.08f + (0.05f * (0.5f + 0.5f * sinf(time_value * 1.41f + 1.8f)));
		ImU32		hero_glow_a	 = igGetColorU32_Vec4((ImVec4){0.44f, 0.28f, 0.86f, glow_alpha_a});
		ImU32		hero_glow_b	 = igGetColorU32_Vec4((ImVec4){0.52f, 0.34f, 0.94f, glow_alpha_b});
		ImU32		hero_glow_c	 = igGetColorU32_Vec4((ImVec4){0.38f, 0.24f, 0.82f, glow_alpha_c});

		ImDrawList_AddRectFilled(draw_list, hero_pos, hero_max, hero_bg, rounding, 0);
		ImDrawList_PushClipRect(draw_list, hero_pos, hero_max, true);
		ImDrawList_AddCircleFilled(draw_list, circle_center_a, fmaxf(circle_radius_a, 0.0f), hero_glow_a, 32);
		ImDrawList_AddCircleFilled(draw_list, circle_center_b, fmaxf(circle_radius_b, 0.0f), hero_glow_b, 32);
		ImDrawList_AddCircleFilled(draw_list, circle_center_c, fmaxf(circle_radius_c, 0.0f), hero_glow_c, 32);
		ImDrawList_PopClipRect(draw_list);
		ImDrawList_AddRect(draw_list, hero_pos, hero_max, hero_edge, rounding, mse_frontend_ui_px(1.5f), 0);
		ImDrawList_AddRectFilled(
			draw_list, (ImVec2){hero_pos.x + mse_frontend_ui_px(18.0f), hero_pos.y + mse_frontend_ui_px(18.0f)},
			(ImVec2){hero_pos.x + mse_frontend_ui_px(24.0f), hero_pos.y + hero_size.y - mse_frontend_ui_px(18.0f)},
			hero_accent, mse_frontend_ui_px(3.0f), 0);
		igDummy(hero_size);

		// Using ImFont_RenderText for rendering strings
		ImFont_RenderText(mse_frontend_imgui_font_small(), draw_list, mse_frontend_imgui_font_size_small(),
						  (ImVec2){hero_pos.x + mse_frontend_ui_px(34.0f), hero_pos.y + mse_frontend_ui_px(24.0f)},
						  igGetColorU32_Vec4((ImVec4){0.72f, 0.62f, 0.98f, 1.0f}),
						  (ImVec4){hero_pos.x, hero_pos.y, hero_pos.x + hero_size.x, hero_pos.y + hero_size.y},
						  "DISCOVER", NULL, 0.0f, 0);
		ImFont_RenderText(mse_frontend_imgui_font_title(), draw_list, mse_frontend_imgui_font_size_title(),
						  (ImVec2){hero_pos.x + mse_frontend_ui_px(34.0f), hero_pos.y + mse_frontend_ui_px(38.0f)},
						  igGetColorU32_Vec4((ImVec4){0.96f, 0.96f, 0.99f, 1.0f}),
						  (ImVec4){hero_pos.x, hero_pos.y, hero_pos.x + hero_size.x, hero_pos.y + hero_size.y},
						  "MSE Library", NULL, 0.0f, 0);

		ImFont_RenderText(
			mse_frontend_imgui_font_small(), draw_list, mse_frontend_ui_px(16.0f),
			(ImVec2){hero_pos.x + mse_frontend_ui_px(34.0f), hero_pos.y + mse_frontend_ui_px(64.0f)},
			igGetColorU32_Vec4((ImVec4){0.80f, 0.82f, 0.88f, 1.0f}),
			(ImVec4){hero_pos.x, hero_pos.y + mse_frontend_ui_px(64.0f),
					 hero_pos.x + hero_size.x - mse_frontend_ui_px(24.0f), hero_pos.y + hero_size.y},
			"Browse featured systems, jump back into recent activity, and keep your collection organized in one place.",
			NULL, hero_size.x - mse_frontend_ui_px(24.0f), 0);

		igDummy((ImVec2){0.0f, mse_frontend_ui_px(8.0f)});
		if (igButton(MSE_ICON_BACKENDS " Browse Backends",
					 (ImVec2){mse_frontend_ui_px(170.0f), mse_frontend_ui_px(34.0f)})) {
		}
		igSameLine(0.0f, mse_frontend_ui_px(10.0f));
		if (igButton(MSE_ICON_LOGS " View Logs", (ImVec2){mse_frontend_ui_px(140.0f), mse_frontend_ui_px(34.0f)})) {
		}
	}

	igSetCursorScreenPos((ImVec2){content_pos.x + pad_x, hero_pos.y + hero_size.y + mse_frontend_ui_px(20.0f)});

	igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
	igTextColored((ImVec4){0.90f, 0.90f, 0.95f, 1.0f}, "Quick Access");
	igPopFont();
	igTextDisabled("A cleaner landing page with featured destinations and status summaries.");
	igSpacing();

	const int column_count =
		content_size.x >= mse_frontend_ui_px(900.0f) ? 3 : (content_size.x >= mse_frontend_ui_px(620.0f) ? 2 : 1);
	const float cards_width = content_size.x - pad_x * 2.0f;
	const float card_width	= (cards_width - gap * (float)(column_count - 1)) / (float)column_count;
	const float card_height = mse_frontend_ui_px(156.0f);

	const struct {
		const char *icon;
		const char *title;
		const char *subtitle;
		const char *meta;
	} cards[] = {
		{MSE_ICON_BACKENDS, "Browse Backends", "Inspect installed emulator backends, metadata, and versions.",
		 "Catalog"},
		{MSE_ICON_START_CORE, "Launch a Core", "Pick a backend, load a ROM, and jump into emulation quickly.", "Play"},
		{MSE_ICON_LOGS, "Review Logs", "Track frontend events and runtime diagnostics in real time.", "Debug"},
	};
	const int	card_count	 = (int)(sizeof(cards) / sizeof(cards[0]));
	const int	row_count	 = (card_count + column_count - 1) / column_count;
	const float grid_start_y = igGetCursorScreenPos().y;

	for (int i = 0; i < card_count; ++i) {
		const int	 column	  = i % column_count;
		const int	 row	  = i / column_count;
		const ImVec2 card_pos = (ImVec2){content_pos.x + pad_x + ((card_width + gap) * (float)column),
										 grid_start_y + ((card_height + gap) * (float)row)};

		igSetCursorScreenPos(card_pos);
		igPushID_Int(i);
		igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, mse_frontend_ui_px(12.0f));
		igPushStyleVar_Float(ImGuiStyleVar_ChildBorderSize, mse_frontend_ui_px(1.0f));
		igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,
							(ImVec2){mse_frontend_ui_px(18.0f), mse_frontend_ui_px(16.0f)});
		igPushStyleColor_Vec4(ImGuiCol_ChildBg, (ImVec4){0.11f, 0.11f, 0.15f, 0.90f});
		igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0.64f, 0.46f, 0.95f, 0.24f});

		if (igBeginChild_Str("##library_card", (ImVec2){card_width, card_height}, ImGuiChildFlags_Borders,
							 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			const bool	 is_hovered		= igIsWindowHovered(ImGuiHoveredFlags_ChildWindows);
			const bool	 is_active		= igIsWindowFocused(ImGuiFocusedFlags_ChildWindows);
			const ImVec2 child_pos		= igGetWindowPos();
			const ImVec2 child_size		= igGetWindowSize();
			const ImVec2 child_max		= (ImVec2){child_pos.x + child_size.x, child_pos.y + child_size.y};
			const float	 rounding		= mse_frontend_ui_px(12.0f);
			ImDrawList	*card_draw_list = igGetWindowDrawList();
			ImU32		 card_bg		= igGetColorU32_Vec4(is_hovered ? (ImVec4){0.15f, 0.13f, 0.22f, 0.96f}
																		: (ImVec4){0.11f, 0.11f, 0.15f, 0.90f});
			ImU32		 card_edge		= igGetColorU32_Vec4(is_hovered ? (ImVec4){0.74f, 0.58f, 1.00f, 0.55f}
																		: (ImVec4){0.64f, 0.46f, 0.95f, 0.24f});
			ImU32		 accent_bg		= igGetColorU32_Vec4((ImVec4){0.25f, 0.18f, 0.40f, is_hovered ? 0.95f : 0.82f});
			ImU32		 glow_col		= igGetColorU32_Vec4((ImVec4){0.52f, 0.36f, 0.92f, 0.14f});

			ImDrawList_AddRectFilled(card_draw_list, child_pos, child_max, card_bg, rounding, 0);
			ImDrawList_AddRect(card_draw_list, child_pos, child_max, card_edge, rounding, mse_frontend_ui_px(1.3f), 0);
			ImDrawList_AddCircleFilled(
				card_draw_list,
				(ImVec2){child_max.x - mse_frontend_ui_px(28.0f), child_pos.y + mse_frontend_ui_px(24.0f)},
				mse_frontend_ui_px(18.0f), glow_col, 20);
			ImDrawList_AddRectFilled(
				card_draw_list,
				(ImVec2){child_pos.x + mse_frontend_ui_px(16.0f), child_pos.y + mse_frontend_ui_px(16.0f)},
				(ImVec2){child_pos.x + mse_frontend_ui_px(56.0f), child_pos.y + mse_frontend_ui_px(56.0f)}, accent_bg,
				mse_frontend_ui_px(10.0f), 0);

			ImFont *icon_font = mse_frontend_imgui_font_icon();
			if (icon_font != NULL) {
				ImDrawList_AddText_FontPtr(
					card_draw_list, icon_font, mse_frontend_imgui_font_size_icon(),
					(ImVec2){child_pos.x + mse_frontend_ui_px(26.0f), child_pos.y + mse_frontend_ui_px(22.0f)},
					igGetColorU32_Vec4((ImVec4){0.90f, 0.84f, 1.0f, 1.0f}), cards[i].icon, NULL, 0.0f, NULL);
			}

			igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
			igTextColored((ImVec4){0.70f, 0.64f, 0.92f, 1.0f}, "%s", cards[i].meta);
			igPopFont();

			igDummy((ImVec2){0.0f, mse_frontend_ui_px(34.0f)});

			igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
			igTextColored((ImVec4){0.97f, 0.97f, 0.99f, 1.0f}, "%s", cards[i].title);
			igPopFont();

			igPushTextWrapPos(child_pos.x + child_size.x - mse_frontend_ui_px(18.0f));
			igTextColored((ImVec4){0.78f, 0.80f, 0.86f, 1.0f}, "%s", cards[i].subtitle);
			igPopTextWrapPos();

			igSetCursorPosY(child_size.y - mse_frontend_ui_px(44.0f));
			{
				char action_label[64];
				snprintf(action_label, sizeof(action_label), "Open##library_card_%d", i);
				if (igButton(action_label, (ImVec2){-1.0f, mse_frontend_ui_px(28.0f)})) {
				}
			}
		}
		igEndChild();
		igPopStyleColor(2);
		igPopStyleVar(3);
		igPopID();
	}

	igSetCursorScreenPos((ImVec2){content_pos.x + pad_x, grid_start_y + ((card_height + gap) * (float)row_count)});

	igSpacing();
	igSeparator();
	igSpacing();

	igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
	igTextColored((ImVec4){0.90f, 0.90f, 0.95f, 1.0f}, "Overview");
	igPopFont();

	if (igBeginTable("library_overview", 3,
					 ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame,
					 (ImVec2){0.0f, 0.0f}, 0.0f)) {
		igTableNextRow(0, 0);

		igTableSetColumnIndex(0);
		igTextDisabled("Status");
		igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
		igTextColored((ImVec4){0.36f, 0.82f, 0.52f, 1.0f}, "Ready");
		igPopFont();
		igTextWrapped("Your workspace is ready for browsing and launching content.");

		igTableSetColumnIndex(1);
		igTextDisabled("Collection");
		igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
		igTextColored((ImVec4){0.64f, 0.50f, 0.92f, 1.0f}, "Curated");
		igPopFont();
		igTextWrapped("Use the Backends and Inspector panes to review emulator capabilities.");

		igTableSetColumnIndex(2);
		igTextDisabled("Diagnostics");
		igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
		igTextColored((ImVec4){0.36f, 0.72f, 0.95f, 1.0f}, "Live");
		igPopFont();
		igTextWrapped("Open the logs view for a continuous stream of debug output.");

		igEndTable();
	}

	igDummy((ImVec2){0.0f, mse_frontend_ui_px(10.0f)});
}

static void mse_frontend_ui_draw_backend_manager_view(mse_frontend_ui_state_t *state)
{
	igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
	igText("BACKENDS");
	igPopFont();

	igInputTextWithHint("##Filter", "Filter backends...", state->search_filter, sizeof(state->search_filter), 0, NULL,
						NULL);

	igSeparator();

	const int num_backends = (int)(state->backend_count);

	ImGuiTableFlags table_flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg |
								  ImGuiTableFlags_Resizable;
	if (igBeginTable("cores_table", 4, table_flags, (ImVec2){0, 0}, 0)) {
		igTableSetupColumn("STS", ImGuiTableColumnFlags_WidthFixed, 30.0f, 0);
		igTableSetupColumn("NAME", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
		igTableSetupColumn("VERSION", ImGuiTableColumnFlags_WidthFixed, 80.0f, 0);
		igTableSetupColumn("AUTHOR", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
		igTableHeadersRow();

		for (int i = 0; i < num_backends; ++i) {
			mse_backend_t *b = state->backends[i];
			if (b == NULL) continue;

			const char *name	= b->info.name ? b->info.name : "(unnamed)";
			const char *version = b->info.version ? b->info.version : "--";
			const char *author	= b->info.author ? b->info.author : "--";

			if (state->search_filter[0] != '\0' && strstr(name, state->search_filter) == NULL) continue;

			igTableNextRow(0, 0);
			igTableSetColumnIndex(0);
			igTextColored((ImVec4){0.2f, 0.8f, 0.2f, 1.0f}, MSE_ICON_INSTALLED);

			igTableSetColumnIndex(1);
			igPushStyleColor_Vec4(ImGuiCol_Header, (ImVec4){0, 0, 0, 0});
			igPushStyleColor_Vec4(ImGuiCol_HeaderHovered, (ImVec4){0, 0, 0, 0});
			igPushStyleColor_Vec4(ImGuiCol_HeaderActive, (ImVec4){0, 0, 0, 0});
			if (igSelectable_Bool(name, state->selected_core_index == i, ImGuiSelectableFlags_SpanAllColumns,
								  (ImVec2){0, 0})) {
				state->selected_core_index = i;
			}
			igPopStyleColor(3);

			igTableSetColumnIndex(2);
			igText("%s", version);

			igTableSetColumnIndex(3);
			igText("%s", author);
		}
		igEndTable();
	}

	if (num_backends == 0) {
		igSpacing();
		igTextDisabled("No backends registered.");
	}
}

static void mse_frontend_ui_draw_inspector_view(mse_frontend_ui_state_t *state)
{
	const int num_backends = (int)(state->backend_count);
	if (state->selected_core_index >= 0 && state->selected_core_index < num_backends) {
		mse_backend_t *b = state->backends[state->selected_core_index];
		if (b == NULL) goto inspector_empty;

		const char *name		  = b->info.name ? b->info.name : "(unnamed)";
		const char *version		  = b->info.version ? b->info.version : "--";
		const char *author		  = b->info.author ? b->info.author : "--";
		const char *licence		  = b->info.licence ? b->info.licence : "--";
		const char *desc		  = b->info.description ? b->info.description : "--";
		const char *built		  = b->info.build_date ? b->info.build_date : "--";
		const float full_width	  = igGetContentRegionAvail().x;
		const float card_gap	  = mse_frontend_ui_px(12.0f);
		const float header_height = mse_frontend_ui_px(210.0f);

		if (mse_frontend_ui_begin_child_window("INSPECTOR_HEADER_CHILD", (ImVec2){0.0f, header_height}, true)) {
			const ImVec2 card_pos	   = igGetWindowPos();
			const ImVec2 card_size	   = igGetWindowSize();
			ImDrawList	*draw_list	   = igGetWindowDrawList();
			const float	 icon_box_size = mse_frontend_ui_px(76.0f);
			const float	 icon_box_x	   = card_pos.x + (card_size.x - icon_box_size) * 0.5f;
			const float	 icon_box_y	   = card_pos.y + mse_frontend_ui_px(18.0f);
			ImU32		 glow_a		   = igGetColorU32_Vec4((ImVec4){0.54f, 0.36f, 0.96f, 0.14f});
			ImU32		 glow_b		   = igGetColorU32_Vec4((ImVec4){0.30f, 0.56f, 0.98f, 0.09f});
			ImU32		 accent		   = igGetColorU32_Vec4((ImVec4){0.82f, 0.74f, 1.0f, 0.92f});
			ImU32		 icon_box_bg   = igGetColorU32_Vec4((ImVec4){0.19f, 0.14f, 0.30f, 0.96f});
			ImU32		 icon_box_edge = igGetColorU32_Vec4((ImVec4){0.74f, 0.60f, 1.0f, 0.48f});

			ImDrawList_AddCircleFilled(
				draw_list,
				(ImVec2){card_pos.x + card_size.x - mse_frontend_ui_px(38.0f), card_pos.y + mse_frontend_ui_px(30.0f)},
				mse_frontend_ui_px(40.0f), glow_a, 24);
			ImDrawList_AddCircleFilled(draw_list,
									   (ImVec2){card_pos.x + card_size.x - mse_frontend_ui_px(82.0f),
												card_pos.y + card_size.y - mse_frontend_ui_px(18.0f)},
									   mse_frontend_ui_px(58.0f), glow_b, 24);
			ImDrawList_AddRectFilled(
				draw_list, (ImVec2){card_pos.x + mse_frontend_ui_px(8.0f), card_pos.y + mse_frontend_ui_px(12.0f)},
				(ImVec2){card_pos.x + mse_frontend_ui_px(13.0f), card_pos.y + card_size.y - mse_frontend_ui_px(12.0f)},
				accent, mse_frontend_ui_px(2.5f), 0);
			ImDrawList_AddRectFilled(draw_list, (ImVec2){icon_box_x, icon_box_y},
									 (ImVec2){icon_box_x + icon_box_size, icon_box_y + icon_box_size}, icon_box_bg,
									 mse_frontend_ui_px(12.0f), 0);
			ImDrawList_AddRect(draw_list, (ImVec2){icon_box_x, icon_box_y},
							   (ImVec2){icon_box_x + icon_box_size, icon_box_y + icon_box_size}, icon_box_edge,
							   mse_frontend_ui_px(12.0f), mse_frontend_ui_px(1.2f), 0);

			igPushFont(mse_frontend_imgui_font_icon(), mse_frontend_imgui_font_size_icon());
			igSetCursorScreenPos(
				(ImVec2){card_pos.x + (card_size.x * 0.5f) - (mse_frontend_imgui_font_size_icon() * 0.5f),
						 icon_box_y + mse_frontend_ui_px(18.0f)});
			igTextColored((ImVec4){0.92f, 0.86f, 1.0f, 1.0f}, MSE_ICON_START_CORE);
			igPopFont();

			igSetCursorPosY(mse_frontend_ui_px(108.0f));
			igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
			igTextColored((ImVec4){0.74f, 0.66f, 0.98f, 1.0f}, "BACKEND PROFILE");
			igPopFont();

			igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
			igTextColored((ImVec4){0.96f, 0.96f, 0.99f, 1.0f}, "%s", name);
			igPopFont();

			igTextWrapped("%s", desc);
			igDummy((ImVec2){0.0f, mse_frontend_ui_px(4.0f)});
			if (igBeginTable("inspector_summary", 3,
							 ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
								 ImGuiTableFlags_SizingStretchSame,
							 (ImVec2){0.0f, 0.0f}, 0.0f)) {
				igTableNextRow(0, 0);

				igTableSetColumnIndex(0);
				igTextDisabled("Version");
				igTextColored((ImVec4){0.90f, 0.90f, 0.96f, 1.0f}, "%s", version);

				igTableSetColumnIndex(1);
				igTextDisabled("Author");
				igTextColored((ImVec4){0.90f, 0.90f, 0.96f, 1.0f}, "%s", author);

				igTableSetColumnIndex(2);
				igTextDisabled("Inputs");
				igTextColored((ImVec4){0.90f, 0.90f, 0.96f, 1.0f}, "%zu", b->input_count);

				igEndTable();
			}
		}
		mse_frontend_ui_end_child_window();

		igDummy((ImVec2){0.0f, card_gap});

		if (mse_frontend_ui_begin_child_window("INSPECTOR_LAUNCH_CHILD", (ImVec2){0.0f, 0.0f}, true)) {
			mse_frontend_ui_icon_text(MSE_ICON_START_CORE, "LAUNCH ROM");
			igSeparator();
			igTextDisabled("Select a ROM image to load with the currently active backend.");
			igSetNextItemWidth(-1.0f);
			igInputTextWithHint("##rom_path", "ROM file path...", state->rom_path, sizeof(state->rom_path), 0, NULL,
								NULL);
			if (igButton("Browse...", (ImVec2){mse_frontend_ui_px(110.0f), 0.0f})) {
				mse_frontend_ui_open_rom_file_dialog(state);
			}
			igSameLine(0.0f, mse_frontend_ui_px(8.0f));
			if (igButton(MSE_ICON_START_CORE " Load ROM", (ImVec2){-1.0f, 0.0f})) {
				mse_backend_t *active = mse_frontend_input_manager_get_backend(state->input_manager);
				if (active != NULL && state->rom_path[0] != '\0') {
					FILE *f = fopen(state->rom_path, "rb");
					if (f) {
						fseek(f, 0, SEEK_END);
						long sz = ftell(f);
						fseek(f, 0, SEEK_SET);
						if (sz > 0) {
							uint8_t *data = (uint8_t *)malloc((size_t)sz);
							if (data) {
								fread(data, 1, (size_t)sz, f);
								if (mse_backend_load_rom(active, data, (size_t)sz)) {
									state->core_view_requested = true;
								}
								free(data);
							}
						}
						fclose(f);
					}
				}
			}
		}
		mse_frontend_ui_end_child_window();

		igDummy((ImVec2){0.0f, card_gap});

		if (mse_frontend_ui_begin_child_window("INSPECTOR_DETAILS_CHILD", (ImVec2){0.0f, 0.0f}, true)) {
			mse_frontend_ui_icon_text(MSE_ICON_INFO, "BACKEND DETAILS");
			igSeparator();
			if (igBeginTable("core_info_table", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp,
							 (ImVec2){0, 0}, 0)) {
				igTableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(96.0f), 0);
				igTableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);

				igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());

				igTableNextRow(0, 0);
				igTableSetColumnIndex(0);
				igTextDisabled("Author");
				igTableSetColumnIndex(1);
				igTextWrapped("%s", author);

				igTableNextRow(0, 0);
				igTableSetColumnIndex(0);
				igTextDisabled("Version");
				igTableSetColumnIndex(1);
				igTextWrapped("%s", version);

				igTableNextRow(0, 0);
				igTableSetColumnIndex(0);
				igTextDisabled("License");
				igTableSetColumnIndex(1);
				igTextWrapped("%s", licence);

				igTableNextRow(0, 0);
				igTableSetColumnIndex(0);
				igTextDisabled("Build Date");
				igTableSetColumnIndex(1);
				igTextWrapped("%s", built);

				igTableNextRow(0, 0);
				igTableSetColumnIndex(0);
				igTextDisabled("Inputs");
				igTableSetColumnIndex(1);
				igTextWrapped("%zu", b->input_count);

				igPopFont();
				igEndTable();
			}
		}
		mse_frontend_ui_end_child_window();

		return;
	}

inspector_empty:
	if (mse_frontend_ui_begin_child_window("INSPECTOR_EMPTY_CHILD", (ImVec2){0.0f, 0.0f}, true)) {
		igPushFont(mse_frontend_imgui_font_title(), mse_frontend_imgui_font_size_title());
		igTextColored((ImVec4){0.90f, 0.90f, 0.95f, 1.0f}, "INSPECTOR");
		igPopFont();
		igSeparator();
		igTextDisabled("Select a backend to view details.");
		igSpacing();
		igTextWrapped(
			"Choose an entry from the Backends list to see its description, metadata, and ROM loading actions here.");
	}
	mse_frontend_ui_end_child_window();
}

static void mse_frontend_ui_draw_systems_window(mse_frontend_ui_state_t *state)
{
	igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
	igTextDisabled("v" LIBMSE_VERSION_STRING);
	igPopFont();
	igSeparator();

	const ImGuiStyle *style = igGetStyle();
	const float		  sidebar_footer_height =
		(mse_frontend_ui_px(42.0f) * 2.0f) + (style != NULL ? (style->WindowPadding.y * 2.0f) : 0.0f); //+
	//(style != NULL ? style->ItemSpacing.y : 0.0f) + 4.0f;

	igBeginChild_Str("SYSTEMS_BODY", (ImVec2){0.0f, -sidebar_footer_height}, 0,
					 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (mse_frontend_ui_sidebar_item(MSE_ICON_HOME, "Home", state->current_nav == MSE_FRONTEND_NAV_HOME))
		state->current_nav = MSE_FRONTEND_NAV_HOME;
	if (mse_frontend_ui_sidebar_item(MSE_ICON_LIBRARY, "Library", state->current_nav == MSE_FRONTEND_NAV_LIBRARY))
		state->current_nav = MSE_FRONTEND_NAV_LIBRARY;
	if (mse_frontend_ui_sidebar_item(MSE_ICON_BACKENDS, "Backends", state->current_nav == MSE_FRONTEND_NAV_BACKENDS))
		state->current_nav = MSE_FRONTEND_NAV_BACKENDS;
	if (mse_frontend_ui_sidebar_item(MSE_ICON_BIOS, "BIOS", state->current_nav == MSE_FRONTEND_NAV_BIOS))
		state->current_nav = MSE_FRONTEND_NAV_BIOS;
	if (mse_frontend_ui_sidebar_item(MSE_ICON_MEMVIEW, "MemView", state->current_nav == MSE_FRONTEND_NAV_MEMVIEW))
		state->current_nav = MSE_FRONTEND_NAV_MEMVIEW;
	if (mse_frontend_ui_sidebar_item(MSE_ICON_LOGS, "Logs", state->current_nav == MSE_FRONTEND_NAV_LOGS))
		state->current_nav = MSE_FRONTEND_NAV_LOGS;
	igEndChild();

	/* record current width so we can snap next frame if user sizes small */
	{
		ImVec2 ws			 = igGetWindowSize();
		g_last_systems_width = ws.x;
	}

	igBeginChild_Str("SYSTEMS_FOOTER", (ImVec2){0.0f, 0.0f}, 0,
					 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	mse_frontend_ui_draw_sidebar_footer(state);
	igEndChild();
}

static void mse_frontend_ui_draw_center_window(mse_frontend_ui_state_t *state)
{
	/* The center dock no longer uses a tab bar — show the active view based on navigation state */
	if (state == NULL) return;

	const ImVec2 content_pos  = igGetCursorScreenPos();
	const ImVec2 content_size = igGetContentRegionAvail();
	const ImVec2 clip_min	  = content_pos;
	const ImVec2 clip_max	  = (ImVec2){content_pos.x + content_size.x, content_pos.y + content_size.y};
	ImDrawList	*bg_draw_list = igGetWindowDrawList();

	ImDrawList_PushClipRect(bg_draw_list, clip_min, clip_max, true);

	const ImVec2 mouse		 = igGetIO_Nil()->MousePos;
	const float	 time_value	 = (float)igGetTime();
	const float	 spacing	 = 34.0f;
	const float	 radius_base = 1.8f;
	for (float y = clip_min.y; y <= clip_max.y + spacing; y += spacing) {
		for (float x = clip_min.x; x <= clip_max.x + spacing; x += spacing) {
			const float dx		  = x - mouse.x;
			const float dy		  = y - mouse.y;
			const float distance  = sqrtf((dx * dx) + (dy * dy));
			float		whiteness = 0.18f + (1.0f - (distance / 360.0f));
			if (whiteness < 0.08f) whiteness = 0.08f;
			if (whiteness > 1.0f) whiteness = 1.0f;

			const float pulse  = 0.5f + 0.5f * sinf(time_value * 1.4f + (x + y) * 0.01f);
			const float radius = radius_base + pulse * 0.9f;
			if ((x + radius) < clip_min.x || (x - radius) > clip_max.x || (y + radius) < clip_min.y ||
				(y - radius) > clip_max.y) {
				continue;
			}
			const float alpha	= 0.08f + (whiteness * 0.22f);
			ImU32		dot_col = igGetColorU32_Vec4((ImVec4){whiteness, whiteness, whiteness, alpha});
			ImDrawList_AddCircleFilled(bg_draw_list, (ImVec2){x, y}, radius, dot_col, 12);
		}
	}

	ImDrawList_PopClipRect(bg_draw_list);

	switch (state->current_nav) {
	case MSE_FRONTEND_NAV_HOME: {
		mse_frontend_ui_draw_home_view();
		break;
	}
	case MSE_FRONTEND_NAV_LIBRARY: {
		//mse_frontend_ui_draw_library_view();
		break;
	}
	case MSE_FRONTEND_NAV_BACKENDS:
		mse_frontend_ui_draw_backend_manager_view(state);
		break;
	case MSE_FRONTEND_NAV_LOGS:
		mse_frontend_ui_draw_logs_view();
		break;
	default:
		mse_frontend_ui_draw_home_view();
		break;
	}
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

	ImGuiID left_id			= 0;
	ImGuiID center_right_id = 0;
	g_frontend_dock_left_id = igDockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.22f, &left_id, &center_right_id);

	ImGuiID center_id = 0;
	ImGuiID right_id  = 0;
	igDockBuilderSplitNode(center_right_id, ImGuiDir_Right, 0.27f, &right_id, &center_id);
	g_frontend_dock_left_id	  = left_id;
	g_frontend_dock_center_id = center_id;
	g_frontend_dock_right_id  = right_id;

	ImGuiDockNode *left_node = igDockBuilderGetNode(g_frontend_dock_left_id);
	if (left_node != NULL) {
		left_node->LocalFlags |= ImGuiDockNodeFlags_AutoHideTabBar;
	}

	ImGuiDockNode *right_node = igDockBuilderGetNode(g_frontend_dock_right_id);
	if (right_node != NULL) {
		right_node->LocalFlags |= ImGuiDockNodeFlags_AutoHideTabBar;
	}

	igDockBuilderDockWindow("SYSTEMS_DOCK", g_frontend_dock_left_id);
	igDockBuilderDockWindow("CENTER_DOCK", g_frontend_dock_center_id);
	igDockBuilderDockWindow("EMULATION_DOCK", g_frontend_dock_center_id);
	igDockBuilderDockWindow("INSPECTOR_DOCK", g_frontend_dock_right_id);
	igDockBuilderFinish(dockspace_id);
	igSetWindowFocus_Str("CENTER_DOCK");

	g_frontend_dock_layout_built = true;
}

static void mse_cvar_fullscreen_cb(libmse_cvar_t *cvar, void *user_data) { 
	if (cvar == NULL) return;

	mse_frontend_ui_state_t *state = (mse_frontend_ui_state_t *)user_data;
	if (state == NULL || state->window == NULL) return;

	SDL_SetWindowFullscreen(state->window, *cvar->data.i);
}

static void mse_cvar_presentation_mode_cb(libmse_cvar_t *cvar, void *user_data) {
	if (cvar == NULL) return;

	if (!SDL_WindowSupportsGPUPresentMode(g_app_ctx.gpu_device, g_app_ctx.window, *cvar->data.i))
	{
		DEBUG_ERROR("Presentation mode %d is not supported on this platform, falling back to VSync", *cvar->data.i);
		g_app_ctx.presentation_mode = SDL_GPU_PRESENTMODE_VSYNC;
		return;
	}
	
	if (!SDL_SetGPUSwapchainParameters(g_app_ctx.gpu_device, g_app_ctx.window, g_app_ctx.swapchain_composition, g_app_ctx.presentation_mode))
		DEBUG_ERROR("Failed to update swapchain parameters: %s", SDL_GetError());
}

static void mse_frontend_register_cvars(mse_frontend_ui_state_t *state) 
{
	libmse_cvar_register("mse_fullscreen", LIBMSE_CVAR_INT, (void *)&state->fullscreen, "Current fullscreen state (0 = windowed, 1 = fullscreen)");
	libmse_cvar_register_change_cb("mse_fullscreen", mse_cvar_fullscreen_cb, (void *)state);

	libmse_cvar_register("mse_presentation_mode", LIBMSE_CVAR_INT, (void *)&g_app_ctx.presentation_mode, "Presentation mode (0 = VSync, 1 = Immediate, 2 = Mailbox)");
	libmse_cvar_register_change_cb("mse_presentation_mode", mse_cvar_presentation_mode_cb, NULL);

	libmse_cvar_register("mse_content_scale", LIBMSE_CVAR_FLOAT, (void *)&state->content_scale, "UI content scale factor");
}

static float mse_frontend_ui_status_bar_height(void)
{
	const float		  text_size = mse_frontend_imgui_font_size_small();
	const ImGuiStyle *style		= igGetStyle();
	const float		  padding_y = style != NULL ? style->FramePadding.y : 0.0f;
	/* reduce by 4 pixels to match requested size */
	return text_size + (padding_y * 2.0f) + mse_frontend_ui_px(10.0f);
}

static void mse_frontend_ui_draw_bottom_bar(const mse_frontend_ui_state_t *state)
{
	ImVec2 bar_size = (ImVec2){0.0f, mse_frontend_ui_status_bar_height()};
	if (igBeginChild_Str("STATUS_BAR", bar_size, ImGuiChildFlags_Borders,
						 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		ImGuiIO *io = igGetIO_Nil();

		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());

		igTextDisabled("CORE_READY");
		igSameLine(0, 10.0f);
		igTextDisabled("|");
		igSameLine(0, 10.0f);
		igText("%.1f FPS", io != NULL ? io->Framerate : 0.0f);
		igSameLine(0, mse_frontend_ui_px(10.0f));
		igTextDisabled("|");
		igSameLine(0, mse_frontend_ui_px(10.0f));
		igText("Backend: %s",
			   (state != NULL && state->active_backend_name != NULL) ? state->active_backend_name : "None");
		igSameLine(0, mse_frontend_ui_px(10.0f));
		igTextDisabled("|");
		igSameLine(0, mse_frontend_ui_px(10.0f));
		const char *view_label = "None";
		if (state != NULL) {
			switch (state->current_nav) {
			case MSE_FRONTEND_NAV_LIBRARY:
				view_label = "Library";
				break;
			case MSE_FRONTEND_NAV_BACKENDS:
				view_label = "Backends";
				break;
			case MSE_FRONTEND_NAV_BIOS:
				view_label = "BIOS";
				break;
			case MSE_FRONTEND_NAV_MEMVIEW:
				view_label = "MemView";
				break;
			case MSE_FRONTEND_NAV_LOGS:
				view_label = "Logs";
				break;
			default:
				view_label = "None";
				break;
			}
		}
		igText("View: %s", view_label);

		igPopFont();

		igSameLine(0.0f, mse_frontend_ui_px(20.0f));
		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
		{
			ImVec2 ver_size = igCalcTextSize(LIBMSE_VERSION_BUILD_STRING, NULL, false, 0.0f);
			//ImVec2 vk_size = igCalcTextSize("VULKAN_API", NULL, false, 0.0f);
			const float spacing		 = mse_frontend_ui_px(10.0f);
			const float badges_width = ver_size.x + /*vk_size.x +*/ spacing;
			const float badge_start	 = igGetCursorPosX() + igGetContentRegionAvail().x - badges_width;
			if (badge_start > igGetCursorPosX()) {
				igSetCursorPosX(badge_start);
			}
			//igTextDisabled("VULKAN_API");
			//igSameLine(0, spacing);
			igTextDisabled("%s", LIBMSE_VERSION_BUILD_STRING);
			igSameLine(0, spacing);
		}
		igPopFont();
	}

	igEndChild();
}

static void mse_frontend_ui_draw_sidebar_footer(mse_frontend_ui_state_t *state)
{
	//igDummy((ImVec2){0.0f, 16.0f});
	igSeparator();
	if (mse_frontend_ui_sidebar_action(MSE_ICON_SETTINGS, "Settings")) {
		state->show_settings_window = true;
	}
	if (mse_frontend_ui_sidebar_action(MSE_ICON_POWER, "Power")) {
		state->show_power_confirm = true;
	}
}

int g_frontend_log_callback_registered = 0;

void mse_frontend_ui_init(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}

	state->show_demo_window		= false;
	state->show_metrics_window	= false;
	state->show_style_editor	= false;
	state->show_settings_window = false;
	state->show_power_confirm	= false;
	state->theme				= MSE_FRONTEND_THEME_MSE;
	state->settings_tab			= MSE_FRONTEND_SETTINGS_TAB_GENERAL;
	state->content_scale		= 1.0f;
	state->window				= NULL;
	state->core_view_requested	= false;
	state->fullscreen			= false;
	state->show_about_window	= false;

	state->current_nav = MSE_FRONTEND_NAV_HOME; /* default to Home */
	memset(state->search_filter, 0, sizeof(state->search_filter));
	state->selected_core_index = -1;
	state->show_installed_only = false;
	state->sidebar_width	   = 0.0f;

	mse_frontend_ui_clear_logs();
	if (g_frontend_file_dialog_event == 0) {
		Uint32 event_id = SDL_RegisterEvents(1);
		if (event_id != (Uint32)-1) {
			g_frontend_file_dialog_event = event_id;
		}
	}

	mse_frontend_register_cvars(state);
}

void mse_frontend_ui_draw(mse_frontend_ui_state_t *state)
{
	if (state == NULL) {
		return;
	}

	g_frontend_ui_scale = state->content_scale > 0.0f ? state->content_scale : 1.0f;

	const float bottom_bar_height = mse_frontend_ui_status_bar_height();

	if (igBeginMainMenuBar()) {
		igTextDisabled("MSE");
		igSameLine(0.0f, 18.0f);
		if (igBeginMenu("File", true)) {
			igMenuItem_Bool("Exit", "Alt+F4", false, true);
			igEndMenu();
		}
		if (igBeginMenu("Emulation", true)) igEndMenu();
		if (igBeginMenu("View", true)) {
			if (igMenuItem_BoolPtr("Fullscreen", "F11", &state->fullscreen, true)) {
				SDL_SetWindowFullscreen(state->window, state->fullscreen);
			}
			igEndMenu();
		}
		if (igBeginMenu("Tools", true)) {
			igMenuItem_BoolPtr("ImGui Demo", NULL, &state->show_demo_window, true);
			igMenuItem_BoolPtr("Metrics", NULL, &state->show_metrics_window, true);
			igMenuItem_BoolPtr("Style Editor", NULL, &state->show_style_editor, true);
			if (igMenuItem_Bool("Terminal", "F10", false, true)) {
				state->show_terminal = true;
			}
			if (igMenuItem_Bool("Settings", NULL, false, true)) {
				state->show_settings_window = true;
			}
			igEndMenu();
		}
		if (igBeginMenu("Help", true)) {
			if (igMenuItem_Bool("About", NULL, false, true)) state->show_about_window = true;

			if (igMenuItem_Bool("Licenses", NULL, false, true)) state->show_licence_window = true;

			if (igMenuItem_Bool("Credits", NULL, false, true)) state->show_credits_window = true;
			igEndMenu();
		}
		igEndMainMenuBar();
	}

	ImGuiViewport *viewport = igGetMainViewport();
	igSetNextWindowPos(viewport->WorkPos, ImGuiCond_Always, (ImVec2){0, 0});
	igSetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
	igSetNextWindowViewport(viewport->ID);

	igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 0.0f);
	igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 0.0f);
	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){0.0f, 0.0f});

	const ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
										ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
										ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
										ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
										ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	igBegin("MSE_MAIN_LAYOUT", NULL, host_flags);
	igPopStyleVar(3);

	igBeginChild_Str("MAIN_DOCKSPACE", (ImVec2){0.0f, -(bottom_bar_height)}, 0, ImGuiWindowFlags_NoBackground);
	mse_frontend_ui_ensure_dock_layout(viewport);
	ImGuiDockNodeFlags dock_flags = ImGuiDockNodeFlags_PassthruCentralNode;
	(void)igDockSpace(igGetID_Str("MSE_DOCKSPACE"), (ImVec2){0.0f, 0.0f}, dock_flags, NULL);

	const ImGuiWindowFlags pane_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground |
										ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
										ImGuiWindowFlags_NoNavFocus;

	/* Snap left dock to icon-only width if the user has resized it below threshold */
	const float icon_only_width = 98.0f; /* target width when snapped */
	const float snap_threshold	= 64.0f; /* if smaller than this, snap */
	if (g_last_systems_width > 0.0f && g_last_systems_width < snap_threshold) {
		igSetNextWindowSize((ImVec2){icon_only_width, viewport->WorkSize.y}, ImGuiCond_Always);
		if (g_frontend_dock_layout_built && g_frontend_dock_left_id != 0) {
			igDockBuilderSetNodeSize(g_frontend_dock_left_id, (ImVec2){icon_only_width, viewport->WorkSize.y});
		}
	}

	igSetNextWindowDockID(g_frontend_dock_left_id, ImGuiCond_FirstUseEver);
	if (igBegin("SYSTEMS_DOCK", NULL, pane_flags)) {
		//igPushStyleVarY(ImGuiStyleVar_WindowPadding, 0.0f);
		mse_frontend_ui_draw_systems_window(state);
		//igPopStyleVar(1);
	}
	igEnd();

	igSetNextWindowDockID(g_frontend_dock_center_id, ImGuiCond_FirstUseEver);
	if (igBegin("CENTER_DOCK", NULL, pane_flags)) {
		mse_frontend_ui_draw_center_window(state);
	}
	igEnd();

	igSetNextWindowDockID(g_frontend_dock_right_id, ImGuiCond_FirstUseEver);
	if (igBegin("INSPECTOR_DOCK", NULL, pane_flags)) {
		mse_frontend_ui_draw_inspector_view(state);
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
	/* draw power confirmation modal if requested */
	if (state->show_power_confirm) {
		ImGuiViewport *viewport = igGetMainViewport();
		if (viewport != NULL) {
			const ImVec2 modal_size = (ImVec2){mse_frontend_ui_px(340.0f), mse_frontend_ui_px(160.0f)};
			const ImVec2 modal_pos	= (ImVec2){viewport->WorkPos.x + (viewport->WorkSize.x - modal_size.x) * 0.5f,
											   viewport->WorkPos.y + (viewport->WorkSize.y - modal_size.y) * 0.5f};
			igSetNextWindowPos(modal_pos, ImGuiCond_Appearing, (ImVec2){0.0f, 0.0f});
			igSetNextWindowSize(modal_size, ImGuiCond_Appearing);
		}
		igOpenPopup_Str("EXIT_CONFIRM", 0);
	}
	if (igBeginPopupModal("EXIT_CONFIRM", &state->show_power_confirm, ImGuiWindowFlags_None)) {
		igText("Exit Application");
		igSeparator();
		igTextDisabled("Are you sure you want to exit the application?");
		igSpacing();
		if (igButton("Cancel", (ImVec2){100.0f, 0.0f})) {
			state->show_power_confirm = false;
		}
		igSameLine(0.0f, 8.0f);
		if (igButton("Exit", (ImVec2){100.0f, 0.0f})) {
			igEndPopup();
			exit(0);
		}
		igEndPopup();
	}

	if (state->show_demo_window) {
		igShowDemoWindow(&state->show_demo_window);
	}
	if (state->show_metrics_window) {
		igShowMetricsWindow(&state->show_metrics_window);
	}
	if (state->show_style_editor) {
		igShowStyleEditor(NULL);
	}
}

static void SDLCALL mse_frontend_ui_open_rom_callback(void *userdata, const char *const *filelist, int filter)
{
	(void)userdata;
	(void)filter;

	if (g_frontend_file_dialog_event == 0) {
		return;
	}

	char *selected_path = NULL;
	if (filelist != NULL && filelist[0] != NULL && filelist[0][0] != '\0') {
		const size_t path_length = strlen(filelist[0]);
		selected_path			 = (char *)malloc(path_length + 1);
		if (selected_path != NULL) {
			memcpy(selected_path, filelist[0], path_length + 1);
		}
	}

	SDL_Event event;
	memset(&event, 0, sizeof(event));
	event.type		 = g_frontend_file_dialog_event;
	event.user.type	 = g_frontend_file_dialog_event;
	event.user.code	 = 0;
	event.user.data1 = selected_path;
	event.user.data2 = NULL;
	SDL_PushEvent(&event);
}

static void mse_frontend_ui_open_rom_file_dialog(mse_frontend_ui_state_t *state)
{
	static const SDL_DialogFileFilter filters[] = {{"ROM files", "nes;tnes;zip"}, {"All files", "*"}};
	const char						 *default_location;

	if (state == NULL) {
		return;
	}

	default_location = state->rom_path[0] != '\0' ? state->rom_path : NULL;
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
	}

	return true;
}
