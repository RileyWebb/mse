cmake_minimum_required(VERSION 3.26)
project(cnes C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

include(cmake/LuaDeps.cmake)

file(GLOB CNES_BACKEND_SOURCES CONFIGURE_DEPENDS
    "src/*.c"
    "src/external/*.c"
    "src/loaders/*.c"
    "src/scripting/*.c"
)

add_library(cnes_backend SHARED ${CNES_BACKEND_SOURCES})
target_include_directories(cnes_backend PUBLIC ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(cnes_backend PROPERTIES PREFIX "" OUTPUT_NAME "libemulator")

if(WIN32)
    set_target_properties(cnes_backend PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cnes/lib/windows)
else()
    set_target_properties(cnes_backend PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cnes/lib/linux)
endif()

if(MSVC)
    target_compile_options(cnes_backend PRIVATE /W4)
else()
    target_compile_options(cnes_backend PRIVATE -Wall)
endif()

target_compile_definitions(cnes_backend PRIVATE LIBMSE_EXPORTS)

target_link_libraries(cnes_backend PRIVATE libmse Threads::Threads)

if(NOT WIN32)
    target_link_libraries(cnes_backend PRIVATE m)
endif()

message(STATUS "Configured cnes backend with sources: ${CNES_BACKEND_SOURCES}")

add_custom_target(cnes_copy_data
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/data/
    ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cnes/data/)

add_dependencies(cnes_backend cnes_copy_data xml2lua)

set(CNES_CORE_SOURCES ${CNES_BACKEND_SOURCES}
    CACHE INTERNAL "cNES core sources")
set(CNES_SCRIPTING_SOURCES ""
    CACHE INTERNAL "cNES Lua scripting sources (now part of CNES_CORE_SOURCES)")
set(CNES_CORE_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    CACHE INTERNAL "cNES core include directories")
