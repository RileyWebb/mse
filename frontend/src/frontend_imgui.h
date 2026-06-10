#ifndef MSE_FRONTEND_IMGUI_H
#define MSE_FRONTEND_IMGUI_H

#include <stdbool.h>

#include "frontend_cimgui.h"
#include "frontend_theme.h"

typedef struct mse_frontend_imgui_backend_s {
    SDL_Window *window;
    SDL_GPUDevice *device;
    SDL_GPUTextureFormat swapchain_format;
    float content_scale;
    bool viewports_supported;
    bool initialized;
} mse_frontend_imgui_backend_t;

typedef struct mse_frontend_imgui_config_s {
    SDL_Window *window;
    SDL_GPUDevice *device;
    SDL_GPUTextureFormat swapchain_format;
    SDL_GPUSampleCount msaa_samples;
    SDL_GPUSwapchainComposition swapchain_composition;
    SDL_GPUPresentMode present_mode;
    float content_scale;
} mse_frontend_imgui_config_t;

bool mse_frontend_imgui_initialize(mse_frontend_imgui_backend_t *backend, const mse_frontend_imgui_config_t *config);
void mse_frontend_imgui_shutdown(mse_frontend_imgui_backend_t *backend);
bool mse_frontend_imgui_viewports_supported(const mse_frontend_imgui_backend_t *backend);
ImFont *mse_frontend_imgui_font_small(void);
ImFont *mse_frontend_imgui_font_body(void);
ImFont *mse_frontend_imgui_font_title(void);
ImFont *mse_frontend_imgui_font_icon(void);
float mse_frontend_imgui_font_size_small(void);
float mse_frontend_imgui_font_size_body(void);
float mse_frontend_imgui_font_size_title(void);
float mse_frontend_imgui_font_size_icon(void);
void mse_frontend_imgui_process_event(const SDL_Event *event);
void mse_frontend_imgui_begin_frame(void);
void mse_frontend_imgui_prepare_draw_data(SDL_GPUCommandBuffer *command_buffer);
void mse_frontend_imgui_render_draw_data(SDL_GPUCommandBuffer *command_buffer, SDL_GPURenderPass *render_pass);

#endif // MSE_FRONTEND_IMGUI_H