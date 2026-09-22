# nestest (Kevtris) - the reference 6502 test ROM.
#
# Three angles on the same ROM:
#   result    automation mode, decoding the $02/$03 failure codes
#   menu      the ROM run normally, reading its per-group results off screen
#   trace     instruction-by-instruction against Nintendulator's golden log

cnes_add_test(
	NAME cnes_nestest_result
	ROM nestest/nestest.nes
	SCRIPT nestest/nestest.lua
	TIMEOUT 60
	LABELS "nestest;cpu"
)

cnes_add_test(
	NAME cnes_nestest_menu
	ROM nestest/nestest.nes
	SCRIPT nestest/nestest_ui.lua
	ARGS page=normal
	TIMEOUT 120
	LABELS "nestest;cpu;ppu"
)

cnes_add_test(
	NAME cnes_nestest_menu_invalid_ops
	ROM nestest/nestest.nes
	SCRIPT nestest/nestest_ui.lua
	ARGS page=invalid
	TIMEOUT 120
	LABELS "nestest;cpu;ppu;illegal"
)

# Registers only: the sharpest signal for "which instruction is wrong".
cnes_add_test(
	NAME cnes_nestest_trace
	ROM nestest/nestest.nes
	SCRIPT nestest/nestest_trace.lua
	TIMEOUT 120
	LABELS "nestest;cpu;trace"
)

# Registers plus cycle counts: instruction timing, which the result codes and
# the register trace both miss.
cnes_add_test(
	NAME cnes_nestest_trace_cycles
	ROM nestest/nestest.nes
	SCRIPT nestest/nestest_trace.lua
	ARGS cycles=1
	TIMEOUT 120
	LABELS "nestest;cpu;trace;timing"
)
