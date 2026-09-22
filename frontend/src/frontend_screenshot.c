#define DEBUG_LOG_SOURCE "frontend"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// This translation unit owns stb_image_write's implementation; nothing else in
// the frontend pulls it in.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "frontend_screenshot.h"

#include "libmse/libmse.h"
#include "libmse/libmse_cmd.h"
#include "libmse/libmse_debug.h"

static libmse_backend_t *g_screenshot_backend = NULL;

void mse_frontend_screenshot_set_backend(libmse_backend_t *backend)
{
	g_screenshot_backend = backend;
}

// stb wants tightly packed RGBA rows. A backend may publish BGRA and may pad
// its rows, so the frame is repacked rather than handed over as it sits.
//
// Alpha is forced opaque: cores store whatever happens to be in the top byte
// of their palette entries, and for several of them that is zero -- which
// writes a PNG that is entirely transparent and looks like a bug in the
// emulator rather than in the screenshot.
static uint8_t *screenshot_repack_rgba(const mse_frame_t *frame)
{
	const size_t pixels = (size_t)frame->width * (size_t)frame->height;
	uint8_t     *out    = (uint8_t *)malloc(pixels * 4);
	if (out == NULL) {
		return NULL;
	}

	const bool swizzle = (frame->format == MSE_FRAME_FORMAT_BGRA8);

	for (uint32_t y = 0; y < frame->height; ++y) {
		const uint8_t *source = frame->pixels + (size_t)y * frame->pitch;
		uint8_t       *dest   = out + (size_t)y * frame->width * 4;

		for (uint32_t x = 0; x < frame->width; ++x) {
			const uint8_t *pixel = source + (size_t)x * 4;
			uint8_t       *slot  = dest + (size_t)x * 4;

			slot[0] = swizzle ? pixel[2] : pixel[0];
			slot[1] = pixel[1];
			slot[2] = swizzle ? pixel[0] : pixel[2];
			slot[3] = 0xFF;
		}
	}

	return out;
}

static bool cmd_screenshot_handler(int argc, const char **argv)
{
	if (g_screenshot_backend == NULL) {
		libmse_log("mse: no backend is running");
		return false;
	}

	mse_frame_t frame;
	if (!mse_backend_get_frame(g_screenshot_backend, &frame) || frame.pixels == NULL ||
	    frame.width == 0 || frame.height == 0) {
		libmse_log("mse: the backend has not produced a frame yet");
		return false;
	}

	// `ready` is deliberately ignored: it means "newer than the one you last
	// took", and a paused emulator never produces a new frame -- which is
	// exactly when you most want a screenshot.
	char generated[64];
	const char *path = (argc > 0) ? argv[0] : generated;

	if (argc == 0) {
		const time_t now = time(NULL);
		struct tm   *local = localtime(&now);
		if (local == NULL || strftime(generated, sizeof(generated),
		                              "screenshot_%Y%m%d_%H%M%S.png", local) == 0) {
			snprintf(generated, sizeof(generated), "screenshot.png");
		}
	}

	uint8_t *rgba = screenshot_repack_rgba(&frame);
	if (rgba == NULL) {
		DEBUG_ERROR("mse: could not allocate %ux%u screenshot buffer", frame.width, frame.height);
		return false;
	}

	const bool ok = stbi_write_png(path, (int)frame.width, (int)frame.height, 4, rgba,
	                               (int)frame.width * 4) != 0;
	free(rgba);

	if (ok) {
		libmse_logf("mse: wrote %ux%u screenshot to %s", frame.width, frame.height, path);
	} else {
		DEBUG_ERROR("mse: could not write screenshot to %s", path);
	}
	return ok;
}

void mse_frontend_screenshot_init(void)
{
	libmse_cmd_register(&(libmse_cmd_t){
		"mse_screenshot", "Writes the current frame to a PNG (default: a timestamped name)",
		0, cmd_screenshot_handler});
}
