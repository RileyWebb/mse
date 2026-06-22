set(NESTEST_ROM "tests/nestest/nestest.nes")
set(NESTEST_SCRIPT "tests/nestest/nestest.lua")

add_test(
	NAME cNES_nestest
	COMMAND $<TARGET_FILE:cNES_headless> ${NESTEST_ROM} --script ${NESTEST_SCRIPT}
)

set_tests_properties(cNES_nestest PROPERTIES
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
	TIMEOUT 30
	PASS_REGULAR_EXPRESSION "Nestest Succeeded\\."
	FAIL_REGULAR_EXPRESSION "Nestest Failed"
	WILL_FAIL FALSE
)
