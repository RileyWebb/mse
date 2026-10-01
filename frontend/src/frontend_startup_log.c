#define DEBUG_LOG_SOURCE "frontend"

// The banner written once at startup.
//
// Every bug report starts with "what were you running on", so this collects
// the answer in one block: build, host, graphics and the backends that were
// found. Read it with the console open (F10) or from the captured log.

#include "frontend_startup_log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include "libmse/libmse.h"
#include "libmse/libmse_debug.h"
#include "libmse/libmse_version.h"

#define RULE "------------------------------------------------------------"

static const char *startup_present_mode_name(SDL_GPUPresentMode mode)
{
	switch (mode) {
	case SDL_GPU_PRESENTMODE_VSYNC:     return "vsync";
	case SDL_GPU_PRESENTMODE_IMMEDIATE: return "immediate";
	case SDL_GPU_PRESENTMODE_MAILBOX:   return "mailbox";
	default:                            return "unknown";
	}
}

static const char *startup_texture_format_name(SDL_GPUTextureFormat format)
{
	switch (format) {
	case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM:      return "B8G8R8A8_UNORM";
	case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM:      return "R8G8B8A8_UNORM";
	case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
	case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
	case SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:  return "R16G16B16A16_FLOAT";
	case SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM:   return "R10G10B10A2_UNORM";
	default:                                        return "other";
	}
}

// Only the flags worth knowing about, joined into one line. A list of every
// extension the CPU has ever supported is noise.
static void startup_cpu_features(char *out, size_t out_size)
{
	static const struct {
		const char *name;
		bool (*probe)(void);
	} features[] = {
		{"SSE2", SDL_HasSSE2},   {"SSE4.1", SDL_HasSSE41}, {"SSE4.2", SDL_HasSSE42},
		{"AVX", SDL_HasAVX},     {"AVX2", SDL_HasAVX2},    {"AVX512F", SDL_HasAVX512F},
		{"NEON", SDL_HasNEON},   {"LSX", SDL_HasLSX},      {"LASX", SDL_HasLASX},
	};

	out[0] = '\0';
	size_t used = 0;
	for (size_t i = 0; i < sizeof(features) / sizeof(features[0]); ++i) {
		if (!features[i].probe()) {
			continue;
		}
		const int n = snprintf(out + used, out_size - used, used == 0 ? "%s" : " %s", features[i].name);
		if (n < 0 || (size_t)n >= out_size - used) {
			break;
		}
		used += (size_t)n;
	}
	if (used == 0) {
		snprintf(out, out_size, "none detected");
	}
}

static void startup_shader_formats(SDL_GPUShaderFormat formats, char *out, size_t out_size)
{
	static const struct {
		const char         *name;
		SDL_GPUShaderFormat bit;
	} kinds[] = {
		{"SPIRV", SDL_GPU_SHADERFORMAT_SPIRV}, {"DXIL", SDL_GPU_SHADERFORMAT_DXIL},
		{"DXBC", SDL_GPU_SHADERFORMAT_DXBC},   {"MSL", SDL_GPU_SHADERFORMAT_MSL},
		{"METALLIB", SDL_GPU_SHADERFORMAT_METALLIB},
	};

	out[0] = '\0';
	size_t used = 0;
	for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
		if ((formats & kinds[i].bit) == 0) {
			continue;
		}
		const int n = snprintf(out + used, out_size - used, used == 0 ? "%s" : " %s", kinds[i].name);
		if (n < 0 || (size_t)n >= out_size - used) {
			break;
		}
		used += (size_t)n;
	}
	if (used == 0) {
		snprintf(out, out_size, "none");
	}
}

void mse_frontend_log_startup(const mse_frontend_startup_info_t *info)
{
	if (info == NULL) {
		return;
	}

	char scratch[256];

	DEBUG_INFO(RULE);
	DEBUG_INFO("MSE - Multi-System Emulator");

	const int sdl_linked = SDL_GetVersion();

	DEBUG_INFO("  version    libmse %s, built %s", LIBMSE_VERSION_STRING, LIBMSE_VERSION_BUILD_STRING);

	{
		const time_t now = time(NULL);
		struct tm    local;
#if defined(_WIN32)
		localtime_s(&local, &now);
#else
		localtime_r(&now, &local);
#endif
		if (strftime(scratch, sizeof(scratch), "%Y-%m-%d %H:%M:%S", &local) > 0) {
			DEBUG_INFO("  started    %s", scratch);
		}
	}

	{
		char *cwd = SDL_GetCurrentDirectory();
		if (cwd != NULL) {
			DEBUG_INFO("  directory  %s", cwd);
			SDL_free(cwd);
		}
	}

	DEBUG_INFO(RULE);

	startup_cpu_features(scratch, sizeof(scratch));
	DEBUG_INFO("  platform   %s", SDL_GetPlatform());
	DEBUG_INFO("  cpu        %d logical cores, %d byte cache line", SDL_GetNumLogicalCPUCores(),
			   SDL_GetCPUCacheLineSize());
	DEBUG_INFO("  cpu flags  %s", scratch);
	DEBUG_INFO("  memory     %d MB", SDL_GetSystemRAM());

	DEBUG_INFO(RULE);

	DEBUG_INFO("  sdl        %d.%d.%d (compiled %d.%d.%d, rev %s)", SDL_VERSIONNUM_MAJOR(sdl_linked),
			   SDL_VERSIONNUM_MINOR(sdl_linked), SDL_VERSIONNUM_MICRO(sdl_linked), SDL_MAJOR_VERSION,
			   SDL_MINOR_VERSION, SDL_MICRO_VERSION, SDL_GetRevision());

	const char *video_driver = SDL_GetCurrentVideoDriver();
	const char *audio_driver = SDL_GetCurrentAudioDriver();
	DEBUG_INFO("  video      %s", video_driver != NULL ? video_driver : "none");
	DEBUG_INFO("  audio      %s", audio_driver != NULL ? audio_driver : "none");

	if (info->window != NULL) {
		int width = 0, height = 0;
		SDL_GetWindowSizeInPixels(info->window, &width, &height);

		const SDL_DisplayID       display = SDL_GetDisplayForWindow(info->window);
		const SDL_DisplayMode    *mode    = SDL_GetCurrentDisplayMode(display);
		const char               *name    = SDL_GetDisplayName(display);

		DEBUG_INFO("  window     %dx%d px at %.2fx scale", width, height, (double)info->content_scale);
		if (mode != NULL) {
			DEBUG_INFO("  display    %s, %dx%d @ %.1f Hz", name != NULL ? name : "unknown", mode->w, mode->h,
					   (double)mode->refresh_rate);
		}
	}

	if (info->device != NULL) {
		const char *driver = SDL_GetGPUDeviceDriver(info->device);

		const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(info->device);
		const char *gpu_name = props != 0 ? SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, NULL) : NULL;
		const char *gpu_ver =
			props != 0 ? SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, NULL) : NULL;

		DEBUG_INFO("  gpu        %s", gpu_name != NULL ? gpu_name : "unknown adapter");
		DEBUG_INFO("  gpu driver %s%s%s", driver != NULL ? driver : "unknown", gpu_ver != NULL ? ", " : "",
				   gpu_ver != NULL ? gpu_ver : "");

		startup_shader_formats(SDL_GetGPUShaderFormats(info->device), scratch, sizeof(scratch));
		DEBUG_INFO("  shaders    %s", scratch);
		DEBUG_INFO("  swapchain  %s, %s present", startup_texture_format_name(info->swapchain_format),
				   startup_present_mode_name(info->present_mode));
	}

	DEBUG_INFO(RULE);

	DEBUG_INFO("  backends   %zu loaded", info->backend_count);
	for (size_t i = 0; i < info->backend_count; ++i) {
		const libmse_backend_t *backend = info->backends != NULL ? info->backends[i] : NULL;
		if (backend == NULL) {
			continue;
		}

		DEBUG_INFO("    %-12s %-8s by %s", backend->info.name ? backend->info.name : "(unnamed)",
				   backend->info.version ? backend->info.version : "--",
				   backend->info.author ? backend->info.author : "unknown");
		DEBUG_INFO("      %s", backend->info.description ? backend->info.description : "no description");
		DEBUG_INFO("      built %s, licence %s, %zu inputs",
				   backend->info.build_date ? backend->info.build_date : "--",
				   backend->info.licence ? backend->info.licence : "--", backend->input_count);
	}
	if (info->backend_count == 0) {
		DEBUG_WARN("    none found - nothing can be run until a backend is installed");
	}

	DEBUG_INFO(RULE);
}
