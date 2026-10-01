#ifndef MSE_FRONTEND_CHROME_H
#define MSE_FRONTEND_CHROME_H

#include <stdbool.h>

#include <SDL3/SDL.h>

// The window's decoration: the system's, or one drawn by the frontend.
//
// Custom decoration is a borderless window whose title bar is the main menu
// bar, with the window buttons drawn at its right-hand end, and an SDL hit-test
// callback that tells the platform which pixels drag the window and which
// resize it. On Windows the drag area reports itself as a caption, so snapping,
// drag-to-top, Win+arrows and double-click-to-maximise stay the system's own.
//
// Under a tiling window manager custom decoration is never used, whatever
// mse_window_decoration says: the manager owns the layout, and a title bar with
// minimise and maximise buttons in a tile is clutter that does nothing.

// Decides the mode before the window exists, so the window can be created with
// the right border rather than drawn once with the wrong one. True when the
// window should be created borderless.
bool mse_frontend_chrome_wants_borderless(void);

// Installs the hit-test callback on the new window and follows the cvar from
// then on. Call once, right after the window is created.
void mse_frontend_chrome_attach(SDL_Window *window);

// True while the frontend is drawing the decoration: custom asked for, no
// tiling manager, and the window not fullscreen.
bool mse_frontend_chrome_active(void);

// The tiling window manager that switched custom decoration off, or NULL.
const char *mse_frontend_chrome_tiling_wm(void);

// What mse_window_decoration asks for: 0 the system's, 1 custom. What is in
// force can differ -- see mse_frontend_chrome_active.
int mse_frontend_chrome_mode(void);

// Inside the main menu bar. Begin draws the logo before the first menu; end
// draws the title and the window buttons after the last one, and records the
// space between them as the area that drags the window. Both do nothing when
// custom decoration is not active.
void mse_frontend_chrome_menu_bar_begin(void);
void mse_frontend_chrome_menu_bar_end(void);

// A one-pixel outline around the window. A borderless window has no frame of
// its own, and without one a dark window disappears into a dark desktop.
void mse_frontend_chrome_draw_frame(void);

#endif // MSE_FRONTEND_CHROME_H
