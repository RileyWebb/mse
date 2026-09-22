#define DEBUG_LOG_SOURCE "frontend"

// The profiler window. See libmse/libmse_profiler.h for the machinery behind
// it; everything here is presentation.
//
// One snapshot is pulled per thread per frame and then read many times, so the
// draw code never touches the profiler's live state -- which is also what
// stops the window's own cost from showing up as jitter in the numbers it is
// displaying.

#include <stdio.h>
#include <string.h>

#include "frontend_profiler.h"

#include "frontend_cimgui.h"
#include "frontend_ui.h"
#include "libmse/libmse_cvar.h"
#include "libmse/libmse_profiler.h"

// A frame that fits in the budget is unremarkable; one that runs over is the
// whole reason the window is open. The graph and the frame readout use the
// same three colours so they agree at a glance.
static const ImVec4 COLOUR_GOOD    = {0.42f, 0.82f, 0.51f, 1.00f};
static const ImVec4 COLOUR_WARN    = {0.96f, 0.78f, 0.35f, 1.00f};
static const ImVec4 COLOUR_BAD     = {0.96f, 0.44f, 0.42f, 1.00f};
static const ImVec4 COLOUR_DIM     = {0.55f, 0.57f, 0.62f, 1.00f};
static const ImVec4 COLOUR_HEADING = {0.74f, 0.66f, 0.98f, 1.00f};

static int   g_profiler_enabled  = 1;
static int   g_show_framecounter = 1;
static float g_frame_budget_ms   = 16.67f;
static bool  g_flat_view         = false;

// The latest snapshot that was read cleanly, per thread. Kept rather than
// re-read into: a read that loses the seqlock race would otherwise make the
// thread's tab vanish for a frame, and a tab that flickers while you are
// reading it is worse than one showing numbers 16ms out of date.
static libmse_profiler_thread_view_t g_views[LIBMSE_PROFILER_MAX_THREADS];
static bool                          g_view_ok[LIBMSE_PROFILER_MAX_THREADS];

void mse_frontend_profiler_init(void)
{
	// Cvar names belong to whoever registers them, and these are the
	// frontend's, so they are mse_* even though the data behind them is not.
	libmse_cvar_register("mse_profiler", LIBMSE_CVAR_INT, &g_profiler_enabled,
	                     "Collect frame profiling data (0 = No, 1 = Yes)");
	libmse_cvar_register("mse_profiler_budget_ms", LIBMSE_CVAR_FLOAT, &g_frame_budget_ms,
	                     "Frame budget the profiler colours against, in milliseconds");
	libmse_cvar_register("mse_framecounter", LIBMSE_CVAR_INT, &g_show_framecounter,
	                     "Show the frame counter over the emulated image (0 = No, 1 = Yes)");

	libmse_profiler_set_enabled(g_profiler_enabled != 0);
}

bool mse_frontend_profiler_framecounter_visible(void)
{
	return g_show_framecounter != 0;
}

void mse_frontend_profiler_set_framecounter_visible(bool visible)
{
	g_show_framecounter = visible ? 1 : 0;
}

// Every snapshot for this UI frame is taken before any of it is drawn, so the
// window and the frame counter cannot disagree about when "now" was. A read
// that loses the seqlock race leaves the previous snapshot in place.
void mse_frontend_profiler_new_frame(void)
{
	// Not gated on libmse_profiler_enabled: frame timing is published either
	// way, and the frame counter needs it with zone collection switched off.
	const size_t threads = libmse_profiler_thread_count();
	libmse_profiler_thread_view_t scratch;

	for (size_t i = 0; i < threads && i < LIBMSE_PROFILER_MAX_THREADS; ++i) {
		if (libmse_profiler_read(i, &scratch)) {
			memcpy(&g_views[i], &scratch, sizeof(scratch));
			g_view_ok[i] = true;
		}
	}
}

// The thread whose rate the frame counter reports.
//
// The backend's, when one is profiling: the viewport is showing frames it
// produced, and how fast the UI happens to be redrawing them is a different
// number that belongs in the profiler window. Falls back to the frontend so
// the counter still says something when no backend is instrumented.
static const libmse_profiler_thread_view_t *profiler_game_thread(void)
{
	const libmse_profiler_thread_view_t *fallback = NULL;
	const size_t threads = libmse_profiler_thread_count();

	for (size_t i = 0; i < threads && i < LIBMSE_PROFILER_MAX_THREADS; ++i) {
		if (!g_view_ok[i] || g_views[i].frame_count == 0) {
			continue;
		}
		if (strcmp(g_views[i].name, MSE_FRONTEND_PROFILER_THREAD_NAME) == 0) {
			fallback = &g_views[i];
			continue;
		}
		return &g_views[i];
	}

	return fallback;
}

static float profiler_ms(uint64_t nanoseconds)
{
	return (float)((double)nanoseconds / 1000000.0);
}

static ImVec4 profiler_budget_colour(float milliseconds)
{
	if (g_frame_budget_ms <= 0.0f) {
		return COLOUR_DIM;
	}
	const float ratio = milliseconds / g_frame_budget_ms;
	if (ratio > 1.5f) return COLOUR_BAD;
	if (ratio > 1.0f) return COLOUR_WARN;
	return COLOUR_GOOD;
}

// ---------------------------------------------------------------------------
// Frame counter
//
// Drawn over the emulated image rather than in a window of its own, so it
// tracks the picture when the view is resized or goes fullscreen.
// ---------------------------------------------------------------------------

void mse_frontend_profiler_draw_framecounter(float x, float y, float width, float height)
{
	(void)height;

	// Not gated on libmse_profiler_enabled. The counter runs on frame pacing,
	// which the profiler publishes whether or not zone collection is on, so
	// unticking "Enabled" in the profiler window does not switch this off.
	if (g_show_framecounter == 0) {
		return;
	}

	const libmse_profiler_thread_view_t *view = profiler_game_thread();
	if (view == NULL || view->history_count == 0) {
		return;
	}

	// Averaged over the last half second or so rather than taken from the last
	// frame. A single frame time jitters far too much to read as a number --
	// the graph in the profiler window is the place to see individual frames.
	const uint32_t window = view->history_count < 30 ? view->history_count : 30;
	float total = 0.0f;
	for (uint32_t i = 0; i < window; ++i) {
		total += view->history[view->history_count - 1 - i];
	}

	const float mean_ms = total / (float)window;
	const float fps     = mean_ms > 0.0f ? 1000.0f / mean_ms : 0.0f;

	char rate[32];
	char timing[32];
	snprintf(rate, sizeof(rate), "%.1f FPS", fps);
	snprintf(timing, sizeof(timing), "%.2f ms", mean_ms);

	ImDrawList *draw_list = igGetWindowDrawList();
	if (draw_list == NULL) {
		return;
	}

	const ImVec2 rate_size   = igCalcTextSize(rate, NULL, false, -1.0f);
	const ImVec2 timing_size = igCalcTextSize(timing, NULL, false, -1.0f);

	const float pad       = mse_frontend_ui_px(6.0f);
	const float margin    = mse_frontend_ui_px(8.0f);
	const float box_width = (rate_size.x > timing_size.x ? rate_size.x : timing_size.x) + pad * 2.0f;
	const float box_height = rate_size.y + timing_size.y + pad * 2.0f;

	const float right = x + width - margin;
	const float top   = y + margin;

	// A backing panel, because the thing underneath is arbitrary game output
	// and white-on-white is not a frame counter.
	ImDrawList_AddRectFilled(draw_list,
	                         (ImVec2){right - box_width, top},
	                         (ImVec2){right, top + box_height},
	                         igGetColorU32_Vec4((ImVec4){0.0f, 0.0f, 0.0f, 0.55f}),
	                         mse_frontend_ui_px(4.0f), 0);

	// Right-aligned, so the box does not twitch as the digits change width.
	ImDrawList_AddText_Vec2(draw_list,
	                        (ImVec2){right - pad - rate_size.x, top + pad},
	                        igGetColorU32_Vec4(profiler_budget_colour(mean_ms)), rate, NULL);

	ImDrawList_AddText_Vec2(draw_list,
	                        (ImVec2){right - pad - timing_size.x, top + pad + rate_size.y},
	                        igGetColorU32_Vec4(COLOUR_DIM), timing, NULL);
}

// ---------------------------------------------------------------------------
// Frame-time graph
// ---------------------------------------------------------------------------

// Drawn by hand rather than with igPlotLines so each bar can be coloured
// against the budget: a plot line tells you the frame time moved, this tells
// you whether it mattered.
static void profiler_draw_graph(const libmse_profiler_thread_view_t *view)
{
	const float  height = mse_frontend_ui_px(58.0f);
	const ImVec2 avail  = igGetContentRegionAvail();
	const float  width  = avail.x > 32.0f ? avail.x : 32.0f;

	const ImVec2 origin = igGetCursorScreenPos();
	igDummy((ImVec2){width, height});

	ImDrawList *draw_list = igGetWindowDrawList();
	if (draw_list == NULL || view->history_count == 0) {
		return;
	}

	ImDrawList_AddRectFilled(draw_list, origin, (ImVec2){origin.x + width, origin.y + height},
	                         igGetColorU32_Vec4((ImVec4){0.08f, 0.08f, 0.11f, 1.0f}),
	                         mse_frontend_ui_px(3.0f), 0);

	// Scaled to the worst frame on screen, not to the budget. Pinning the
	// scale to the budget sounds right and is not: a frontend running at
	// 0.1ms a frame then draws every bar one pixel tall, and the variation you
	// opened the window to look at disappears. The budget line below is drawn
	// only when it lands inside the scale, so it appears exactly when it has
	// something to say.
	float peak = 0.0f;
	for (uint32_t i = 0; i < view->history_count; ++i) {
		if (view->history[i] > peak) {
			peak = view->history[i];
		}
	}
	peak *= 1.15f; // a little headroom, so the worst bar is not flush with the top
	if (peak <= 0.0001f) {
		peak = 1.0f;
	}

	const float bar_width = width / (float)LIBMSE_PROFILER_HISTORY;

	for (uint32_t i = 0; i < view->history_count; ++i) {
		const float sample = view->history[i];
		float bar_height = (sample / peak) * height;
		if (bar_height < 1.0f) {
			bar_height = 1.0f;
		}

		// Right-aligned, so the newest frame is always against the same edge
		// and the history grows leftwards into the empty space.
		const float right = origin.x + width - (float)(view->history_count - 1 - i) * bar_width;
		const float left  = right - bar_width;
		if (left < origin.x) {
			continue;
		}

		ImVec4 colour = profiler_budget_colour(sample);
		ImDrawList_AddRectFilled(draw_list,
		                         (ImVec2){left, origin.y + height - bar_height},
		                         (ImVec2){right - 1.0f, origin.y + height},
		                         igGetColorU32_Vec4(colour), 0.0f, 0);
	}

	if (g_frame_budget_ms > 0.0f && g_frame_budget_ms < peak) {
		const float y = origin.y + height - (g_frame_budget_ms / peak) * height;
		ImDrawList_AddLine(draw_list, (ImVec2){origin.x, y}, (ImVec2){origin.x + width, y},
		                   igGetColorU32_Vec4((ImVec4){1.0f, 1.0f, 1.0f, 0.28f}), 1.0f);
	}
}

// ---------------------------------------------------------------------------
// Zone table
// ---------------------------------------------------------------------------

// True when any zone names `index` as its parent.
static bool profiler_zone_has_children(const libmse_profiler_thread_view_t *view, uint32_t index)
{
	for (uint32_t i = 0; i < view->zone_count; ++i) {
		if (view->zones[i].parent == (int16_t)index) {
			return true;
		}
	}
	return false;
}

// Every column but the first. The caller has already filled that one with
// either a tree node or a plain label, which is the only thing the tree and
// flat views disagree about.
static void profiler_draw_zone_columns(const libmse_profiler_thread_view_t *view,
                                       const libmse_profiler_zone_t *zone)
{
	const float frame_ms = profiler_ms(view->frame_ns);
	const float last_ms  = profiler_ms(zone->total_ns);
	const float self_ms  = profiler_ms(zone->total_ns > zone->child_ns
	                                       ? zone->total_ns - zone->child_ns
	                                       : 0);
	const float avg_ms   = zone->samples > 0
	                           ? profiler_ms(zone->sum_ns / zone->samples)
	                           : 0.0f;
	const float min_ms   = zone->min_ns == UINT64_MAX ? 0.0f : profiler_ms(zone->min_ns);
	const float max_ms   = profiler_ms(zone->max_ns);
	const float share    = frame_ms > 0.0f ? (last_ms / frame_ms) * 100.0f : 0.0f;

	igTableSetColumnIndex(1);
	igTextDisabled("%u", zone->calls);

	igTableSetColumnIndex(2);
	igTextColored(profiler_budget_colour(last_ms), "%.3f", last_ms);

	igTableSetColumnIndex(3);
	igText("%.3f", self_ms);

	igTableSetColumnIndex(4);
	igText("%.3f", avg_ms);

	igTableSetColumnIndex(5);
	igTextDisabled("%.3f", min_ms);

	igTableSetColumnIndex(6);
	igTextColored(profiler_budget_colour(max_ms), "%.3f", max_ms);

	igTableSetColumnIndex(7);
	igText("%5.1f%%", share);

	// A bar in the last column reads faster than the percentage beside it, and
	// makes a row that has quietly grown obvious without reading any numbers.
	igTableSetColumnIndex(8);
	const ImVec2 avail  = igGetContentRegionAvail();
	const ImVec2 origin = igGetCursorScreenPos();
	const float  height = igGetTextLineHeight();
	const float  filled = avail.x * (share > 100.0f ? 1.0f : share / 100.0f);
	igDummy((ImVec2){avail.x, height});

	ImDrawList *draw_list = igGetWindowDrawList();
	if (draw_list != NULL && filled > 1.0f) {
		ImDrawList_AddRectFilled(draw_list,
		                         (ImVec2){origin.x, origin.y + height * 0.2f},
		                         (ImVec2){origin.x + filled, origin.y + height * 0.8f},
		                         igGetColorU32_Vec4(profiler_budget_colour(last_ms)),
		                         mse_frontend_ui_px(2.0f), 0);
	}
}

// Emits a zone and everything under it, as a collapsible tree.
//
// The zone array is in first-encounter order, which is already depth-first for
// a tree that has not changed shape -- but a zone entered for the first time
// halfway through a session is appended at the end, so walking the parent
// links is the only ordering that stays right.
static void profiler_draw_subtree(const libmse_profiler_thread_view_t *view, int16_t parent)
{
	for (uint32_t i = 0; i < view->zone_count; ++i) {
		const libmse_profiler_zone_t *zone = &view->zones[i];
		if (zone->parent != parent) {
			continue;
		}

		const bool has_children = profiler_zone_has_children(view, i);

		igTableNextRow(0, 0);
		igTableSetColumnIndex(0);

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns |
		                           ImGuiTreeNodeFlags_DefaultOpen;
		if (!has_children) {
			// A leaf still draws as a row, just with no arrow and nothing
			// pushed for it -- which is why the pop below is conditional.
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}

		// Zone names repeat across the tree -- "publish" under two different
		// parents, say -- and a node's open state is keyed by its ID. The
		// index after "##" makes each row's ID its own without showing, so
		// collapsing one row cannot collapse its namesake elsewhere.
		char label[128];
		snprintf(label, sizeof(label), "%s##z%u", zone->name ? zone->name : "?", i);

		const bool open = igTreeNodeEx_Str(label, flags);

		profiler_draw_zone_columns(view, zone);

		if (open && has_children) {
			profiler_draw_subtree(view, (int16_t)i);
			igTreePop();
		}
	}
}

static void profiler_draw_flat(const libmse_profiler_thread_view_t *view)
{
	// Sorted by this frame's time. An insertion sort over an array this small
	// is not worth improving on, and it keeps the order stable for equal rows
	// so the table does not shuffle while being read.
	uint32_t order[LIBMSE_PROFILER_MAX_ZONES];
	uint32_t count = view->zone_count;

	for (uint32_t i = 0; i < count; ++i) {
		uint32_t j = i;
		while (j > 0 && view->zones[order[j - 1]].total_ns < view->zones[i].total_ns) {
			order[j] = order[j - 1];
			j--;
		}
		order[j] = i;
	}

	for (uint32_t i = 0; i < count; ++i) {
		const libmse_profiler_zone_t *zone = &view->zones[order[i]];

		igTableNextRow(0, 0);
		igTableSetColumnIndex(0);
		// No tree node here: the whole point of the flat view is that the rows
		// are ranked rather than nested, so an arrow would be a lie.
		igText("%s", zone->name ? zone->name : "?");

		profiler_draw_zone_columns(view, zone);
	}
}

static void profiler_draw_thread(const libmse_profiler_thread_view_t *view)
{
	const float frame_ms = profiler_ms(view->frame_ns);
	const float fps      = frame_ms > 0.0f ? 1000.0f / frame_ms : 0.0f;

	// The window's own numbers, over the whole visible history rather than
	// this frame: a single frame's time says almost nothing on its own.
	float worst = 0.0f;
	float total = 0.0f;
	for (uint32_t i = 0; i < view->history_count; ++i) {
		total += view->history[i];
		if (view->history[i] > worst) {
			worst = view->history[i];
		}
	}
	const float mean = view->history_count > 0 ? total / (float)view->history_count : 0.0f;

	igTextColored(profiler_budget_colour(frame_ms), "%.2f ms", frame_ms);
	igSameLine(0.0f, mse_frontend_ui_px(10.0f));
	igText("%.1f fps", fps);
	igSameLine(0.0f, mse_frontend_ui_px(18.0f));
	igTextDisabled("avg %.2f   worst %.2f   over %u frames", mean, worst, view->history_count);

	if (view->dropped > 0 || view->unbalanced > 0) {
		igTextColored(COLOUR_BAD, "instrumentation: %u dropped, %u left open",
		              view->dropped, view->unbalanced);
	}

	profiler_draw_graph(view);
	igSpacing();

	// Resizable because the zone column now carries indentation as well as the
	// name, so how much room it needs depends on how deep the tree goes.
	const ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg |
	                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
	                              ImGuiTableFlags_Resizable;

	if (igBeginTable("profiler_zones", 9, flags, (ImVec2){0.0f, 0.0f}, 0.0f)) {
		igTableSetupColumn("zone", ImGuiTableColumnFlags_WidthFixed, mse_frontend_ui_px(235.0f), 0);
		igTableSetupColumn("calls", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
		igTableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
		igTableSetupColumn("self", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
		igTableSetupColumn("avg", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
		igTableSetupColumn("min", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
		igTableSetupColumn("max", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
		igTableSetupColumn("share", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
		igTableSetupColumn("", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
		igTableHeadersRow();

		if (g_flat_view) {
			profiler_draw_flat(view);
		} else {
			profiler_draw_subtree(view, -1);
		}

		igEndTable();
	}

	if (view->counter_count == 0) {
		return;
	}

	igSpacing();
	igTextColored(COLOUR_HEADING, "COUNTERS");
	igSeparator();

	if (igBeginTable("profiler_counters", 4,
	                 ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg,
	                 (ImVec2){0.0f, 0.0f}, 0.0f)) {
		igTableSetupColumn("counter", ImGuiTableColumnFlags_WidthFixed,
		                   mse_frontend_ui_px(190.0f), 0);
		igTableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
		igTableSetupColumn("avg", ImGuiTableColumnFlags_WidthFixed, 0.0f, 0);
		igTableSetupColumn("range", ImGuiTableColumnFlags_WidthStretch, 0.0f, 0);
		igTableHeadersRow();

		for (uint32_t i = 0; i < view->counter_count; ++i) {
			const libmse_profiler_counter_t *counter = &view->counters[i];
			const double average = counter->samples > 0
			                           ? counter->sum / (double)counter->samples
			                           : 0.0;

			igTableNextRow(0, 0);
			igTableSetColumnIndex(0);
			igText("%s", counter->name ? counter->name : "?");
			igTableSetColumnIndex(1);
			igText("%.2f", counter->value);
			igTableSetColumnIndex(2);
			igTextDisabled("%.2f", average);
			igTableSetColumnIndex(3);
			igTextDisabled("%.2f .. %.2f", counter->min, counter->max);
		}

		igEndTable();
	}
}

// ---------------------------------------------------------------------------

void mse_frontend_profiler_draw(bool *open)
{
	if (open == NULL || !*open) {
		return;
	}

	igSetNextWindowSize((ImVec2){mse_frontend_ui_px(880.0f), mse_frontend_ui_px(560.0f)},
	                    ImGuiCond_FirstUseEver);

	if (!igBegin("Profiler", open, 0)) {
		igEnd();
		return;
	}

	// Via a bool rather than casting the cvar's int: ImGui would write a
	// single byte into a four-byte object, which happens to work here and
	// would stop working the moment the cvar changed type.
	bool enabled = g_profiler_enabled != 0;
	if (igCheckbox("Enabled", &enabled)) {
		g_profiler_enabled = enabled ? 1 : 0;
		libmse_profiler_set_enabled(enabled);
	}
	igSameLine(0.0f, mse_frontend_ui_px(12.0f));
	if (igButton("Reset stats", (ImVec2){0.0f, 0.0f})) {
		libmse_profiler_reset();
	}
	igSameLine(0.0f, mse_frontend_ui_px(12.0f));
	igCheckbox("Flat", &g_flat_view);
	igSameLine(0.0f, mse_frontend_ui_px(12.0f));
	igSetNextItemWidth(mse_frontend_ui_px(90.0f));
	igInputFloat("budget ms", &g_frame_budget_ms, 0.0f, 0.0f, "%.2f", 0);

	igSeparator();

	if (!libmse_profiler_enabled()) {
		igTextDisabled("Zone collection is off. Frame rates are still being measured,\n"
		               "so the frame counter keeps working.");
		igEnd();
		return;
	}

	const size_t threads = libmse_profiler_thread_count();
	if (threads == 0) {
		igTextDisabled("No thread has reported a frame yet.");
		igEnd();
		return;
	}

	// Snapshots were taken by mse_frontend_profiler_new_frame, so this draws
	// exactly what the frame counter is showing.
	if (igBeginTabBar("profiler_threads", 0)) {
		for (size_t i = 0; i < threads && i < LIBMSE_PROFILER_MAX_THREADS; ++i) {
			// A thread that has registered but not finished a frame has
			// nothing to show yet; it gets a tab once it has.
			if (!g_view_ok[i] || g_views[i].frame_count == 0) {
				continue;
			}

			// Tab labels have to be unique and the thread name alone need not
			// be; the index is hidden behind ## so it does not show.
			char label[LIBMSE_PROFILER_NAME_MAX + 16];
			snprintf(label, sizeof(label), "%s##thread%zu", g_views[i].name, i);

			if (igBeginTabItem(label, NULL, 0)) {
				profiler_draw_thread(&g_views[i]);
				igEndTabItem();
			}
		}
		igEndTabBar();
	}

	igEnd();
}
