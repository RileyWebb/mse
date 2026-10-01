#define DEBUG_LOG_SOURCE "frontend"

#include "frontend_logo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define STBI_NO_STDIO
#include <stb_image.h>

#include "libmse/libmse_debug.h"

// Everything below is in the drawing's own 64-unit square.

// ImU32 is ABGR: alpha in the top byte, red in the bottom.
#define LOGO_RGB(r, g, b) ((ImU32)(0xFF000000u | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r)))

#define LOGO_BODY  LOGO_RGB(0x4C, 0xC9, 0xF0)
#define LOGO_LABEL LOGO_RGB(0x10, 0x10, 0x18)
#define LOGO_CORAL LOGO_RGB(0xF2, 0x54, 0x5B)
#define LOGO_AMBER LOGO_RGB(0xF5, 0x9E, 0x0B)
#define LOGO_GREEN LOGO_RGB(0x4A, 0xDE, 0x80)

#define LOGO_SMALL_BELOW 48.0f

typedef struct {
	ImDrawList *dl;
	ImVec2      origin;
	float       scale;
} logo_pen_t;

static ImVec2 logo_at(const logo_pen_t *pen, float x, float y)
{
	return (ImVec2){pen->origin.x + (x * pen->scale), pen->origin.y + (y * pen->scale)};
}

static void logo_rect(const logo_pen_t *pen, float x0, float y0, float x1, float y1, ImU32 colour, float radius,
					  ImDrawFlags corners)
{
	ImDrawList_AddRectFilled(pen->dl, logo_at(pen, x0, y0), logo_at(pen, x1, y1), colour, radius * pen->scale,
							 radius > 0.0f ? corners : ImDrawFlags_RoundCornersNone);
}

// The body outline: top-left corner rounded, top-right cut at 45 degrees from
// (cut_x, top), square along the bottom. Clockwise on screen, which is the
// winding ImGui's anti-aliased convex fill expects.
static void logo_body(const logo_pen_t *pen, float top, float bottom, float cut_x)
{
	const float pi = 3.14159265f;

	ImDrawList_PathArcTo(pen->dl, logo_at(pen, 12.0f, top + 4.0f), 4.0f * pen->scale, pi, 1.5f * pi, 8);
	ImDrawList_PathLineTo(pen->dl, logo_at(pen, cut_x, top));
	ImDrawList_PathLineTo(pen->dl, logo_at(pen, 56.0f, top + (56.0f - cut_x)));
	ImDrawList_PathLineTo(pen->dl, logo_at(pen, 56.0f, bottom));
	ImDrawList_PathLineTo(pen->dl, logo_at(pen, 8.0f, bottom));
	ImDrawList_PathFillConvex(pen->dl, LOGO_BODY);
}

// The groove and the gaps between the connector's prongs are left undrawn
// rather than painted in a background colour, so the logo sits correctly on
// whatever surface it is drawn over.
static void logo_draw_full(const logo_pen_t *pen)
{
	static const struct {
		float       x0, x1;
		ImDrawFlags corner;
	} prongs[] = {
		{14.0f, 18.5f, ImDrawFlags_RoundCornersBottomLeft},
		{21.5f, 24.5f, 0},
		{27.5f, 30.5f, 0},
		{33.5f, 36.5f, 0},
		{39.5f, 42.5f, 0},
		{45.5f, 50.0f, ImDrawFlags_RoundCornersBottomRight},
	};

	logo_body(pen, 5.0f, 43.0f, 47.0f);
	logo_rect(pen, 8.0f, 45.0f, 56.0f, 49.0f, LOGO_BODY, 0.0f, 0);
	logo_rect(pen, 14.0f, 48.0f, 50.0f, 52.0f, LOGO_BODY, 0.0f, 0);
	for (size_t i = 0; i < sizeof(prongs) / sizeof(prongs[0]); ++i) {
		logo_rect(pen, prongs[i].x0, 52.0f, prongs[i].x1, 59.0f, LOGO_BODY, prongs[i].corner ? 3.0f : 0.0f,
				  prongs[i].corner);
	}

	logo_rect(pen, 14.0f, 12.0f, 50.0f, 38.0f, LOGO_LABEL, 3.0f, ImDrawFlags_RoundCornersAll);
	logo_rect(pen, 19.0f, 18.0f, 45.0f, 21.5f, LOGO_CORAL, 1.75f, ImDrawFlags_RoundCornersAll);
	logo_rect(pen, 19.0f, 24.0f, 45.0f, 27.5f, LOGO_AMBER, 1.75f, ImDrawFlags_RoundCornersAll);
	logo_rect(pen, 19.0f, 30.0f, 35.0f, 33.5f, LOGO_GREEN, 1.75f, ImDrawFlags_RoundCornersAll);
}

// Every edge on a multiple of 4 units, which is one pixel at 16px.
static void logo_draw_small(const logo_pen_t *pen)
{
	logo_body(pen, 4.0f, 48.0f, 48.0f);
	logo_rect(pen, 16.0f, 48.0f, 48.0f, 52.0f, LOGO_BODY, 0.0f, 0);
	logo_rect(pen, 16.0f, 52.0f, 24.0f, 60.0f, LOGO_BODY, 2.0f, ImDrawFlags_RoundCornersBottomLeft);
	logo_rect(pen, 28.0f, 52.0f, 36.0f, 60.0f, LOGO_BODY, 0.0f, 0);
	logo_rect(pen, 40.0f, 52.0f, 48.0f, 60.0f, LOGO_BODY, 2.0f, ImDrawFlags_RoundCornersBottomRight);

	logo_rect(pen, 16.0f, 12.0f, 48.0f, 32.0f, LOGO_LABEL, 2.0f, ImDrawFlags_RoundCornersAll);
	logo_rect(pen, 20.0f, 16.0f, 44.0f, 20.0f, LOGO_CORAL, 0.0f, 0);
	logo_rect(pen, 20.0f, 24.0f, 36.0f, 28.0f, LOGO_AMBER, 0.0f, 0);
}

void mse_frontend_logo_draw(ImDrawList *dl, ImVec2 pos, float size)
{
	if (dl == NULL || size <= 0.0f) {
		return;
	}

	// Whole pixels, so the small drawing's grid lands on the screen's.
	const logo_pen_t pen = {dl, {floorf(pos.x), floorf(pos.y)}, size / 64.0f};

	if (size < LOGO_SMALL_BELOW) {
		logo_draw_small(&pen);
	} else {
		logo_draw_full(&pen);
	}
}

static SDL_Surface *logo_load_png(int size)
{
	char path[64];
	snprintf(path, sizeof(path), "data/logo/mse-%d.png", size);

	size_t file_size = 0;
	void  *file      = SDL_LoadFile(path, &file_size);
	if (file == NULL) {
		return NULL;
	}

	int            w = 0, h = 0, channels = 0;
	unsigned char *pixels = stbi_load_from_memory((const stbi_uc *)file, (int)file_size, &w, &h, &channels, 4);
	SDL_free(file);
	if (pixels == NULL) {
		return NULL;
	}

	// Copied into a surface SDL owns, so the decoded buffer can go straight
	// back and nothing has to outlive this function.
	SDL_Surface *surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
	if (surface != NULL) {
		for (int y = 0; y < h; ++y) {
			memcpy((Uint8 *)surface->pixels + ((size_t)y * (size_t)surface->pitch), pixels + ((size_t)y * (size_t)w * 4u),
				   (size_t)w * 4u);
		}
	}

	stbi_image_free(pixels);
	return surface;
}

bool mse_frontend_logo_set_window_icon(SDL_Window *window)
{
	if (window == NULL) {
		return false;
	}

	// Largest first: it is the one SDL_SetWindowIcon is handed, and the rest
	// ride along as alternates for the title bar, Alt-Tab and the taskbar to
	// choose between.
	static const int sizes[] = {256, 128, 64, 48, 32, 24, 16};

	SDL_Surface *icon = NULL;
	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
		SDL_Surface *surface = logo_load_png(sizes[i]);
		if (surface == NULL) {
			continue;
		}
		if (icon == NULL) {
			icon = surface;
		} else {
			// Takes its own reference, so ours is released either way.
			SDL_AddSurfaceAlternateImage(icon, surface);
			SDL_DestroySurface(surface);
		}
	}

	if (icon == NULL) {
		DEBUG_WARN("No window icon: could not read data/logo/mse-*.png");
		return false;
	}

	const bool ok = SDL_SetWindowIcon(window, icon);
	if (!ok) {
		DEBUG_WARN("Could not set the window icon: %s", SDL_GetError());
	}
	SDL_DestroySurface(icon);
	return ok;
}
