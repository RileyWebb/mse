#define LIBMSE_EXPORTS

#include "libmse/libmse.h"
#include "cNES/nes.h"
#include "cNES/rom.h"
#include "cNES/ppu.h"
#include "cNES/version.h"
#include "cNES/palette.h"
#include "libmse/libmse_gfx.h"
#include "libmse/libmse_sync.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

LIBMSE_API mse_backend_info_t info = {.name		   = "cNES",
									  .version	   = CNES_VERSION_STRING,
									  .author	   = "Riley Webb",
									  .description = "cNES Emulator Backend for libmse",
									  .licence	   = "MIT",
									  .repository  = "https://github.com/RileyWebb/mse",
									  .build_date  = __DATE__,
									  .build_time  = __TIME__};

LIBMSE_API const mse_backend_caps_e capabilities = MSE_BACKEND_CAPS_THREADED;

/* Define NES inputs (standard controller) */
LIBMSE_API const mse_backend_input_desc_t inputs[] = {
	{.id = "DPAD_UP", .type = MSE_INPUT_TYPE_BUTTON},	 {.id = "DPAD_DOWN", .type = MSE_INPUT_TYPE_BUTTON},
	{.id = "DPAD_LEFT", .type = MSE_INPUT_TYPE_BUTTON},	 {.id = "DPAD_RIGHT", .type = MSE_INPUT_TYPE_BUTTON},
	{.id = "BTN_A", .type = MSE_INPUT_TYPE_BUTTON},		 {.id = "BTN_B", .type = MSE_INPUT_TYPE_BUTTON},
	{.id = "BTN_SELECT", .type = MSE_INPUT_TYPE_BUTTON}, {.id = "BTN_START", .type = MSE_INPUT_TYPE_BUTTON},
};
LIBMSE_API const size_t input_count = sizeof(inputs) / sizeof(inputs[0]);

static bool cnes_file_handler(const char *filename);

LIBMSE_API const mse_file_handler_t mse_file_handlers[] = {{".nes", "iNES ROM", cnes_file_handler},
														   {".tnes", "iNES ROM (trimmed)", cnes_file_handler},
														   {".zip", "ZIP Archive", cnes_file_handler},
														   {NULL, NULL, NULL}};

/* Internal state */
static NES	*g_nes			   = NULL;
static char *g_active_rom_path = NULL;
static mse_gfx_texture_t *g_texture = NULL;
static mse_gfx_transfer_buffer_t *g_transfer_buffer = NULL;

bool cmd_reset_handler(const libmse_cmd_arg_t* args) {
	if (g_nes) {
		NES_Reset(g_nes);
	}
	return true;
}

bool cmd_load_palette_handler(const libmse_cmd_arg_t* args) {
	if (g_nes && args && args[0].s) {
		uint32_t *new_palette = PALETTE_Load(args[0].s);
		if (new_palette) {
			PPU_SetPalette(g_nes->ppu, new_palette);
			PALETTE_Destroy(new_palette);
			return true;
		}
	}
	return false;
}

static void register_cvars(NES *nes)
{
    // Emulation settings
	//libmse_cvar_register("cnes_emu_region", LIBMSE_CVAR_INT, &(int){0}, "Emulation region: 0 = AUTO, 1 = NTSC, 2 = PAL");
	libmse_cvar_register("cnes_emu_frame_time", LIBMSE_CVAR_FLOAT, &g_nes->settings.frame_time, "Target frame time in milliseconds");

    // CPU settings
	//libmse_cvar_register("cnes_cpu_break_on_illegal", LIBMSE_CVAR_INT, &(int){0}, "Break on illegal instructions: 0 = Disabled, 1 = Enabled");

    // PPU settings
	//libmse_cvar_register("cnes_ppu_sprite_limit", LIBMSE_CVAR_INT, &(int){1}, "Sprite limit: 0 = Unlimited (draw all sprites), 1 = NES-style (max 8 sprites per scanline)");
	//libmse_cvar_register("cnes_ppu_mirroring_override", LIBMSE_CVAR_INT, &(int){0}, "Force mirroring override: 0 = Use Cartridge/Mapper default, 1 = Horizontal, 2 = Vertical, 3 = Four-Screen");
	//libmse_cvar_register("cnes_ppu_layer_mask", LIBMSE_CVAR_INT, &(int){0}, "Layer visibility mask: 0 = Draw normally, 1 = Hide Background layer, 2 = Hide Sprite layer");


    // Video settings
	//libmse_cvar_register("cnes_video_aspect", LIBMSE_CVAR_FLOAT, &(float){4.0f / 3.0f}, "Aspect ratio (width/height) for rendering the NES framebuffer");
	//libmse_cvar_register("cnes_video_crop_overscan", LIBMSE_CVAR_INT, &(int){1}, "Crop overscan: 0 = Disabled, 1 = Enabled");

    // Audio settings
	libmse_cvar_register("cnes_audio_volume", LIBMSE_CVAR_FLOAT, &g_nes->settings.audio.volume, "Master audio volume (0.0 to 1.0)");
	libmse_cvar_register("cnes_audio_samplerate", LIBMSE_CVAR_INT, &g_nes->settings.audio.sample_rate, "Audio sample rate for output");
	//libmse_cvar_register("cnes_audio_chan_pulse1", LIBMSE_CVAR_INT, &(int){1}, "Enable Pulse Channel 1: 0 = Disabled, 1 = Enabled");
	//libmse_cvar_register("cnes_audio_chan_pulse2", LIBMSE_CVAR_INT, &(int){1}, "Enable Pulse Channel 2: 0 = Disabled, 1 = Enabled");
	//libmse_cvar_register("cnes_audio_chan_triangle", LIBMSE_CVAR_INT, &(int){1}, "Enable Triangle Channel: 0 = Disabled, 1 = Enabled");
	//libmse_cvar_register("cnes_audio_chan_noise", LIBMSE_CVAR_INT, &(int){1}, "Enable Noise Channel: 0 = Disabled, 1 = Enabled");
	//libmse_cvar_register("cnes_audio_chan_dmc", LIBMSE_CVAR_INT, &(int){1}, "Enable DMC Channel: 0 = Disabled, 1 = Enabled");

    // Input settings
	libmse_cvar_register("cnes_input_threshold", LIBMSE_CVAR_INT, &g_nes->settings.input.actuation_threshold, "Input actuation threshold (for analog inputs, 0 to 1)");
}

static void register_cmds(NES *nes)
{
	libmse_command_register(&(libmse_command_t){"cnes_reset", "Resets the NES emulator state", 0, NULL, cmd_reset_handler});
	libmse_command_register(&(libmse_command_t){"cnes_load_palette", "Loads a palette from the specified path", 1, (const libmse_cvar_type_t[]){LIBMSE_CVAR_STRING}, cmd_load_palette_handler});
	//libmse_command_register(&(libmse_command_t){"cnes_load_rom", "Loads a ROM from the specified path", 1, (const libmse_cvar_type_t[]){LIBMSE_CVAR_STRING}, cmd_load_rom_handler});
}

LIBMSE_API bool init(void)
{
	g_nes = NES_Create();

	if (g_nes == NULL) return false;

	g_texture = mse_gfx_create_texture(256, 240, MSE_GFX_TEXTURE_FORMAT_RGBA8_UNORM);
	if (!g_texture) {
		NES_Destroy(g_nes);
		return false;
	}

	g_transfer_buffer = mse_gfx_create_transfer_buffer(256 * 240 * sizeof(uint32_t));
	if (!g_transfer_buffer) {
		mse_gfx_destroy_texture(g_texture);
		NES_Destroy(g_nes);
		return false;
	}

	register_cvars(g_nes);
	register_cmds(g_nes);

	return true;
}

/* Add this at the top of your file with your other static variables */
static uint8_t *g_rom_data = NULL;

LIBMSE_API bool load_rom(const uint8_t *data, size_t size)
{
	if (g_nes == NULL || data == NULL || size == 0) {
		return false;
	}

	// Clean up the previously loaded ROM data if it exists
	if (g_rom_data != NULL) {
		free(g_rom_data);
		g_rom_data = NULL;
	}

	// Make a persistent copy of the ROM data that outlives this function
	g_rom_data = malloc(size);
	if (!g_rom_data) return false;
	memcpy(g_rom_data, data, size);

	ROM *rom = ROM_LoadMemory(g_rom_data, size);
	if (rom == NULL) {
		free(g_rom_data);
		g_rom_data = NULL;
		return false;
	}

	if (NES_Load(g_nes, rom) != 0) {
		ROM_Destroy(rom);
		free(g_rom_data);
		g_rom_data = NULL;
		return false;
	}

	return true;
}

LIBMSE_API bool load_rom_from_path(const char *path)
{
	if (path == NULL) return false;

	FILE *f = fopen(path, "rb");
	if (!f) return false;

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return false;
	}
	long sz = ftell(f);
	if (sz <= 0) {
		fclose(f);
		return false;
	}
	rewind(f);

	uint8_t *buf = malloc((size_t)sz);
	if (!buf) {
		fclose(f);
		return false;
	}

	size_t read = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	if (read != (size_t)sz) {
		free(buf);
		return false;
	}

	// Pass to load_rom, which now makes its OWN persistent copy
	bool ok = load_rom(buf, (size_t)sz);
	
	// Because load_rom handled persistence, we can safely free this temp buffer
	free(buf);

	if (ok) {
		free(g_active_rom_path);
		g_active_rom_path = strdup(path);
	}

	return ok;
}

LIBMSE_API void shutdown(void)
{
	if (g_transfer_buffer) {
		mse_gfx_destroy_transfer_buffer(g_transfer_buffer);
		g_transfer_buffer = NULL;
	}
	if (g_texture) {
		mse_gfx_destroy_texture(g_texture);
		g_texture = NULL;
	}
	if (g_nes != NULL) {
		NES_Destroy(g_nes);
		g_nes = NULL;
	}
	free(g_active_rom_path);
	g_active_rom_path = NULL;

	// Free our global ROM buffer on shutdown
	if (g_rom_data) {
		free(g_rom_data);
		g_rom_data = NULL;
	}
}

LIBMSE_API bool set_active_rom(const char *path)
{
	return load_rom_from_path(path);
}

static bool cnes_file_handler(const char *filename)
{
	if (filename == NULL) return false;
	return load_rom_from_path(filename);
}

/* Map the libmse input array (float) to NES controller bits */
static uint8_t pack_nes_controller(const float *states)
{
	uint8_t state = 0;
	if (states == NULL) return 0;

	/* NES Controller order (standard): A, B, Select, Start, Up, Down, Left, Right 
       cNES usually expects standard order. Let's check bit order or just use index.
       Actually, standard NES bit order from register 4016 read:
       Bit 0: A
       Bit 1: B
       Bit 2: Select
       Bit 3: Start
       Bit 4: Up
       Bit 5: Down
       Bit 6: Left
       Bit 7: Right
    */
	if (states[4] > 0.5f) state |= (1 << 0); // A
	if (states[5] > 0.5f) state |= (1 << 1); // B
	if (states[6] > 0.5f) state |= (1 << 2); // Select
	if (states[7] > 0.5f) state |= (1 << 3); // Start
	if (states[0] > 0.5f) state |= (1 << 4); // Up
	if (states[1] > 0.5f) state |= (1 << 5); // Down
	if (states[2] > 0.5f) state |= (1 << 6); // Left
	if (states[3] > 0.5f) state |= (1 << 7); // Right

	return state;
}

LIBMSE_API void update_inputs(const float *inputs)
{
	if (g_nes == NULL) return;
	uint8_t state = pack_nes_controller(inputs);
	NES_SetController(g_nes, 0, state);
}

LIBMSE_API void start(mse_event_t *stop_event)
{
	if (g_nes == NULL || stop_event == NULL || g_texture == NULL || g_transfer_buffer == NULL) {
		return;
	}

	while (!mse_event_wait_timeout(stop_event, 0)) {
		struct timespec start_time;
		timespec_get(&start_time, TIME_UTC);

		NES_StepFrame(g_nes);

		/* Copy PPU framebuffer to mse_gfx_transfer_buffer_t */
		const uint32_t *ppu_pixels = PPU_GetFramebuffer(g_nes->ppu);
		if (ppu_pixels != NULL) {
			void *mapped_ptr = mse_gfx_map_transfer_buffer(g_transfer_buffer);
			if (mapped_ptr) {
				// Fast memory copy instead of iterating over 61,440 individual array elements
				memcpy(mapped_ptr, ppu_pixels, 256 * 240 * sizeof(uint32_t));
				
				mse_gfx_unmap_transfer_buffer(g_transfer_buffer);
				mse_gfx_upload_texture(g_texture, g_transfer_buffer, 256 * 4);
			}
		}

		long frame_time_ms;
		struct timespec end_time;
		do
		{
			timespec_get(&end_time, TIME_UTC);
			frame_time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 + (end_time.tv_nsec - start_time.tv_nsec) / 1000000;
		} while (frame_time_ms < (long)(g_nes->settings.frame_time));
	}
}

LIBMSE_API mse_gfx_texture_t* get_texture(void)
{
	return g_texture;
}
