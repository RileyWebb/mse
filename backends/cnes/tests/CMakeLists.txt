# cNES accuracy test suite.
#
# The runner is built straight from the cNES core sources with Lua scripting
# enabled, so tests can drive the console from script without changing what the
# shipped backend DLL contains.
#
# Tests run with this directory as their working directory, so ROMs, scripts and
# baselines are used in place -- there is no staging copy to go stale.

# backend.c is the libmse plugin entry point and only makes sense inside the
# shared backend; the runner talks to the NES core directly. src/external goes
# with it, since everything there reaches the emulator through backend.c's lock.
list(FILTER CNES_CORE_SOURCES EXCLUDE REGEX "/(backend\\.c$|external/)")

add_executable(cNES_headless
    cnes_headless.c
    ${CNES_CORE_SOURCES}
    ${CNES_SCRIPTING_SOURCES}
)

target_include_directories(cNES_headless PRIVATE
    ${CNES_CORE_INCLUDE_DIRS}
    ${LUA_INCLUDE}
)

target_compile_definitions(cNES_headless PRIVATE USE_LUAJIT=1)

target_link_libraries(cNES_headless PRIVATE libmse libluajit pthread)

if(NOT WIN32)
    target_link_libraries(cNES_headless PRIVATE m)
endif()

set(CNES_TESTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# cnes_add_test(NAME <name> ROM <path> SCRIPT <path>
#               [TIMEOUT <seconds>] [LABELS <label>...] [ARGS <key=value>...])
#
# ROM and SCRIPT are relative to this directory. Verdicts come from the process
# exit code, never from matching stdout, so a crashed or timed-out run cannot be
# mistaken for a pass.
function(cnes_add_test)
    set(one_value_args NAME ROM SCRIPT TIMEOUT)
    set(multi_value_args LABELS ARGS)
    cmake_parse_arguments(T "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT T_NAME OR NOT T_ROM OR NOT T_SCRIPT)
        message(FATAL_ERROR "cnes_add_test requires NAME, ROM and SCRIPT")
    endif()

    if(NOT EXISTS ${CNES_TESTS_ROOT}/${T_ROM})
        message(WARNING "cnes_add_test(${T_NAME}): ROM not found, skipping: ${T_ROM}")
        return()
    endif()

    set(script_args "")
    foreach(arg IN LISTS T_ARGS)
        list(APPEND script_args --arg ${arg})
    endforeach()

    add_test(
        NAME ${T_NAME}
        COMMAND $<TARGET_FILE:cNES_headless> ${T_ROM} --script ${T_SCRIPT} ${script_args}
    )

    if(NOT T_TIMEOUT)
        set(T_TIMEOUT 120)
    endif()

    set_tests_properties(${T_NAME} PROPERTIES
        WORKING_DIRECTORY ${CNES_TESTS_ROOT}
        TIMEOUT ${T_TIMEOUT}
        LABELS "cnes;${T_LABELS}"
    )
endfunction()


add_subdirectory(nestest)
add_subdirectory(AccuracyCoin)
add_subdirectory(blargg)

# CTest prints a test's output only when it fails, and these tests pass by
# design while individual ROM checks still fail (see the baselines in
# README.md). These targets run the suite with every result on screen.
add_custom_target(cnes_accuracycoin_report
	COMMAND $<TARGET_FILE:cNES_headless> AccuracyCoin/AccuracyCoin.nes
		--script AccuracyCoin/accuracycoin.lua --arg page=all
	WORKING_DIRECTORY ${CNES_TESTS_ROOT}
	DEPENDS cNES_headless
	USES_TERMINAL
	COMMENT "AccuracyCoin: every test on every page"
)

add_custom_target(cnes_test_report
	COMMAND ${CMAKE_CTEST_COMMAND} --verbose --label-regex cnes
	WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
	DEPENDS cNES_headless
	USES_TERMINAL
	COMMENT "Running the cNES suite with full per-test output"
)
