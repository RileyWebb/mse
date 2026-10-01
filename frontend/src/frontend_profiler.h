#ifndef MSE_FRONTEND_PROFILER_H
#define MSE_FRONTEND_PROFILER_H

#include <stdbool.h>

// The profiler window.
//
// Drawn from the main loop rather than from mse_frontend_ui_draw, because the
// menu bar only exists in the menu view and the frame you most want to look at
// is the one being spent emulating.

// What the frontend calls its own profiler thread. The frame counter uses it
// to tell the UI thread apart from whichever thread is running the emulation.
#define MSE_FRONTEND_PROFILER_THREAD_NAME "frontend"

// Registers the profiler's cvars and picks up whatever they were set to.
void mse_frontend_profiler_init(void);

// Pulls one snapshot per thread for this UI frame. Call once, early, before
// anything that draws profiler data -- the window and the frame counter then
// share the same snapshots and cannot disagree about when "now" was.
void mse_frontend_profiler_new_frame(void);

// Draws the window when `*open`. Clears *open when the window is closed.
void mse_frontend_profiler_draw(bool *open);

// Where the frame counter sits within the image it is drawn over.
typedef enum mse_frontend_framecounter_pos_e {
	MSE_FRONTEND_FRAMECOUNTER_TOP_LEFT = 0,
	MSE_FRONTEND_FRAMECOUNTER_TOP_CENTRE,
	MSE_FRONTEND_FRAMECOUNTER_TOP_RIGHT,
	MSE_FRONTEND_FRAMECOUNTER_BOTTOM_LEFT,
	MSE_FRONTEND_FRAMECOUNTER_BOTTOM_CENTRE,
	MSE_FRONTEND_FRAMECOUNTER_BOTTOM_RIGHT,
	MSE_FRONTEND_FRAMECOUNTER_POS_COUNT
} mse_frontend_framecounter_pos_t;

// Draws the frame counter inside the given rectangle, in screen coordinates.
// Meant for the emulated image's own bounds, so the number sits over the
// picture it describes rather than over the whole window.
//
// Draws nothing when the counter is switched off.
void mse_frontend_profiler_draw_framecounter(float x, float y, float width, float height);

bool mse_frontend_profiler_framecounter_visible(void);
void mse_frontend_profiler_set_framecounter_visible(bool visible);

int  mse_frontend_profiler_framecounter_pos(void);
void mse_frontend_profiler_set_framecounter_pos(int pos);
const char *mse_frontend_profiler_framecounter_pos_name(int pos);

// Whether the counter prints the frame time under the rate.
bool mse_frontend_profiler_framecounter_detailed(void);
void mse_frontend_profiler_set_framecounter_detailed(bool detailed);

// Whether the counter carries a frame-time trace under the numbers. The mean
// tells you the rate; the trace is what shows a stutter the mean smooths away.
bool mse_frontend_profiler_framecounter_graph(void);
void mse_frontend_profiler_set_framecounter_graph(bool graph);

// Zone collection. Frame pacing is gathered either way, so the counter keeps
// working with this off.
bool mse_frontend_profiler_enabled(void);
void mse_frontend_profiler_set_enabled(bool enabled);

// The frame budget everything is coloured against, in milliseconds.
float mse_frontend_profiler_budget_ms(void);
void  mse_frontend_profiler_set_budget_ms(float budget);

#endif // MSE_FRONTEND_PROFILER_H
