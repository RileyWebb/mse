#ifndef MSE_FRONTEND_LOGO_H
#define MSE_FRONTEND_LOGO_H

#include <stdbool.h>

#include <SDL3/SDL.h>

#include "frontend_cimgui.h"

// The cartridge. Drawn as shapes rather than from the icon files, so it stays
// sharp at any interface scale. The icon files come from frontend/tools/gen_logo.py,
// which holds the same geometry: change one, change the other.

// Draws the logo into the square at `pos`, `size` pixels a side. Below 48px it
// switches to the simplified drawing, as the icon files do: the full one's
// teeth and stripes are thinner than a pixel there.
void mse_frontend_logo_draw(ImDrawList *dl, ImVec2 pos, float size);

// Gives the window its icon from data/logo/, every size at once so the
// platform can pick the one it needs. False if none could be read; the window
// keeps the default icon and nothing else is affected.
bool mse_frontend_logo_set_window_icon(SDL_Window *window);

#endif // MSE_FRONTEND_LOGO_H
