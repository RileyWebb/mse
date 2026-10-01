#ifndef CNES_DEBUG_API_H
#define CNES_DEBUG_API_H

// Read-only view of the emulator for debug UI.
//
// The emulator runs on its own thread behind a mutex, so UI code cannot walk
// the NES structs directly: a ROM swap frees the memory the bus is reading
// through, and a half-updated CPU reads as nonsense. Everything here is
// snapshotted or copied under that same lock.
//
// This is a plain C ABI on purpose. The frontend's Lua reaches it through FFI,
// which means no lua_State crosses the DLL boundary and the layout below is the
// only thing the two sides have to agree on.
//
// Layout is part of the ABI. Add fields at the end and bump
// CNES_DEBUG_ABI_VERSION; the Lua side checks it before trusting the struct.

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// The only consumer is the frontend's Lua reaching in through FFI, which
// resolves by symbol name and never includes this header, so these are always
// exported rather than switching on an EXPORTS macro.
#ifndef CNES_DEBUG_API
	#if defined(_WIN32)
		#define CNES_DEBUG_API __declspec(dllexport)
	#else
		#define CNES_DEBUG_API __attribute__((visibility("default")))
	#endif
#endif

#define CNES_DEBUG_ABI_VERSION 8u

// Address spaces that can be read in bulk.
typedef enum cnes_debug_space_e {
	CNES_DEBUG_SPACE_CPU = 0, // $0000-$FFFF as the CPU sees it
	CNES_DEBUG_SPACE_RAM,     // the 2KB of internal RAM, unmirrored
	CNES_DEBUG_SPACE_PPU,     // $0000-$3FFF as the PPU sees it
	CNES_DEBUG_SPACE_OAM,     // 256 bytes of primary OAM
	CNES_DEBUG_SPACE_PALETTE, // 32 bytes of palette RAM
	CNES_DEBUG_SPACE_CHR,     // pattern data, as mapped
	CNES_DEBUG_SPACE_PRG,     // PRG ROM, unmapped and whole
	CNES_DEBUG_SPACE_COUNT
} cnes_debug_space_t;

typedef struct cnes_debug_cpu_s {
	uint16_t pc;
	uint8_t  a, x, y, sp, status;
	uint64_t cycles;
	uint8_t  nmi_pending;
	uint8_t  _pad[7];
} cnes_debug_cpu_t;

typedef struct cnes_debug_ppu_s {
	int32_t  scanline;   // -1/261 pre-render, 0-239 visible, 241+ vblank
	int32_t  dot;        // 0-340
	uint64_t frame;
	uint16_t v, t;       // current and temporary VRAM addresses
	uint8_t  x, w;       // fine X, write latch
	uint8_t  ctrl, mask, status, oam_addr;
	uint8_t  data_buffer; // the $2007 read buffer, which lags reads by one
	uint8_t  _pad[3];
} cnes_debug_ppu_t;

typedef struct cnes_debug_state_s {
	uint32_t abi_version;
	uint32_t valid;      // 0 when no ROM is loaded; the rest is then stale

	cnes_debug_cpu_t cpu;
	cnes_debug_ppu_t ppu;

	uint32_t mapper;
	uint32_t prg_size;
	uint32_t chr_size;
	uint32_t mirroring;  // as the ROM/mapper currently reports it
} cnes_debug_state_t;

// Fills *out with a consistent snapshot. Returns false if the emulator is not
// running or out is NULL. Cheap enough to call once per UI frame.
CNES_DEBUG_API int cnes_debug_get_state(cnes_debug_state_t *out);

// The buttons held on `port` (0 or 1) right now, in the order the controller
// shifts them out: bit 0 A, bit 1 B, bit 2 Select, bit 3 Start, then Up, Down,
// Left, Right. The same byte an FM2 row decodes to, so a live pad and a movie
// row can be drawn by the same code.
CNES_DEBUG_API uint8_t cnes_debug_controller(uint32_t port);

// Copies len bytes from an address space into dst, under the same lock.
// Returns the number of bytes actually copied, which is short at the end of a
// space and zero for an unknown one. Reads are side-effect free: ports that
// change state when the CPU reads them are read from their backing value
// instead, so opening a memory viewer cannot alter what the ROM sees.
CNES_DEBUG_API size_t cnes_debug_read(cnes_debug_space_t space, uint32_t address, uint8_t *dst, size_t len);

// Size of an address space in bytes, or 0 if unavailable.
CNES_DEBUG_API size_t cnes_debug_space_size(cnes_debug_space_t space);

// Writes a single byte into an address space. Returns non-zero on success.
// Intended for poking values from a memory viewer.
//
// Only the parts that are plain memory can be written: internal RAM, OAM,
// palette, and nametable RAM through the PPU space. Registers and pattern
// memory are refused -- writing a register would fire the side effects this
// API exists to avoid, and whether CHR is writable at all is the mapper's
// business.
CNES_DEBUG_API int cnes_debug_write(cnes_debug_space_t space, uint32_t address, uint8_t value);

// Disassembles one instruction at address, writing text into buf. Returns the
// instruction length in bytes so a caller can walk forward.
//
// Convenience only: it takes the lock for a single instruction. A listing
// should use cnes_debug_disassemble_range instead.
CNES_DEBUG_API size_t cnes_debug_disassemble(uint16_t address, char *buf, size_t buf_size);

// ---------------------------------------------------------------------------
// Structured disassembly
// ---------------------------------------------------------------------------

// Addressing modes, as the disassembler reports them. The values are part of
// the ABI; append rather than reorder.
typedef enum cnes_debug_addrmode_e {
	CNES_DEBUG_MODE_IMP = 0, // implied
	CNES_DEBUG_MODE_ACC,     // accumulator
	CNES_DEBUG_MODE_IMM,     // #$nn
	CNES_DEBUG_MODE_ZP,      // $nn
	CNES_DEBUG_MODE_ZPX,     // $nn,X
	CNES_DEBUG_MODE_ZPY,     // $nn,Y
	CNES_DEBUG_MODE_REL,     // branch displacement
	CNES_DEBUG_MODE_ABS,     // $nnnn
	CNES_DEBUG_MODE_ABSX,    // $nnnn,X
	CNES_DEBUG_MODE_ABSY,    // $nnnn,Y
	CNES_DEBUG_MODE_IND,     // ($nnnn)
	CNES_DEBUG_MODE_IZX,     // ($nn,X)
	CNES_DEBUG_MODE_IZY,     // ($nn),Y
	CNES_DEBUG_MODE_COUNT
} cnes_debug_addrmode_t;

// Bits in cnes_debug_insn_t::flags.
#define CNES_DEBUG_INSN_ILLEGAL   0x01u // undocumented opcode
#define CNES_DEBUG_INSN_BRANCH    0x02u // conditional branch
#define CNES_DEBUG_INSN_JUMP      0x04u // JMP, in either form
#define CNES_DEBUG_INSN_CALL      0x08u // JSR
#define CNES_DEBUG_INSN_RETURN    0x10u // RTS or RTI
#define CNES_DEBUG_INSN_STOP      0x20u // control flow does not fall through
#define CNES_DEBUG_INSN_TARGET    0x40u // `target` holds a code address
#define CNES_DEBUG_INSN_EFFECTIVE 0x80u // `effective` and `value` are resolved

#define CNES_DEBUG_MNEMONIC_MAX 5
#define CNES_DEBUG_OPERAND_MAX  16

// One decoded instruction.
//
// The text is split into mnemonic and operand so a listing can column-align
// them, and the numbers are kept alongside so the UI can make its own
// decisions -- follow a branch, colour an illegal opcode -- without parsing
// the text back apart.
//
// Layout is part of the ABI, like the rest of this header.
typedef struct cnes_debug_insn_s {
	uint16_t address;
	// Where this instruction sends the PC, when CNES_DEBUG_INSN_TARGET is set:
	// the resolved destination of a branch, JSR or JMP. For the indirect JMP
	// the pointer is followed, page-wrap bug and all.
	uint16_t target;
	// The address this instruction reads or writes, when
	// CNES_DEBUG_INSN_EFFECTIVE is set. Indexing and indirection are resolved
	// against the CPU's registers *now*, so it is exact for the instruction at
	// PC and a good guess elsewhere.
	uint16_t effective;
	uint8_t  value;  // the byte currently at `effective`
	uint8_t  length; // 1 to 3, including the opcode
	uint8_t  bytes[3];
	uint8_t  mode;   // cnes_debug_addrmode_t
	uint8_t  flags;  // CNES_DEBUG_INSN_*
	uint8_t  cycles; // base cycle count, before page-cross and branch penalties
	char     mnemonic[CNES_DEBUG_MNEMONIC_MAX]; // "LDA", NUL-terminated
	char     operand[CNES_DEBUG_OPERAND_MAX];   // "($12),Y", empty when implied
	// One line on what the instruction does, for a tooltip. Points at static
	// storage inside the backend and is never NULL, so it outlives the struct
	// and needs no copying; the undocumented opcodes say so in their text.
	const char *description;
} cnes_debug_insn_t;

// Decodes up to `count` consecutive instructions starting at `address` into
// `out`, taking the emulator lock once for the whole run. Returns how many were
// written, which is short only when the walk would run past $FFFF.
//
// This is the call a listing should use: one lock for a screenful rather than
// one per line.
CNES_DEBUG_API size_t cnes_debug_disassemble_range(uint16_t address, cnes_debug_insn_t *out, size_t count);

// Returns the address `count` instructions before `address`, for scrolling a
// listing backwards.
//
// Walking back through variable-length instructions is guesswork: nothing in
// the byte stream says where an instruction starts. This sweeps forward from
// further back and keeps the alignment that lands exactly on `address`, which
// is what every 6502 debugger does and is right for ordinary code. Data
// embedded in the instruction stream can still throw it off, so treat the
// result as a good place to start reading, not as ground truth.
CNES_DEBUG_API uint16_t cnes_debug_disassemble_rewind(uint16_t address, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif // CNES_DEBUG_API_H
