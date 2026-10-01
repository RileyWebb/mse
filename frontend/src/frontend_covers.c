#define DEBUG_LOG_SOURCE "frontend"

#include "frontend_covers.h"

// This translation unit owns stb_image's implementation; frontend_screenshot.c
// owns stb_image_write's. Keeping them apart avoids two copies of either.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "libmse/libmse.h"
#include "libmse/libmse_db.h"
#include "libmse/libmse_debug.h"

// A grid shows a few dozen covers at once, so this only evicts when someone
// scrolls a long way. Each slot is up to COVER_MAX_EDGE squared of RGBA, which
// for portrait box art works out around 750 KB.
#define COVER_SLOTS    64
#define COVER_MAX_EDGE 512

// Frames to leave a texture alone after asking for its destruction, so the
// renderer backend has seen the request before the ImTextureData is freed.
#define COVER_DESTROY_DELAY 4
#define COVER_PENDING_MAX   16

// A game with no artwork yet is remembered as a miss rather than re-queried
// every frame, but the scraper may fill it in at any moment, so the miss
// expires.
#define COVER_MISS_RETRY_SECONDS 5.0

typedef enum {
	SLOT_FREE = 0,
	SLOT_READY,
	SLOT_MISSING
} slot_state_t;

typedef struct {
	int64_t              game_id;
	ImTextureData       *texture;
	mse_frontend_cover_t cover;
	slot_state_t         state;
	int                  last_used;  // igGetFrameCount()
	double               retry_after; // SLOT_MISSING only
} cover_slot_t;

typedef struct {
	ImTextureData *texture;
	int            due_frame;
} cover_pending_t;

static cover_slot_t    g_slots[COVER_SLOTS];
static cover_pending_t g_pending[COVER_PENDING_MAX];
static int             g_pending_count = 0;

// --- texture lifetime -------------------------------------------------------

static void cover_reap_pending(void)
{
	const int frame = igGetFrameCount();

	for (int i = 0; i < g_pending_count;) {
		if (frame < g_pending[i].due_frame) {
			++i;
			continue;
		}

		igUnregisterUserTexture(g_pending[i].texture);
		ImTextureData_DestroyPixels(g_pending[i].texture);
		free(g_pending[i].texture);

		g_pending[i] = g_pending[--g_pending_count];
	}
}

static void cover_release_texture(ImTextureData *texture)
{
	if (texture == NULL) {
		return;
	}

	// The backend frees the GPU side when it sees this; the CPU side goes once
	// enough frames have passed that it certainly has.
	ImTextureData_SetStatus(texture, ImTextureStatus_WantDestroy);

	if (g_pending_count < COVER_PENDING_MAX) {
		g_pending[g_pending_count].texture   = texture;
		g_pending[g_pending_count].due_frame = igGetFrameCount() + COVER_DESTROY_DELAY;
		++g_pending_count;
	}
	// Past that the ImTextureData leaks rather than risking a free while the
	// renderer still holds it. It takes 16 evictions in 4 frames to get here.
}

static void cover_clear_slot(cover_slot_t *slot)
{
	cover_release_texture(slot->texture);
	memset(slot, 0, sizeof(*slot));
}

// --- decoding ---------------------------------------------------------------

// Decodes to RGBA and scales the long edge down to COVER_MAX_EDGE. Nothing
// draws these anywhere near full size, and a shelf of 700x1000 covers is a lot
// of VRAM for no visible gain.
static unsigned char *cover_decode(const void *blob, size_t size, int *out_w, int *out_h)
{
	int            width = 0, height = 0, channels = 0;
	unsigned char *pixels = stbi_load_from_memory((const stbi_uc *)blob, (int)size, &width, &height, &channels, 4);
	if (pixels == NULL || width <= 0 || height <= 0) {
		if (pixels != NULL) {
			stbi_image_free(pixels);
		}
		return NULL;
	}

	const int longest = width > height ? width : height;
	if (longest <= COVER_MAX_EDGE) {
		*out_w = width;
		*out_h = height;
		return pixels;
	}

	const float scale      = (float)COVER_MAX_EDGE / (float)longest;
	int         new_width  = (int)((float)width * scale);
	int         new_height = (int)((float)height * scale);
	if (new_width < 1) new_width = 1;
	if (new_height < 1) new_height = 1;

	unsigned char *scaled = (unsigned char *)malloc((size_t)new_width * (size_t)new_height * 4u);
	if (scaled == NULL) {
		*out_w = width;
		*out_h = height;
		return pixels;
	}

	stbir_resize_uint8_srgb(pixels, width, height, 0, scaled, new_width, new_height, 0, STBIR_RGBA);
	stbi_image_free(pixels);

	*out_w = new_width;
	*out_h = new_height;
	return scaled;
}

// Pulls the blob out of the library database. The scraper writes it from its
// own thread, hence the lock.
static unsigned char *cover_load_from_db(int64_t game_id, int *out_w, int *out_h)
{
	if (g_temp_db == NULL) {
		return NULL;
	}

	libmse_db_lock(g_temp_db);

	unsigned char *pixels = NULL;
	libmse_stmt_t *stmt =
		libmse_db_stmt_prepare(g_temp_db, "SELECT artwork_blob FROM games WHERE id = ?1 AND artwork_blob IS NOT NULL;");
	if (stmt != NULL) {
		libmse_db_bind_int64(stmt, 1, game_id);
		if (libmse_db_stmt_step(stmt) == 1) {
			const void  *blob = libmse_db_col_blob(stmt, 0);
			const size_t size = libmse_db_col_bytes(stmt, 0);
			if (blob != NULL && size > 0) {
				pixels = cover_decode(blob, size, out_w, out_h);
				if (pixels == NULL) {
					DEBUG_WARN("cover for game %lld could not be decoded", (long long)game_id);
				}
			}
		}
		libmse_db_stmt_finalize(stmt);
	}

	libmse_db_unlock(g_temp_db);
	return pixels;
}

// --- cache ------------------------------------------------------------------

static cover_slot_t *cover_find(int64_t game_id)
{
	for (int i = 0; i < COVER_SLOTS; ++i) {
		if (g_slots[i].state != SLOT_FREE && g_slots[i].game_id == game_id) {
			return &g_slots[i];
		}
	}
	return NULL;
}

static cover_slot_t *cover_claim_slot(void)
{
	cover_slot_t *oldest = NULL;

	for (int i = 0; i < COVER_SLOTS; ++i) {
		if (g_slots[i].state == SLOT_FREE) {
			return &g_slots[i];
		}
		if (oldest == NULL || g_slots[i].last_used < oldest->last_used) {
			oldest = &g_slots[i];
		}
	}

	cover_clear_slot(oldest);
	return oldest;
}

static bool cover_upload(cover_slot_t *slot, const unsigned char *pixels, int width, int height)
{
	ImTextureData *texture = ImTextureData_ImTextureData();
	if (texture == NULL) {
		return false;
	}

	ImTextureData_Create(texture, ImTextureFormat_RGBA32, width, height);

	void *destination = ImTextureData_GetPixels(texture);
	if (destination == NULL) {
		free(texture);
		return false;
	}
	memcpy(destination, pixels, (size_t)width * (size_t)height * 4u);

	igRegisterUserTexture(texture);

	slot->texture      = texture;
	slot->cover.ref    = ImTextureData_GetTexRef(texture);
	slot->cover.width  = width;
	slot->cover.height = height;
	return true;
}

const mse_frontend_cover_t *mse_frontend_cover_get(int64_t game_id)
{
	if (game_id < 0) {
		return NULL;
	}

	cover_reap_pending();

	const int    frame = igGetFrameCount();
	cover_slot_t *slot = cover_find(game_id);

	if (slot != NULL) {
		slot->last_used = frame;

		if (slot->state == SLOT_READY) {
			// A backend restart destroys textures out from under us. Rare, but
			// drawing through a dead handle is not worth the risk.
			if (slot->texture->Status == ImTextureStatus_Destroyed) {
				cover_clear_slot(slot);
			} else {
				return &slot->cover;
			}
		} else if (igGetTime() < slot->retry_after) {
			return NULL;
		} else {
			cover_clear_slot(slot);
		}
	}

	int            width = 0, height = 0;
	unsigned char *pixels = cover_load_from_db(game_id, &width, &height);

	slot            = cover_claim_slot();
	slot->game_id   = game_id;
	slot->last_used = frame;

	if (pixels == NULL) {
		slot->state       = SLOT_MISSING;
		slot->retry_after = igGetTime() + COVER_MISS_RETRY_SECONDS;
		return NULL;
	}

	const bool uploaded = cover_upload(slot, pixels, width, height);
	free(pixels);

	if (!uploaded) {
		slot->state       = SLOT_MISSING;
		slot->retry_after = igGetTime() + COVER_MISS_RETRY_SECONDS;
		return NULL;
	}

	slot->state = SLOT_READY;
	return &slot->cover;
}

// --- drawing ----------------------------------------------------------------

bool mse_frontend_cover_draw(ImDrawList *draw_list, ImVec2 min, ImVec2 max, int64_t game_id,
							 mse_frontend_cover_fit_t fit, float rounding, ImDrawFlags corners)
{
	const mse_frontend_cover_t *cover = mse_frontend_cover_get(game_id);
	if (cover == NULL || draw_list == NULL) {
		return false;
	}

	const float rect_w  = max.x - min.x;
	const float rect_h  = max.y - min.y;
	const float image_w = (float)cover->width;
	const float image_h = (float)cover->height;
	if (rect_w <= 0.0f || rect_h <= 0.0f || image_w <= 0.0f || image_h <= 0.0f) {
		return false;
	}

	ImVec2 uv0 = {0.0f, 0.0f};
	ImVec2 uv1 = {1.0f, 1.0f};
	ImVec2 p0  = min;
	ImVec2 p1  = max;

	if (fit == MSE_FRONTEND_COVER_FIT_COVER) {
		// Fill the rect and crop the overflow, centred. Box art in a grid reads
		// better cropped than letterboxed against the card.
		const float scale     = fmaxf(rect_w / image_w, rect_h / image_h);
		const float drawn_w   = image_w * scale;
		const float drawn_h   = image_h * scale;
		const float visible_u = rect_w / drawn_w;
		const float visible_v = rect_h / drawn_h;

		uv0 = (ImVec2){(1.0f - visible_u) * 0.5f, (1.0f - visible_v) * 0.5f};
		uv1 = (ImVec2){uv0.x + visible_u, uv0.y + visible_v};
	} else {
		const float scale   = fminf(rect_w / image_w, rect_h / image_h);
		const float drawn_w = image_w * scale;
		const float drawn_h = image_h * scale;

		p0 = (ImVec2){min.x + ((rect_w - drawn_w) * 0.5f), min.y + ((rect_h - drawn_h) * 0.5f)};
		p1 = (ImVec2){p0.x + drawn_w, p0.y + drawn_h};
	}

	if (rounding > 0.0f) {
		ImDrawList_AddImageRounded(draw_list, cover->ref, p0, p1, uv0, uv1, 0xFFFFFFFF, rounding, corners);
	} else {
		ImDrawList_AddImage(draw_list, cover->ref, p0, p1, uv0, uv1, 0xFFFFFFFF);
	}
	return true;
}

void mse_frontend_covers_shutdown(void)
{
	for (int i = 0; i < COVER_SLOTS; ++i) {
		if (g_slots[i].state != SLOT_FREE) {
			cover_clear_slot(&g_slots[i]);
		}
	}
	g_pending_count = 0;
}
