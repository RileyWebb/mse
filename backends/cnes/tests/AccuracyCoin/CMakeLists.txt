set(ACCURACYCOIN_ROM "tests/AccuracyCoin/AccuracyCoin.nes")

add_test(
        NAME cNES_accuracycoin_0_CPU_Behavior
        COMMAND cNES_headless ${ACCURACYCOIN_ROM} --script tests/AccuracyCoin/test_page_0_CPU_Behavior.lua
        --build-target cNES_headless
)
set_tests_properties(cNES_accuracycoin_0_CPU_Behavior PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
        WILL_FAIL FALSE
)

add_test(
        NAME cNES_accuracycoin_1_Addressing_Mode_Wraparound
        COMMAND cNES_headless ${ACCURACYCOIN_ROM} --script tests/AccuracyCoin/test_page_1_Addressing_Mode_Wraparound.lua
        --build-target cNES_headless
)
set_tests_properties(cNES_accuracycoin_1_Addressing_Mode_Wraparound PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
        WILL_FAIL FALSE
)

add_test(
        NAME cNES_accuracycoin_2_CPU_Interrupts
        COMMAND cNES_headless ${ACCURACYCOIN_ROM} --script tests/AccuracyCoin/test_page_2_CPU_Interrupts.lua
        --build-target cNES_headless
)
set_tests_properties(cNES_accuracycoin_2_CPU_Interrupts PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
        WILL_FAIL FALSE
)

add_test(
        NAME cNES_accuracycoin_3_APU_Registers_and_DMA_tests
        COMMAND cNES_headless ${ACCURACYCOIN_ROM} --script tests/AccuracyCoin/test_page_3_APU_Registers_and_DMA_tests.lua
        --build-target cNES_headless
)
set_tests_properties(cNES_accuracycoin_3_APU_Registers_and_DMA_tests PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
        WILL_FAIL FALSE
)

add_test(
        NAME cNES_accuracycoin_4_APU_Tests
        COMMAND cNES_headless ${ACCURACYCOIN_ROM} --script tests/AccuracyCoin/test_page_4_APU_Tests.lua
        --build-target cNES_headless
)
set_tests_properties(cNES_accuracycoin_4_APU_Tests PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
        WILL_FAIL FALSE
)

add_test(
        NAME cNES_accuracycoin_5_Power_On_State
        COMMAND cNES_headless ${ACCURACYCOIN_ROM} --script tests/AccuracyCoin/test_page_5_Power_On_State.lua
        --build-target cNES_headless
)
set_tests_properties(cNES_accuracycoin_5_Power_On_State PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
        WILL_FAIL FALSE
)

add_test(
        NAME cNES_accuracycoin_6_PPU_Behavior
        COMMAND cNES_headless ${ACCURACYCOIN_ROM} --script tests/AccuracyCoin/test_page_6_PPU_Behavior.lua
        --build-target cNES_headless
)
set_tests_properties(cNES_accuracycoin_6_PPU_Behavior PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
        WILL_FAIL FALSE
)

add_test(
        NAME cNES_accuracycoin_7_PPU_VBlank_Timing
        COMMAND cNES_headless ${ACCURACYCOIN_ROM} --script tests/AccuracyCoin/test_page_7_PPU_VBlank_Timing.lua
        --build-target cNES_headless
)
set_tests_properties(cNES_accuracycoin_7_PPU_VBlank_Timing PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
        WILL_FAIL FALSE
)

add_test(
        NAME cNES_accuracycoin_8_Sprite_Evaluation
        COMMAND cNES_headless ${ACCURACYCOIN_ROM} --script tests/AccuracyCoin/test_page_8_Sprite_Evaluation.lua
        --build-target cNES_headless
)
set_tests_properties(cNES_accuracycoin_8_Sprite_Evaluation PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
        WILL_FAIL FALSE
)

add_test(
        NAME cNES_accuracycoin_9_PPU_Misc
        COMMAND cNES_headless ${ACCURACYCOIN_ROM} --script tests/AccuracyCoin/test_page_9_PPU_Misc..lua
        --build-target cNES_headless
)
set_tests_properties(cNES_accuracycoin_9_PPU_Misc PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
        WILL_FAIL FALSE
)

add_test(
        NAME cNES_accuracycoin_10_CPU_Behavior_2
        COMMAND cNES_headless ${ACCURACYCOIN_ROM} --script tests/AccuracyCoin/test_page_10_CPU_Behavior_2.lua
        --build-target cNES_headless
)
set_tests_properties(cNES_accuracycoin_10_CPU_Behavior_2 PROPERTIES
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
        WILL_FAIL FALSE
)
