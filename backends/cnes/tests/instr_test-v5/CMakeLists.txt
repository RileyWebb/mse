set(NESTEST_ROM "tests/instr_test-v5/all_instrs.nes")
set(NESTEST_SCRIPT "tests/instr_test-v5/instr_test.lua")

add_test(
	NAME cNES_instr_test_v5
	COMMAND $<TARGET_FILE:cNES_headless> ${NESTEST_ROM} --script ${NESTEST_SCRIPT}
)

set_tests_properties(cNES_instr_test_v5 PROPERTIES
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
	TIMEOUT 30
	PASS_REGULAR_EXPRESSION "InstrTest Succeeded\\."
	FAIL_REGULAR_EXPRESSION "InstrTest Failed"
	WILL_FAIL FALSE
)