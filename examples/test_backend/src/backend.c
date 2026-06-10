#include "libmse/libmse.h"

#include <string.h>

LIBMSE_API mse_backend_info_t info = {
    .name = "Test Backend",
    .version = "0.1.0",
    .author = "Riley Webb",
    .description = "A test backend for libmse.",
    .licence = "N/A",
    .repository = "https://github.com/RileyWebb/mse",

    .build_date = __DATE__,
    .build_time = __TIME__
};

LIBMSE_API bool request_frame(mse_frame_t *frame) {
    static uint32_t frame_index = 0;

    if (frame == NULL || frame->pixels == NULL || frame->format != MSE_FRAME_FORMAT_RGBA8) {
        return false;
    }

    if (frame->width == 0U || frame->height == 0U || frame->pitch < frame->width * 4U) {
        return false;
    }

    if (frame->pixels_size < ((size_t)frame->pitch * (size_t)frame->height)) {
        return false;
    }

    for (uint32_t y = 0; y < frame->height; ++y) {
        uint8_t *row = frame->pixels + ((size_t)y * frame->pitch);
        for (uint32_t x = 0; x < frame->width; ++x) {
            const uint8_t checker = (uint8_t)(((x / 16U) + (y / 16U) + (frame_index / 8U)) & 1U);
            const uint8_t wave = (uint8_t)((x * 255U) / (frame->width > 1U ? frame->width - 1U : 1U));
            const uint8_t motion = (uint8_t)((y * 255U) / (frame->height > 1U ? frame->height - 1U : 1U));
            const uint8_t tint = checker ? 220U : 40U;
            const size_t pixel_index = (size_t)x * 4U;

            row[pixel_index + 0U] = (uint8_t)(wave ^ tint);
            row[pixel_index + 1U] = (uint8_t)(motion ^ (uint8_t)(frame_index * 3U));
            row[pixel_index + 2U] = (uint8_t)(tint + (uint8_t)(frame_index * 5U));
            row[pixel_index + 3U] = 255U;
        }
    }

    ++frame_index;
    return true;
}