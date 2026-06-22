add_executable(cNES_headless cnes_headless.c)

target_link_libraries(cNES_headless PUBLIC ${PLATFORM_LIBS} libcNES)

add_custom_target(copy_tests
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/tests)

add_dependencies(cNES_headless copy_tests copy_info)

add_subdirectory(AccuracyCoin)
add_subdirectory(nestest)
#add_subdirectory(cpu_dummy_writes)
add_subdirectory(instr_test-v5)