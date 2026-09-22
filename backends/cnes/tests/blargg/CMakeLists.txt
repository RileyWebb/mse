# blargg's test ROMs.
#
# One script covers all of them: they share a status protocol in PRG RAM, and
# where the mapper provides no RAM at $6000 the same report is read off screen.
#
# The ROMs live in their original directories next to their readmes, so the
# paths below point back out of this one.
#
# Not wired up: cpu_flag_concurrency/test_cpu_flag_concurrency.nes. It is a
# data-collection ROM that prints a table and asks the reader to report what
# they see; it has no pass/fail verdict to check.

# cnes_add_blargg(<test name> <rom path> [LABELS ...] [ARGS ...] [TIMEOUT n])
function(cnes_add_blargg name rom)
	cmake_parse_arguments(B "" "TIMEOUT" "LABELS;ARGS" ${ARGN})

	cnes_add_test(
		NAME cnes_blargg_${name}
		ROM ${rom}
		SCRIPT blargg/blargg.lua
		ARGS name=${name} ${B_ARGS}
		TIMEOUT ${B_TIMEOUT}
		LABELS "blargg;${B_LABELS}"
	)
endfunction()

# instr_test-v5: every addressing mode and instruction, official and unofficial.
# The singles are what you want when something breaks - each names one area.
set(INSTR_SINGLES
	01-basics 02-implied 03-immediate 04-zero_page 05-zp_xy 06-absolute
	07-abs_xy 08-ind_x 09-ind_y 10-branches 11-stack 12-jmp_jsr 13-rts
	14-rti 15-brk 16-special
)

foreach(single IN LISTS INSTR_SINGLES)
	cnes_add_blargg(instr_${single}
		instr_test-v5/rom_singles/${single}.nes
		LABELS "cpu;instr"
		TIMEOUT 90
	)
endforeach()

cnes_add_blargg(instr_official_only
	instr_test-v5/official_only.nes
	LABELS "cpu;instr;slow"
	ARGS frames=20000
	TIMEOUT 300
)

cnes_add_blargg(instr_all
	instr_test-v5/all_instrs.nes
	LABELS "cpu;instr;illegal;slow"
	ARGS frames=20000
	TIMEOUT 300
)

# Instruction and branch cycle counts.
cnes_add_blargg(instr_timing_all
	instr_timing/instr_timing.nes
	LABELS "cpu;timing;slow"
	ARGS frames=20000
	TIMEOUT 300
)

cnes_add_blargg(instr_timing_instructions
	instr_timing/rom_singles/1-instr_timing.nes
	LABELS "cpu;timing"
	ARGS frames=20000
	TIMEOUT 180
)

cnes_add_blargg(instr_timing_branches
	instr_timing/rom_singles/2-branch_timing.nes
	LABELS "cpu;timing"
	TIMEOUT 120
)

# Odd corners: dummy reads and writes, interrupt behaviour, ROM-detected
# hardware quirks.
cnes_add_blargg(instr_misc
	instr_misc/instr_misc.nes
	LABELS "cpu;instr"
	TIMEOUT 120
)

cnes_add_blargg(cpu_interrupts
	cpu_interrupts/cpu_interrupts.nes
	LABELS "cpu;interrupts"
	TIMEOUT 180
)

cnes_add_blargg(cpu_dummy_reads
	cpu_dummy_reads/cpu_dummy_reads.nes
	LABELS "cpu;dummy"
	TIMEOUT 90
)

cnes_add_blargg(cpu_dummy_writes_oam
	cpu_dummy_writes/cpu_dummy_writes_oam.nes
	LABELS "cpu;dummy;ppu"
	TIMEOUT 120
)

cnes_add_blargg(cpu_dummy_writes_ppumem
	cpu_dummy_writes/cpu_dummy_writes_ppumem.nes
	LABELS "cpu;dummy;ppu"
	TIMEOUT 120
)

# Whole-instruction cycle timing; the ROM itself says it takes 16 seconds.
cnes_add_blargg(cpu_timing
	cpu_timing_test6/cpu_timing_test.nes
	LABELS "cpu;timing"
	ARGS frames=3000
	TIMEOUT 120
)

# Branch cycle counts, including the page-cross penalty.
cnes_add_blargg(branch_basics
	branch_timing_tests/1.Branch_Basics.nes
	LABELS "cpu;timing;branch"
	TIMEOUT 90
)

cnes_add_blargg(branch_backward
	branch_timing_tests/2.Backward_Branch.nes
	LABELS "cpu;timing;branch"
	TIMEOUT 90
)

cnes_add_blargg(branch_forward
	branch_timing_tests/3.Forward_Branch.nes
	LABELS "cpu;timing;branch"
	TIMEOUT 90
)
