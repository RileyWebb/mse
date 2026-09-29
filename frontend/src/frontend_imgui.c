#include "frontend_imgui.h"

#include <string.h>

#include "libmse/libmse_cvar.h"
#include "libmse/libmse_debug.h"
#include "libmse/libmse_resource.h"

static void mse_cvar_font_cb(libmse_cvar_t *cvar, void *user_data);

static ImFont *g_font_small = NULL;
// The typeface, as a setting. Changing it rebuilds the atlas rather than
// needing a restart: the renderer owns the texture, and ImGui 1.92 will ask it
// for a new one as soon as the atlas is marked dirty.
LIBMSE_CVAR_DEFINE_STRING(g_cv_font, "mse_font", "data/fonts/JetBrainsMono.ttf",
                          "TrueType file the interface is drawn with");

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

// Builds the four faces from whatever mse_font currently names. Returns false
// and leaves the old atlas alone if the file cannot be read, so a typo in the
// cvar costs an error line rather than an unreadable interface.
static bool mse_frontend_imgui_build_fonts(void);

static ImFont *mse_frontend_imgui_add_font(const char *font_path, float size_pixels, const ImWchar *glyph_ranges) {
    ImFontAtlas *atlas = igGetIO_Nil()->Fonts;
    //ImFontAtlas_SetTexDesiredWidth(atlas, 2048); // Optional: larger texture space
    // Enable FreeType hinting flags for extra crispness
    //unsigned int flags = ; 
    //ImFontAtlas_SetBuilderFlags(atlas, flags);
    return ImFontAtlas_AddFontFromFileTTF(atlas, font_path, size_pixels, NULL, glyph_ranges);
}

static bool mse_frontend_imgui_build_fonts(void) {
    ImGuiIO *io = igGetIO_Nil();
    if (io == NULL || io->Fonts == NULL) {
        return false;
    }

    const char *path = (g_cv_font != NULL && *g_cv_font != NULL && **g_cv_font != '\0')
                           ? *g_cv_font
                           : "data/fonts/JetBrainsMono.ttf";

    // Everything, not just the changed face: the atlas is one texture and the
    // four sizes share it, so they are rebuilt or none of them are.
    //
    // ClearFonts, not Clear: Clear also throws away the atlas's texture and the
    // renderer's link to it, which on the first build -- before the backend has
    // attached -- leaves a window that draws nothing at all.
    ImFontAtlas_ClearFonts(io->Fonts);

    ImFont *body  = mse_frontend_imgui_add_font(path, g_font_size_body, g_body_ranges);
    ImFont *small = mse_frontend_imgui_add_font(path, g_font_size_small, NULL);
    ImFont *title = mse_frontend_imgui_add_font(path, g_font_size_title, NULL);
    ImFont *icon  = mse_frontend_imgui_add_font(path, g_font_size_icon, g_icon_ranges);

    if (body == NULL || small == NULL || title == NULL || icon == NULL) {
        DEBUG_ERROR("Could not load font '%s'", path);
        return false;
    }

    g_font_body  = body;
    g_font_small = small;
    g_font_title = title;
    g_font_icon  = icon;
    return true;
}

static void mse_cvar_font_cb(libmse_cvar_t *cvar, void *user_data) {
    (void)cvar;
    (void)user_data;

    // A failed rebuild has already cleared the atlas, so fall back to the
    // stock face rather than leaving the interface with no glyphs at all.
    if (!mse_frontend_imgui_build_fonts()) {
        libmse_cvar_set_s("mse_font", "data/fonts/JetBrainsMono.ttf");
        mse_frontend_imgui_build_fonts();
    }
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

    // Window layout goes next to the rest of the user's state rather than into
    // the install directory, which may not be writable and is not theirs. The
    // buffer is static because ImGui keeps the pointer rather than a copy.
    {
        static char ini_path[512];
        const char *appdata = libmse_resource_get_appdata_path();
        if (appdata != NULL) {
            snprintf(ini_path, sizeof(ini_path), "%s/imgui.ini", appdata);
            io->IniFilename = ini_path;
        }
    }

    const float font_scale = backend->content_scale;
    g_font_size_small = 13.0f * font_scale;
    g_font_size_body = 16.0f * font_scale;
    g_font_size_title = 22.0f * font_scale;
    g_font_size_icon = 24.0f * font_scale;

    if (!mse_frontend_imgui_build_fonts()) {
        igDestroyContext(NULL);
        return false;
    }

    // Reloads on change, from the console or a config, with no restart.
    libmse_cvar_register_change_cb("mse_font", mse_cvar_font_cb, NULL);

    // Style metrics are owned by the theme, which scales them itself. Only the
    // scale is set here: applying a theme would have to name one, and which
    // theme this session uses is not known until the UI state is up. Naming
    // Midnight here made the first real apply look like a theme change, which
    // wrote the palette over every colour a config had just set.
    mse_frontend_theme_set_scale(backend->content_scale);

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