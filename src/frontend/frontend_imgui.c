#include "frontend_imgui.h"

#include <string.h>

static ImFont *g_font_small = NULL;
static ImFont *g_font_body = NULL;
static ImFont *g_font_title = NULL;
static ImFont *g_font_icon = NULL;
static float g_font_size_small = 0.0f;
static float g_font_size_body = 0.0f;
static float g_font_size_title = 0.0f;
static float g_font_size_icon = 0.0f;

static const ImWchar g_icon_ranges[] = {
    0x2190, 0x21FF, // Arrows
    0x2300, 0x23FF, // Miscellaneous Technical
    0x25A0, 0x25FF, // Geometric Shapes
    0x2600, 0x26FF, // Miscellaneous Symbols
    0x2700, 0x27BF, // Dingbats
    0x2B00, 0x2BFF, // Miscellaneous Symbols and Arrows
    0xE000, 0xF8FF, // Private Use Area (covers EEE8, EFxx, etc.)
    0xF0000, 0xF0379, // Supplementary Private Use Area-A (covers F0379, F0279, etc.)
    0
};

static const ImWchar g_body_ranges[] = {
    0x0020, 0x00FF,
    0x2190, 0x21FF,
    0x2300, 0x23FF,
    0x25A0, 0x25FF,
    0x2600, 0x26FF,
    0x2700, 0x27BF,
    0x2B00, 0x2BFF,
    0xF000, 0xF3FF,
    0
};

static ImFont *mse_frontend_imgui_add_font(const char *font_path, float size_pixels, const ImWchar *glyph_ranges) {
    ImFontAtlas *atlas = igGetIO_Nil()->Fonts;
    return ImFontAtlas_AddFontFromFileTTF(atlas, font_path, size_pixels, NULL, glyph_ranges);
}

bool mse_frontend_imgui_initialize(mse_frontend_imgui_backend_t *backend, const mse_frontend_imgui_config_t *config) {
    if (backend == NULL || config == NULL || config->window == NULL || config->device == NULL) {
        return false;
    }

    backend->window = config->window;
    backend->device = config->device;
    backend->swapchain_format = config->swapchain_format;
    backend->content_scale = config->content_scale > 0.0f ? config->content_scale : 1.0f;
    backend->viewports_supported = false;
    backend->initialized = false;

    if (igCreateContext(NULL) == NULL) {
        return false;
    }

    ImGuiIO *io = igGetIO_Nil();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    const float font_scale = backend->content_scale;
    g_font_size_small = 13.0f * font_scale;
    g_font_size_body = 16.0f * font_scale;
    g_font_size_title = 22.0f * font_scale;
    g_font_size_icon = 24.0f * font_scale;

    g_font_body = mse_frontend_imgui_add_font("data/fonts/JetBrainsMono.ttf", g_font_size_body, g_body_ranges);
    g_font_small = mse_frontend_imgui_add_font("data/fonts/JetBrainsMono.ttf", g_font_size_small, NULL);
    g_font_title = mse_frontend_imgui_add_font("data/fonts/JetBrainsMono.ttf", g_font_size_title, NULL);
    g_font_icon = mse_frontend_imgui_add_font("data/fonts/JetBrainsMono.ttf", g_font_size_icon, g_icon_ranges);

    if (g_font_body == NULL || g_font_small == NULL || g_font_title == NULL || g_font_icon == NULL) {
        igDestroyContext(NULL);
        g_font_small = NULL;
        g_font_body = NULL;
        g_font_title = NULL;
        g_font_icon = NULL;
        g_font_size_small = 0.0f;
        g_font_size_body = 0.0f;
        g_font_size_title = 0.0f;
        g_font_size_icon = 0.0f;
        return false;
    }

    igStyleColorsDark(NULL);

    ImGuiStyle *style = igGetStyle();
    if (style != NULL && backend->content_scale != 1.0f) {
        style->WindowPadding.x *= backend->content_scale;
        style->WindowPadding.y *= backend->content_scale;
        style->FramePadding.x *= backend->content_scale;
        style->FramePadding.y *= backend->content_scale;
        style->ItemSpacing.x *= backend->content_scale;
        style->ItemSpacing.y *= backend->content_scale;
        style->ItemInnerSpacing.x *= backend->content_scale;
        style->ItemInnerSpacing.y *= backend->content_scale;
        style->IndentSpacing *= backend->content_scale;
        style->ScrollbarSize *= backend->content_scale;
        style->GrabMinSize *= backend->content_scale;
        style->WindowRounding *= backend->content_scale;
        style->ChildRounding *= backend->content_scale;
        style->FrameRounding *= backend->content_scale;
        style->PopupRounding *= backend->content_scale;
        style->ScrollbarRounding *= backend->content_scale;
        style->GrabRounding *= backend->content_scale;
        style->LogSliderDeadzone *= backend->content_scale;
        style->TabRounding *= backend->content_scale;
        style->TabBarBorderSize *= backend->content_scale;
    }

    if (!ImGui_ImplSDL3_InitForSDLGPU(config->window)) {
        igDestroyContext(NULL);
        return false;
    }

    ImGui_ImplSDLGPU3_InitInfo init_info = {0};
    init_info.Device = config->device;
    init_info.ColorTargetFormat = config->swapchain_format;
    init_info.MSAASamples = config->msaa_samples;
    init_info.SwapchainComposition = config->swapchain_composition;
    init_info.PresentMode = config->present_mode;

    if (!ImGui_ImplSDLGPU3_Init(&init_info)) {
        ImGui_ImplSDL3_Shutdown();
        igDestroyContext(NULL);
        return false;
    }

    ImGuiIO *io_after_init = igGetIO_Nil();
    const ImGuiBackendFlags backend_flags = io_after_init->BackendFlags;
    const bool renderer_supports_viewports = (backend_flags & ImGuiBackendFlags_RendererHasViewports) != 0;
    const bool platform_supports_viewports = (backend_flags & ImGuiBackendFlags_PlatformHasViewports) != 0;

    io_after_init->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    backend->viewports_supported = renderer_supports_viewports && platform_supports_viewports;
    if (backend->viewports_supported) {
        io_after_init->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }

    backend->initialized = true;
    return true;
}

void mse_frontend_imgui_shutdown(mse_frontend_imgui_backend_t *backend) {
    if (backend == NULL || !backend->initialized) {
        return;
    }

    ImGui_ImplSDL3_Shutdown();
    ImGui_ImplSDLGPU3_Shutdown();
    igDestroyContext(NULL);

    g_font_small = NULL;
    g_font_body = NULL;
    g_font_title = NULL;
    g_font_icon = NULL;
    g_font_size_small = 0.0f;
    g_font_size_body = 0.0f;
    g_font_size_title = 0.0f;
    g_font_size_icon = 0.0f;

    backend->initialized = false;
    backend->window = NULL;
    backend->device = NULL;
    backend->swapchain_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    backend->content_scale = 1.0f;
    backend->viewports_supported = false;
}

bool mse_frontend_imgui_viewports_supported(const mse_frontend_imgui_backend_t *backend) {
    return backend != NULL && backend->initialized && backend->viewports_supported;
}

ImFont *mse_frontend_imgui_font_small(void) {
    return g_font_small;
}

ImFont *mse_frontend_imgui_font_body(void) {
    return g_font_body;
}

ImFont *mse_frontend_imgui_font_title(void) {
    return g_font_title;
}

ImFont *mse_frontend_imgui_font_icon(void) {
    return g_font_icon;
}

float mse_frontend_imgui_font_size_small(void) {
    return g_font_size_small;
}

float mse_frontend_imgui_font_size_body(void) {
    return g_font_size_body;
}

float mse_frontend_imgui_font_size_title(void) {
    return g_font_size_title;
}

float mse_frontend_imgui_font_size_icon(void) {
    return g_font_size_icon;
}

void mse_frontend_imgui_process_event(const SDL_Event *event) {
    if (event != NULL) {
        ImGui_ImplSDL3_ProcessEvent(event);
    }
}

void mse_frontend_imgui_begin_frame(void) {
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    igNewFrame();
}

void mse_frontend_imgui_prepare_draw_data(SDL_GPUCommandBuffer *command_buffer) {
    if (command_buffer == NULL) {
        return;
    }

    ImDrawData *draw_data = igGetDrawData();
    if (draw_data != NULL) {
        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);
    }
}

void mse_frontend_imgui_render_draw_data(SDL_GPUCommandBuffer *command_buffer, SDL_GPURenderPass *render_pass) {
    ImDrawData *draw_data = igGetDrawData();
    ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass, NULL);
}