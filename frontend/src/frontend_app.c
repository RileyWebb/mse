#define DEBUG_LOG_SOURCE "frontend"
#include "frontend_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libmse/libmse.h"
#include "libmse/libmse_gfx_internal.h"
#include "libmse/libmse_debug.h"
#include "frontend_cimgui.h"
#include "frontend_imgui.h"
#include "frontend_theme.h"
#include "frontend_ui.h"
#include "frontend_input.h"

typedef struct mse_frontend_backend_preview_s {
    mse_backend_t *backend;
    SDL_Thread *thread;
    mse_event_t *stop_event;
    SDL_GPUTextureSamplerBinding texture_binding;
    SDL_Window *window;
    
    // --- DIRECT DRAW MODIFICATION ---
    // Track the reserved screen-space coordinates for the direct SDL_GPU draw pass
    bool has_valid_bounds;
    float draw_x;
    float draw_y;
    float draw_w;
    float draw_h;
} mse_frontend_backend_preview_t;

static const uint32_t MSE_BACKEND_PREVIEW_WIDTH = 256;
static const uint32_t MSE_BACKEND_PREVIEW_HEIGHT = 240;

typedef enum mse_frontend_view_mode_e {
    MSE_FRONTEND_VIEW_MENU = 0,
    MSE_FRONTEND_VIEW_TRANSITION_TO_CORE,
    MSE_FRONTEND_VIEW_CORE,
    MSE_FRONTEND_VIEW_TRANSITION_TO_MENU
} mse_frontend_view_mode_t;

struct mse_gfx_pipeline_s {
    SDL_GPUGraphicsPipeline *pipeline;
};

mse_frontend_context_t g_app_ctx = {0};

void mse_frontend_quit() 
{
    g_app_ctx.is_running = false;
}

static float mse_frontend_clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

// --- SDL3 Settings Cache ---
char g_video_resolutions_buf[64][64];
const char* g_video_resolutions[64];
int g_video_resolution_count;

char g_gpu_drivers_buf[16][64];
const char* g_gpu_drivers[16];
int g_gpu_driver_count;

SDL_DisplayID g_last_display_id = 0;
bool g_gpu_drivers_cached = false;
bool g_gpu_devices_cached = false;
bool g_running = false;

static void mse_frontend_ui_cache_sdl_settings(SDL_Window *window)
{
    // 2. Cache Graphics Drivers (Only needs to happen once at startup)
    if (!g_gpu_drivers_cached) {
        strcpy(g_gpu_drivers_buf[0], "Auto-Select (Default)");
        g_gpu_drivers[0] = g_gpu_drivers_buf[0];
        g_gpu_driver_count = 1;

        int num_drivers = SDL_GetNumGPUDrivers();
        for (int i = 0; i < num_drivers && g_gpu_driver_count < 16; i++) {
            snprintf(g_gpu_drivers_buf[g_gpu_driver_count], 64, "%s", SDL_GetGPUDriver(i));
            g_gpu_drivers[g_gpu_driver_count] = g_gpu_drivers_buf[g_gpu_driver_count];
            g_gpu_driver_count++;
        }
        g_gpu_drivers_cached = true;
    }

    // 3. Cache Resolutions for the current display (Updates if window moves to a new monitor)
    if (window != NULL) {
        SDL_DisplayID current_display = SDL_GetDisplayForWindow(window);
        
        if (current_display != g_last_display_id) {
            g_video_resolutions_buf[0][0] = '\0';
            strcpy(g_video_resolutions_buf[0], "Auto (Windowed)");
            g_video_resolutions[0] = g_video_resolutions_buf[0];
            g_video_resolution_count = 1;

            int mode_count = 0;
            SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(current_display, &mode_count);
            if (modes != NULL) {
                for (int i = 0; i < mode_count && g_video_resolution_count < 64; i++) {
                    snprintf(g_video_resolutions_buf[g_video_resolution_count], 64, 
                             "%d x %d (%.0f Hz)", modes[i]->w, modes[i]->h, modes[i]->refresh_rate);
                    g_video_resolutions[g_video_resolution_count] = g_video_resolutions_buf[g_video_resolution_count];
                    g_video_resolution_count++;
                }
                SDL_free(modes);
            }
            g_last_display_id = current_display;
        }
    }
}

// Helper function to read a binary file into memory using standard C I/O
static uint8_t* load_shader_file_from_disk(const char *filepath, size_t *out_size) {
    // Open in binary mode ("rb") is critical for SPIR-V bytecode on Windows platforms
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        fprintf(stderr, "Gfx Error: Failed to open shader file: %s\n", filepath);
        if (out_size) *out_size = 0;
        return NULL;
    }

    // Seek to end to calculate exact file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    if (size <= 0) {
        fprintf(stderr, "Gfx Error: Shader file is empty or invalid: %s\n", filepath);
        fclose(file);
        if (out_size) *out_size = 0;
        return NULL;
    }
    rewind(file);

    // Allocate buffer for the bytecode
    uint8_t *buffer = (uint8_t*)malloc(size);
    if (!buffer) {
        fprintf(stderr, "Gfx Error: Memory allocation failed for shader: %s\n", filepath);
        fclose(file);
        if (out_size) *out_size = 0;
        return NULL;
    }

    // Read bytes into buffer
    size_t bytes_read = fread(buffer, 1, size, file);
    fclose(file);

    if (bytes_read != (size_t)size) {
        fprintf(stderr, "Gfx Error: Could not read entire file content: %s\n", filepath);
        free(buffer);
        if (out_size) *out_size = 0;
        return NULL;
    }

    if (out_size) *out_size = (size_t)size;
    return buffer;
}

typedef struct {
    mse_gfx_shader_t *vert_shader;
    mse_gfx_shader_t *frag_shader;
    mse_gfx_pipeline_t *pipeline;
} RenderPipelineContext;

RenderPipelineContext setup_graphics_pipeline_from_disk(const char *base_shader_dir, mse_gfx_texture_format_t target_format) {
    RenderPipelineContext ctx = {0};
    char vert_path[512];
    char frag_path[512];

    // Construct full paths matching your CMake output layout
    snprintf(vert_path, sizeof(vert_path), "%s/passthrough.vert.spv", base_shader_dir);
    snprintf(frag_path, sizeof(frag_path), "%s/passthrough.frag.spv", base_shader_dir);

    size_t vert_size = 0;
    size_t frag_size = 0;

    // 1. Read binary files from disk
    uint8_t *vert_code = load_shader_file_from_disk(vert_path, &vert_size);
    uint8_t *frag_code = load_shader_file_from_disk(frag_path, &frag_size);

    if (!vert_code || !frag_code) {
        if (vert_code) free(vert_code);
        if (frag_code) free(frag_code);
        return ctx;
    }

    // 2. Feed the loaded standard buffers into your existing driver layout
    ctx.vert_shader = mse_gfx_create_shader(vert_code, vert_size, true);
    ctx.frag_shader = mse_gfx_create_shader(frag_code, frag_size, false);

    // Free buffers now that the graphics driver has instantiated the modules
    free(vert_code);
    free(frag_code);

    if (!ctx.vert_shader || !ctx.frag_shader) {
        if (ctx.vert_shader) mse_gfx_destroy_shader(ctx.vert_shader);
        if (ctx.frag_shader) mse_gfx_destroy_shader(ctx.frag_shader);
        memset(&ctx, 0, sizeof(ctx));
        return ctx;
    }

    // 3. Complete pipeline creation
    ctx.pipeline = mse_gfx_create_pipeline(ctx.vert_shader, ctx.frag_shader, target_format);

    if (!ctx.pipeline) {
        fprintf(stderr, "Gfx Error: Failed to compile pipeline from disk shaders.\n");
        mse_gfx_destroy_shader(ctx.vert_shader);
        mse_gfx_destroy_shader(ctx.frag_shader);
        memset(&ctx, 0, sizeof(ctx));
    }

    return ctx;
}

static void mse_frontend_set_window_fullscreen(SDL_Window *window, bool fullscreen) {
    if (window != NULL) {
        SDL_SetWindowFullscreen(window, fullscreen);
    }
}

static ImTextureRef_c mse_frontend_make_texture_ref(const SDL_GPUTextureSamplerBinding *binding) {
    ImTextureRef_c texture_ref;

    texture_ref._TexData = NULL;
    texture_ref._TexID = (ImTextureID)binding->texture;
    return texture_ref;
}

static void mse_frontend_backend_preview_shutdown(mse_frontend_backend_preview_t *preview) {
    if (preview == NULL) {
        return;
    }

    if (preview->thread != NULL) {
        if (preview->stop_event != NULL) {
            mse_event_set(preview->stop_event);
        }
        SDL_WaitThread(preview->thread, NULL);
        preview->thread = NULL;
    }

    if (preview->stop_event != NULL) {
        mse_event_destroy(preview->stop_event);
        preview->stop_event = NULL;
    }

    preview->backend = NULL;
    preview->texture_binding.texture = NULL;
    preview->texture_binding.sampler = NULL;
}

static int mse_frontend_backend_thread_func(void *data) {
    mse_frontend_backend_preview_t *preview = (mse_frontend_backend_preview_t *)data;
    if (preview && preview->backend && preview->backend->start) {
        preview->backend->start(preview->stop_event);
    }
    return 0;
}

static bool mse_frontend_backend_preview_init(mse_frontend_backend_preview_t *preview, mse_backend_t *backend) {
    if (preview == NULL) {
        return false;
    }

    memset(preview, 0, sizeof(*preview));
    preview->backend = backend;
    if (backend != NULL && (backend->capabilities & MSE_BACKEND_CAPS_THREADED)) {
        preview->stop_event = mse_event_create();
    }
    return true;
}

// --- Custom Callback Rendering State ---
typedef struct {
    SDL_GPUTextureSamplerBinding binding;
    ImVec2 rect_min;     // Top-left bounds of the preview image
    ImVec2 rect_max;     // Bottom-right bounds
    ImVec2 display_pos;  // The OS-level desktop coordinate of the current viewport
    ImVec2 fb_scale;     // High-DPI scaling factor for the current viewport
} mse_preview_callback_data_t;

static mse_preview_callback_data_t g_preview_cb_data;

// Set this right before calling mse_frontend_imgui_render_draw_data()
// and reset it to NULL immediately after.
static SDL_GPURenderPass *g_current_render_pass = NULL;

static void mse_frontend_backend_preview_contents(const mse_frontend_backend_preview_t *preview, float ui_scale) {
    if (preview == NULL) {
        return;
    }

    (void)ui_scale;

    if (preview->texture_binding.texture != NULL) {
        uint32_t tex_w = 256;
        uint32_t tex_h = 240;

        const ImVec2 avail = igGetContentRegionAvail();
        const float aspect_ratio = tex_h > 0 ? ((float)tex_w / (float)tex_h) : 1.0f;
        ImVec2 image_size = avail;
        
        if (avail.x > 0.0f && avail.y > 0.0f) {
            if ((avail.x / aspect_ratio) > avail.y) {
                image_size.x = avail.y * aspect_ratio;
            } else {
                image_size.y = avail.x / aspect_ratio;
            }

            const float off_x = (avail.x - image_size.x) * 0.5f;
            const float off_y = (avail.y - image_size.y) * 0.5f;
            igSetCursorPos((ImVec2){igGetCursorPosX() + off_x, igGetCursorPosY() + off_y});
        }

        ImGuiViewport *current_vp = igGetWindowViewport(); // Multi-Viewport Docking Support
        ImGuiIO *io = igGetIO_Nil();

        ImVec2 cursor_screen_pos = igGetCursorScreenPos();
        ImVec2 image_max = {cursor_screen_pos.x + image_size.x, cursor_screen_pos.y + image_size.y};
        
        igDummy((ImVec2){image_size.x, image_size.y});

        ImDrawList *draw_list = igGetWindowDrawList();

        ImDrawList_AddCallback(draw_list, igGetPlatformIO_Nil()->DrawCallback_SetSamplerNearest, NULL, 0);

        ImDrawList_AddImage(draw_list, mse_frontend_make_texture_ref(&preview->texture_binding), 
                cursor_screen_pos, image_max, (ImVec2){0.0f, 0.0f}, (ImVec2){1.0f, 1.0f}, 0xFFFFFFFF);

        ImDrawList_AddCallback(draw_list, igGetPlatformIO_Nil()->DrawCallback_SetSamplerLinear, NULL, 0);
    } else {
        igTextDisabled("No backend frame available.");
    }
}

// --- DIRECT DRAW MODIFICATION ---
// Removed `const` from signature to match contents function
static void mse_frontend_backend_emulation_draw(mse_frontend_backend_preview_t *preview, float ui_scale, bool fill_viewport) {
    ImGuiWindowFlags flags = 0;

    if (fill_viewport) {
        ImGuiViewport *viewport = igGetMainViewport();
        if (viewport != NULL) {
            igSetNextWindowPos(viewport->WorkPos, ImGuiCond_Always, (ImVec2){0.0f, 0.0f});
            igSetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
            igSetNextWindowViewport(viewport->ID);
        }

        flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus;
    }
    //igPushStyleColor_Vec4(ImGuiCol_WindowBg, (ImVec4){0.0f, 0.0f, 0.0f, 0.0f});
    if (igBegin(fill_viewport ? "EMULATION_FULLSCREEN" : "EMULATION_DOCK", NULL, flags)) {
        mse_frontend_backend_preview_contents(preview, ui_scale);
    }
    
    igEnd();
}

static SDL_Window *mse_frontend_create_window(const mse_frontend_app_config_t *config, float *content_scale_out) {
    float content_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (content_scale <= 0.0f) {
        content_scale = 1.0f;
    }

    if (content_scale_out != NULL) {
        *content_scale_out = content_scale;
    }

    SDL_WindowFlags flags = SDL_WINDOW_HIDDEN;
    if (config != NULL && config->resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (config != NULL && config->high_pixel_density) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }

    const char *title = (config != NULL && config->title != NULL) ? config->title : "Multi-System Emulator";
    const int width = (config != NULL && config->width > 0) ? config->width : 1280;
    const int height = (config != NULL && config->height > 0) ? config->height : 720;
    const int scaled_width = (int)((float)width * content_scale);
    const int scaled_height = (int)((float)height * content_scale);

    SDL_Window *window = SDL_CreateWindow(title, scaled_width, scaled_height, flags);
    if (window != NULL) {
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }

    return window;
}

static SDL_GPUDevice *mse_frontend_create_gpu_device(void) {
    return SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV, //| SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_METALLIB,
#ifdef DEBUG
        true,
#else
        false,
#endif
        NULL);
}

static SDL_GPUPresentMode mse_frontend_choose_present_mode(SDL_GPUDevice *device, SDL_Window *window) {
    if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_MAILBOX)) {
        return SDL_GPU_PRESENTMODE_MAILBOX;
    }

    return SDL_GPU_PRESENTMODE_VSYNC;
}

static SDL_GPUPresentMode mse_frontend_choose_viewport_present_mode(SDL_GPUDevice *device, SDL_Window *window, SDL_GPUPresentMode fallback_present_mode) {
    if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
        return SDL_GPU_PRESENTMODE_IMMEDIATE;
    }

    return fallback_present_mode;
}

static bool mse_frontend_claim_swapchain(SDL_GPUDevice *device, SDL_Window *window) {
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        return false;
    }

    return true;
}

static bool mse_frontend_set_swapchain_present_mode(SDL_GPUDevice *device, SDL_Window *window, SDL_GPUPresentMode present_mode) {
    if (!SDL_SetGPUSwapchainParameters(device, window, g_app_ctx.swapchain_composition, present_mode)) {
        if (present_mode != SDL_GPU_PRESENTMODE_VSYNC) {
            if (!SDL_SetGPUSwapchainParameters(device, window, g_app_ctx.swapchain_composition, SDL_GPU_PRESENTMODE_VSYNC)) {
                return false;
            }
        } else {
            return false;
        }
    }

    return true;
}

FILE *mse_register_log_file() 
{
    FILE *f_log = NULL; // TODO: Add timestamp-based log file naming and rotation and change log location to something like %APPDATA%/mse/logs on Windows and ~/.local/share/mse/logs on Linux

	if (!f_log) {
		f_log = fopen("log.txt", "w");
	}

	return f_log;
}

int mse_frontend_run(const mse_frontend_app_config_t *config) {
    FILE *log_file = mse_register_log_file();
    libmse_debug_register_buffer(log_file);

    const char *config_file = "config.cfg";
    if (!libmse_cmd_execute("exec", 1, &config_file)) {
        DEBUG_ERROR("Failed to parse config.cfg");
    }

    SDL_SetHint("SDL_IME_SHOW_UI", "1");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC)) {
        DEBUG_ERROR("Failed to initialize SDL: %s", SDL_GetError());
        return 1;
    }

    float content_scale = 1.0f;
    SDL_Window *window = mse_frontend_create_window(config, &content_scale);
    if (window == NULL) {
        DEBUG_ERROR("Failed to create window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_ShowWindow(window);

    SDL_GPUDevice *device = mse_frontend_create_gpu_device();
    if (device == NULL) {
        DEBUG_ERROR("Failed to create GPU device: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    g_app_ctx.window = window;
    g_app_ctx.gpu_device = device;
    g_app_ctx.content_scale = content_scale;
    g_app_ctx.is_running = true;
    g_app_ctx.swapchain_composition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;

    mse_gfx_init(device);

    mse_frontend_ui_cache_sdl_settings(window);
    mse_frontend_terminal_init();

    if (!SDL_SetGPUAllowedFramesInFlight(device, 3)) {
        DEBUG_ERROR("Failed to raise GPU frames in flight: %s", SDL_GetError());
    }

    if (!mse_frontend_claim_swapchain(device, window)) {
        DEBUG_ERROR("Failed to claim window for GPU device: %s", SDL_GetError());
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    g_app_ctx.presentation_mode = mse_frontend_choose_present_mode(device, window);
    if (!mse_frontend_set_swapchain_present_mode(device, window, g_app_ctx.presentation_mode)) {
        DEBUG_ERROR("Failed to set swapchain parameters: %s", SDL_GetError());
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GPUTextureFormat swapchain_format = SDL_GetGPUSwapchainTextureFormat(device, window);

    mse_frontend_imgui_backend_t imgui_backend;
    mse_frontend_imgui_config_t imgui_config;
    imgui_config.window = window;
    imgui_config.device = device;
    imgui_config.swapchain_format = swapchain_format;
    imgui_config.msaa_samples = SDL_GPU_SAMPLECOUNT_1;
    imgui_config.swapchain_composition = g_app_ctx.swapchain_composition;
    imgui_config.present_mode = g_app_ctx.presentation_mode;
    imgui_config.content_scale = content_scale;

    if (!mse_frontend_imgui_initialize(&imgui_backend, &imgui_config)) {
        DEBUG_ERROR("Failed to initialize ImGui: %s", SDL_GetError());
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    mse_backend_t *cnes_backend = mse_backend_register_folder("cnes");

    /* Initialise the cNES backend lifecycle */
    if (cnes_backend != NULL) {
        mse_backend_init(cnes_backend);
    }

    mse_frontend_backend_preview_t backend_preview;
    if (!mse_frontend_backend_preview_init(&backend_preview, cnes_backend)) {
        memset(&backend_preview, 0, sizeof(backend_preview));
    } else if (cnes_backend != NULL && (cnes_backend->capabilities & MSE_BACKEND_CAPS_THREADED)) {
        backend_preview.thread = SDL_CreateThread(mse_frontend_backend_thread_func, "BackendThread", &backend_preview);
    }

    mse_frontend_input_manager_t *input_manager = mse_frontend_input_manager_create();
    mse_frontend_input_manager_set_backend(input_manager, cnes_backend);
    mse_frontend_input_thread_start(input_manager);

    /* Build the backends list for the UI (pointer array, owned by libmse) */
    static mse_backend_t *g_backend_list[1];
    size_t backend_count = 0;
    if (cnes_backend != NULL) {
        g_backend_list[backend_count++] = cnes_backend;
    }

    const bool render_viewports = mse_frontend_imgui_viewports_supported(&imgui_backend);

    mse_frontend_view_mode_t view_mode = MSE_FRONTEND_VIEW_MENU;
    float transition_t = 0.0f;
    const float transition_duration = 0.25f;
    Uint64 last_frame_ticks = SDL_GetTicksNS();

    mse_frontend_ui_state_t ui_state;
    mse_frontend_ui_init(&ui_state);
    ui_state.content_scale       = content_scale;
    ui_state.active_backend_name = (cnes_backend != NULL && cnes_backend->info.name != NULL)
                                        ? cnes_backend->info.name : NULL;
    ui_state.input_manager  = input_manager;
    ui_state.window         = window;
    ui_state.backends       = g_backend_list;
    ui_state.backend_count  = backend_count;
    ui_state.selected_core_index = backend_count > 0 ? 0 : -1;
    DEBUG_INFO("Frontend initialized");

    g_app_ctx.is_running = true;
    while (g_app_ctx.is_running) {
        Uint64 now_ticks = SDL_GetTicksNS();
        float delta_seconds = (now_ticks >= last_frame_ticks) ? ((float)(now_ticks - last_frame_ticks) / 1000000000.0f) : 0.0f;
        if (delta_seconds > 0.1f) {
            delta_seconds = 0.1f;
        }
        last_frame_ticks = now_ticks;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            mse_frontend_imgui_process_event(&event);

            if (mse_frontend_ui_handle_event(&ui_state, &event)) {
                continue;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                if (view_mode == MSE_FRONTEND_VIEW_CORE || view_mode == MSE_FRONTEND_VIEW_TRANSITION_TO_CORE) {
                    view_mode = MSE_FRONTEND_VIEW_TRANSITION_TO_MENU;
                    transition_t = 0.0f;
                }
                continue;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_F10 || event.key.key == SDLK_GRAVE)) {
                ui_state.show_terminal = !ui_state.show_terminal;
                continue;
            }

            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11) {
                ui_state.fullscreen = !ui_state.fullscreen;
                mse_frontend_set_window_fullscreen(window, ui_state.fullscreen);
                continue;
            }

            if (event.type == SDL_EVENT_QUIT) {
                g_app_ctx.is_running = false;
            } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
                g_app_ctx.is_running = false;
            } else if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                mse_frontend_input_on_gamepad_added(input_manager, (int)event.gdevice.which);
            } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                mse_frontend_input_on_gamepad_removed(input_manager, (int)event.gdevice.which);
            }
        }

        if (ui_state.core_view_requested) {
            ui_state.core_view_requested = false;
            view_mode = MSE_FRONTEND_VIEW_TRANSITION_TO_CORE;
            transition_t = 0.0f;
        }

        if (view_mode == MSE_FRONTEND_VIEW_TRANSITION_TO_CORE) {
            transition_t += delta_seconds;
            if (transition_t >= transition_duration) {
                transition_t = transition_duration;
                view_mode = MSE_FRONTEND_VIEW_CORE;
            }
        } else if (view_mode == MSE_FRONTEND_VIEW_TRANSITION_TO_MENU) {
            transition_t += delta_seconds;
            if (transition_t >= transition_duration) {
                transition_t = transition_duration;
                view_mode = MSE_FRONTEND_VIEW_MENU;
                ui_state.fullscreen = false;
                mse_frontend_set_window_fullscreen(window, false);
            }
        }

        mse_backend_t *active_backend = mse_frontend_input_manager_get_backend(input_manager);
        backend_preview.backend = active_backend;
        if (active_backend != NULL) {
            if (active_backend->update_inputs != NULL && active_backend->input_states != NULL) {
                mse_backend_update_inputs(active_backend, active_backend->input_states);
            }
            if (active_backend->get_texture != NULL) {
                backend_preview.texture_binding = mse_gfx_get_texture_sampler_binding(active_backend->get_texture());
            }
        }
        
        mse_frontend_theme_apply(ui_state.theme);
        mse_frontend_imgui_begin_frame();
        if (view_mode == MSE_FRONTEND_VIEW_MENU || view_mode == MSE_FRONTEND_VIEW_TRANSITION_TO_MENU) {
            mse_frontend_ui_draw(&ui_state);
        }
        
        mse_frontend_backend_emulation_draw(&backend_preview, content_scale, view_mode != MSE_FRONTEND_VIEW_MENU);

        if (view_mode == MSE_FRONTEND_VIEW_TRANSITION_TO_CORE || view_mode == MSE_FRONTEND_VIEW_TRANSITION_TO_MENU) {
            ImDrawList *overlay = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
            const float fade = (view_mode == MSE_FRONTEND_VIEW_TRANSITION_TO_CORE)
                                   ? (1.0f - mse_frontend_clamp01(transition_t / transition_duration))
                                   : mse_frontend_clamp01(transition_t / transition_duration);
            ImU32 fade_col = igGetColorU32_Vec4((ImVec4){0.0f, 0.0f, 0.0f, fade});
            ImGuiViewport *main_viewport = igGetMainViewport();
            if (overlay != NULL && main_viewport != NULL) {
                ImDrawList_AddRectFilled(overlay,
                                         main_viewport->WorkPos,
                                         (ImVec2){main_viewport->WorkPos.x + main_viewport->WorkSize.x,
                                                  main_viewport->WorkPos.y + main_viewport->WorkSize.y},
                                         fade_col,
                                         0.0f,
                                         0);
            }
        }
        igRender();

        SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
        if (command_buffer == NULL) {
            DEBUG_ERROR("Failed to acquire GPU command buffer: %s", SDL_GetError());
            continue;
        }

        mse_frontend_imgui_prepare_draw_data(command_buffer);

        SDL_GPUTexture *swapchain_texture = NULL;
        SDL_AcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, NULL, NULL);

        if (swapchain_texture != NULL) {
            SDL_GPUColorTargetInfo color_target_info;
            color_target_info.texture = swapchain_texture;
            color_target_info.mip_level = 0;
            color_target_info.layer_or_depth_plane = 0;
            color_target_info.cycle = false;
            color_target_info.clear_color.r = 0.0f;
            color_target_info.clear_color.g = 0.0f;
            color_target_info.clear_color.b = 0.0f;
            color_target_info.clear_color.a = 1.0f;
            color_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
            color_target_info.store_op = SDL_GPU_STOREOP_STORE;
            color_target_info.resolve_texture = NULL;
            color_target_info.resolve_mip_level = 0;
            color_target_info.resolve_layer = 0;
            color_target_info.cycle_resolve_texture = false;
            color_target_info.padding1 = 0;
            color_target_info.padding2 = 0;

            SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target_info, 1, NULL);
            if (render_pass != NULL) {
                mse_frontend_imgui_render_draw_data(command_buffer, render_pass);
               
                SDL_EndGPURenderPass(render_pass);
            }
        }

        SDL_SubmitGPUCommandBuffer(command_buffer);

        if (render_viewports) {
            igUpdatePlatformWindows();
            igRenderPlatformWindowsDefault(NULL, NULL);
        }
    }

    SDL_WaitForGPUIdle(device);
    
    if (!libmse_cvar_export("config.cfg"))
        DEBUG_ERROR("Failed to export config.cfg");

    mse_frontend_input_manager_destroy(input_manager);
    
    mse_frontend_backend_preview_shutdown(&backend_preview);
    if (cnes_backend != NULL)
        mse_backend_shutdown(cnes_backend);

    mse_frontend_imgui_shutdown(&imgui_backend);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (log_file) {
        libmse_debug_flush_all();
        fclose(log_file);
    }

    return 0;
}