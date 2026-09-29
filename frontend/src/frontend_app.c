#define DEBUG_LOG_SOURCE "frontend"
#include "frontend_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libmse/libmse.h"
#include "libmse/libmse_resource.h"
#include "libmse/libmse_debug.h"
#include "libmse/libmse_profiler.h"
#include "frontend_profiler.h"
#include "frontend_screenshot.h"
#include "frontend_startup_log.h"
#include "frontend_covers.h"
#include "frontend_cimgui.h"
#include "frontend_imgui.h"
#include "frontend_theme.h"
#include "frontend_widgets.h"
#include "frontend_icons.h"
#include "frontend_ui.h"
#include "frontend_lua_ui.h"
#include "frontend_input.h"

typedef struct mse_frontend_backend_preview_s {
    libmse_backend_t *backend;
    SDL_Thread *thread;
    mse_event_t *stop_event;
    SDL_GPUTextureSamplerBinding texture_binding;
    SDL_Window *window;

    // --- Backend video ---
    // Backends hand us finished frames as CPU pixels; they have no business
    // knowing about SDL_GPU, so the upload happens here.
    SDL_GPUTexture *frame_texture;
    SDL_GPUSampler *frame_sampler;
    SDL_GPUTransferBuffer *frame_transfer;
    uint32_t frame_width;
    uint32_t frame_height;
    bool frame_swizzle;        // Host lacks BGRA8, so convert while copying
    bool frame_upload_pending; // Staged this tick, still to be copied on the GPU
    
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

static void mse_frontend_backend_preview_release_video(mse_frontend_backend_preview_t *preview,
                                                       SDL_GPUDevice *device) {
    if (preview == NULL || device == NULL) {
        return;
    }

    if (preview->frame_transfer != NULL) {
        SDL_ReleaseGPUTransferBuffer(device, preview->frame_transfer);
        preview->frame_transfer = NULL;
    }
    if (preview->frame_texture != NULL) {
        SDL_ReleaseGPUTexture(device, preview->frame_texture);
        preview->frame_texture = NULL;
    }
    if (preview->frame_sampler != NULL) {
        SDL_ReleaseGPUSampler(device, preview->frame_sampler);
        preview->frame_sampler = NULL;
    }

    preview->frame_width = 0;
    preview->frame_height = 0;
    preview->frame_upload_pending = false;
    preview->texture_binding.texture = NULL;
    preview->texture_binding.sampler = NULL;
}

// (Re)creates the texture, sampler and staging buffer for a frame of the given
// size and format. Cheap to call every tick: it returns immediately unless the
// geometry actually changed.
static bool mse_frontend_backend_preview_ensure_video(mse_frontend_backend_preview_t *preview,
                                                      SDL_GPUDevice *device,
                                                      uint32_t width,
                                                      uint32_t height,
                                                      mse_frame_format_t format) {
    if (preview == NULL || device == NULL || width == 0 || height == 0) {
        return false;
    }

    if (preview->frame_texture != NULL && preview->frame_width == width && preview->frame_height == height) {
        return true;
    }

    mse_frontend_backend_preview_release_video(preview, device);

    // BGRA8 is what palette-based cores produce for free on a little-endian
    // host. It is almost always available, but fall back to RGBA8 plus a
    // per-pixel swap rather than refusing to display anything.
    SDL_GPUTextureFormat gpu_format = (format == MSE_FRAME_FORMAT_BGRA8)
                                          ? SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM
                                          : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    preview->frame_swizzle = false;
    if (!SDL_GPUTextureSupportsFormat(device, gpu_format, SDL_GPU_TEXTURETYPE_2D,
                                      SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        DEBUG_WARN("GPU does not support the frame format this backend publishes; converting per pixel.");
        gpu_format = (gpu_format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM)
                         ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
                         : SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        preview->frame_swizzle = true;
    }

    SDL_GPUTextureCreateInfo texture_info;
    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = gpu_format;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    preview->frame_texture = SDL_CreateGPUTexture(device, &texture_info);
    if (preview->frame_texture == NULL) {
        DEBUG_ERROR("Failed to create backend frame texture: %s", SDL_GetError());
        return false;
    }

    // Nearest: a 256x240 image scaled up to the window should stay crisp.
    SDL_GPUSamplerCreateInfo sampler_info;
    SDL_zero(sampler_info);
    sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    preview->frame_sampler = SDL_CreateGPUSampler(device, &sampler_info);
    if (preview->frame_sampler == NULL) {
        DEBUG_ERROR("Failed to create backend frame sampler: %s", SDL_GetError());
        mse_frontend_backend_preview_release_video(preview, device);
        return false;
    }

    SDL_GPUTransferBufferCreateInfo transfer_info;
    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = width * height * 4u;

    preview->frame_transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (preview->frame_transfer == NULL) {
        DEBUG_ERROR("Failed to create backend frame transfer buffer: %s", SDL_GetError());
        mse_frontend_backend_preview_release_video(preview, device);
        return false;
    }

    preview->frame_width = width;
    preview->frame_height = height;
    preview->texture_binding.texture = preview->frame_texture;
    preview->texture_binding.sampler = preview->frame_sampler;

    DEBUG_INFO("Backend video: %ux%u %s", width, height,
               preview->frame_swizzle ? "(converted)" : "(native)");
    return true;
}

// Pulls the newest frame from the backend and stages it. Runs before the UI is
// built so the image is in place for this tick; the GPU copy itself is issued
// later, once there is a command buffer.
static void mse_frontend_backend_preview_pull_frame(mse_frontend_backend_preview_t *preview,
                                                    SDL_GPUDevice *device) {
    if (preview == NULL || device == NULL || preview->backend == NULL) {
        return;
    }

    mse_frame_t frame;
    SDL_zero(frame);
    if (!mse_backend_get_frame(preview->backend, &frame)) {
        return; // Backend publishes no video, or has not produced a frame yet
    }

    if (!mse_frontend_backend_preview_ensure_video(preview, device, frame.width, frame.height, frame.format)) {
        return;
    }

    // Nothing new since last tick: keep displaying what is already uploaded.
    if (!frame.ready || frame.pixels == NULL) {
        return;
    }

    uint8_t *staging = (uint8_t *)SDL_MapGPUTransferBuffer(device, preview->frame_transfer, true);
    if (staging == NULL) {
        DEBUG_ERROR("Failed to map backend frame transfer buffer: %s", SDL_GetError());
        return;
    }

    const uint32_t row_bytes = frame.width * 4u;
    const uint32_t src_pitch = frame.pitch ? frame.pitch : row_bytes;

    for (uint32_t y = 0; y < frame.height; ++y) {
        const uint8_t *src = frame.pixels + (size_t)y * src_pitch;
        uint8_t *dst = staging + (size_t)y * row_bytes;

        if (!preview->frame_swizzle) {
            memcpy(dst, src, row_bytes);
        } else {
            for (uint32_t x = 0; x < frame.width; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = src[x * 4 + 3];
            }
        }
    }

    SDL_UnmapGPUTransferBuffer(device, preview->frame_transfer);
    preview->frame_upload_pending = true;
}

// Issues the staged copy. Must run on the same command buffer as, and before,
// the render pass that samples the texture.
static void mse_frontend_backend_preview_upload_frame(mse_frontend_backend_preview_t *preview,
                                                      SDL_GPUCommandBuffer *command_buffer) {
    if (preview == NULL || command_buffer == NULL || !preview->frame_upload_pending) {
        return;
    }

    preview->frame_upload_pending = false;

    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (copy_pass == NULL) {
        return;
    }

    SDL_GPUTextureTransferInfo source;
    SDL_zero(source);
    source.transfer_buffer = preview->frame_transfer;
    source.offset = 0;
    source.pixels_per_row = preview->frame_width;
    source.rows_per_layer = preview->frame_height;

    SDL_GPUTextureRegion destination;
    SDL_zero(destination);
    destination.texture = preview->frame_texture;
    destination.w = preview->frame_width;
    destination.h = preview->frame_height;
    destination.d = 1;

    SDL_UploadToGPUTexture(copy_pass, &source, &destination, true);
    SDL_EndGPUCopyPass(copy_pass);
}

static void mse_frontend_backend_preview_shutdown(mse_frontend_backend_preview_t *preview,
                                                  SDL_GPUDevice *device) {
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

    // After the emulation thread is joined, so nothing can be publishing into
    // the buffers we are about to drop.
    mse_frontend_backend_preview_release_video(preview, device);

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

static bool mse_frontend_backend_preview_init(mse_frontend_backend_preview_t *preview, libmse_backend_t *backend) {
    if (preview == NULL) {
        return false;
    }

    memset(preview, 0, sizeof(*preview));
    preview->backend = backend;
    if (backend != NULL) {
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
        uint32_t tex_w = preview->frame_width  ? preview->frame_width  : MSE_BACKEND_PREVIEW_WIDTH;
        uint32_t tex_h = preview->frame_height ? preview->frame_height : MSE_BACKEND_PREVIEW_HEIGHT;

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

        // The picture is letterboxed, so the bars have to be filled with
        // something. Black, not the theme surface: anything else reads as part
        // of the game and throws the colour off.
        {
            const ImVec2 win_pos  = igGetWindowPos();
            const ImVec2 win_size = igGetWindowSize();
            ImDrawList_AddRectFilled(draw_list, win_pos,
                                     (ImVec2){win_pos.x + win_size.x, win_pos.y + win_size.y},
                                     igGetColorU32_Vec4((ImVec4){0.0f, 0.0f, 0.0f, 1.0f}), 0.0f, 0);
        }

        ImDrawList_AddCallback(draw_list, igGetPlatformIO_Nil()->DrawCallback_SetSamplerNearest, NULL, 0);

        ImDrawList_AddImage(draw_list, mse_frontend_make_texture_ref(&preview->texture_binding), 
                cursor_screen_pos, image_max, (ImVec2){0.0f, 0.0f}, (ImVec2){1.0f, 1.0f}, 0xFFFFFFFF);

        ImDrawList_AddCallback(draw_list, igGetPlatformIO_Nil()->DrawCallback_SetSamplerLinear, NULL, 0);

        // Game overlays, drawn in the picture's own pixel coordinates so a
        // hitbox lands on the sprite it belongs to at any window size. Before
        // the frame counter, which belongs to the frontend and should stay on
        // top of whatever a script draws.
        mse_frontend_lua_ui_draw_overlays(&(mse_frontend_overlay_view_t){
            .x        = cursor_screen_pos.x,
            .y        = cursor_screen_pos.y,
            .width    = image_size.x,
            .height   = image_size.y,
            .pixels_x = (int)tex_w,
            .pixels_y = (int)tex_h,
        });

        // After the image, so it draws on top of it, and against the image's
        // own bounds rather than the window's -- the picture is letterboxed
        // inside the window, and a counter in the black bar reads as a bug.
        mse_frontend_profiler_draw_framecounter(cursor_screen_pos.x, cursor_screen_pos.y,
                                                image_size.x, image_size.y);
    } else {
        mse_frontend_ui_empty_state(MSE_ICON_START_CORE, "Nothing running",
                                    "Load a ROM from the library, or from File > Open ROM, to start a backend.");
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
            igSetWindowViewport(igGetCurrentWindow(), (ImGuiViewportP*)viewport);
        }

        flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus;
    }
    //igPushStyleColor_Vec4(ImGuiCol_WindowBg, (ImVec4){0.0f, 0.0f, 0.0f, 0.0f});
    if (igBegin(fill_viewport ? "###EMULATION_FULLSCREEN" : "Emulation###EMULATION_DOCK", NULL, flags)) {
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

int mse_frontend_run(const mse_frontend_app_config_t *config) {
    static const mse_frontend_args_t no_args = {0};
    const mse_frontend_args_t *args = (config != NULL && config->args != NULL) ? config->args : &no_args;

    // --set lands before the config file, so a value asked for on the command
    // line is not quietly overwritten by whatever was saved last run. Cvars
    // that do not exist yet are held as placeholders and applied when their
    // owner registers them, which is the same path config.cfg takes.
    for (size_t i = 0; i < args->set_count; ++i) {
        char assignment[512];
        snprintf(assignment, sizeof(assignment), "%s", args->sets[i]);

        char *equals = strchr(assignment, '=');
        if (equals == NULL) {
            DEBUG_ERROR("--set wants name=value, got '%s'", args->sets[i]);
            continue;
        }
        *equals = '\0';

        const char *set_argv[] = {assignment, equals + 1};
        libmse_cmd_execute("set", 2, set_argv);
    }

    if (args->config_path != NULL) {
        if (!libmse_cmd_execute("exec", 1, (const char **)&args->config_path)) {
            DEBUG_ERROR("Failed to parse %s", args->config_path);
        }
    }

    /* The user's own startup script, after config.cfg so it has the last word.
     * config.cfg is written by the app and rewritten whenever settings are
     * exported; this one is never touched, which is what makes it the place to
     * keep a change you want to survive. Created empty if it is not there. */
    const char *autoexec = libmse_resource_get_autoexec_path();
    if (autoexec != NULL) {
        libmse_cmd_execute("exec", 1, &autoexec);
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

    //mse_gfx_init(device);

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

    libmse_backend_t *cnes_backend = mse_backend_register_folder("cnes");

    /* Initialise the cNES backend lifecycle */
    if (cnes_backend != NULL) {
        mse_backend_init(cnes_backend);
    }

    /* Debug UI the backend defines for itself. Loaded after init so its scripts
     * can talk to a live emulator, and before the first frame so the panels are
     * in the View menu from the start. */
    if (mse_frontend_lua_ui_init()) {
        mse_frontend_lua_ui_load_backend(cnes_backend);

        /* Drop-in overlays. The backend's own go first, because its scripts put
         * their directory on package.path and a user script in data/lua/overlays
         * reaches the debug API through require("ui.debug") the same way. */
        mse_frontend_lua_ui_load_directory("cnes/data/lua/overlays");
        mse_frontend_lua_ui_load_directory("data/lua/overlays");
    }

    mse_frontend_backend_preview_t backend_preview;
    if (!mse_frontend_backend_preview_init(&backend_preview, cnes_backend)) {
        memset(&backend_preview, 0, sizeof(backend_preview));
    } else if (cnes_backend != NULL) {
        backend_preview.thread = SDL_CreateThread(mse_frontend_backend_thread_func, "BackendThread", &backend_preview);
    }

    mse_frontend_input_manager_t *input_manager = mse_frontend_input_manager_create();
    mse_frontend_input_manager_set_backend(input_manager, cnes_backend);
    mse_frontend_input_thread_start(input_manager);

    /* Build the backends list for the UI (pointer array, owned by libmse) */
    static libmse_backend_t *g_backend_list[1];
    size_t backend_count = 0;
    if (cnes_backend != NULL) {
        g_backend_list[backend_count++] = cnes_backend;
    }

    const bool render_viewports = mse_frontend_imgui_viewports_supported(&imgui_backend);

    mse_frontend_view_mode_t view_mode = MSE_FRONTEND_VIEW_MENU;
    float transition_t = 0.0f;
    const float transition_duration = 0.25f;
    Uint64 last_frame_ticks = SDL_GetTicksNS();

    // static, not a local: cvars are bound to fields of this, and a cvar that
    // points into a stack frame is only valid while that frame is. It happens
    // to outlive everything here today, which is exactly the kind of thing
    // that stops being true during a refactor and fails silently when it does.
    static mse_frontend_ui_state_t ui_state;
    mse_frontend_ui_init(&ui_state);
    ui_state.content_scale       = args->scale > 0.0f ? args->scale : content_scale;
    ui_state.active_backend_name = (cnes_backend != NULL && cnes_backend->info.name != NULL)
                                        ? cnes_backend->info.name : NULL;
    ui_state.input_manager  = input_manager;
    ui_state.window         = window;
    ui_state.backends       = g_backend_list;
    ui_state.backend_count  = backend_count;
    ui_state.selected_core_index = backend_count > 0 ? 0 : -1;

    libmse_profiler_thread_name(MSE_FRONTEND_PROFILER_THREAD_NAME);
    mse_frontend_profiler_init();
    mse_frontend_screenshot_init();

    if (args->fullscreen) {
        ui_state.fullscreen = 1;
        mse_frontend_set_window_fullscreen(window, true);
    }

    mse_frontend_log_startup(&(mse_frontend_startup_info_t){
        .window           = window,
        .device           = device,
        .swapchain_format = swapchain_format,
        .present_mode     = g_app_ctx.presentation_mode,
        .content_scale    = content_scale,
        .backends         = g_backend_list,
        .backend_count    = backend_count,
    });

    // Loaded after the banner so the log reads in the order things happened.
    if (args->rom_path != NULL && !mse_frontend_ui_load_rom(&ui_state, args->rom_path)) {
        DEBUG_ERROR("Could not load '%s'", args->rom_path);
    }

    for (size_t i = 0; i < args->exec_count; ++i) {
        libmse_cmd_execute("exec", 1, (const char **)&args->execs[i]);
    }

    g_app_ctx.is_running = true;
    while (g_app_ctx.is_running) {
        Uint64 now_ticks = SDL_GetTicksNS();
        float delta_seconds = (now_ticks >= last_frame_ticks) ? ((float)(now_ticks - last_frame_ticks) / 1000000000.0f) : 0.0f;
        if (delta_seconds > 0.1f) {
            delta_seconds = 0.1f;
        }
        last_frame_ticks = now_ticks;

        LIBMSE_PROFILE_START("events");
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

            // Pause/resume toggles on one key, like every other emulator.
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F5) {
                libmse_backend_t *transport = mse_frontend_input_manager_get_backend(input_manager);
                if (mse_backend_get_state(transport) == LIBMSE_BACKEND_PAUSED) {
                    mse_backend_resume(transport);
                } else {
                    mse_backend_pause(transport);
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

            // On a key as well as in the menu: the menu bar only exists in the
            // menu view, and a frame worth profiling is usually one being
            // spent emulating.
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F9) {
                ui_state.show_profiler = !ui_state.show_profiler;
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
        LIBMSE_PROFILE_END();

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

        libmse_backend_t *active_backend = mse_frontend_input_manager_get_backend(input_manager);
        backend_preview.backend = active_backend;
        mse_frontend_screenshot_set_backend(active_backend);
        if (active_backend != NULL) {
            LIBMSE_PROFILE_START("backend inputs");
            if (active_backend->update_inputs != NULL && active_backend->input_states != NULL) {
                mse_backend_update_inputs(active_backend, active_backend->input_states);
            }
            LIBMSE_PROFILE_END();

            LIBMSE_PROFILE_START("pull frame");
            mse_frontend_backend_preview_pull_frame(&backend_preview, device);
            LIBMSE_PROFILE_END();
        }

        LIBMSE_PROFILE_START("ui");
        mse_frontend_theme_set_scale(ui_state.content_scale);
        mse_frontend_theme_apply();

        LIBMSE_PROFILE_START("imgui begin");
        mse_frontend_imgui_begin_frame();
        LIBMSE_PROFILE_END();

        // One snapshot per thread for this frame, shared by the profiler window
        // and the frame counter.
        mse_frontend_profiler_new_frame();

        if (view_mode == MSE_FRONTEND_VIEW_MENU || view_mode == MSE_FRONTEND_VIEW_TRANSITION_TO_MENU) {
            LIBMSE_PROFILE_START("menus and panels");
            mse_frontend_ui_draw(&ui_state);
            LIBMSE_PROFILE_END();
        } else if (ui_state.menu_bar_in_game) {
            // Only the bar. Before the emulation view, because ImGui takes the
            // bar's height out of the viewport's work area and the fullscreen
            // picture is sized from that -- the game then sits below the bar
            // rather than behind it.
            LIBMSE_PROFILE_START("menu bar");
            mse_frontend_ui_draw_menu_bar_only(&ui_state);
            LIBMSE_PROFILE_END();
        }

        LIBMSE_PROFILE_START("emulation view");
        {
            // In the menu the preview is only worth a window while something is
            // actually running. It used to be drawn regardless, which left an
            // empty "Emulation" window floating over the frontend -- and with
            // multi-viewport on it could detach into an OS window of its own and
            // sit there swallowing clicks.
            const bool core_view = view_mode != MSE_FRONTEND_VIEW_MENU;
            const bool has_content = mse_backend_get_state(active_backend) != LIBMSE_BACKEND_STOPPED;
            if (core_view || has_content) {
                mse_frontend_backend_emulation_draw(&backend_preview, content_scale, core_view);
            }
        }
        LIBMSE_PROFILE_END();

        // Backend-defined panels, after the emulation view so they stack above
        // it, and outside the view_mode check above so they stay up while a
        // game runs -- watching a CPU or a movie's input log against a frozen
        // menu is not what any of them are for. Each panel is isolated inside
        // here, so one erroring cannot take the rest of the frame with it.
        LIBMSE_PROFILE_START("lua panels");
        mse_frontend_lua_ui_draw();
        LIBMSE_PROFILE_END();

        // Last, and inside "ui", so the window is honest about what drawing it
        // costs rather than quietly leaving itself out of the total.
        LIBMSE_PROFILE_START("profiler window");
        mse_frontend_profiler_draw(&ui_state.show_profiler);
        LIBMSE_PROFILE_END();

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
        LIBMSE_PROFILE_END(); // "ui"

        LIBMSE_PROFILE_START("imgui render");
        igRender();
        LIBMSE_PROFILE_END();

        LIBMSE_PROFILE_START("gpu");

        SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(device);
        if (command_buffer == NULL) {
            DEBUG_ERROR("Failed to acquire GPU command buffer: %s", SDL_GetError());
            // "gpu" has to be closed by hand on the way out; the frame boundary
            // would forgive it, but at the cost of an unbalanced-zone warning
            // every time the GPU is busy.
            LIBMSE_PROFILE_END();
            libmse_profiler_frame();
            continue;
        }

        // Before the render pass, so the texture the UI samples holds this frame.
        LIBMSE_PROFILE_START("upload frame");
        mse_frontend_backend_preview_upload_frame(&backend_preview, command_buffer);
        LIBMSE_PROFILE_END();

        LIBMSE_PROFILE_START("prepare draw data");
        mse_frontend_imgui_prepare_draw_data(command_buffer);
        LIBMSE_PROFILE_END();

        SDL_GPUTexture *swapchain_texture = NULL;
        LIBMSE_PROFILE_START("acquire swapchain");
        SDL_AcquireGPUSwapchainTexture(command_buffer, window, &swapchain_texture, NULL, NULL);
        LIBMSE_PROFILE_END();

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

            LIBMSE_PROFILE_START("render pass");
            SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target_info, 1, NULL);
            if (render_pass != NULL) {
                mse_frontend_imgui_render_draw_data(command_buffer, render_pass);

                SDL_EndGPURenderPass(render_pass);
            }
            LIBMSE_PROFILE_END();
        }

        // Where waiting for the GPU and for vsync tends to land, so it is
        // usually the largest row and the one worth reading first.
        LIBMSE_PROFILE_START("submit");
        SDL_SubmitGPUCommandBuffer(command_buffer);
        LIBMSE_PROFILE_END();

        if (render_viewports) {
            LIBMSE_PROFILE_START("viewports");
            igUpdatePlatformWindows();
            igRenderPlatformWindowsDefault(NULL, NULL);
            LIBMSE_PROFILE_END();
        }

        LIBMSE_PROFILE_END(); // "gpu"
        libmse_profiler_frame();
    }

    SDL_WaitForGPUIdle(device);
    
    if (!libmse_cvar_export("config.cfg"))
        DEBUG_ERROR("Failed to export config.cfg");

    mse_frontend_input_manager_destroy(input_manager);
    
    mse_frontend_backend_preview_shutdown(&backend_preview, device);
    if (cnes_backend != NULL)
        mse_backend_shutdown(cnes_backend);

    // Before the ImGui context goes, while the texture list is still valid.
    mse_frontend_covers_shutdown();

    mse_frontend_imgui_shutdown(&imgui_backend);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();

    libmse_log_flush_all();

    return 0;
}