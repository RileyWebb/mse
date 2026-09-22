#include "libmse/libmse.h"
#include "cNES/nes.h"
#include "cNES/rom.h"
#include "cNES/ppu.h"
#include "cNES/apu.h"
#include "cNES/version.h"
#include "cNES/palette.h"
#include "libmse/libmse_debug.h"
#include "libmse/libmse_sync.h"
#include "libmse/libmse_profiler.h"
#include "cNES/cpu.h"
#include "cNES/tas.h"
#include "cNES/external/cpu_debug.h"
#include "cNES/external/cheat_api.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdatomic.h>
#include <pthread.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

LIBMSE_API mse_backend_info_t info = {.name		   = "cNES",
									  .version	   = CNES_VERSION_STRING,
									  .author	   = "Riley Webb",
									  .description = "cNES Emulator Backend for libmse",
									  .licence	   = "MIT",
									  .repository  = "https://github.com/RileyWebb/mse",
									  .build_date  = __DATE__,
									  .build_time  = __TIME__};

/* Define NES inputs (standard controller) */
LIBMSE_API const mse_backend_input_desc_t inputs[] = {
	{.id = "DPAD_UP", .type = MSE_INPUT_TYPE_BUTTON},	 {.id = "DPAD_DOWN", .type = MSE_INPUT_TYPE_BUTTON},
	{.id = "DPAD_LEFT", .type = MSE_INPUT_TYPE_BUTTON},	 {.id = "DPAD_RIGHT", .type = MSE_INPUT_TYPE_BUTTON},
	{.id = "BTN_A", .type = MSE_INPUT_TYPE_BUTTON},		 {.id = "BTN_B", .type = MSE_INPUT_TYPE_BUTTON},
	{.id = "BTN_SELECT", .type = MSE_INPUT_TYPE_BUTTON}, {.id = "BTN_START", .type = MSE_INPUT_TYPE_BUTTON},
};
LIBMSE_API const size_t input_count = sizeof(inputs) / sizeof(inputs[0]);

/* Debug UI this backend defines for itself. The frontend loads these into its
 * UI Lua state and they register panels through mse.ui; they reach the emulator
 * through the plain C API in cNES/external/cpu_debug.h, so no Lua state is shared across
 * the DLL boundary. Paths are relative to the working directory. */
LIBMSE_API const char *lua_libraries[] = {
	"cnes/data/lua/ui/cheats.lua",
	"cnes/data/lua/ui/cpu.lua",
	"cnes/data/lua/ui/disasm.lua",
	"cnes/data/lua/ui/memory.lua",
	"cnes/data/lua/ui/ppu.lua",
};
LIBMSE_API const size_t lua_library_count = sizeof(lua_libraries) / sizeof(lua_libraries[0]);

static bool cnes_file_handler(const char *filename);

// No ".zip" entry: load_rom_from_path reads the file verbatim and hands it to
// the iNES parser, so an archive was advertised and then rejected. Re-add it
// alongside actual decompression, not before.
LIBMSE_API const mse_file_handler_t mse_file_handlers[] = {{".nes", "iNES ROM", cnes_file_handler},
														   {".tnes", "iNES ROM (trimmed)", cnes_file_handler},
														   {NULL, NULL, NULL}};

/* Internal state */
static NES	*g_nes			   = NULL;
static char *g_active_rom_path = NULL;

// Guards every touch of g_nes. start() drives the emulator on its own thread
// while the host may call load_rom / set_active_rom / update_inputs / the reset
// command from another, and a ROM swap frees the PRG data the bus is reading
// out of. Without this, hot-swapping a ROM mid-frame is a use-after-free.
static pthread_mutex_t g_nes_mutex = PTHREAD_MUTEX_INITIALIZER;

// pthread_setname_np is not portable: glibc takes (thread, name), macOS takes
// (name) and can only name the calling thread, and the Windows pthread shims
// vary in whether they provide it at all. Naming a thread is a debugging
// convenience, so where it is unavailable this is simply a no-op.
static void cnes_set_thread_name(pthread_t thread, const char *name)
{
#if defined(__linux__) || defined(__GLIBC__)
	pthread_setname_np(thread, name);
#elif defined(__APPLE__)
	// Only works from inside the thread being named; callers here are not.
	(void)thread;
	(void)name;
#else
	(void)thread;
	(void)name;
#endif
}
// === Video output ===
//
// The frontend owns the GPU, so this publishes finished frames as CPU pixels
// and lets it do the upload. What used to be here was a two-buffer handshake
// driven by a worker thread whose body was entirely commented out, pointing at
// an mse_gfx_* texture API that does not exist -- the emulator rendered into
// its own internal buffer and nothing ever left the backend.
//
// Three buffers, rotated without a lock:
//   render  - the PPU is drawing into it right now (owned by the emu thread)
//   publish - what the frontend is reading  (owned by the frontend thread)
//   spare   - parked in g_frame_slot, up for grabs by whoever swaps next
//
// Each side owns its own index outright and they meet only at a single atomic
// exchange, so neither can ever block the other. This matters more than it
// looks: with the frame limiter off, the emulator finishes thousands of frames
// a second, and a mutex here would have the frontend queueing behind a writer
// that reacquires it immediately -- pthread mutexes are not fair, so the UI
// thread could be starved for a very long time.
#define CNES_FRAME_WIDTH  256
#define CNES_FRAME_HEIGHT 240
#define CNES_FRAME_PIXELS (CNES_FRAME_WIDTH * CNES_FRAME_HEIGHT)
#define CNES_FRAME_BYTES  (CNES_FRAME_PIXELS * (int)sizeof(uint32_t))

#define CNES_SLOT_MASK  0x3u
#define CNES_SLOT_FRESH 0x4u // Set by the producer, cleared when collected

static uint32_t *g_frame_slots[3] = {NULL, NULL, NULL};

// The handoff point. Holds the index of the spare slot, plus CNES_SLOT_FRESH
// when that slot holds a frame the frontend has not taken yet.
static _Atomic unsigned g_frame_slot = 2u;

static unsigned g_frame_render_slot  = 0u; // emu thread only
static unsigned g_frame_publish_slot = 1u; // frontend thread only

static atomic_bool g_frame_seen = false;   // at least one frame exists

static void cnes_frames_destroy(void)
{
	for (int i = 0; i < 3; i++) {
		free(g_frame_slots[i]);
		g_frame_slots[i] = NULL;
	}
	g_frame_render_slot  = 0u;
	g_frame_publish_slot = 1u;
	atomic_store(&g_frame_slot, 2u);
	atomic_store(&g_frame_seen, false);
}

static bool cnes_frames_create(void)
{
	for (int i = 0; i < 3; i++) {
		g_frame_slots[i] = calloc(CNES_FRAME_PIXELS, sizeof(uint32_t));
		if (g_frame_slots[i] == NULL) {
			cnes_frames_destroy();
			return false;
		}
	}

	g_frame_render_slot  = 0u;
	g_frame_publish_slot = 1u;
	atomic_store(&g_frame_slot, 2u);
	atomic_store(&g_frame_seen, false);
	return true;
}

// Emulation thread: hand off the finished frame and pick up the spare.
static void cnes_frame_publish(void)
{
	unsigned previous = atomic_exchange_explicit(&g_frame_slot,
	                                             g_frame_render_slot | CNES_SLOT_FRESH,
	                                             memory_order_acq_rel);
	g_frame_render_slot = previous & CNES_SLOT_MASK;
	atomic_store_explicit(&g_frame_seen, true, memory_order_release);

	// Draw the next frame into whichever buffer we just took.
	PPU_SetOutputBuffers(g_nes->ppu, g_frame_slots[g_frame_render_slot], NULL);
}

// Frontend thread: take the newest frame if there is one. Returns true when a
// fresh frame was collected; the frame we were already showing stays valid and
// keeps being shown otherwise.
//
// Single consumer only. That is what makes the check-then-exchange safe: the
// producer only ever *sets* CNES_SLOT_FRESH, so once we have seen it nobody can
// take it away from us before the exchange, and the slot we get back is
// guaranteed to be the fresh one. A second consumer would break that.
static bool cnes_frame_collect(void)
{
	if (!(atomic_load_explicit(&g_frame_slot, memory_order_acquire) & CNES_SLOT_FRESH)) {
		return false;
	}

	// Swapping our slot in also clears the fresh bit, which is how the producer
	// learns the frame was taken.
	unsigned previous = atomic_exchange_explicit(&g_frame_slot,
	                                             g_frame_publish_slot,
	                                             memory_order_acq_rel);
	g_frame_publish_slot = previous & CNES_SLOT_MASK;
	return true;
}

// ---------------------------------------------------------------------------
// Controller input
//
// Also lock-free. update_inputs runs on the frontend thread once per UI frame;
// routing it through g_nes_mutex meant it queued behind whole emulated frames,
// and that is what made an uncapped emulator look like a frozen one. The
// emulation thread picks these up at the top of each frame instead.
// ---------------------------------------------------------------------------
static atomic_uchar g_controller_state[2] = {0, 0};

// ---------------------------------------------------------------------------
// Exclusive access to the NES, for the rare operations that rebuild it.
//
// g_nes_mutex alone is not enough: the emulation thread releases it and
// immediately retakes it, and an unfair mutex may never let a waiter in. The
// waiter count lets the emulation thread notice someone is trying and stand
// aside for a moment.
// ---------------------------------------------------------------------------
static atomic_int g_nes_waiters = 0;

static void cnes_nes_lock(void)
{
	atomic_fetch_add_explicit(&g_nes_waiters, 1, memory_order_release);
	pthread_mutex_lock(&g_nes_mutex);
}

static void cnes_nes_unlock(void)
{
	pthread_mutex_unlock(&g_nes_mutex);
	atomic_fetch_sub_explicit(&g_nes_waiters, 1, memory_order_release);
}

NES *cnes_backend_lock_nes(void)
{
	cnes_nes_lock();
	return g_nes;
}

void cnes_backend_unlock_nes(void)
{
	cnes_nes_unlock();
}

// Audio worker thread variables
static pthread_t g_audio_thread;
// Read by the worker, written by shutdown() on another thread.
static atomic_bool g_audio_thread_running = false;
static mse_event_t *g_audio_event = NULL;

static void* audio_worker_thread(void* arg) {
    float local_buffer[1024];

    while (g_audio_thread_running) {
        if (g_nes && g_nes->apu) {
            // Consume samples to prevent buffer overflow and simulate processing
            size_t read = APU_ReadSamples(g_nes->apu, local_buffer, 1024);
            if (read > 0) {
                // Here we would apply filtering and send to host audio API
                // For now, it just drains the lock-free buffer
            }
        }
        
        // Sleep ~5ms (roughly 200Hz polling rate) to avoid pegging CPU
        if (g_audio_event) {
            mse_event_wait_timeout(g_audio_event, 5);
        }
    }
    
    return NULL;
}

// ---------------------------------------------------------------------------
// Run state
//
// The debug API can disassemble and read memory at any time, but reading a CPU
// that is executing a thousand instructions between two glances at it is not
// debugging. These let the console stop the emulation thread and walk it
// forward by instruction or by frame.
//
// All of it is atomics rather than the NES lock: the commands run on the UI
// thread, and a paused emulator still has to release the lock so a ROM swap or
// a memory read can get in.
// ---------------------------------------------------------------------------

static atomic_bool g_paused = false;

// Work the emulation thread owes before it pauses again. Counts down there.
static atomic_int g_pending_instructions = 0;
static atomic_int g_pending_frames       = 0;

#define CNES_MAX_BREAKPOINTS 16

static atomic_uint g_breakpoint_count               = 0;
static atomic_uint g_breakpoints[CNES_MAX_BREAKPOINTS];

// Checking a breakpoint means stepping instruction by instruction and looking
// at the PC between each, which is far slower than letting a frame run. So the
// emulation thread only takes that path while at least one is armed.
static bool cnes_breakpoint_hit(uint16_t pc)
{
	unsigned count = atomic_load_explicit(&g_breakpoint_count, memory_order_relaxed);
	for (unsigned i = 0; i < count && i < CNES_MAX_BREAKPOINTS; ++i) {
		if ((uint16_t)atomic_load_explicit(&g_breakpoints[i], memory_order_relaxed) == pc) {
			return true;
		}
	}
	return false;
}

// Mirrors NES_StepFrame, but looks at the PC between instructions. Kept here
// rather than in the core so the core's hot path stays a plain loop; if
// NES_StepFrame ever grows a step it does per frame, this needs it too.
//
// Returns true when a breakpoint stopped the frame part way through.
static bool cnes_step_frame_checked(void)
{
	if (!TAS_IsFinished(g_nes->tas, g_nes)) {
		TAS_ApplyFrame(g_nes->tas, g_nes);
	}

	const int starting_frame = g_nes->ppu->frame_odd;
	while (g_nes->ppu->frame_odd == starting_frame) {
		NES_Step(g_nes);
		if (cnes_breakpoint_hit(g_nes->cpu->pc)) {
			return true;
		}
	}
	return false;
}

static void cnes_pause(const char *reason)
{
	atomic_store_explicit(&g_pending_instructions, 0, memory_order_relaxed);
	atomic_store_explicit(&g_pending_frames, 0, memory_order_relaxed);
	atomic_store_explicit(&g_paused, true, memory_order_release);
	if (reason != NULL) {
		libmse_logf("cnes: paused (%s)", reason);
	}
}

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

// Addresses are hex, with or without a "$" or "0x" on the front -- every other
// tool that prints a 6502 address prints it in hex, so typing one back in
// decimal would be the surprising choice. Counts are decimal.
static bool cnes_parse_address(const char *text, uint32_t *out)
{
	if (text == NULL || *text == '\0') {
		return false;
	}
	if (*text == '$') {
		text++;
	} else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		text += 2;
	}

	char *end = NULL;
	unsigned long value = strtoul(text, &end, 16);
	if (end == text || (end != NULL && *end != '\0')) {
		return false;
	}

	*out = (uint32_t)value;
	return true;
}

static bool cnes_parse_count(const char *text, int *out)
{
	char *end = NULL;
	long value = strtol(text, &end, 10);
	if (end == text || (end != NULL && *end != '\0') || value <= 0) {
		return false;
	}
	*out = (int)value;
	return true;
}

static const struct {
	const char        *name;
	cnes_debug_space_t space;
} CNES_SPACE_NAMES[] = {
	{"cpu", CNES_DEBUG_SPACE_CPU},         {"ram", CNES_DEBUG_SPACE_RAM},
	{"ppu", CNES_DEBUG_SPACE_PPU},         {"oam", CNES_DEBUG_SPACE_OAM},
	{"palette", CNES_DEBUG_SPACE_PALETTE}, {"chr", CNES_DEBUG_SPACE_CHR},
	{"prg", CNES_DEBUG_SPACE_PRG},
};

static bool cnes_parse_space(const char *text, cnes_debug_space_t *out)
{
	for (size_t i = 0; i < sizeof(CNES_SPACE_NAMES) / sizeof(CNES_SPACE_NAMES[0]); ++i) {
		if (strcmp(text, CNES_SPACE_NAMES[i].name) == 0) {
			*out = CNES_SPACE_NAMES[i].space;
			return true;
		}
	}

	libmse_log("cnes: address space must be one of cpu, ram, ppu, oam, palette, chr, prg");
	return false;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool cmd_reset_handler(int argc, const char** argv) {
	(void)argc;
	(void)argv;

	cnes_nes_lock();
	if (g_nes)
		NES_Reset(g_nes);
	cnes_nes_unlock();

	return true;
}

static bool cmd_pause_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	cnes_pause("requested");
	return true;
}

static bool cmd_resume_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	atomic_store_explicit(&g_paused, false, memory_order_release);
	libmse_log("cnes: running");
	return true;
}

// Stepping implies pausing: asking for one instruction from a running emulator
// would hand back whichever one it happened to be on.
static bool cmd_step_handler(int argc, const char **argv)
{
	int count = 1;
	if (argc > 0 && !cnes_parse_count(argv[0], &count)) {
		libmse_log("usage: cnes_step [instructions]");
		return false;
	}

	atomic_store_explicit(&g_paused, true, memory_order_release);
	atomic_fetch_add_explicit(&g_pending_instructions, count, memory_order_release);
	return true;
}

static bool cmd_step_frame_handler(int argc, const char **argv)
{
	int count = 1;
	if (argc > 0 && !cnes_parse_count(argv[0], &count)) {
		libmse_log("usage: cnes_step_frame [frames]");
		return false;
	}

	atomic_store_explicit(&g_paused, true, memory_order_release);
	atomic_fetch_add_explicit(&g_pending_frames, count, memory_order_release);
	return true;
}

static bool cmd_break_handler(int argc, const char **argv)
{
	uint32_t address = 0;
	if (argc < 1 || !cnes_parse_address(argv[0], &address) || address > 0xFFFF) {
		libmse_log("usage: cnes_break <address>   (hex, e.g. cnes_break $8000)");
		return false;
	}

	unsigned count = atomic_load_explicit(&g_breakpoint_count, memory_order_relaxed);
	if (count >= CNES_MAX_BREAKPOINTS) {
		libmse_logf("cnes: no room for another breakpoint (limit %d)", CNES_MAX_BREAKPOINTS);
		return false;
	}

	// The slot is filled before the count is published, so the emulation thread
	// never reads a slot that has not been written yet.
	atomic_store_explicit(&g_breakpoints[count], address, memory_order_relaxed);
	atomic_store_explicit(&g_breakpoint_count, count + 1, memory_order_release);

	libmse_logf("cnes: breakpoint %u at $%04X", count, (unsigned)address);
	return true;
}

static bool cmd_break_clear_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	atomic_store_explicit(&g_breakpoint_count, 0, memory_order_release);
	libmse_log("cnes: breakpoints cleared");
	return true;
}

static bool cmd_regs_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	cnes_debug_state_t state;
	if (!cnes_debug_get_state(&state) || !state.valid) {
		libmse_log("cnes: no ROM running");
		return false;
	}

	static const char FLAG_NAMES[8] = {'N', 'V', 'U', 'B', 'D', 'I', 'Z', 'C'};
	char flags[9];
	for (int i = 0; i < 8; ++i) {
		const bool set = (state.cpu.status & (1u << (7 - i))) != 0;
		// Lower case for a clear flag rather than a gap, so the field keeps a
		// fixed width and the eye can track one column.
		flags[i] = set ? FLAG_NAMES[i] : (char)(FLAG_NAMES[i] + ('a' - 'A'));
	}
	flags[8] = '\0';

	char disasm[64];
	cnes_debug_disassemble(state.cpu.pc, disasm, sizeof(disasm));

	libmse_logf("PC $%04X  A $%02X  X $%02X  Y $%02X  SP $%02X  P $%02X [%s]",
	            state.cpu.pc, state.cpu.a, state.cpu.x, state.cpu.y,
	            state.cpu.sp, state.cpu.status, flags);
	libmse_logf("cyc %llu  scanline %d  dot %d  frame %llu%s",
	            (unsigned long long)state.cpu.cycles, state.ppu.scanline, state.ppu.dot,
	            (unsigned long long)state.ppu.frame,
	            state.cpu.nmi_pending ? "  NMI pending" : "");
	libmse_logf("  $%04X  %s", state.cpu.pc, disasm);
	return true;
}

static bool cmd_disasm_handler(int argc, const char **argv)
{
	cnes_debug_state_t state;
	if (!cnes_debug_get_state(&state) || !state.valid) {
		libmse_log("cnes: no ROM running");
		return false;
	}

	// No address given means "where execution actually is", which is what you
	// want immediately after a breakpoint or a step.
	uint32_t address = state.cpu.pc;
	if (argc > 0 && !cnes_parse_address(argv[0], &address)) {
		libmse_log("usage: cnes_disasm [address] [count]");
		return false;
	}

	int count = 16;
	if (argc > 1 && !cnes_parse_count(argv[1], &count)) {
		libmse_log("usage: cnes_disasm [address] [count]");
		return false;
	}
	if (count > 256) {
		count = 256;
	}

	cnes_debug_insn_t insns[256];
	size_t got = cnes_debug_disassemble_range((uint16_t)address, insns, (size_t)count);

	for (size_t i = 0; i < got; ++i) {
		const cnes_debug_insn_t *insn = &insns[i];

		char bytes[12] = {0};
		for (uint8_t b = 0; b < insn->length; ++b) {
			snprintf(bytes + b * 3, sizeof(bytes) - b * 3, "%02X ", insn->bytes[b]);
		}

		// The separator goes with the operand rather than before it, so an
		// implied instruction does not print a trailing space.
		libmse_logf("%s$%04X  %-9s %s%s%s%s",
		            insn->address == state.cpu.pc ? ">" : " ",
		            insn->address, bytes, insn->mnemonic,
		            insn->operand[0] != '\0' ? " " : "", insn->operand,
		            (insn->flags & CNES_DEBUG_INSN_ILLEGAL) ? "   ; undocumented" : "");
	}

	return got > 0;
}

static bool cmd_peek_handler(int argc, const char **argv)
{
	cnes_debug_space_t space;
	uint32_t           address = 0;

	if (argc < 2 || !cnes_parse_space(argv[0], &space) || !cnes_parse_address(argv[1], &address)) {
		libmse_log("usage: cnes_peek <cpu|ram|ppu|oam|palette|chr|prg> <address> [length]");
		return false;
	}

	int length = 16;
	if (argc > 2 && !cnes_parse_count(argv[2], &length)) {
		libmse_log("usage: cnes_peek <space> <address> [length]");
		return false;
	}
	if (length > 256) {
		length = 256;
	}

	uint8_t data[256];
	size_t  got = cnes_debug_read(space, address, data, (size_t)length);
	if (got == 0) {
		libmse_log("cnes: nothing to read there");
		return false;
	}

	for (size_t offset = 0; offset < got; offset += 16) {
		char line[80];
		int  written = snprintf(line, sizeof(line), "$%04X ", (unsigned)(address + offset));

		for (size_t i = 0; i < 16 && offset + i < got; ++i) {
			written += snprintf(line + written, sizeof(line) - (size_t)written,
			                    "%s%02X", (i % 8 == 0) ? "  " : " ", data[offset + i]);
		}
		libmse_log(line);
	}

	return true;
}

static bool cmd_poke_handler(int argc, const char **argv)
{
	cnes_debug_space_t space;
	uint32_t           address = 0;
	uint32_t           value   = 0;

	if (argc < 3 || !cnes_parse_space(argv[0], &space) ||
	    !cnes_parse_address(argv[1], &address) || !cnes_parse_address(argv[2], &value) ||
	    value > 0xFF) {
		libmse_log("usage: cnes_poke <cpu|ram|oam|palette> <address> <byte>");
		return false;
	}

	if (!cnes_debug_write(space, address, (uint8_t)value)) {
		// Only the spaces that can be written without side effects are allowed;
		// see cnes_debug_write.
		libmse_logf("cnes: cannot write $%02X to %s $%04X",
		            (unsigned)value, argv[0], (unsigned)address);
		return false;
	}

	libmse_logf("cnes: %s $%04X = $%02X", argv[0], (unsigned)address, (unsigned)value);
	return true;
}

static bool cmd_sram_save_handler(int argc, const char **argv)
{
	if (argc < 1) {
		libmse_log("usage: cnes_sram_save <path>");
		return false;
	}

	bool ok = false;

	// Written under the lock, so the file cannot catch a half-finished write
	// from the emulation thread. 8KB, so the stall is not worth avoiding.
	NES *nes = cnes_backend_lock_nes();
	if (nes != NULL) {
		size_t   size = 0;
		uint8_t *ram  = NES_GetCartridgeRam(nes, &size);

		if (ram != NULL && size > 0) {
			FILE *file = fopen(argv[0], "wb");
			if (file != NULL) {
				ok = fwrite(ram, 1, size, file) == size;
				fclose(file);
				if (ok) {
					NES_ClearCartridgeRamDirty(nes);
				}
			}
		} else {
			libmse_log("cnes: this cartridge has no RAM to save");
		}
	}
	cnes_backend_unlock_nes();

	if (ok) {
		libmse_logf("cnes: saved cartridge RAM to %s", argv[0]);
	} else {
		DEBUG_ERROR("cnes: could not save cartridge RAM to %s", argv[0]);
	}
	return ok;
}

static bool cmd_sram_load_handler(int argc, const char **argv)
{
	if (argc < 1) {
		libmse_log("usage: cnes_sram_load <path>");
		return false;
	}

	bool ok = false;

	NES *nes = cnes_backend_lock_nes();
	if (nes != NULL) {
		size_t   size = 0;
		uint8_t *ram  = NES_GetCartridgeRam(nes, &size);

		if (ram != NULL && size > 0) {
			FILE *file = fopen(argv[0], "rb");
			if (file != NULL) {
				// A short file fills what it can and leaves the rest alone,
				// which is what lets a save from a smaller RAM still load.
				size_t read = fread(ram, 1, size, file);
				fclose(file);
				ok = read > 0;
				if (ok) {
					NES_ClearCartridgeRamDirty(nes);
					libmse_logf("cnes: loaded %zu bytes of cartridge RAM from %s", read, argv[0]);
				}
			}
		} else {
			libmse_log("cnes: this cartridge has no RAM to load into");
		}
	}
	cnes_backend_unlock_nes();

	if (!ok) {
		DEBUG_ERROR("cnes: could not load cartridge RAM from %s", argv[0]);
	}
	return ok;
}

static bool cmd_tas_play_handler(int argc, const char **argv)
{
	if (argc < 1) {
		libmse_log("usage: cnes_tas_play <file.fm2>");
		return false;
	}

	bool ok = false;

	NES *nes = cnes_backend_lock_nes();
	if (nes != NULL && nes->tas != NULL) {
		ok = TAS_Load(nes->tas, argv[0]);
		if (ok) {
			// A movie is only reproducible from a known state, and the state a
			// .fm2 assumes is power-on.
			NES_Reset(nes);
			nes->tas->playback_frame = 0;
			libmse_logf("cnes: playing %s (%zu frames)", argv[0], TAS_GetTotalFrames(nes->tas));
		}
	}
	cnes_backend_unlock_nes();

	if (!ok) {
		DEBUG_ERROR("cnes: could not load TAS %s", argv[0]);
	}
	return ok;
}

static const char *cnes_cheat_kind_name(const cnes_cheat_t *cheat)
{
	return (cheat->kind == CNES_CHEAT_CARTRIDGE) ? "cart" : "system";
}

static bool cmd_cheat_add_handler(int argc, const char **argv)
{
	if (argc < 1) {
		libmse_log("usage: cnes_cheat_add <code> [description]");
		libmse_log("  code: Game Genie (SXIOPO), Pro Action Replay (0010FF00),");
		libmse_log("        or raw AAAA:VV / AAAA?CC:VV");
		return false;
	}

	const int index = cnes_cheat_add(argv[0], (argc > 1) ? argv[1] : NULL);
	if (index < 0) {
		libmse_logf("cnes: '%s' is not a code this understands, or the list is full", argv[0]);
		return false;
	}

	cnes_cheat_t cheat;
	cnes_cheat_get((size_t)index, &cheat);

	libmse_logf("cnes: cheat %d  %s  $%04X = $%02X  (%s)", index, cheat.code,
	            cheat.address, cheat.value, cnes_cheat_kind_name(&cheat));
	return true;
}

static bool cmd_cheat_list_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	const size_t count = cnes_cheat_count();
	if (count == 0) {
		libmse_log("cnes: no cheats");
		return true;
	}

	for (size_t i = 0; i < count; ++i) {
		cnes_cheat_t cheat;
		if (!cnes_cheat_get(i, &cheat)) {
			continue;
		}

		char compare[16] = "";
		if (cheat.has_compare) {
			snprintf(compare, sizeof(compare), " if $%02X", cheat.compare);
		}

		libmse_logf("%2zu  [%s] %-10s $%04X = $%02X%s  %s  %s", i,
		            cheat.enabled ? "x" : " ", cheat.code, cheat.address, cheat.value,
		            compare, cnes_cheat_kind_name(&cheat), cheat.name);
	}
	return true;
}

static bool cmd_cheat_toggle_handler(int argc, const char **argv)
{
	int index = 0;
	if (argc < 1 || !cnes_parse_count(argv[0], &index)) {
		// parse_count rejects zero, and zero is a valid index here.
		if (argc < 1 || strcmp(argv[0], "0") != 0) {
			libmse_log("usage: cnes_cheat_toggle <index>");
			return false;
		}
		index = 0;
	}

	cnes_cheat_t cheat;
	if (!cnes_cheat_get((size_t)index, &cheat)) {
		libmse_logf("cnes: no cheat %d", index);
		return false;
	}

	cnes_cheat_set_enabled((size_t)index, !cheat.enabled);
	libmse_logf("cnes: cheat %d %s", index, cheat.enabled ? "disabled" : "enabled");
	return true;
}

static bool cmd_cheat_remove_handler(int argc, const char **argv)
{
	int index = 0;
	if (argc < 1 || (!cnes_parse_count(argv[0], &index) && strcmp(argv[0], "0") != 0)) {
		libmse_log("usage: cnes_cheat_remove <index>");
		return false;
	}

	if (!cnes_cheat_remove((size_t)index)) {
		libmse_logf("cnes: no cheat %d", index);
		return false;
	}

	libmse_logf("cnes: removed cheat %d", index);
	return true;
}

static bool cmd_cheat_clear_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	cnes_cheat_clear();
	libmse_log("cnes: cheats cleared");
	return true;
}

static bool cmd_tas_stop_handler(int argc, const char **argv)
{
	(void)argc;
	(void)argv;

	NES *nes = cnes_backend_lock_nes();
	if (nes != NULL && nes->tas != NULL) {
		// Winding playback to the end is how a TAS reports itself finished, and
		// it makes TAS_ApplyFrame release the controllers rather than holding
		// whatever the last frame pressed.
		nes->tas->playback_frame = nes->tas->frame_count;
	}
	cnes_backend_unlock_nes();

	libmse_log("cnes: TAS playback stopped");
	return true;
}

bool cmd_load_palette_handler(int argc, const char** argv) {
	bool ok = false;

	cnes_nes_lock();
	if (g_nes && argc > 0 && argv[0]) {
		uint32_t *new_palette = PALETTE_Load(argv[0]);
		if (new_palette) {
			PPU_SetPalette(g_nes->ppu, new_palette);
			PALETTE_Destroy(new_palette);
			ok = true;
		}
	}
	cnes_nes_unlock();

	return ok;
}

static void register_cvars(void)
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
	// actuation_threshold is a float. Registering it as LIBMSE_CVAR_INT had the
	// cvar system reading and writing an int through a float*.
	libmse_cvar_register("cnes_input_threshold", LIBMSE_CVAR_FLOAT, &g_nes->settings.input.actuation_threshold, "Input actuation threshold (for analog inputs, 0 to 1)");
}

static void register_cmds(void)
{
	libmse_cmd_register(&(libmse_cmd_t){"cnes_reset", "Resets the NES emulator state", 0, cmd_reset_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_load_palette", "Loads a palette from the specified path", 1, cmd_load_palette_handler});

	// Execution control
	libmse_cmd_register(&(libmse_cmd_t){"cnes_pause", "Halts emulation", 0, cmd_pause_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_resume", "Resumes emulation", 0, cmd_resume_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_step", "Runs N instructions and pauses (default 1)", 0, cmd_step_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_step_frame", "Runs N frames and pauses (default 1)", 0, cmd_step_frame_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_break", "Pauses when the PC reaches a hex address", 1, cmd_break_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_break_clear", "Removes every breakpoint", 0, cmd_break_clear_handler});

	// Inspection
	libmse_cmd_register(&(libmse_cmd_t){"cnes_regs", "Prints CPU registers, flags and the instruction at the PC", 0, cmd_regs_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_disasm", "Disassembles [address] [count], defaulting to the PC", 0, cmd_disasm_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_peek", "Hex-dumps <space> <address> [length]", 2, cmd_peek_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_poke", "Writes <space> <address> <byte>", 3, cmd_poke_handler});

	// Cartridge RAM and movie playback
	libmse_cmd_register(&(libmse_cmd_t){"cnes_sram_save", "Writes cartridge RAM to a file", 1, cmd_sram_save_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_sram_load", "Reads cartridge RAM from a file", 1, cmd_sram_load_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_tas_play", "Plays an FCEUX .fm2 movie from power-on", 1, cmd_tas_play_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_tas_stop", "Stops movie playback and releases the controllers", 0, cmd_tas_stop_handler});

	// Cheats
	libmse_cmd_register(&(libmse_cmd_t){"cnes_cheat_add", "Adds a Game Genie, Pro Action Replay or raw code", 1, cmd_cheat_add_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_cheat_list", "Lists the active cheats", 0, cmd_cheat_list_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_cheat_toggle", "Enables or disables a cheat by index", 1, cmd_cheat_toggle_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_cheat_remove", "Removes a cheat by index", 1, cmd_cheat_remove_handler});
	libmse_cmd_register(&(libmse_cmd_t){"cnes_cheat_clear", "Removes every cheat", 0, cmd_cheat_clear_handler});
	//libmse_cmd_register(&(libmse_cmd_t){"cnes_load_rom", "Loads a ROM from the specified path", 1, (const libmse_cmd_type_t[]){LIBMSE_CMD_STRING}, cmd_load_rom_handler});
}

LIBMSE_API bool init(void)
{
	g_nes = NES_Create();

	if (g_nes == NULL) return false;

	//libmse_lua_load_script("cnes/data/lua/cnes_backend.lua");

	if (!cnes_frames_create()) {
		DEBUG_ERROR("Failed to allocate video frame buffers");
		NES_Destroy(g_nes);
		g_nes = NULL;
		return false;
	}
	PPU_SetOutputBuffers(g_nes->ppu, g_frame_slots[g_frame_render_slot], NULL);

	register_cvars();
	register_cmds();

    // Initialize Audio Worker
    g_audio_thread_running = true;
    g_audio_event = mse_event_create();
    pthread_create(&g_audio_thread, NULL, audio_worker_thread, NULL);
	cnes_set_thread_name(g_audio_thread, "cNES_AudioWorker");

	return true;
}

LIBMSE_API bool load_rom(const uint8_t *data, size_t size)
{
	if (data == NULL || size == 0) {
		return false;
	}

	// ROM_LoadMemory already takes its own copy of the image (see
	// INES_LoadFromBuffer), and ROM_Destroy frees it. The module-level copy this
	// used to keep was therefore redundant, and worse: freeing it at the top of
	// the next load left bus->prgRomData pointing into released memory while the
	// emulation thread was still reading through it.
	ROM *rom = ROM_LoadMemory((uint8_t *)data, size);
	if (rom == NULL) {
		return false;
	}

	cnes_nes_lock();
	bool ok = (g_nes != NULL) && (NES_Load(g_nes, rom) == 0);
	cnes_nes_unlock();

	if (!ok) {
		// NES_Load hands ownership back on failure.
		ROM_Destroy(rom);
		return false;
	}

	// ROM patches point into the PRG image the load just replaced, so they have
	// to be laid down again. Outside the lock above because it takes its own.
	cnes_cheat_reapply();

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

	// ROM_LoadMemory copies the image, so this staging buffer can go immediately.
	bool ok = load_rom(buf, (size_t)sz);
	free(buf);

	if (ok) {
		free(g_active_rom_path);
		g_active_rom_path = strdup(path);
	}

	return ok;
}

LIBMSE_API void shutdown(void)
{
    g_audio_thread_running = false;
    pthread_join(g_audio_thread, NULL);

	// Both workers are joined by now, so the only remaining contender for
	// g_nes is a host thread calling in.
	cnes_nes_lock();
	if (g_nes != NULL) {
		NES_Destroy(g_nes);
		g_nes = NULL;
	}
	cnes_nes_unlock();
	free(g_active_rom_path);
	g_active_rom_path = NULL;

	// After the NES is gone, so the emulation thread can no longer be publishing.
	cnes_frames_destroy();
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

// Parameter named for what it is: the per-frame axis values, not the `inputs`
// descriptor table declared at the top of this file, which it used to shadow.
// Hands the frontend the most recent finished frame. Runs on the frontend's
// thread while the emulator is still running on ours, hence the mutex; it only
// rotates two pointers, so the emulation thread is never held up for long.
LIBMSE_API bool get_frame(mse_frame_t *frame)
{
	if (frame == NULL) {
		return false;
	}

	if (!atomic_load_explicit(&g_frame_seen, memory_order_acquire)) {
		return false; // Nothing rendered yet
	}

	bool is_new = cnes_frame_collect();

	frame->width       = CNES_FRAME_WIDTH;
	frame->height      = CNES_FRAME_HEIGHT;
	frame->pitch       = CNES_FRAME_WIDTH * (uint32_t)sizeof(uint32_t);
	// The PPU writes 0xAABBGGRR words; on a little-endian host that is R,G,B,A
	// in memory. Declaring it rather than swizzling keeps this a pointer swap.
	frame->format      = MSE_FRAME_FORMAT_RGBA8;
	frame->pixels      = (const uint8_t *)g_frame_slots[g_frame_publish_slot];
	frame->pixels_size = (size_t)CNES_FRAME_BYTES;
	frame->ready       = is_new;

	return true;
}

LIBMSE_API void update_inputs(const float *input_states)
{
	// Just publish the bits; the emulation thread applies them when it starts
	// its next frame. No lock, so a busy emulator cannot stall the UI thread.
	atomic_store_explicit(&g_controller_state[0], pack_nes_controller(input_states),
	                      memory_order_relaxed);
}

// Monotonic where the platform offers it, so adjusting the system clock cannot
// make a frame appear to take a negative amount of time.
static uint64_t cnes_now_ns(void)
{
#if defined(CLOCK_MONOTONIC)
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
	}
#endif
	struct timespec fallback;
	timespec_get(&fallback, TIME_UTC);
	return (uint64_t)fallback.tv_sec * 1000000000ull + (uint64_t)fallback.tv_nsec;
}

// Waits until deadline_ns, or until the stop event fires. Returns true if we
// are shutting down.
//
// This used to be a bare spin on timespec_get, which burned a whole core for
// the idle part of every frame and could not notice the stop event until the
// frame was over. Sleep for the bulk of it and spin only the last fraction of a
// millisecond, which is as fine as the OS timer goes anyway.
#define CNES_SPIN_MARGIN_NS 1500000ull // 1.5ms

static bool cnes_wait_until(mse_event_t *stop_event, uint64_t deadline_ns)
{
	for (;;) {
		uint64_t now = cnes_now_ns();
		if (now >= deadline_ns) {
			return false;
		}

		uint64_t remaining = deadline_ns - now;
		if (remaining <= CNES_SPIN_MARGIN_NS) {
			continue; // Final sliver: spin for accuracy
		}

		uint32_t sleep_ms = (uint32_t)((remaining - CNES_SPIN_MARGIN_NS) / 1000000ull);
		if (sleep_ms == 0) {
			continue;
		}
		if (mse_event_wait_timeout(stop_event, sleep_ms)) {
			return true; // Stop requested; do not finish the nap
		}
	}
}

LIBMSE_API void start(mse_event_t *stop_event)
{
	if (g_nes == NULL || stop_event == NULL || g_frame_slots[0] == NULL) {
		return;
	}

	uint64_t next_frame_ns = cnes_now_ns();

	libmse_profiler_thread_name("cnes emulation");

	while (!mse_event_wait_timeout(stop_event, 0)) {
		// At the top rather than after publishing, so one profiler frame spans
		// a whole emulated frame including the throttle that paces it. The
		// first call publishes nothing, which costs one empty row once.
		libmse_profiler_frame();

		// Let any pending load/reset/shutdown take the NES before we grab it
		// again. Releasing and immediately reacquiring an unfair mutex can
		// starve the other side indefinitely.
		LIBMSE_PROFILE_START("yield to waiters");
		while (atomic_load_explicit(&g_nes_waiters, memory_order_acquire) > 0) {
			if (mse_event_wait_timeout(stop_event, 1)) {
				return;
			}
		}
		LIBMSE_PROFILE_END();

		LIBMSE_PROFILE_START("lock");
		cnes_nes_lock();
		LIBMSE_PROFILE_END();
		if (g_nes == NULL) {
			cnes_nes_unlock();
			break;
		}

		NES_SetController(g_nes, 0, atomic_load_explicit(&g_controller_state[0], memory_order_relaxed));
		NES_SetController(g_nes, 1, atomic_load_explicit(&g_controller_state[1], memory_order_relaxed));

		// What this iteration owes: whole frames, loose instructions, or
		// nothing at all because the console has stopped us.
		int instructions = atomic_exchange_explicit(&g_pending_instructions, 0, memory_order_acquire);
		int frames       = atomic_load_explicit(&g_pending_frames, memory_order_acquire);
		const bool paused = atomic_load_explicit(&g_paused, memory_order_acquire);

		if (paused && instructions == 0 && frames == 0) {
			// Idle, but still releasing the lock every time round so a memory
			// read or a ROM swap is not locked out for as long as we are.
			cnes_nes_unlock();
			if (mse_event_wait_timeout(stop_event, 4)) {
				return;
			}
			continue;
		}

		bool breakpoint = false;

		LIBMSE_PROFILE_START("step frame");
		if (instructions > 0) {
			for (int i = 0; i < instructions && !breakpoint; ++i) {
				NES_Step(g_nes);
				breakpoint = cnes_breakpoint_hit(g_nes->cpu->pc);
			}
		} else {
			// One frame per iteration even when several are owed, so the
			// throttle below still paces them and a long step stays watchable.
			if (atomic_load_explicit(&g_breakpoint_count, memory_order_relaxed) > 0) {
				breakpoint = cnes_step_frame_checked();
			} else {
				NES_StepFrame(g_nes);
			}
			if (frames > 0) {
				atomic_fetch_sub_explicit(&g_pending_frames, 1, memory_order_release);
			}
		}
		LIBMSE_PROFILE_END();

		LIBMSE_PROFILE_START("publish");
		cnes_frame_publish();
		LIBMSE_PROFILE_END();

		const uint16_t pc = g_nes->cpu->pc;
		float frame_time_ms = g_nes->settings.frame_time;
		cnes_nes_unlock();

		if (breakpoint) {
			// Reported outside the lock: libmse_logf takes its own, and taking
			// two locks in one order here and the other order in a command
			// handler is how deadlocks get written.
			cnes_pause(NULL);
			libmse_logf("cnes: breakpoint at $%04X", pc);
			continue;
		}

		// Instruction stepping skips the frame pacing below. Waiting 16ms after
		// a single instruction would make stepping through a routine feel
		// broken, and nothing is being displayed at that rate anyway.
		if (instructions > 0) {
			continue;
		}

		// A frame time of zero (or less) means run unthrottled.
		if (frame_time_ms <= 0.0f) {
			next_frame_ns = cnes_now_ns();
			continue;
		}

		const uint64_t period_ns = (uint64_t)(frame_time_ms * 1000000.0f);

		// Advance the deadline by exactly one period rather than measuring from
		// the start of this frame, so rounding does not accumulate into drift.
		next_frame_ns += period_ns;

		uint64_t now = cnes_now_ns();
		if (now > next_frame_ns + period_ns) {
			// Fell badly behind (a breakpoint, a stall, a ROM swap). Resync
			// instead of sprinting to catch up.
			next_frame_ns = now;
			continue;
		}

		// The wait is captured rather than returned on directly, so the zone
		// closes before the thread leaves.
		LIBMSE_PROFILE_START("throttle");
		const bool stop_requested = cnes_wait_until(stop_event, next_frame_ns);
		LIBMSE_PROFILE_END();

		if (stop_requested) {
			return;
		}
	}
}

// Probably need threading
static bool metadata_lua_loaded = false;

LIBMSE_API bool library_meta_handler(const char *rom_path, libmse_game_meta_t *out_meta, void *user_data) {
    // 1. Get the worker instance (adjust if you pass this via user_data instead)
    libmse_lua_worker_t *worker = libmse_lua_get_default_worker();

	if (!metadata_lua_loaded) {
		libmse_lua_worker_execute_script(worker, "cnes/data/lua/matcher.lua");
		libmse_log("Loaded matcher.lua for metadata matching.\n");
		metadata_lua_loaded = true;
	}
	
    if (!worker || !worker->L) {
        DEBUG_ERROR("Error: Lua worker not available.\n");
        return false;
    }
    
    lua_State *L = worker->L;
    
    // 2. Fetch the "match_rom" function we wrote in matcher.lua
    lua_getglobal(L, "match_rom");
    if (!lua_isfunction(L, -1)) {
        DEBUG_ERROR("Error: match_rom Lua function not found!\n");
        lua_pop(L, 1);
        return false;
    }
    
    // 3. Push the rom_path argument
    lua_pushstring(L, rom_path);
    
    // 4. Call Lua function: 1 Argument, 3 Expected Results (name, year, artwork)
    if (lua_pcall(L, 1, 3, 0)) {
        DEBUG_ERROR("Lua Execution Error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    
    // Stack contains: -3 = name, -2 = release_year, -1 = artwork_data (or nil)
    bool successfully_matched = false;
    
    // Read Game Name (-3)
    if (lua_isstring(L, -3)) {
        out_meta->name = strdup(lua_tostring(L, -3));
        successfully_matched = true;
    }
    
    // Read Release Year (-2)
    if (lua_isnumber(L, -2)) {
        out_meta->release_year = (int)lua_tointeger(L, -2);
    }
    
    // Read Artwork Binary Blob (-1)
    if (lua_isstring(L, -1)) {
        size_t artwork_len = 0;
        const char *art_buffer = lua_tolstring(L, -1, &artwork_len);
        
        if (artwork_len > 0) {
            out_meta->artwork_data = malloc(artwork_len);
            if (out_meta->artwork_data) {
                memcpy(out_meta->artwork_data, art_buffer, artwork_len);
                out_meta->artwork_size = artwork_len;
            }
        }
    }
    
    // Pop the 3 results off the stack to keep it clean
    lua_pop(L, 3);
    
    return successfully_matched;
}

//LIBMSE_API mse_gfx_texture_t* get_texture(void)
//{
//	return g_texture;
//}
