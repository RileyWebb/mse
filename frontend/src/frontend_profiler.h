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

// Draws the frame counter in the top-right of the given rectangle, in screen
// coordinates. Meant for the emulated image's own bounds, so the number sits
// over the picture it describes rather than over the whole window.
//
// Draws nothing when the counter is switched off or profiling is disabled.
void mse_frontend_profiler_draw_framecounter(float x, float y, float width, float height);

bool mse_frontend_profiler_framecounter_visible(void);
void mse_frontend_profiler_set_framecounter_visible(bool visible);

#endif // MSE_FRONTEND_PROFILER_H
