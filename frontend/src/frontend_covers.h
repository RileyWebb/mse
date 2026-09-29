#ifndef MSE_FRONTEND_COVERS_H
#define MSE_FRONTEND_COVERS_H

#include <stdbool.h>
#include <stdint.h>

#include "frontend_cimgui.h"

// Cover art from the library database, decoded and handed to ImGui as
// textures.
//
// The blobs live in games.artwork_blob; the scraper puts them there. Decoding
// is lazy and the cache is bounded, because a library can hold thousands of
// titles and a decoded cover is most of a megabyte of VRAM.

typedef struct mse_frontend_cover_s {
	ImTextureRef ref;
	int          width;
	int          height;
} mse_frontend_cover_t;

typedef enum mse_frontend_cover_fit_e {
	MSE_FRONTEND_COVER_FIT_COVER = 0, // fill the rect, cropping the overflow
	MSE_FRONTEND_COVER_FIT_CONTAIN    // fit inside the rect, letterboxed
} mse_frontend_cover_fit_t;

// The cover for `game_id`, or NULL when the game has none. Decodes on first
// use; safe and cheap to call every frame.
const mse_frontend_cover_t *mse_frontend_cover_get(int64_t game_id);

// Draws a cover into `min`..`max`. Returns false when the game has no cover,
// so the caller can fall back to a placeholder.
bool mse_frontend_cover_draw(ImDrawList *draw_list, ImVec2 min, ImVec2 max, int64_t game_id,
							 mse_frontend_cover_fit_t fit, float rounding, ImDrawFlags corners);

void mse_frontend_covers_shutdown(void);

#endif // MSE_FRONTEND_COVERS_H
