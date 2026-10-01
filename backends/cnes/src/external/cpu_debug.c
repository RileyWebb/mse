// Read-only view of the emulator for debug UI. See include/cNES/external/cpu_debug.h.
//
// Everything here runs on the caller's thread -- the frontend's UI thread --
// while the emulator runs on its own, so every access takes the same lock the
// rest of the backend uses. That lock is not free: the emulation thread yields
// for a moment whenever someone is waiting, so callers should pull a whole
// address space in one cnes_debug_read rather than a byte at a time.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "libmse/libmse_debug.h"

#include "cNES/nes.h"
#include "cNES/cpu.h"
#include "cNES/bus.h"
#include "cNES/ppu.h"
#include "cNES/rom.h"
#include "cNES/mapper.h"
#include "cNES/external/cpu_debug.h"

#include "../backend_internal.h"

// The disassembler reads g_opcode_info below rather than building its own table
// from cpu_opcodes.inc. That file is shaped for the interpreter -- it names
// handlers, so ASL arrives as two rows -- and carries neither an instruction
// length nor a description. g_opcode_info carries both, plus the illegal flag
// that used to be reconstructed from the mnemonic.

// g_opcode_info stores a CPU_AddressingMode; the ABI reports a
// cnes_debug_addrmode_t. The two agree value for value, which is what lets
// cnes_dis_decode cast between them. Reordering either enum stops the build
// here rather than leaving a listing to decode ABS as REL.
#define CNES_DIS_ASSERT_MODE(name) \
	_Static_assert((int)CPU_MODE_##name == (int)CNES_DEBUG_MODE_##name, \
	               "CPU_MODE_" #name " and CNES_DEBUG_MODE_" #name " have diverged")

CNES_DIS_ASSERT_MODE(IMP);
CNES_DIS_ASSERT_MODE(ACC);
CNES_DIS_ASSERT_MODE(IMM);
CNES_DIS_ASSERT_MODE(ZP);
CNES_DIS_ASSERT_MODE(ZPX);
CNES_DIS_ASSERT_MODE(ZPY);
CNES_DIS_ASSERT_MODE(REL);
CNES_DIS_ASSERT_MODE(ABS);
CNES_DIS_ASSERT_MODE(ABSX);
CNES_DIS_ASSERT_MODE(ABSY);
CNES_DIS_ASSERT_MODE(IND);
CNES_DIS_ASSERT_MODE(IZX);
CNES_DIS_ASSERT_MODE(IZY);

#undef CNES_DIS_ASSERT_MODE

// One row of g_opcode_info: everything about an encoding that a listing wants
// and the interpreter has no use for. It describes the CPU rather than the
// debug ABI, so it keeps cpu.h's subsystem prefix; it lives here because the
// disassembler is its only reader, and it stays out of cpu_debug.h because the
// ABI that crosses the DLL boundary is cnes_debug_insn_t, not this.
typedef struct CPU_OpcodeInfo {
	const char        *mnemonic;    // e.g. "LDA"
	const char        *description; // Human-readable summary of what it does
	CPU_AddressingMode addressing_mode;
	uint8_t            cycles;      // Base cycles, before page-cross / branch penalties
	uint8_t            size;        // Instruction length in bytes, including the opcode
	bool               illegal;     // True for the undocumented opcodes
} CPU_OpcodeInfo;

// Opcode metadata
static const CPU_OpcodeInfo g_opcode_info[256] = {
	{"BRK", "Force an interrupt; push PC and status, then vector through $FFFE", CPU_MODE_IMP, 7, 1, false},
	{"ORA", "Bitwise OR memory with the accumulator", CPU_MODE_IZX, 6, 2, false},
	{"KIL", "Unofficial: jams the CPU, no further instructions execute", CPU_MODE_IMP, 2, 1, true},
	{"SLO", "Unofficial: shift memory left, then OR the result into the accumulator", CPU_MODE_IZX, 8, 2, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ZP, 3, 2, true},
	{"ORA", "Bitwise OR memory with the accumulator", CPU_MODE_ZP, 3, 2, false},
	{"ASL", "Arithmetic shift left one bit", CPU_MODE_ZP, 5, 2, false},
	{"SLO", "Unofficial: shift memory left, then OR the result into the accumulator", CPU_MODE_ZP, 5, 2, true},
	{"PHP", "Push the processor status onto the stack", CPU_MODE_IMP, 3, 1, false},
	{"ORA", "Bitwise OR memory with the accumulator", CPU_MODE_IMM, 2, 2, false},
	{"ASL", "Arithmetic shift left one bit", CPU_MODE_ACC, 2, 1, false},
	{"ANC", "Unofficial: AND with the accumulator, then copy bit 7 into the carry flag", CPU_MODE_IMM, 2, 2, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ABS, 4, 3, true},
	{"ORA", "Bitwise OR memory with the accumulator", CPU_MODE_ABS, 4, 3, false},
	{"ASL", "Arithmetic shift left one bit", CPU_MODE_ABS, 6, 3, false},
	{"SLO", "Unofficial: shift memory left, then OR the result into the accumulator", CPU_MODE_ABS, 6, 3, true},
	{"BPL", "Branch if plus (negative flag clear)", CPU_MODE_REL, 2, 2, false},
	{"ORA", "Bitwise OR memory with the accumulator", CPU_MODE_IZY, 5, 2, false},
	{"KIL", "Unofficial: jams the CPU, no further instructions execute", CPU_MODE_IMP, 2, 1, true},
	{"SLO", "Unofficial: shift memory left, then OR the result into the accumulator", CPU_MODE_IZY, 8, 2, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ZPX, 4, 2, true},
	{"ORA", "Bitwise OR memory with the accumulator", CPU_MODE_ZPX, 4, 2, false},
	{"ASL", "Arithmetic shift left one bit", CPU_MODE_ZPX, 6, 2, false},
	{"SLO", "Unofficial: shift memory left, then OR the result into the accumulator", CPU_MODE_ZPX, 6, 2, true},
	{"CLC", "Clear the carry flag", CPU_MODE_IMP, 2, 1, false},
	{"ORA", "Bitwise OR memory with the accumulator", CPU_MODE_ABSY, 4, 3, false},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_IMP, 2, 1, true},
	{"SLO", "Unofficial: shift memory left, then OR the result into the accumulator", CPU_MODE_ABSY, 7, 3, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ABSX, 4, 3, true},
	{"ORA", "Bitwise OR memory with the accumulator", CPU_MODE_ABSX, 4, 3, false},
	{"ASL", "Arithmetic shift left one bit", CPU_MODE_ABSX, 7, 3, false},
	{"SLO", "Unofficial: shift memory left, then OR the result into the accumulator", CPU_MODE_ABSX, 7, 3, true},
	{"JSR", "Jump to a subroutine, pushing the return address", CPU_MODE_ABS, 6, 3, false},
	{"AND", "Bitwise AND memory with the accumulator", CPU_MODE_IZX, 6, 2, false},
	{"KIL", "Unofficial: jams the CPU, no further instructions execute", CPU_MODE_IMP, 2, 1, true},
	{"RLA", "Unofficial: rotate memory left, then AND the result into the accumulator", CPU_MODE_IZX, 8, 2, true},
	{"BIT", "Test bits in memory against the accumulator", CPU_MODE_ZP, 3, 2, false},
	{"AND", "Bitwise AND memory with the accumulator", CPU_MODE_ZP, 3, 2, false},
	{"ROL", "Rotate left one bit through the carry flag", CPU_MODE_ZP, 5, 2, false},
	{"RLA", "Unofficial: rotate memory left, then AND the result into the accumulator", CPU_MODE_ZP, 5, 2, true},
	{"PLP", "Pull the processor status from the stack", CPU_MODE_IMP, 4, 1, false},
	{"AND", "Bitwise AND memory with the accumulator", CPU_MODE_IMM, 2, 2, false},
	{"ROL", "Rotate left one bit through the carry flag", CPU_MODE_ACC, 2, 1, false},
	{"ANC", "Unofficial: AND with the accumulator, then copy bit 7 into the carry flag", CPU_MODE_IMM, 2, 2, true},
	{"BIT", "Test bits in memory against the accumulator", CPU_MODE_ABS, 4, 3, false},
	{"AND", "Bitwise AND memory with the accumulator", CPU_MODE_ABS, 4, 3, false},
	{"ROL", "Rotate left one bit through the carry flag", CPU_MODE_ABS, 6, 3, false},
	{"RLA", "Unofficial: rotate memory left, then AND the result into the accumulator", CPU_MODE_ABS, 6, 3, true},
	{"BMI", "Branch if minus (negative flag set)", CPU_MODE_REL, 2, 2, false},
	{"AND", "Bitwise AND memory with the accumulator", CPU_MODE_IZY, 5, 2, false},
	{"KIL", "Unofficial: jams the CPU, no further instructions execute", CPU_MODE_IMP, 2, 1, true},
	{"RLA", "Unofficial: rotate memory left, then AND the result into the accumulator", CPU_MODE_IZY, 8, 2, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ZPX, 4, 2, true},
	{"AND", "Bitwise AND memory with the accumulator", CPU_MODE_ZPX, 4, 2, false},
	{"ROL", "Rotate left one bit through the carry flag", CPU_MODE_ZPX, 6, 2, false},
	{"RLA", "Unofficial: rotate memory left, then AND the result into the accumulator", CPU_MODE_ZPX, 6, 2, true},
	{"SEC", "Set the carry flag", CPU_MODE_IMP, 2, 1, false},
	{"AND", "Bitwise AND memory with the accumulator", CPU_MODE_ABSY, 4, 3, false},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_IMP, 2, 1, true},
	{"RLA", "Unofficial: rotate memory left, then AND the result into the accumulator", CPU_MODE_ABSY, 7, 3, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ABSX, 4, 3, true},
	{"AND", "Bitwise AND memory with the accumulator", CPU_MODE_ABSX, 4, 3, false},
	{"ROL", "Rotate left one bit through the carry flag", CPU_MODE_ABSX, 7, 3, false},
	{"RLA", "Unofficial: rotate memory left, then AND the result into the accumulator", CPU_MODE_ABSX, 7, 3, true},
	{"RTI", "Return from interrupt, pulling status and PC", CPU_MODE_IMP, 6, 1, false},
	{"EOR", "Bitwise exclusive-OR memory with the accumulator", CPU_MODE_IZX, 6, 2, false},
	{"KIL", "Unofficial: jams the CPU, no further instructions execute", CPU_MODE_IMP, 2, 1, true},
	{"SRE", "Unofficial: shift memory right, then EOR the result into the accumulator", CPU_MODE_IZX, 8, 2, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ZP, 3, 2, true},
	{"EOR", "Bitwise exclusive-OR memory with the accumulator", CPU_MODE_ZP, 3, 2, false},
	{"LSR", "Logical shift right one bit", CPU_MODE_ZP, 5, 2, false},
	{"SRE", "Unofficial: shift memory right, then EOR the result into the accumulator", CPU_MODE_ZP, 5, 2, true},
	{"PHA", "Push the accumulator onto the stack", CPU_MODE_IMP, 3, 1, false},
	{"EOR", "Bitwise exclusive-OR memory with the accumulator", CPU_MODE_IMM, 2, 2, false},
	{"LSR", "Logical shift right one bit", CPU_MODE_ACC, 2, 1, false},
	{"ALR", "Unofficial: AND with the accumulator, then shift the accumulator right", CPU_MODE_IMM, 2, 2, true},
	{"JMP", "Jump to a new location", CPU_MODE_ABS, 3, 3, false},
	{"EOR", "Bitwise exclusive-OR memory with the accumulator", CPU_MODE_ABS, 4, 3, false},
	{"LSR", "Logical shift right one bit", CPU_MODE_ABS, 6, 3, false},
	{"SRE", "Unofficial: shift memory right, then EOR the result into the accumulator", CPU_MODE_ABS, 6, 3, true},
	{"BVC", "Branch if overflow clear", CPU_MODE_REL, 2, 2, false},
	{"EOR", "Bitwise exclusive-OR memory with the accumulator", CPU_MODE_IZY, 5, 2, false},
	{"KIL", "Unofficial: jams the CPU, no further instructions execute", CPU_MODE_IMP, 2, 1, true},
	{"SRE", "Unofficial: shift memory right, then EOR the result into the accumulator", CPU_MODE_IZY, 8, 2, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ZPX, 4, 2, true},
	{"EOR", "Bitwise exclusive-OR memory with the accumulator", CPU_MODE_ZPX, 4, 2, false},
	{"LSR", "Logical shift right one bit", CPU_MODE_ZPX, 6, 2, false},
	{"SRE", "Unofficial: shift memory right, then EOR the result into the accumulator", CPU_MODE_ZPX, 6, 2, true},
	{"CLI", "Clear the interrupt disable flag", CPU_MODE_IMP, 2, 1, false},
	{"EOR", "Bitwise exclusive-OR memory with the accumulator", CPU_MODE_ABSY, 4, 3, false},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_IMP, 2, 1, true},
	{"SRE", "Unofficial: shift memory right, then EOR the result into the accumulator", CPU_MODE_ABSY, 7, 3, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ABSX, 4, 3, true},
	{"EOR", "Bitwise exclusive-OR memory with the accumulator", CPU_MODE_ABSX, 4, 3, false},
	{"LSR", "Logical shift right one bit", CPU_MODE_ABSX, 7, 3, false},
	{"SRE", "Unofficial: shift memory right, then EOR the result into the accumulator", CPU_MODE_ABSX, 7, 3, true},
	{"RTS", "Return from subroutine", CPU_MODE_IMP, 6, 1, false},
	{"ADC", "Add memory to the accumulator with carry", CPU_MODE_IZX, 6, 2, false},
	{"KIL", "Unofficial: jams the CPU, no further instructions execute", CPU_MODE_IMP, 2, 1, true},
	{"RRA", "Unofficial: rotate memory right, then add the result to the accumulator", CPU_MODE_IZX, 8, 2, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ZP, 3, 2, true},
	{"ADC", "Add memory to the accumulator with carry", CPU_MODE_ZP, 3, 2, false},
	{"ROR", "Rotate right one bit through the carry flag", CPU_MODE_ZP, 5, 2, false},
	{"RRA", "Unofficial: rotate memory right, then add the result to the accumulator", CPU_MODE_ZP, 5, 2, true},
	{"PLA", "Pull the accumulator from the stack", CPU_MODE_IMP, 4, 1, false},
	{"ADC", "Add memory to the accumulator with carry", CPU_MODE_IMM, 2, 2, false},
	{"ROR", "Rotate right one bit through the carry flag", CPU_MODE_ACC, 2, 1, false},
	{"ARR", "Unofficial: AND with the accumulator, rotate right, then set carry and overflow from bits 6 and 5",
	 CPU_MODE_IMM, 2, 2, true},
	{"JMP", "Jump to a new location", CPU_MODE_IND, 5, 3, false},
	{"ADC", "Add memory to the accumulator with carry", CPU_MODE_ABS, 4, 3, false},
	{"ROR", "Rotate right one bit through the carry flag", CPU_MODE_ABS, 6, 3, false},
	{"RRA", "Unofficial: rotate memory right, then add the result to the accumulator", CPU_MODE_ABS, 6, 3, true},
	{"BVS", "Branch if overflow set", CPU_MODE_REL, 2, 2, false},
	{"ADC", "Add memory to the accumulator with carry", CPU_MODE_IZY, 5, 2, false},
	{"KIL", "Unofficial: jams the CPU, no further instructions execute", CPU_MODE_IMP, 2, 1, true},
	{"RRA", "Unofficial: rotate memory right, then add the result to the accumulator", CPU_MODE_IZY, 8, 2, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ZPX, 4, 2, true},
	{"ADC", "Add memory to the accumulator with carry", CPU_MODE_ZPX, 4, 2, false},
	{"ROR", "Rotate right one bit through the carry flag", CPU_MODE_ZPX, 6, 2, false},
	{"RRA", "Unofficial: rotate memory right, then add the result to the accumulator", CPU_MODE_ZPX, 6, 2, true},
	{"SEI", "Set the interrupt disable flag", CPU_MODE_IMP, 2, 1, false},
	{"ADC", "Add memory to the accumulator with carry", CPU_MODE_ABSY, 4, 3, false},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_IMP, 2, 1, true},
	{"RRA", "Unofficial: rotate memory right, then add the result to the accumulator", CPU_MODE_ABSY, 7, 3, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ABSX, 4, 3, true},
	{"ADC", "Add memory to the accumulator with carry", CPU_MODE_ABSX, 4, 3, false},
	{"ROR", "Rotate right one bit through the carry flag", CPU_MODE_ABSX, 7, 3, false},
	{"RRA", "Unofficial: rotate memory right, then add the result to the accumulator", CPU_MODE_ABSX, 7, 3, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_IMM, 2, 2, true},
	{"STA", "Store the accumulator in memory", CPU_MODE_IZX, 6, 2, false},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_IMM, 2, 2, true},
	{"SAX", "Unofficial: store the bitwise AND of the accumulator and the X register", CPU_MODE_IZX, 6, 2, true},
	{"STY", "Store the Y register in memory", CPU_MODE_ZP, 3, 2, false},
	{"STA", "Store the accumulator in memory", CPU_MODE_ZP, 3, 2, false},
	{"STX", "Store the X register in memory", CPU_MODE_ZP, 3, 2, false},
	{"SAX", "Unofficial: store the bitwise AND of the accumulator and the X register", CPU_MODE_ZP, 3, 2, true},
	{"DEY", "Decrement the Y register by one", CPU_MODE_IMP, 2, 1, false},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_IMM, 2, 2, true},
	{"TXA", "Transfer the X register to the accumulator", CPU_MODE_IMP, 2, 1, false},
	{"ANE", "Unofficial and unstable: (A OR magic) AND X AND immediate into the accumulator", CPU_MODE_IMM, 2, 2, true},
	{"STY", "Store the Y register in memory", CPU_MODE_ABS, 4, 3, false},
	{"STA", "Store the accumulator in memory", CPU_MODE_ABS, 4, 3, false},
	{"STX", "Store the X register in memory", CPU_MODE_ABS, 4, 3, false},
	{"SAX", "Unofficial: store the bitwise AND of the accumulator and the X register", CPU_MODE_ABS, 4, 3, true},
	{"BCC", "Branch if carry clear", CPU_MODE_REL, 2, 2, false},
	{"STA", "Store the accumulator in memory", CPU_MODE_IZY, 6, 2, false},
	{"KIL", "Unofficial: jams the CPU, no further instructions execute", CPU_MODE_IMP, 2, 1, true},
	{"SHA", "Unofficial and unstable: store A AND X AND the target address high byte plus one", CPU_MODE_IZY, 6, 2,
	 true},
	{"STY", "Store the Y register in memory", CPU_MODE_ZPX, 4, 2, false},
	{"STA", "Store the accumulator in memory", CPU_MODE_ZPX, 4, 2, false},
	{"STX", "Store the X register in memory", CPU_MODE_ZPY, 4, 2, false},
	{"SAX", "Unofficial: store the bitwise AND of the accumulator and the X register", CPU_MODE_ZPY, 4, 2, true},
	{"TYA", "Transfer the Y register to the accumulator", CPU_MODE_IMP, 2, 1, false},
	{"STA", "Store the accumulator in memory", CPU_MODE_ABSY, 5, 3, false},
	{"TXS", "Transfer the X register to the stack pointer", CPU_MODE_IMP, 2, 1, false},
	{"TAS", "Unofficial and unstable: set the stack pointer to A AND X, then store it AND the address high byte",
	 CPU_MODE_ABSY, 5, 3, true},
	{"SHY", "Unofficial and unstable: store Y AND the target address high byte plus one", CPU_MODE_ABSX, 5, 3, true},
	{"STA", "Store the accumulator in memory", CPU_MODE_ABSX, 5, 3, false},
	{"SHX", "Unofficial and unstable: store X AND the target address high byte plus one", CPU_MODE_ABSY, 5, 3, true},
	{"SHA", "Unofficial and unstable: store A AND X AND the target address high byte plus one", CPU_MODE_ABSY, 5, 3,
	 true},
	{"LDY", "Load the Y register from memory", CPU_MODE_IMM, 2, 2, false},
	{"LDA", "Load the accumulator from memory", CPU_MODE_IZX, 6, 2, false},
	{"LDX", "Load the X register from memory", CPU_MODE_IMM, 2, 2, false},
	{"LAX", "Unofficial: load both the accumulator and the X register from memory", CPU_MODE_IZX, 6, 2, true},
	{"LDY", "Load the Y register from memory", CPU_MODE_ZP, 3, 2, false},
	{"LDA", "Load the accumulator from memory", CPU_MODE_ZP, 3, 2, false},
	{"LDX", "Load the X register from memory", CPU_MODE_ZP, 3, 2, false},
	{"LAX", "Unofficial: load both the accumulator and the X register from memory", CPU_MODE_ZP, 3, 2, true},
	{"TAY", "Transfer the accumulator to the Y register", CPU_MODE_IMP, 2, 1, false},
	{"LDA", "Load the accumulator from memory", CPU_MODE_IMM, 2, 2, false},
	{"TAX", "Transfer the accumulator to the X register", CPU_MODE_IMP, 2, 1, false},
	{"LXA", "Unofficial and unstable: (A OR magic) AND immediate into the accumulator and X", CPU_MODE_IMM, 2, 2, true},
	{"LDY", "Load the Y register from memory", CPU_MODE_ABS, 4, 3, false},
	{"LDA", "Load the accumulator from memory", CPU_MODE_ABS, 4, 3, false},
	{"LDX", "Load the X register from memory", CPU_MODE_ABS, 4, 3, false},
	{"LAX", "Unofficial: load both the accumulator and the X register from memory", CPU_MODE_ABS, 4, 3, true},
	{"BCS", "Branch if carry set", CPU_MODE_REL, 2, 2, false},
	{"LDA", "Load the accumulator from memory", CPU_MODE_IZY, 5, 2, false},
	{"KIL", "Unofficial: jams the CPU, no further instructions execute", CPU_MODE_IMP, 2, 1, true},
	{"LAX", "Unofficial: load both the accumulator and the X register from memory", CPU_MODE_IZY, 5, 2, true},
	{"LDY", "Load the Y register from memory", CPU_MODE_ZPX, 4, 2, false},
	{"LDA", "Load the accumulator from memory", CPU_MODE_ZPX, 4, 2, false},
	{"LDX", "Load the X register from memory", CPU_MODE_ZPY, 4, 2, false},
	{"LAX", "Unofficial: load both the accumulator and the X register from memory", CPU_MODE_ZPY, 4, 2, true},
	{"CLV", "Clear the overflow flag", CPU_MODE_IMP, 2, 1, false},
	{"LDA", "Load the accumulator from memory", CPU_MODE_ABSY, 4, 3, false},
	{"TSX", "Transfer the stack pointer to the X register", CPU_MODE_IMP, 2, 1, false},
	{"LAS", "Unofficial: AND memory with the stack pointer into the accumulator, X and the stack pointer",
	 CPU_MODE_ABSY, 4, 3, true},
	{"LDY", "Load the Y register from memory", CPU_MODE_ABSX, 4, 3, false},
	{"LDA", "Load the accumulator from memory", CPU_MODE_ABSX, 4, 3, false},
	{"LDX", "Load the X register from memory", CPU_MODE_ABSY, 4, 3, false},
	{"LAX", "Unofficial: load both the accumulator and the X register from memory", CPU_MODE_ABSY, 4, 3, true},
	{"CPY", "Compare memory with the Y register", CPU_MODE_IMM, 2, 2, false},
	{"CMP", "Compare memory with the accumulator", CPU_MODE_IZX, 6, 2, false},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_IMM, 2, 2, true},
	{"DCP", "Unofficial: decrement memory, then compare it with the accumulator", CPU_MODE_IZX, 8, 2, true},
	{"CPY", "Compare memory with the Y register", CPU_MODE_ZP, 3, 2, false},
	{"CMP", "Compare memory with the accumulator", CPU_MODE_ZP, 3, 2, false},
	{"DEC", "Decrement memory by one", CPU_MODE_ZP, 5, 2, false},
	{"DCP", "Unofficial: decrement memory, then compare it with the accumulator", CPU_MODE_ZP, 5, 2, true},
	{"INY", "Increment the Y register by one", CPU_MODE_IMP, 2, 1, false},
	{"CMP", "Compare memory with the accumulator", CPU_MODE_IMM, 2, 2, false},
	{"DEX", "Decrement the X register by one", CPU_MODE_IMP, 2, 1, false},
	{"SBX", "Unofficial: subtract immediate from (A AND X) into X, ignoring the carry flag", CPU_MODE_IMM, 2, 2, true},
	{"CPY", "Compare memory with the Y register", CPU_MODE_ABS, 4, 3, false},
	{"CMP", "Compare memory with the accumulator", CPU_MODE_ABS, 4, 3, false},
	{"DEC", "Decrement memory by one", CPU_MODE_ABS, 6, 3, false},
	{"DCP", "Unofficial: decrement memory, then compare it with the accumulator", CPU_MODE_ABS, 6, 3, true},
	{"BNE", "Branch if not equal (zero flag clear)", CPU_MODE_REL, 2, 2, false},
	{"CMP", "Compare memory with the accumulator", CPU_MODE_IZY, 5, 2, false},
	{"KIL", "Unofficial: jams the CPU, no further instructions execute", CPU_MODE_IMP, 2, 1, true},
	{"DCP", "Unofficial: decrement memory, then compare it with the accumulator", CPU_MODE_IZY, 8, 2, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ZPX, 4, 2, true},
	{"CMP", "Compare memory with the accumulator", CPU_MODE_ZPX, 4, 2, false},
	{"DEC", "Decrement memory by one", CPU_MODE_ZPX, 6, 2, false},
	{"DCP", "Unofficial: decrement memory, then compare it with the accumulator", CPU_MODE_ZPX, 6, 2, true},
	{"CLD", "Clear the decimal flag (no effect on the NES)", CPU_MODE_IMP, 2, 1, false},
	{"CMP", "Compare memory with the accumulator", CPU_MODE_ABSY, 4, 3, false},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_IMP, 2, 1, true},
	{"DCP", "Unofficial: decrement memory, then compare it with the accumulator", CPU_MODE_ABSY, 7, 3, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ABSX, 4, 3, true},
	{"CMP", "Compare memory with the accumulator", CPU_MODE_ABSX, 4, 3, false},
	{"DEC", "Decrement memory by one", CPU_MODE_ABSX, 7, 3, false},
	{"DCP", "Unofficial: decrement memory, then compare it with the accumulator", CPU_MODE_ABSX, 7, 3, true},
	{"CPX", "Compare memory with the X register", CPU_MODE_IMM, 2, 2, false},
	{"SBC", "Subtract memory from the accumulator with borrow", CPU_MODE_IZX, 6, 2, false},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_IMM, 2, 2, true},
	{"ISC", "Unofficial: increment memory, then subtract it from the accumulator", CPU_MODE_IZX, 8, 2, true},
	{"CPX", "Compare memory with the X register", CPU_MODE_ZP, 3, 2, false},
	{"SBC", "Subtract memory from the accumulator with borrow", CPU_MODE_ZP, 3, 2, false},
	{"INC", "Increment memory by one", CPU_MODE_ZP, 5, 2, false},
	{"ISC", "Unofficial: increment memory, then subtract it from the accumulator", CPU_MODE_ZP, 5, 2, true},
	{"INX", "Increment the X register by one", CPU_MODE_IMP, 2, 1, false},
	{"SBC", "Subtract memory from the accumulator with borrow", CPU_MODE_IMM, 2, 2, false},
	{"NOP", "No operation", CPU_MODE_IMP, 2, 1, false},
	{"SBC", "Unofficial duplicate of SBC immediate", CPU_MODE_IMM, 2, 2, true},
	{"CPX", "Compare memory with the X register", CPU_MODE_ABS, 4, 3, false},
	{"SBC", "Subtract memory from the accumulator with borrow", CPU_MODE_ABS, 4, 3, false},
	{"INC", "Increment memory by one", CPU_MODE_ABS, 6, 3, false},
	{"ISC", "Unofficial: increment memory, then subtract it from the accumulator", CPU_MODE_ABS, 6, 3, true},
	{"BEQ", "Branch if equal (zero flag set)", CPU_MODE_REL, 2, 2, false},
	{"SBC", "Subtract memory from the accumulator with borrow", CPU_MODE_IZY, 5, 2, false},
	{"KIL", "Unofficial: jams the CPU, no further instructions execute", CPU_MODE_IMP, 2, 1, true},
	{"ISC", "Unofficial: increment memory, then subtract it from the accumulator", CPU_MODE_IZY, 8, 2, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ZPX, 4, 2, true},
	{"SBC", "Subtract memory from the accumulator with borrow", CPU_MODE_ZPX, 4, 2, false},
	{"INC", "Increment memory by one", CPU_MODE_ZPX, 6, 2, false},
	{"ISC", "Unofficial: increment memory, then subtract it from the accumulator", CPU_MODE_ZPX, 6, 2, true},
	{"SED", "Set the decimal flag (no effect on the NES)", CPU_MODE_IMP, 2, 1, false},
	{"SBC", "Subtract memory from the accumulator with borrow", CPU_MODE_ABSY, 4, 3, false},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_IMP, 2, 1, true},
	{"ISC", "Unofficial: increment memory, then subtract it from the accumulator", CPU_MODE_ABSY, 7, 3, true},
	{"NOP", "Unofficial: no operation, but still reads its operand", CPU_MODE_ABSX, 4, 3, true},
	{"SBC", "Subtract memory from the accumulator with borrow", CPU_MODE_ABSX, 4, 3, false},
	{"INC", "Increment memory by one", CPU_MODE_ABSX, 7, 3, false},
	{"ISC", "Unofficial: increment memory, then subtract it from the accumulator", CPU_MODE_ABSX, 7, 3, true}
};


// ---------------------------------------------------------------------------
// Side-effect-free reads
//
// CPU-space peeks go through BUS_Peek, which is the bus's own debugger read and
// the only place the "what does $2002 hold without clearing it" rules live. The
// PPU bus has no equivalent, so it is decoded here.
// ---------------------------------------------------------------------------

static uint8_t cnes_peek_ppu(NES *nes, uint16_t address)
{
	PPU *ppu = nes->ppu;
	if (ppu == NULL) {
		return 0;
	}

	address &= 0x3FFF;

	if (address < 0x2000) {
		return BUS_PPU_ReadCHR(nes->bus, address);
	}

	if (address < 0x3F00) {
		// Nametables, through the mirroring the mapper currently has set up.
		uint16_t       offset = (uint16_t)((address - 0x2000) & 0x0FFF);
		const uint8_t *table  = PPU_GetNametable(ppu, offset / 0x400);
		return table ? table[offset & 0x03FF] : 0;
	}

	// Palette RAM, with the $3F10/$14/$18/$1C mirrors folded down.
	uint8_t index = (uint8_t)(address & 0x001F);
	if ((index & 0x13) == 0x10) {
		index = (uint8_t)(index & ~0x10);
	}
	return ppu->palette[index];
}

// ---------------------------------------------------------------------------
// Memory access
// ---------------------------------------------------------------------------

// Clamped memcpy out of one of the spaces that is a flat buffer.
static size_t cnes_debug_copy(const uint8_t *src, size_t size, uint32_t address, uint8_t *dst, size_t len)
{
	if (src == NULL || address >= size) {
		return 0;
	}
	if (len > size - address) {
		len = size - address;
	}
	memcpy(dst, src + address, len);
	return len;
}

size_t cnes_debug_space_size(cnes_debug_space_t space)
{
	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL) {
		cnes_backend_unlock_nes();
		return 0;
	}

	size_t size = 0;
	switch (space) {
		case CNES_DEBUG_SPACE_CPU:     size = 0x10000; break;
		case CNES_DEBUG_SPACE_RAM:     size = BUS_RAM_SIZE; break;
		case CNES_DEBUG_SPACE_PPU:     size = 0x4000; break;
		case CNES_DEBUG_SPACE_OAM:     size = PPU_OAM_SIZE; break;
		case CNES_DEBUG_SPACE_PALETTE: size = PPU_PALETTE_RAM_SIZE; break;
		case CNES_DEBUG_SPACE_CHR:     size = nes->bus->chrRomDataSize; break;
		case CNES_DEBUG_SPACE_PRG:     size = nes->bus->prgRomDataSize; break;
		default:                       break;
	}

	cnes_backend_unlock_nes();
	return size;
}

size_t cnes_debug_read(cnes_debug_space_t space, uint32_t address, uint8_t *dst, size_t len)
{
	if (dst == NULL || len == 0) {
		return 0;
	}

	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL) {
		cnes_backend_unlock_nes();
		return 0;
	}

	PPU   *ppu    = nes->ppu;
	size_t copied = 0;

	switch (space) {
		case CNES_DEBUG_SPACE_CPU:
			for (; copied < len && (address + copied) <= 0xFFFF; ++copied) {
				dst[copied] = BUS_Peek(nes, (uint16_t)(address + copied));
			}
			break;

		case CNES_DEBUG_SPACE_PPU:
			for (; copied < len && (address + copied) <= 0x3FFF; ++copied) {
				dst[copied] = cnes_peek_ppu(nes, (uint16_t)(address + copied));
			}
			break;

		case CNES_DEBUG_SPACE_RAM:
			copied = cnes_debug_copy(nes->bus->ram, BUS_RAM_SIZE, address, dst, len);
			break;

		case CNES_DEBUG_SPACE_OAM:
			copied = (ppu != NULL) ? cnes_debug_copy(ppu->oam, PPU_OAM_SIZE, address, dst, len) : 0;
			break;

		case CNES_DEBUG_SPACE_PALETTE:
			copied = (ppu != NULL) ? cnes_debug_copy(ppu->palette, PPU_PALETTE_RAM_SIZE, address, dst, len) : 0;
			break;

		case CNES_DEBUG_SPACE_CHR:
			copied = cnes_debug_copy(nes->bus->chrRomData, nes->bus->chrRomDataSize, address, dst, len);
			break;

		case CNES_DEBUG_SPACE_PRG:
			copied = cnes_debug_copy(nes->bus->prgRomData, nes->bus->prgRomDataSize, address, dst, len);
			break;

		default:
			break;
	}

	cnes_backend_unlock_nes();
	return copied;
}

int cnes_debug_write(cnes_debug_space_t space, uint32_t address, uint8_t value)
{
	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL) {
		cnes_backend_unlock_nes();
		return 0;
	}

	PPU *ppu = nes->ppu;
	bool ok  = false;

	switch (space) {
		case CNES_DEBUG_SPACE_RAM:
			if (address < BUS_RAM_SIZE) {
				nes->bus->ram[address] = value;
				ok                     = true;
			}
			break;

		case CNES_DEBUG_SPACE_CPU:
			// Deliberately only internal RAM: writing a register from the UI
			// would trigger the side effects this API exists to avoid.
			if (address < 0x2000) {
				nes->bus->ram[address & (BUS_RAM_SIZE - 1)] = value;
				ok                                          = true;
			}
			break;

		case CNES_DEBUG_SPACE_OAM:
			if (ppu != NULL && address < PPU_OAM_SIZE) {
				ppu->oam[address] = value;
				ok                = true;
			}
			break;

		case CNES_DEBUG_SPACE_PALETTE:
			if (ppu != NULL && address < PPU_PALETTE_RAM_SIZE) {
				ppu->palette[address] = value;
				ok                    = true;
			}
			break;

		case CNES_DEBUG_SPACE_PPU:
			// Nametable RAM and palette only. Below $2000 is pattern memory,
			// which may be ROM and is the mapper's to decide about.
			if (ppu != NULL) {
				const uint16_t masked = (uint16_t)(address & 0x3FFF);
				if (masked >= 0x3F00) {
					ppu->palette[masked & 0x1F] = value;
					ok                          = true;
				} else if (masked >= 0x2000) {
					// Through nametable_ptrs so a write lands where the PPU
					// would read it, mirroring and all.
					uint8_t *table = ppu->nametable_ptrs[(masked >> 10) & 0x03];
					if (table != NULL) {
						table[masked & 0x03FF] = value;
						ok                     = true;
					}
				}
			}
			break;

		default:
			break;
	}

	cnes_backend_unlock_nes();
	return ok ? 1 : 0;
}

int cnes_debug_get_state(cnes_debug_state_t *out)
{
	if (out == NULL) {
		return 0;
	}

	memset(out, 0, sizeof(*out));
	out->abi_version = CNES_DEBUG_ABI_VERSION;

	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL || nes->cpu == NULL) {
		cnes_backend_unlock_nes();
		return 0;
	}

	CPU *cpu             = nes->cpu;
	out->cpu.pc          = cpu->pc;
	out->cpu.a           = cpu->a;
	out->cpu.x           = cpu->x;
	out->cpu.y           = cpu->y;
	out->cpu.sp          = cpu->sp;
	out->cpu.status      = cpu->status;
	out->cpu.cycles      = cpu->total_cycles;
	out->cpu.nmi_pending = cpu->nmi_pending ? 1u : 0u;

	PPU *ppu = nes->ppu;
	if (ppu != NULL) {
		out->ppu.scanline = ppu->scanline;
		out->ppu.dot      = ppu->cycle;
		out->ppu.frame    = ppu->frame_count;
		out->ppu.v        = ppu->vram_addr;
		out->ppu.t        = ppu->temp_addr;
		out->ppu.x        = ppu->fine_x;
		out->ppu.w        = ppu->addr_latch;
		out->ppu.ctrl     = ppu->ctrl;
		out->ppu.mask     = ppu->mask;
		out->ppu.status   = ppu->status;
		out->ppu.oam_addr = ppu->oam_addr;
		out->ppu.data_buffer = ppu->data_buffer;
		out->mirroring    = (uint32_t)ppu->mirror_mode;
	}

	out->mapper   = nes->bus->mapper;
	out->prg_size = (uint32_t)nes->bus->prgRomDataSize;
	out->chr_size = (uint32_t)nes->bus->chrRomDataSize;
	out->valid    = (nes->rom != NULL) ? 1u : 0u;

	cnes_backend_unlock_nes();
	return 1;
}

// ---------------------------------------------------------------------------
// Instruction decoding
// ---------------------------------------------------------------------------

// Follows a 16-bit pointer the way JMP ($nnnn) does, reproducing the hardware
// bug where a pointer ending in $FF takes its high byte from the start of the
// same page rather than the next one.
static uint16_t cnes_peek_pointer_buggy(NES *nes, uint16_t pointer)
{
	uint8_t lo = BUS_Peek(nes, pointer);
	uint8_t hi = BUS_Peek(nes, (uint16_t)((pointer & 0xFF00) | ((pointer + 1) & 0x00FF)));
	return (uint16_t)(lo | ((uint16_t)hi << 8));
}

// Zero-page pointers wrap inside the page, so the high byte of a pointer at
// $FF comes from $00.
static uint16_t cnes_peek_pointer_zp(NES *nes, uint8_t pointer)
{
	uint8_t lo = BUS_Peek(nes, pointer);
	uint8_t hi = BUS_Peek(nes, (uint8_t)(pointer + 1));
	return (uint16_t)(lo | ((uint16_t)hi << 8));
}

// Decodes one instruction and returns its length. The caller must hold the
// emulator lock. This is the only decoder -- both the range walk and the
// single-instruction convenience call go through it -- so their answers cannot
// disagree.
static uint8_t cnes_dis_decode(NES *nes, uint16_t address, cnes_debug_insn_t *out)
{
	memset(out, 0, sizeof(*out));

	const CPU_OpcodeInfo *entry = &g_opcode_info[BUS_Peek(nes, address)];

	uint8_t length = entry->size;
	for (uint8_t i = 0; i < length; ++i) {
		out->bytes[i] = BUS_Peek(nes, (uint16_t)(address + i));
	}

	out->address     = address;
	out->length      = length;
	out->mode        = (uint8_t)entry->addressing_mode;
	out->cycles      = entry->cycles;
	out->description = entry->description;
	snprintf(out->mnemonic, sizeof(out->mnemonic), "%s", entry->mnemonic);

	const char *name = out->mnemonic;
	uint8_t     lo   = out->bytes[1];
	uint16_t    word = (uint16_t)(out->bytes[1] | ((uint16_t)out->bytes[2] << 8));

	// Registers are read once: an effective address is only ever a snapshot,
	// and re-reading them per line would make a listing inconsistent with
	// itself.
	uint8_t x = nes->cpu ? nes->cpu->x : 0;
	uint8_t y = nes->cpu ? nes->cpu->y : 0;

	bool is_jump = (strcmp(name, "JMP") == 0);
	bool is_call = (strcmp(name, "JSR") == 0);

	if (entry->illegal) {
		out->flags |= CNES_DEBUG_INSN_ILLEGAL;
	}
	if (is_jump) {
		out->flags |= CNES_DEBUG_INSN_JUMP | CNES_DEBUG_INSN_STOP;
	}
	if (is_call) {
		out->flags |= CNES_DEBUG_INSN_CALL;
	}
	if (strcmp(name, "RTS") == 0 || strcmp(name, "RTI") == 0) {
		out->flags |= CNES_DEBUG_INSN_RETURN | CNES_DEBUG_INSN_STOP;
	}
	if (strcmp(name, "BRK") == 0 || strcmp(name, "KIL") == 0) {
		out->flags |= CNES_DEBUG_INSN_STOP;
	}

	// Where the instruction points. A jump or a call names a code address; the
	// rest name a byte, worth resolving through whatever indexing and
	// indirection the mode applies. Negative means the mode names neither.
	int32_t effective = -1;

	switch ((cnes_debug_addrmode_t)entry->addressing_mode) {
		case CNES_DEBUG_MODE_REL:
			out->flags |= CNES_DEBUG_INSN_BRANCH | CNES_DEBUG_INSN_TARGET;
			out->target = (uint16_t)(address + 2 + (int8_t)lo);
			break;

		case CNES_DEBUG_MODE_IND:
			out->flags |= CNES_DEBUG_INSN_TARGET;
			out->target = cnes_peek_pointer_buggy(nes, word);
			break;

		case CNES_DEBUG_MODE_ABS:
			if (is_jump || is_call) {
				out->flags |= CNES_DEBUG_INSN_TARGET;
				out->target = word;
			} else {
				effective = word;
			}
			break;

		case CNES_DEBUG_MODE_ZP:   effective = lo; break;
		case CNES_DEBUG_MODE_ZPX:  effective = (uint8_t)(lo + x); break;
		case CNES_DEBUG_MODE_ZPY:  effective = (uint8_t)(lo + y); break;
		case CNES_DEBUG_MODE_ABSX: effective = (uint16_t)(word + x); break;
		case CNES_DEBUG_MODE_ABSY: effective = (uint16_t)(word + y); break;
		case CNES_DEBUG_MODE_IZX:  effective = cnes_peek_pointer_zp(nes, (uint8_t)(lo + x)); break;
		case CNES_DEBUG_MODE_IZY:  effective = (uint16_t)(cnes_peek_pointer_zp(nes, lo) + y); break;

		default: break;
	}

	if (effective >= 0) {
		out->flags |= CNES_DEBUG_INSN_EFFECTIVE;
		out->effective = (uint16_t)effective;
		out->value     = BUS_Peek(nes, out->effective);
	}

	// The operand text, without the mnemonic: a listing wants its own column
	// for it, and joining the two is a one-liner for anything that does not.
	switch ((cnes_debug_addrmode_t)entry->addressing_mode) {
		case CNES_DEBUG_MODE_ACC:  snprintf(out->operand, sizeof(out->operand), "A"); break;
		case CNES_DEBUG_MODE_IMM:  snprintf(out->operand, sizeof(out->operand), "#$%02X", lo); break;
		case CNES_DEBUG_MODE_ZP:   snprintf(out->operand, sizeof(out->operand), "$%02X", lo); break;
		case CNES_DEBUG_MODE_ZPX:  snprintf(out->operand, sizeof(out->operand), "$%02X,X", lo); break;
		case CNES_DEBUG_MODE_ZPY:  snprintf(out->operand, sizeof(out->operand), "$%02X,Y", lo); break;
		case CNES_DEBUG_MODE_ABS:  snprintf(out->operand, sizeof(out->operand), "$%04X", word); break;
		case CNES_DEBUG_MODE_ABSX: snprintf(out->operand, sizeof(out->operand), "$%04X,X", word); break;
		case CNES_DEBUG_MODE_ABSY: snprintf(out->operand, sizeof(out->operand), "$%04X,Y", word); break;
		case CNES_DEBUG_MODE_IND:  snprintf(out->operand, sizeof(out->operand), "($%04X)", word); break;
		case CNES_DEBUG_MODE_IZX:  snprintf(out->operand, sizeof(out->operand), "($%02X,X)", lo); break;
		case CNES_DEBUG_MODE_IZY:  snprintf(out->operand, sizeof(out->operand), "($%02X),Y", lo); break;
		// Branch displacements are far more useful resolved than as an offset.
		case CNES_DEBUG_MODE_REL:  snprintf(out->operand, sizeof(out->operand), "$%04X", out->target); break;
		default:                   out->operand[0] = '\0'; break;
	}

	return length;
}

size_t cnes_debug_disassemble_range(uint16_t address, cnes_debug_insn_t *out, size_t count)
{
	if (out == NULL || count == 0) {
		return 0;
	}

	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL) {
		cnes_backend_unlock_nes();
		return 0;
	}

	size_t   written = 0;
	uint32_t pc      = address;

	// Stops at the top of the address space rather than wrapping: a listing
	// that ran off $FFFF back round to $0000 would look like ordinary code
	// following on, which it is not.
	while (written < count && pc <= 0xFFFF) {
		pc += cnes_dis_decode(nes, (uint16_t)pc, &out[written]);
		written++;
	}

	cnes_backend_unlock_nes();
	return written;
}

// How far back a rewind is willing to look, in instructions. A caller asking
// for more than a screenful is not scrolling, and the sweep below is quadratic
// in this.
#define CNES_DIS_REWIND_MAX 256

// Misaligned sweeps overshoot `address` by one or two bytes, so shifting the
// start by that much is normally all it takes to resynchronise. Three attempts
// covers it and keeps the work bounded when nothing will ever line up --
// scrolling back into a table of data, say.
#define CNES_DIS_REWIND_ATTEMPTS 3

uint16_t cnes_debug_disassemble_rewind(uint16_t address, uint32_t count)
{
	if (count == 0 || address == 0) {
		return address;
	}
	if (count > CNES_DIS_REWIND_MAX) {
		count = CNES_DIS_REWIND_MAX;
	}

	NES *nes = cnes_backend_lock_nes();
	if (nes == NULL) {
		cnes_backend_unlock_nes();
		return address;
	}

	// The furthest back `count` instructions can possibly begin, plus slack so
	// a sweep has room to settle into the real alignment before it arrives.
	uint32_t span  = count * 3u + 8u;
	uint32_t start = (address > span) ? (uint32_t)address - span : 0u;

	uint16_t boundaries[CNES_DIS_REWIND_MAX * 3 + 8];
	uint16_t best  = address;
	bool     found = false;

	for (uint32_t attempt = 0; attempt < CNES_DIS_REWIND_ATTEMPTS && start < address; ++attempt, ++start) {
		size_t   n  = 0;
		uint32_t pc = start;

		while (pc < address) {
			boundaries[n++] = (uint16_t)pc;
			pc += g_opcode_info[BUS_Peek(nes, (uint16_t)pc)].size;
		}

		uint16_t candidate = (n >= count) ? boundaries[n - count] : boundaries[0];

		// The first attempt's answer is kept as a fallback so a stretch of
		// bytes that never lines up still scrolls somewhere sensible.
		if (attempt == 0) {
			best = candidate;
		}

		// Landing exactly on `address` means this alignment agrees with the one
		// the listing is already showing, which is the whole test.
		if (pc == address) {
			best  = candidate;
			found = true;
			break;
		}
	}

	cnes_backend_unlock_nes();

	// Nothing synchronised: back off by one byte so the view still moves.
	if (!found && best == address) {
		best = (uint16_t)(address - 1);
	}
	return best;
}

size_t cnes_debug_disassemble(uint16_t address, char *buf, size_t buf_size)
{
	if (buf == NULL || buf_size == 0) {
		return 0;
	}
	buf[0] = '\0';

	cnes_debug_insn_t insn;
	if (cnes_debug_disassemble_range(address, &insn, 1) == 0) {
		return 0;
	}

	if (insn.operand[0] != '\0') {
		snprintf(buf, buf_size, "%s %s", insn.mnemonic, insn.operand);
	} else {
		snprintf(buf, buf_size, "%s", insn.mnemonic);
	}

	return insn.length;
}

uint8_t cnes_debug_controller(uint32_t port)
{
	if (port > 1u) {
		return 0;
	}

	NES *nes = cnes_backend_lock_nes();
	const uint8_t state = (nes != NULL) ? nes->controllers[port] : 0u;
	cnes_backend_unlock_nes();

	return state;
}
