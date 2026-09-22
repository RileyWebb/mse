// Instruction dispatch for CPU_Step.
//
// Included by cpu.c, which forward declares every addressing mode, every
// operation and CPU_PollInterrupts above the include. It is a .inc rather than
// its own translation unit on purpose: the whole point of the file is that the
// addressing mode and the operation are inlined into each opcode's block, and a
// separate .c would put a call boundary back exactly where this removes one.
//
// What it replaces
// ----------------
// CPU_Step used to fetch a CPU_Opcode row from a 256-entry table and make two
// indirect calls through it -- addr_mode_handlers[inst->mode](...) and then
// inst->operation(...). Both dispatched from a single site in the binary, and
// an indirect branch predictor keys on the branch's own address: one site
// jumping to ~70 different operations in program-dependent order trains badly,
// and each miss costs the best part of twenty cycles.
//
// Here every encoding gets its own block, and the mode, the operation, the
// cycle count and the page-cross penalty arrive as compile-time constants. That
// buys three separate things, in rough order of how much they are worth:
//
//   1. The addressing mode inlines, so addr_flags is a constant for the ten
//      modes that cannot index. The whole dummy-read ladder below folds away
//      for them rather than being tested at runtime on every instruction.
//   2. The operation is a direct call, which the compiler is free to inline,
//      and the cycles_ref round-trip through memory can stay in a register.
//   3. Nothing is loaded from a dispatch table at all -- the four fields that
//      used to live in CPU_Opcode are immediates now, so the table and the
//      load that read it are both gone.
//
// Portability: labels-as-values is a GCC/Clang extension. MSVC gets the switch
// below instead, generated from the same rows, so the two can never disagree
// about what an encoding does.

#if defined(__GNUC__) || defined(__clang__)
#define CPU_COMPUTED_GOTO 1
#else
#define CPU_COMPUTED_GOTO 0
#endif

// One opcode's worth of work, with everything about the encoding constant.
//
// This is the body of the old CPU_Step verbatim, with inst->mode, inst->cycles
// and inst->add_cycles_on_page_cross substituted in. Both dummy-read branches
// are kept as they were: a carried index makes the 6502 read the address it
// computed before the carry propagated, and a store or read-modify-write does
// the same read even when the index did not carry.
#define CPU_EXEC(opfn, mode, cyc, pgx)                                       \
	do {                                                                     \
		uint8_t	 addr_flags		   = 0;                                      \
		uint16_t effective_address = CPU_ADDR_##mode(cpu, &addr_flags);      \
                                                                             \
		current_opcode_cycles = (cyc);                                       \
		cpu->dummy_read_dma	  = false;                                       \
                                                                             \
		if (addr_flags & CPU_ADDR_FLAG_PAGE_CROSSED) {                       \
			const uint64_t stalls_before = cpu->stall_cycles;                \
			CPU_ReadCycle(cpu, (uint16_t)(effective_address - 0x0100));      \
			cpu->dummy_read_dma = (cpu->stall_cycles != stalls_before);      \
			current_opcode_cycles += (pgx);                                  \
		} else if ((addr_flags & CPU_ADDR_FLAG_INDEXED) && !(pgx)) {         \
			const uint64_t stalls_before = cpu->stall_cycles;                \
			CPU_ReadCycle(cpu, effective_address);                           \
			cpu->dummy_read_dma = (cpu->stall_cycles != stalls_before);      \
		}                                                                    \
                                                                             \
		opfn(cpu, effective_address, &current_opcode_cycles);                \
	} while (0)

int CPU_Step(CPU *cpu)
{
	// The cached subsystem pointers are only safe while nothing reallocates a
	// subsystem behind the CPU's back. Nothing does today; this is here so that
	// the day something tries, it fails loudly instead of reading freed memory.
	// Plain assert rather than DEBUG_ASSERT: that macro logs unconditionally,
	// even in release builds, which is not something this path can afford.
	assert(cpu->bus == cpu->nes->bus && cpu->ppu == cpu->nes->ppu && cpu->apu == cpu->nes->apu);

	const uint64_t cycles_at_entry = cpu->total_cycles;
	const uint64_t stalls_at_entry = cpu->stall_cycles;

	CPU_ServicePendingDMA(cpu);

	if (cpu->nmi_pending) {
		cpu->nmi_pending = false;
		CPU_NMI(cpu);
		return (int)(cpu->total_cycles - cycles_at_entry);
	}

	if (cpu->irq_line && !CPU_GetFlag(cpu, CPU_FLAG_INTERRUPT)) {
		CPU_IRQ(cpu);
		return (int)(cpu->total_cycles - cycles_at_entry);
	}

	// Written by whichever block runs, read by the cycle top-up below.
	uint8_t current_opcode_cycles = 0;

	const uint8_t opcode = CPU_ReadCycle(cpu, cpu->pc++);

#if CPU_COMPUTED_GOTO

	static const void *const dispatch[256] = {
#define CPU_OPCODE(code, opfn, mode, cyc, pgx) [code] = &&blk_##code,
#include "cpu_opcodes.inc"
#undef CPU_OPCODE
	};

	goto *dispatch[opcode];

#define CPU_OPCODE(code, opfn, mode, cyc, pgx) \
	blk_##code:                                \
	CPU_EXEC(opfn, mode, cyc, pgx);            \
	goto opcode_done;
#include "cpu_opcodes.inc"
#undef CPU_OPCODE

opcode_done:;

#else // Portable fallback: same rows, one ordinary jump table.

	switch (opcode) {
#define CPU_OPCODE(code, opfn, mode, cyc, pgx) \
	case code:                                 \
		CPU_EXEC(opfn, mode, cyc, pgx);        \
		break;
#include "cpu_opcodes.inc"
#undef CPU_OPCODE
	}

#endif

	// The per-access ticks above put every bus cycle at the right point in time.
	// Top up to the opcode's documented length to cover the internal cycles that
	// touch no address (JSR's spare cycle, a taken branch, implied operations).
	// DMA stalls are subtracted out first so they are not swallowed by the top-up.
	const uint64_t stalls  = cpu->stall_cycles - stalls_at_entry;
	const uint64_t elapsed = cpu->total_cycles - cycles_at_entry;
	const uint64_t target  = (uint64_t)current_opcode_cycles + stalls;
	if (elapsed < target) cpu->total_cycles += target - elapsed;

	CPU_PollInterrupts(cpu);

	return (int)(cpu->total_cycles - cycles_at_entry);
}

#undef CPU_EXEC
