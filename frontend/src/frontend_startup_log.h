#ifndef MSE_FRONTEND_STARTUP_LOG_H
#define MSE_FRONTEND_STARTUP_LOG_H

#include <stddef.h>

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

typedef struct mse_backend_s libmse_backend_t;

typedef struct mse_frontend_startup_info_s {
	SDL_Window           *window;
	SDL_GPUDevice        *device;
	SDL_GPUTextureFormat  swapchain_format;
	SDL_GPUPresentMode    present_mode;
	float                 content_scale;

	libmse_backend_t    **backends;
	size_t                backend_count;
} mse_frontend_startup_info_t;

// Writes the startup banner: build, host, graphics and loaded backends.
void mse_frontend_log_startup(const mse_frontend_startup_info_t *info);

#endif // MSE_FRONTEND_STARTUP_LOG_H
