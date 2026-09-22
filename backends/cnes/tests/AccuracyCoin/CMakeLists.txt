# AccuracyCoin - 138 accuracy tests plus 5 informational readouts on one NROM
# cartridge. One script drives every page; the page number is an argument.
#
# The page names below are the ROM's own, so `ctest -R accuracycoin` reads like
# the menu. A single page takes a second or two, which is what makes these worth
# running individually while working on one subsystem.

# accuracycoin_page(<zero-padded page> <name> [labels...])
function(accuracycoin_page number name)
	# The script takes the page number the ROM displays, without the padding
	# that keeps the test names sorting correctly.
	string(REGEX REPLACE "^0+" "" page_index "${number}")

	cnes_add_test(
		NAME cnes_accuracycoin_${number}_${name}
		ROM AccuracyCoin/AccuracyCoin.nes
		SCRIPT AccuracyCoin/accuracycoin.lua
		ARGS page=${page_index}
		TIMEOUT 120
		LABELS "accuracycoin;${ARGN}"
	)
endfunction()

accuracycoin_page(01 CPU_Behavior                cpu)
accuracycoin_page(02 Addressing_Mode_Wraparound  cpu)
accuracycoin_page(03 Unofficial_SLO              cpu illegal)
accuracycoin_page(04 Unofficial_RLA              cpu illegal)
accuracycoin_page(05 Unofficial_SRE              cpu illegal)
accuracycoin_page(06 Unofficial_RRA              cpu illegal)
accuracycoin_page(07 Unofficial_SAX_LAX          cpu illegal)
accuracycoin_page(08 Unofficial_DCP              cpu illegal)
accuracycoin_page(09 Unofficial_ISC              cpu illegal)
accuracycoin_page(10 Unofficial_SH               cpu illegal)
accuracycoin_page(11 Unofficial_Immediates       cpu illegal)
accuracycoin_page(12 CPU_Interrupts              cpu interrupts)
accuracycoin_page(13 APU_Registers_and_DMA       apu dma)
accuracycoin_page(14 APU_Tests                   apu)
accuracycoin_page(15 Power_On_State              ppu cpu)
accuracycoin_page(16 PPU_Behavior                ppu)
accuracycoin_page(17 PPU_VBlank_Timing           ppu timing)
accuracycoin_page(18 Sprite_Evaluation           ppu sprites)
accuracycoin_page(19 PPU_Misc                    ppu)
accuracycoin_page(20 CPU_Behavior_2              cpu timing)

# Every page in one session. Covered by the per-page tests, so it is labelled
# "slow" and can be skipped with `ctest -LE slow`.
cnes_add_test(
	NAME cnes_accuracycoin_all
	ROM AccuracyCoin/AccuracyCoin.nes
	SCRIPT AccuracyCoin/accuracycoin.lua
	ARGS page=all
	TIMEOUT 300
	LABELS "accuracycoin;slow"
)
