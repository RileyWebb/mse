cmake_minimum_required(VERSION 3.10)
project(test_backend C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB TEST_BACKEND_SOURCES
    "src/*.c"
)

add_library(test_backend_plugin SHARED ${TEST_BACKEND_SOURCES})

# Project-wide public include directory
target_include_directories(test_backend_plugin PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Make plugin have no 'lib' prefix on Unix-like platforms
set_target_properties(test_backend_plugin PROPERTIES PREFIX "" OUTPUT_NAME "libemulator")

if(WIN32)
    set_target_properties(test_backend_plugin PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/test_backend/lib/windows)
else()
    set_target_properties(test_backend_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/test_backend/lib/linux)
endif()

target_compile_definitions(test_backend_plugin PRIVATE LIBMSE_EXPORTS)

# Example: link to common deps if needed (uncomment/adjust)
# target_link_libraries(test_backend PUBLIC cjson)

message(STATUS "Configured test_backend with sources: ${TEST_BACKEND_SOURCES}")
