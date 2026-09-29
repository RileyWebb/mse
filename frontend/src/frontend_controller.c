#define DEBUG_LOG_SOURCE "frontend"

#include "frontend_controller.h"

#include "frontend_cimgui.h"
#include "frontend_icons.h"
#include "frontend_imgui.h"
#include "frontend_input.h"
#include "frontend_ui.h"
#include "frontend_widgets.h"

#include "libmse/libmse.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TOK (mse_frontend_theme())

static const ImVec2 ZERO = {0.0f, 0.0f};

// Which control the pointer is over, resolved while drawing the diagram and
// used by the list below it so the two highlight together.
static int s_hovered_input = -1;

// --- helpers ----------------------------------------------------------------

static const char *controller_input_name(const mse_backend_input_desc_t *desc)
{
	if (desc == NULL) {
		return "?";
	}
	if (desc->name != NULL && desc->name[0] != '\0') {
		return desc->name;
	}
	return desc->id != NULL ? desc->id : "?";
}

// True while the backend reports this input as held. Read straight from the
// array the input thread writes, which is the same value the emulator sees --
// so a control lighting up here means the binding really works.
static bool controller_input_active(const libmse_backend_t *backend, size_t index)
{
	if (backend == NULL || backend->input_states == NULL || index >= backend->input_count) {
		return false;
	}
	return fabsf(backend->input_states[index]) > 0.4f;
}

// --- the diagram ------------------------------------------------------------

typedef struct {
	ImVec2 min, max;
	float  rounding;
	bool   circle;
} control_rect_t;

// Places a control from body units into screen space.
static control_rect_t controller_place(const mse_backend_input_layout_t *layout, ImVec2 origin, float unit)
{
	control_rect_t out;

	const float w = layout->w * unit;
	const float h = layout->h * unit;
	const float cx = origin.x + (layout->x * unit);
	const float cy = origin.y + (layout->y * unit);

	out.min = (ImVec2){cx - (w * 0.5f), cy - (h * 0.5f)};
	out.max = (ImVec2){cx + (w * 0.5f), cy + (h * 0.5f)};
	out.circle = layout->shape == MSE_INPUT_SHAPE_CIRCLE;

	switch (layout->shape) {
	case MSE_INPUT_SHAPE_PILL:   out.rounding = fminf(w, h) * 0.5f; break;
	case MSE_INPUT_SHAPE_DPAD:   out.rounding = unit * 0.008f; break;
	case MSE_INPUT_SHAPE_RECT:   out.rounding = unit * 0.02f; break;
	default:                     out.rounding = 0.0f; break;
	}
	return out;
}

static void controller_draw_shape(ImDrawList *dl, const control_rect_t *rect, ImU32 fill, ImU32 edge,
								  float thickness)
{
	if (rect->circle) {
		const ImVec2 centre = {(rect->min.x + rect->max.x) * 0.5f, (rect->min.y + rect->max.y) * 0.5f};
		const float  radius = (rect->max.x - rect->min.x) * 0.5f;
		ImDrawList_AddCircleFilled(dl, centre, radius, fill, 32);
		if (thickness > 0.0f) {
			ImDrawList_AddCircle(dl, centre, radius, edge, 32, thickness);
		}
	} else {
		ImDrawList_AddRectFilled(dl, rect->min, rect->max, fill, rect->rounding, 0);
		if (thickness > 0.0f) {
			ImDrawList_AddRect(dl, rect->min, rect->max, edge, rect->rounding, 0, thickness);
		}
	}
}

// Draws the pad and returns the control the pointer is over, or -1.
static int controller_draw_diagram(mse_frontend_ui_state_t *state, libmse_backend_t *backend, float width)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	const mse_backend_controller_desc_t *desc = backend->controller_desc;
	const float aspect = (desc != NULL && desc->aspect > 0.1f) ? desc->aspect : 2.0f;

	// The body is `aspect` wide and 1 tall in its own units; `unit` converts
	// those to pixels. Margin leaves room for the labels hung off each control.
	const float margin_top = mse_frontend_ui_px(34.0f);
	const float margin_x   = mse_frontend_ui_px(30.0f);
	const float margin_bot = mse_frontend_ui_px(46.0f);
	const float unit       = (width - (margin_x * 2.0f)) / aspect;
	const float height     = unit + margin_top + margin_bot;

	const ImVec2 area   = igGetCursorScreenPos();
	const ImVec2 origin = {area.x + margin_x, area.y + margin_top};
	ImDrawList  *dl     = igGetWindowDrawList();

	igInvisibleButton("##pad_area", (ImVec2){width, height}, 0);
	const ImVec2 mouse = igGetIO_Nil()->MousePos;
	const bool   area_hovered = igIsItemHovered(ImGuiHoveredFlags_None);

	// --- body
	const ImVec2 body_min = origin;
	const ImVec2 body_max = {origin.x + (aspect * unit), origin.y + unit};
	const float  body_round = unit * 0.06f;

	mse_frontend_ui_shadow(dl, body_min, body_max, body_round, 0.6f);
	ImDrawList_AddRectFilled(dl, body_min, body_max, igGetColorU32_Vec4(t->bg_raised), body_round, 0);
	ImDrawList_AddRectFilledMultiColor(dl, body_min, (ImVec2){body_max.x, body_min.y + (unit * 0.45f)},
									   mse_frontend_theme_u32(t->text, 0.05f), mse_frontend_theme_u32(t->text, 0.05f),
									   mse_frontend_theme_u32(t->text, 0.0f), mse_frontend_theme_u32(t->text, 0.0f));
	ImDrawList_AddRect(dl, body_min, body_max, igGetColorU32_Vec4(t->border_strong), body_round, 0,
					   mse_frontend_ui_px(1.5f));

	// The recessed plates are sized from the controls that sit on them rather
	// than from fractions of the body, so they suit whatever a backend
	// declares instead of only ever suiting a NES pad.
	for (int pass = 0; pass < 2; ++pass) {
		const uint32_t want = (pass == 0) ? MSE_INPUT_SHAPE_DPAD : MSE_INPUT_SHAPE_PILL;

		float lo_x = FLT_MAX, lo_y = FLT_MAX, hi_x = -FLT_MAX, hi_y = -FLT_MAX;
		bool  any = false;
		for (size_t i = 0; i < backend->input_count; ++i) {
			if (backend->input_layouts[i].shape != want) {
				continue;
			}
			const control_rect_t r = controller_place(&backend->input_layouts[i], origin, unit);
			lo_x = fminf(lo_x, r.min.x);
			lo_y = fminf(lo_y, r.min.y);
			hi_x = fmaxf(hi_x, r.max.x);
			hi_y = fmaxf(hi_y, r.max.y);
			any = true;
		}
		if (!any) {
			continue;
		}

		const float pad = unit * (pass == 0 ? 0.030f : 0.055f);
		ImDrawList_AddRectFilled(dl, (ImVec2){lo_x - pad, lo_y - pad}, (ImVec2){hi_x + pad, hi_y + pad},
								 igGetColorU32_Vec4(t->bg_sunken), unit * 0.035f, 0);
		ImDrawList_AddRect(dl, (ImVec2){lo_x - pad, lo_y - pad}, (ImVec2){hi_x + pad, hi_y + pad},
						   igGetColorU32_Vec4(t->border), unit * 0.035f, 0, 1.0f);
	}

	// --- controls
	size_t capture_index = 0;
	const bool capturing = mse_frontend_input_is_capturing(state->input_manager, &capture_index);

	int hovered = -1;
	for (size_t i = 0; i < backend->input_count; ++i) {
		const mse_backend_input_layout_t *layout = &backend->input_layouts[i];
		if (layout->shape == MSE_INPUT_SHAPE_NONE) {
			continue;
		}

		const control_rect_t rect = controller_place(layout, origin, unit);

		const bool over = area_hovered && mouse.x >= rect.min.x && mouse.x <= rect.max.x &&
						  mouse.y >= rect.min.y && mouse.y <= rect.max.y;
		if (over) {
			hovered = (int)i;
		}

		const bool waiting = capturing && capture_index == i;
		const bool active  = controller_input_active(backend, i);

		ImVec4 fill = t->bg_hover;
		ImVec4 edge = t->border_strong;
		float  thickness = mse_frontend_ui_px(1.5f);

		if (waiting) {
			// Pulses while it waits, so it is obvious which control the next
			// press will land on.
			const float pulse = 0.5f + (0.5f * sinf((float)igGetTime() * 6.0f));
			fill = mse_frontend_theme_alpha(t->accent, 0.35f + (0.35f * pulse));
			edge = t->accent;
			thickness = mse_frontend_ui_px(2.5f);
		} else if (active) {
			fill = t->accent;
			edge = t->accent_hover;
		} else if (over) {
			fill = t->bg_active;
			edge = t->accent;
		}

		// A touch of shadow under each control, so they sit on the plate
		// rather than looking cut out of it.
		if (!active && !waiting) {
			const control_rect_t drop = {{rect.min.x, rect.min.y + (unit * 0.012f)},
										 {rect.max.x, rect.max.y + (unit * 0.012f)},
										 rect.rounding,
										 rect.circle};
			controller_draw_shape(dl, &drop, mse_frontend_theme_u32((ImVec4){0.0f, 0.0f, 0.0f, 1.0f}, 0.35f), 0,
								  0.0f);
		}

		controller_draw_shape(dl, &rect, igGetColorU32_Vec4(fill), igGetColorU32_Vec4(edge), thickness);

		// The control is moulded with its own name on a real pad, and without
		// it there is no telling Select from Start. Only drawn where it fits;
		// a d-pad arm is obvious from its position anyway.
		ImFont *face = mse_frontend_imgui_font_small();
		if (face != NULL && (layout->shape == MSE_INPUT_SHAPE_CIRCLE || layout->shape == MSE_INPUT_SHAPE_PILL ||
							 layout->shape == MSE_INPUT_SHAPE_RECT)) {
			const char *name = controller_input_name(&backend->input_descs[i]);
			const float avail_w = (rect.max.x - rect.min.x) - (unit * 0.03f);
			const float avail_h = (rect.max.y - rect.min.y) - (unit * 0.02f);

			float face_size = mse_frontend_imgui_font_size_small();
			ImVec2 extent = ImFont_CalcTextSizeA(face, face_size, FLT_MAX, 0.0f, name, NULL, NULL);
			if (extent.x > avail_w && extent.x > 0.0f) {
				face_size *= avail_w / extent.x;
				extent = ImFont_CalcTextSizeA(face, face_size, FLT_MAX, 0.0f, name, NULL, NULL);
			}

			if (face_size >= mse_frontend_ui_px(7.0f) && extent.y <= avail_h) {
				const ImVec4 on_control = (active || waiting) ? t->text_on_accent : t->text_muted;
				ImDrawList_AddText_FontPtr(dl, face, face_size,
										   (ImVec2){((rect.min.x + rect.max.x) * 0.5f) - (extent.x * 0.5f),
													((rect.min.y + rect.max.y) * 0.5f) - (extent.y * 0.5f)},
										   igGetColorU32_Vec4(on_control), name, NULL, 0.0f, NULL);
			}
		}
	}

	// --- labels, hung outside the body so they never sit on a control
	ImFont *font = mse_frontend_imgui_font_small();
	if (font != NULL) {
		const float size = mse_frontend_imgui_font_size_small();
		const float gap  = mse_frontend_ui_px(10.0f);

		typedef struct {
			size_t input;
			float  anchor_x; // the control this belongs to
			float  x, w;     // where the text ends up
			float  y;
			float  tie_y;    // where the leader meets the control
			bool   above;
			char   text[64];
		} label_t;

		label_t labels[MSE_INPUT_MAX_INPUTS];
		int     count = 0;

		for (size_t i = 0; i < backend->input_count && count < MSE_INPUT_MAX_INPUTS; ++i) {
			const mse_backend_input_layout_t *layout = &backend->input_layouts[i];
			if (layout->shape == MSE_INPUT_SHAPE_NONE) {
				continue;
			}

			label_t *entry = &labels[count++];
			entry->input = i;

			mse_input_binding_t binding;
			snprintf(entry->text, sizeof(entry->text), "%s", "---");
			if (mse_frontend_input_get_binding(state->input_manager, i, &binding)) {
				mse_frontend_input_binding_label(&binding, entry->text, sizeof(entry->text));
			}

			const control_rect_t rect = controller_place(layout, origin, unit);
			entry->anchor_x = (rect.min.x + rect.max.x) * 0.5f;
			entry->w = ImFont_CalcTextSizeA(font, size, FLT_MAX, 0.0f, entry->text, NULL, NULL).x;
			entry->above = ((rect.min.y + rect.max.y) * 0.5f) < (body_min.y + (unit * 0.5f));
			entry->tie_y = entry->above ? rect.min.y : rect.max.y;
			entry->y = entry->above ? body_min.y - margin_top + mse_frontend_ui_px(4.0f)
									: body_max.y + mse_frontend_ui_px(14.0f);
			entry->x = entry->anchor_x - (entry->w * 0.5f);
		}

		// Two rows, resolved separately: sort by anchor, sweep right pushing
		// overlaps apart, then sweep back left off the right edge. Without it
		// the d-pad's three lower labels ran into each other.
		for (int row = 0; row < 2; ++row) {
			const bool above = row == 0;

			int order[MSE_INPUT_MAX_INPUTS];
			int n = 0;
			for (int i = 0; i < count; ++i) {
				if (labels[i].above == above) {
					order[n++] = i;
				}
			}
			for (int i = 1; i < n; ++i) {
				const int key = order[i];
				int j = i - 1;
				while (j >= 0 && labels[order[j]].anchor_x > labels[key].anchor_x) {
					order[j + 1] = order[j];
					--j;
				}
				order[j + 1] = key;
			}

			for (int i = 1; i < n; ++i) {
				label_t *prev = &labels[order[i - 1]];
				label_t *cur  = &labels[order[i]];
				const float limit = prev->x + prev->w + gap;
				if (cur->x < limit) {
					cur->x = limit;
				}
			}
			for (int i = n - 1; i >= 0; --i) {
				label_t *cur = &labels[order[i]];
				const float right_edge = (i == n - 1) ? (area.x + width) : (labels[order[i + 1]].x - gap);
				if (cur->x + cur->w > right_edge) {
					cur->x = right_edge - cur->w;
				}
				if (cur->x < area.x) {
					cur->x = area.x;
				}
			}
		}

		for (int i = 0; i < count; ++i) {
			const label_t *entry = &labels[i];
			const bool     lit   = (hovered == (int)entry->input) || controller_input_active(backend, entry->input);
			const ImVec4   colour = lit ? t->accent : t->text_muted;
			const float    text_centre = entry->x + (entry->w * 0.5f);
			const float    meet_y = entry->above ? entry->y + size : entry->y;

			// An elbow rather than a straight line: once a label has been
			// pushed sideways a vertical tick would point at the wrong control.
			const ImU32 line = mse_frontend_theme_u32(lit ? t->accent : t->border_strong, lit ? 0.9f : 0.45f);
			const float mid_y = (entry->tie_y + meet_y) * 0.5f;
			ImDrawList_AddLine(dl, (ImVec2){entry->anchor_x, entry->tie_y}, (ImVec2){entry->anchor_x, mid_y}, line,
							   1.0f);
			ImDrawList_AddLine(dl, (ImVec2){entry->anchor_x, mid_y}, (ImVec2){text_centre, mid_y}, line, 1.0f);
			ImDrawList_AddLine(dl, (ImVec2){text_centre, mid_y}, (ImVec2){text_centre, meet_y}, line, 1.0f);

			ImDrawList_AddText_FontPtr(dl, font, size, (ImVec2){entry->x, entry->y}, igGetColorU32_Vec4(colour),
									   entry->text, NULL, 0.0f, NULL);
		}
	}

	// Clicking a control starts a capture for it.
	if (hovered >= 0 && igIsItemClicked(ImGuiMouseButton_Left)) {
		mse_frontend_input_start_capture(state->input_manager, (size_t)hovered);
	}

	return hovered;
}

// --- the binding list -------------------------------------------------------

static void controller_draw_list(mse_frontend_ui_state_t *state, libmse_backend_t *backend)
{
	const mse_frontend_theme_tokens_t *t = TOK;

	size_t     capture_index = 0;
	const bool capturing     = mse_frontend_input_is_capturing(state->input_manager, &capture_index);

	const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;

	igPushStyleVar_Vec2(ImGuiStyleVar_CellPadding, (ImVec2){mse_frontend_ui_px(10.0f), mse_frontend_ui_px(6.0f)});
	if (igBeginTable("controller_bindings", 3, flags, ZERO, 0.0f)) {
		igTableSetupScrollFreeze(0, 1);
		igTableSetupColumn("CONTROL", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
		igTableSetupColumn("BOUND TO", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
		igTableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(74.0f), 0);

		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
		igTableHeadersRow();
		igPopFont();

		for (size_t i = 0; i < backend->input_count; ++i) {
			const bool waiting = capturing && capture_index == i;
			const bool active  = controller_input_active(backend, i);

			igTableNextRow(0, 0.0f);
			igPushID_Int((int)i);

			igTableSetColumnIndex(0);
			if (active) {
				mse_frontend_ui_dot(t->accent, controller_input_name(&backend->input_descs[i]));
			} else {
				igTextColored(s_hovered_input == (int)i ? t->accent : t->text, "%s",
							  controller_input_name(&backend->input_descs[i]));
			}

			igTableSetColumnIndex(1);
			if (waiting) {
				igTextColored(t->accent, "Press any key or button...");
			} else {
				mse_input_binding_t binding;
				char                label[64] = "---";
				const bool          bound =
					mse_frontend_input_get_binding(state->input_manager, i, &binding) &&
					binding.source_type != MSE_INPUT_SOURCE_NONE;
				if (bound) {
					mse_frontend_input_binding_label(&binding, label, sizeof(label));
					igTextColored(t->text_muted, "%s", label);
				} else {
					igTextColored(t->text_faint, "unbound");
				}
			}

			igTableSetColumnIndex(2);
			if (waiting) {
				if (mse_frontend_ui_button("Cancel", (ImVec2){-FLT_MIN, 0.0f}, MSE_FRONTEND_UI_BUTTON_GHOST)) {
					mse_frontend_input_cancel_capture(state->input_manager);
				}
			} else if (mse_frontend_ui_button("Rebind", (ImVec2){-FLT_MIN, 0.0f},
											  MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
				mse_frontend_input_start_capture(state->input_manager, i);
			}

			igPopID();
		}
		igEndTable();
	}
	igPopStyleVar(1);
}

// --- window -----------------------------------------------------------------

void mse_frontend_controller_draw(mse_frontend_ui_state_t *state)
{
	if (state == NULL || !state->show_controller_window) {
		return;
	}

	const mse_frontend_theme_tokens_t *t = TOK;

	ImGuiViewport *viewport = igGetMainViewport();
	if (viewport != NULL) {
		const ImVec2 size = {mse_frontend_ui_px(620.0f), mse_frontend_ui_px(640.0f)};
		igSetNextWindowPos((ImVec2){viewport->WorkPos.x + ((viewport->WorkSize.x - size.x) * 0.5f),
									viewport->WorkPos.y + ((viewport->WorkSize.y - size.y) * 0.5f)},
						   ImGuiCond_FirstUseEver, ZERO);
		igSetNextWindowSize(size, ImGuiCond_FirstUseEver);
	}
	igSetNextWindowSizeConstraints((ImVec2){mse_frontend_ui_px(440.0f), mse_frontend_ui_px(360.0f)},
								   (ImVec2){FLT_MAX, FLT_MAX}, NULL, NULL);

	bool open = state->show_controller_window;

	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,
						(ImVec2){mse_frontend_ui_px(22.0f), mse_frontend_ui_px(18.0f)});
	if (!igBegin("Controller###MSE_CONTROLLER", &open, ImGuiWindowFlags_NoCollapse)) {
		igEnd();
		igPopStyleVar(1);
		state->show_controller_window = open;
		return;
	}
	igPopStyleVar(1);

	libmse_backend_t *backend = mse_frontend_input_manager_get_backend(state->input_manager);

	if (backend == NULL || backend->input_descs == NULL || backend->input_count == 0U) {
		mse_frontend_ui_empty_state(MSE_ICON_CONTROLLER, "No backend",
									"Load a backend and its controls will show up here.");
		igEnd();
		state->show_controller_window = open;
		return;
	}

	// --- header
	const char *device_name = (backend->controller_desc != NULL && backend->controller_desc->name != NULL)
								  ? backend->controller_desc->name
								  : "Controls";

	mse_frontend_ui_eyebrow(backend->info.name != NULL ? backend->info.name : "BACKEND");
	mse_frontend_ui_gap(6.0f);

	igBeginGroup();
	mse_frontend_ui_title_text("%s", device_name);
	igEndGroup();

	{
		const float button_w = mse_frontend_ui_px(150.0f);
		const float right    = igGetCursorPosX() + igGetContentRegionAvail().x - button_w;
		igSameLine(0.0f, 0.0f);
		if (right > igGetCursorPosX()) {
			igSetCursorPosX(right);
		}
		if (mse_frontend_ui_button("Reset to defaults", (ImVec2){button_w, mse_frontend_ui_px(32.0f)},
								   MSE_FRONTEND_UI_BUTTON_SECONDARY)) {
			mse_frontend_input_reset_defaults(state->input_manager);
		}
	}

	mse_frontend_ui_gap(10.0f);

	// --- diagram, when the backend described one
	if (backend->input_layouts != NULL) {
		// Capped and centred: stretched to the full window width the pad
		// dwarfed the list under it and pushed the bindings off the bottom.
		const float avail_w   = igGetContentRegionAvail().x;
		const float diagram_w = fminf(avail_w, mse_frontend_ui_px(520.0f));
		if (diagram_w < avail_w) {
			igSetCursorPosX(igGetCursorPosX() + ((avail_w - diagram_w) * 0.5f));
		}
		s_hovered_input = controller_draw_diagram(state, backend, diagram_w);
		mse_frontend_ui_gap(6.0f);

		size_t     capture_index = 0;
		const bool capturing     = mse_frontend_input_is_capturing(state->input_manager, &capture_index);

		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
		if (capturing && capture_index < backend->input_count) {
			igTextColored(t->accent, "Press any key or button for %s, or Escape to cancel.",
						  controller_input_name(&backend->input_descs[capture_index]));
		} else {
			igTextColored(t->text_faint, "Click a control to rebind it. Held inputs light up as you press them.");
		}
		igPopFont();
	} else {
		s_hovered_input = -1;
		igPushFont(mse_frontend_imgui_font_small(), mse_frontend_imgui_font_size_small());
		igTextColored(t->text_faint, "%s does not describe a pad layout, so its inputs are listed instead.",
					  backend->info.name != NULL ? backend->info.name : "This backend");
		igPopFont();
	}

	mse_frontend_ui_gap(12.0f);
	mse_frontend_ui_section("BINDINGS");
	mse_frontend_ui_gap(6.0f);

	controller_draw_list(state, backend);

	igEnd();
	state->show_controller_window = open;
}
