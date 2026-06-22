cmake_minimum_required(VERSION 3.10)
project(cnes C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB CNES_BACKEND_SOURCES
    "src/*.c"
    "src/loaders/*.c"
)

add_library(cnes_backend SHARED ${CNES_BACKEND_SOURCES})
target_include_directories(cnes_backend PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(cnes_backend PROPERTIES PREFIX "" OUTPUT_NAME "libemulator")

if(WIN32)
    set_target_properties(cnes_backend PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cnes/lib/windows)
else()
    set_target_properties(cnes_backend PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cnes/lib/linux)
endif()

target_compile_definitions(cnes_backend PRIVATE LIBMSE_EXPORTS)
# Link against libmse to provide DEBUG_WriteLog and other core symbols
target_link_libraries(cnes_backend PRIVATE libmse pthread)

if(NOT WIN32)
    target_link_libraries(cnes_backend PRIVATE m)
endif()

message(STATUS "Configured cnes backend with sources: ${CNES_BACKEND_SOURCES}")