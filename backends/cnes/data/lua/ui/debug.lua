-- FFI binding to the cNES debug API (backends/cnes/include/cNES/external/cpu_debug.h).
--
-- The emulator runs on its own thread behind a lock, so nothing here reaches
-- into its structs: the C side snapshots state and copies memory out under that
-- lock. Taking it makes the emulation thread stand aside for a moment, so
-- panels should pull a whole region once per frame rather than a byte at a
-- time, and share what they pull through this module's cache.

local ffi = require("ffi")

-- Must match cpu_debug.h. The abi_version field is checked below, so a struct
-- that has changed underneath us is reported rather than silently misread.
ffi.cdef [[
typedef struct {
	uint16_t pc;
	uint8_t  a, x, y, sp, status;
	uint64_t cycles;
	uint8_t  nmi_pending;
	uint8_t  _pad[7];
} cnes_debug_cpu_t;

typedef struct {
	int32_t  scanline;
	int32_t  dot;
	uint64_t frame;
	uint16_t v, t;
	uint8_t  x, w;
	uint8_t  ctrl, mask, status, oam_addr;
	uint8_t  data_buffer;
	uint8_t  _pad[3];
} cnes_debug_ppu_t;

typedef struct {
	uint32_t abi_version;
	uint32_t valid;
	cnes_debug_cpu_t cpu;
	cnes_debug_ppu_t ppu;
	uint32_t mapper;
	uint32_t prg_size;
	uint32_t chr_size;
	uint32_t mirroring;
} cnes_debug_state_t;

typedef struct {
	uint16_t address;
	uint16_t target;
	uint16_t effective;
	uint8_t  value;
	uint8_t  length;
	uint8_t  bytes[3];
	uint8_t  mode;
	uint8_t  flags;
	uint8_t  cycles;
	char     mnemonic[5];
	char     operand[16];
	const char *description;
} cnes_debug_insn_t;

int      cnes_debug_get_state(cnes_debug_state_t *out);
uint8_t  cnes_debug_controller(uint32_t port);
size_t   cnes_debug_read(int space, uint32_t address, uint8_t *dst, size_t len);
size_t   cnes_debug_space_size(int space);
int      cnes_debug_write(int space, uint32_t address, uint8_t value);
size_t   cnes_debug_disassemble(uint16_t address, char *buf, size_t buf_size);
size_t   cnes_debug_disassemble_range(uint16_t address, cnes_debug_insn_t *out, size_t count);
uint16_t cnes_debug_disassemble_rewind(uint16_t address, uint32_t count);

size_t   cnes_debug_render_pattern_table(uint32_t table, uint32_t palette,
                                         uint32_t *out, size_t out_pixels);
size_t   cnes_debug_render_sprites(uint32_t *out, size_t out_pixels);
size_t   cnes_debug_render_nametable(uint32_t index, uint32_t *out, size_t out_pixels);

typedef struct {
	char     code[16];
	char     name[48];
	uint32_t kind;
	uint16_t address;
	uint8_t  value;
	uint8_t  compare;
	uint8_t  has_compare;
	uint8_t  enabled;
	uint8_t  _pad[2];
} cnes_cheat_t;

int    cnes_cheat_parse(const char *code, cnes_cheat_t *out);
int    cnes_cheat_add(const char *code, const char *name);
size_t cnes_cheat_count(void);
int    cnes_cheat_get(size_t index, cnes_cheat_t *out);
int    cnes_cheat_set_enabled(size_t index, int enabled);
int    cnes_cheat_remove(size_t index);
void   cnes_cheat_clear(void);

typedef struct {
	uint32_t loaded, playing, seeking, ports, pal, _pad;
	uint64_t frame, total, seek_target;
	char     path[260];
	char     rom_filename[128];
	char     rom_checksum[128];
} cnes_tas_state_t;

int    cnes_tas_get_state(cnes_tas_state_t *out);
size_t cnes_tas_read_frames(uint64_t first, uint8_t *out, size_t count);
size_t cnes_tas_list_movies(char *out, size_t stride, size_t max);
int    cnes_tas_load(const char *path);
int    cnes_tas_stop(void);
int    cnes_tas_restart(void);
int    cnes_tas_seek(uint64_t frame);
int    cnes_tas_cancel_seek(void);
void   cnes_tas_frame_advance(uint32_t frames);

/* The console's transport, from libmse_backend.h. Exported by the same DLL,
   and plain C either side, so a panel can drive it directly. */
void     backend_pause(void);
void     backend_resume(void);
int      backend_get_state(void);
void   cnes_cheat_reapply(void);
]]

local ABI_VERSION = 8

local M = {}

M.SPACE = {
	CPU     = 0,
	RAM     = 1,
	PPU     = 2,
	OAM     = 3,
	PALETTE = 4,
	CHR     = 5,
	PRG     = 6,
}

-- Bits in cnes_debug_insn_t.flags. See cpu_debug.h.
M.INSN = {
	ILLEGAL   = 0x01,
	BRANCH    = 0x02,
	JUMP      = 0x04,
	CALL      = 0x08,
	RETURN    = 0x10,
	STOP      = 0x20,
	TARGET    = 0x40,
	EFFECTIVE = 0x80,
}

-- cnes_debug_addrmode_t.
M.MODE = {
	IMP = 0, ACC = 1, IMM = 2, ZP  = 3, ZPX = 4, ZPY  = 5, REL = 6,
	ABS = 7, ABSX = 8, ABSY = 9, IND = 10, IZX = 11, IZY = 12,
}

-- The backend is already loaded into this process, but it lives beside its data
-- rather than on the library search path, so it has to be named explicitly.
local CANDIDATES = {
	"cnes/lib/windows/libemulator.dll",
	"cnes/lib/linux/libemulator.so",
	"libemulator",
}

local lib
local load_error

for _, candidate in ipairs(CANDIDATES) do
	local ok, result = pcall(ffi.load, candidate)
	if ok then
		lib = result
		break
	end
	load_error = result
end

M.available = lib ~= nil

if not M.available then
	M.error = "could not load the cNES backend for debugging: " .. tostring(load_error)
	return M
end

-- Reused between frames; these are hot paths called from draw().
local state_buf = ffi.new("cnes_debug_state_t")
local disasm_buf = ffi.new("char[64]")

local abi_checked = false

--- Returns the current snapshot, or nil if no ROM is running.
function M.state()
	if lib.cnes_debug_get_state(state_buf) == 0 then
		return nil
	end

	if not abi_checked then
		abi_checked = true
		if state_buf.abi_version ~= ABI_VERSION then
			M.error = string.format(
				"cNES debug ABI is version %d but these scripts expect %d - rebuild one or the other",
				tonumber(state_buf.abi_version), ABI_VERSION)
			M.available = false
		end
	end

	if not M.available then
		return nil
	end

	return state_buf
end

function M.space_size(space)
	return tonumber(lib.cnes_debug_space_size(space))
end

-- Buffers are kept per address space so repeated reads of the same region do
-- not reallocate, and so two panels reading different spaces do not fight.
local buffers = {}

--- Reads len bytes. Returns a uint8_t* and the number of bytes actually read.
function M.read(space, address, len)
	local buf = buffers[space]
	if not buf or buf.len < len then
		buf = { data = ffi.new("uint8_t[?]", len), len = len }
		buffers[space] = buf
	end

	local got = tonumber(lib.cnes_debug_read(space, address, buf.data, len))
	return buf.data, got
end

function M.write(space, address, value)
	return lib.cnes_debug_write(space, address, value) ~= 0
end

--- Disassembles one instruction. Returns the text and its length in bytes.
--
-- Takes the emulator lock for a single instruction, so a listing should use
-- disassemble_range instead of calling this in a loop.
function M.disassemble(address)
	local size = tonumber(lib.cnes_debug_disassemble(address, disasm_buf, ffi.sizeof(disasm_buf)))
	return ffi.string(disasm_buf), size
end

-- Grown on demand and kept between frames: a listing asks for the same number
-- of instructions every frame, so after the first this never reallocates.
local insn_buf, insn_capacity = nil, 0

--- Decodes `count` consecutive instructions from `address`, in one lock.
--
-- Returns a cnes_debug_insn_t[] and how many entries were filled, which is
-- short only at the top of the address space. The array is reused between
-- calls, so copy anything that has to outlive the next one.
function M.disassemble_range(address, count)
	if count <= 0 then
		return nil, 0
	end

	if count > insn_capacity then
		insn_buf = ffi.new("cnes_debug_insn_t[?]", count)
		insn_capacity = count
	end

	local got = tonumber(lib.cnes_debug_disassemble_range(address, insn_buf, count))
	return insn_buf, got
end

--- The address `count` instructions before `address`.
--
-- Nothing in a 6502 byte stream marks where an instruction starts, so this is
-- the backend's best reconstruction rather than a fact. See cpu_debug.h.
function M.rewind(address, count)
	return tonumber(lib.cnes_debug_disassemble_rewind(address, count))
end

--- Reads the three CPU vectors. Returns nil when no ROM is running.
function M.vectors()
	-- Its own buffer rather than M.read's: the vectors are read in the middle
	-- of panels that are already using the CPU-space buffer for something else.
	local buf = ffi.new("uint8_t[6]")
	if tonumber(lib.cnes_debug_read(M.SPACE.CPU, 0xFFFA, buf, 6)) < 6 then
		return nil
	end
	return {
		nmi   = buf[0] + buf[1] * 256,
		reset = buf[2] + buf[3] * 256,
		irq   = buf[4] + buf[5] * 256,
	}
end

-- Names for the addresses that mean something on every NES, so a listing can
-- say PPUADDR rather than $2006. Cartridge symbols would have to come from a
-- per-ROM symbol file; these are the ones that are always true.
local SYMBOLS = {
	[0x2000] = "PPUCTRL",  [0x2001] = "PPUMASK",  [0x2002] = "PPUSTATUS",
	[0x2003] = "OAMADDR",  [0x2004] = "OAMDATA",  [0x2005] = "PPUSCROLL",
	[0x2006] = "PPUADDR",  [0x2007] = "PPUDATA",

	[0x4000] = "SQ1_VOL",  [0x4001] = "SQ1_SWEEP", [0x4002] = "SQ1_LO",
	[0x4003] = "SQ1_HI",   [0x4004] = "SQ2_VOL",   [0x4005] = "SQ2_SWEEP",
	[0x4006] = "SQ2_LO",   [0x4007] = "SQ2_HI",    [0x4008] = "TRI_LINEAR",
	[0x400A] = "TRI_LO",   [0x400B] = "TRI_HI",    [0x400C] = "NOISE_VOL",
	[0x400E] = "NOISE_LO", [0x400F] = "NOISE_HI",  [0x4010] = "DMC_FREQ",
	[0x4011] = "DMC_RAW",  [0x4012] = "DMC_START", [0x4013] = "DMC_LEN",
	[0x4014] = "OAMDMA",   [0x4015] = "APUSTATUS", [0x4016] = "JOY1",
	[0x4017] = "JOY2",

	[0xFFFA] = "NMI_VEC",  [0xFFFC] = "RESET_VEC", [0xFFFE] = "IRQ_VEC",
}

--- The conventional name for an address, or nil.
function M.symbol(address)
	-- The eight PPU registers repeat every eight bytes up to $3FFF, and code
	-- does use the mirrors, so fold them down rather than miss them.
	if address >= 0x2000 and address <= 0x3FFF then
		return SYMBOLS[0x2000 + address % 8]
	end
	return SYMBOLS[address]
end

-- Rasterised PPU views. See cNES/external/ppu_debug.h for why the pixels are built in
-- C rather than here.
M.PATTERN_DIM    = 128
M.SPRITE_COLUMNS = 8
M.SPRITE_WIDTH   = 64
M.SPRITE_HEIGHT  = 128
M.NAMETABLE_WIDTH  = 256
M.NAMETABLE_HEIGHT = 240

-- One buffer per view, reused every frame. These are the largest allocations
-- the debug scripts make, and remaking them 60 times a second would be the
-- only garbage this module produces.
local pattern_pixels = ffi.new("uint32_t[?]", M.PATTERN_DIM * M.PATTERN_DIM)
local sprite_pixels  = ffi.new("uint32_t[?]", M.SPRITE_WIDTH * M.SPRITE_HEIGHT)

-- One buffer shared by all four nametables: each is rendered and uploaded in
-- turn, so they never need to exist at the same time.
local nametable_pixels = ffi.new("uint32_t[?]", M.NAMETABLE_WIDTH * M.NAMETABLE_HEIGHT)

--- Renders pattern table 0 or 1 through palette 0-7 (0-3 background, 4-7
--- sprite). Returns a uint32_t* of PATTERN_DIM^2 RGBA pixels, or nil.
function M.render_pattern_table(table_index, palette)
	local got = tonumber(lib.cnes_debug_render_pattern_table(
		table_index, palette, pattern_pixels, M.PATTERN_DIM * M.PATTERN_DIM))
	if got == 0 then
		return nil
	end
	return pattern_pixels
end

--- Renders all 64 OAM sprites as an 8x8 grid of 8x16 cells. Returns a
--- uint32_t* of SPRITE_WIDTH*SPRITE_HEIGHT RGBA pixels, or nil.
function M.render_sprites()
	local got = tonumber(lib.cnes_debug_render_sprites(
		sprite_pixels, M.SPRITE_WIDTH * M.SPRITE_HEIGHT))
	if got == 0 then
		return nil
	end
	return sprite_pixels
end

--- Renders nametable 0-3 as it would appear on screen. Returns a uint32_t* of
--- NAMETABLE_WIDTH*NAMETABLE_HEIGHT RGBA pixels, or nil.
function M.render_nametable(index)
	local got = tonumber(lib.cnes_debug_render_nametable(
		index, nametable_pixels, M.NAMETABLE_WIDTH * M.NAMETABLE_HEIGHT))
	if got == 0 then
		return nil
	end
	return nametable_pixels
end

-- Cheats. The list lives in the backend because the emulation thread applies
-- RAM codes every frame and ROM codes are patched into the PRG image.
M.CHEAT = { CARTRIDGE = 0, SYSTEM = 1 }

local cheat_buf = ffi.new("cnes_cheat_t")

--- Adds a code. Returns its index, or -1 if it does not parse or the list is
--- full. `name` is optional.
function M.cheat_add(code, name)
	return tonumber(lib.cnes_cheat_add(code, name or ""))
end

function M.cheat_count()
	return tonumber(lib.cnes_cheat_count())
end

--- Returns a cnes_cheat_t for `index`, or nil. The struct is reused between
--- calls, so read what you need before calling again.
function M.cheat_get(index)
	if lib.cnes_cheat_get(index, cheat_buf) == 0 then
		return nil
	end
	return {
		code        = ffi.string(cheat_buf.code),
		name        = ffi.string(cheat_buf.name),
		kind        = tonumber(cheat_buf.kind),
		address     = cheat_buf.address,
		value       = cheat_buf.value,
		compare     = cheat_buf.compare,
		has_compare = cheat_buf.has_compare,
		enabled     = cheat_buf.enabled,
	}
end

function M.cheat_set_enabled(index, enabled)
	return lib.cnes_cheat_set_enabled(index, enabled and 1 or 0) ~= 0
end

function M.cheat_remove(index)
	return lib.cnes_cheat_remove(index) ~= 0
end

function M.cheat_clear()
	lib.cnes_cheat_clear()
end

--- The buttons held on a port right now, as a CNES_TAS_BTN_* bitfield. Port is
--- 0 or 1.
function M.controller(port)
	return tonumber(lib.cnes_debug_controller(port or 0))
end

-- Movie playback. The player is core (cNES/tas.h); this is the locked view of
-- it, and a seek runs on the emulation thread rather than blocking a draw.

M.TAS_BUTTONS = { "A", "B", "S", "T", "U", "D", "L", "R" }

local tas_state_buf = ffi.new("cnes_tas_state_t[1]")

--- Snapshots movie playback, or nil if there is no console.
function M.tas_state()
	if lib.cnes_tas_get_state(tas_state_buf) == 0 then
		return nil
	end
	local s = tas_state_buf[0]
	return {
		loaded       = s.loaded ~= 0,
		playing      = s.playing ~= 0,
		seeking      = s.seeking ~= 0,
		ports        = tonumber(s.ports),
		pal          = s.pal ~= 0,
		frame        = tonumber(s.frame),
		total        = tonumber(s.total),
		seek_target  = tonumber(s.seek_target),
		path         = ffi.string(s.path),
		rom_filename = ffi.string(s.rom_filename),
		rom_checksum = ffi.string(s.rom_checksum),
	}
end

-- Sized for the input log window a panel can actually show at once; a movie is
-- 20k rows and pulling all of them every frame would be pointless work.
local TAS_ROW_MAX = 256
local tas_rows    = ffi.new("uint8_t[?]", TAS_ROW_MAX * 2)

--- Reads up to `count` movie rows from `first`. Returns the raw buffer and how
--- many rows it holds; row i (0-based) is buffer[i*2] and buffer[i*2+1].
function M.tas_read_frames(first, count)
	if count > TAS_ROW_MAX then
		count = TAS_ROW_MAX
	end
	local got = tonumber(lib.cnes_tas_read_frames(first, tas_rows, count))
	return tas_rows, got
end

local TAS_LIST_MAX    = 64
local TAS_LIST_STRIDE = 260
local tas_list_buf    = ffi.new("char[?]", TAS_LIST_MAX * TAS_LIST_STRIDE)

--- Lists the .fm2 files next to the executable, as a table of names.
function M.tas_list_movies()
	local got = tonumber(lib.cnes_tas_list_movies(tas_list_buf, TAS_LIST_STRIDE, TAS_LIST_MAX))
	local names = {}
	for i = 0, got - 1 do
		names[i + 1] = ffi.string(tas_list_buf + i * TAS_LIST_STRIDE)
	end
	return names
end

function M.tas_load(path)    return lib.cnes_tas_load(path) ~= 0 end
function M.tas_stop()        return lib.cnes_tas_stop() ~= 0 end
function M.tas_restart()     return lib.cnes_tas_restart() ~= 0 end
function M.tas_seek(frame)   return lib.cnes_tas_seek(frame) ~= 0 end
function M.tas_cancel_seek() return lib.cnes_tas_cancel_seek() ~= 0 end
function M.tas_frame_advance(frames) lib.cnes_tas_frame_advance(frames or 1) end

M.STATE_STOPPED = 0
M.STATE_RUNNING = 1
M.STATE_PAUSED  = 2

function M.backend_state()  return tonumber(lib.backend_get_state()) end
function M.backend_pause()  lib.backend_pause() end
function M.backend_resume() lib.backend_resume() end

--- Decodes the CPU status byte into the conventional NV-BDIZC string.
function M.flags_string(status)
	local names = { "N", "V", "-", "B", "D", "I", "Z", "C" }
	local out = {}
	for i = 1, 8 do
		local bit_set = bit.band(status, bit.lshift(1, 8 - i)) ~= 0
		out[i] = bit_set and names[i] or names[i]:lower()
	end
	return table.concat(out)
end

return M
