add_subdirectory(SDL EXCLUDE_FROM_ALL)

# CJSON
set(BUILD_SHARED_AND_STATIC_LIBS TRUE)
set(CJSON_BUILD_SHARED_LIBS FALSE)
set(ENABLE_CJSON_TEST FALSE)
set(ENABLE_CUSTOM_COMPILER_FLAGS OFF CACHE BOOL "Disable cJSON strict warning flags" FORCE)

add_subdirectory(cJSON EXCLUDE_FROM_ALL)
#target_include_directories(cjson public ext/cjson)
target_include_directories(cjson PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cjson>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/cjson>
)

# MINIZIP
set(SKIP_INSTALL_ALL ON)
add_subdirectory(minizip-ng EXCLUDE_FROM_ALL)

#STB
#add_library(stb INTERFACE)
#if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/stb")
#    target_include_directories(stb INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stb")
#else()
#    message(WARNING "stb headers not found at ext/stb; stb target provides no include path")
#endif()

# IMGUI
project(imgui LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB IMGUI_SOURCES
        cimgui/imgui/imgui.cpp
        cimgui/imgui/imgui_draw.cpp
        cimgui/imgui/imgui_demo.cpp
        cimgui/imgui/imgui_widgets.cpp
        cimgui/imgui/imgui_tables.cpp)

add_library(imgui SHARED ${IMGUI_SOURCES})

target_include_directories(imgui PUBLIC cimgui/imgui)
target_include_directories(imgui PUBLIC cimgui/imgui/backends)
target_include_directories(imgui PUBLIC sdl/include)

if (WIN32)
    target_link_libraries(imgui PRIVATE imm32)
    #target_compile_definitions(imgui PUBLIC IMGUI_IMPL_API="extern \"C\" __declspec(dllexport)")
else()
    #target_compile_definitions(imgui PUBLIC IMGUI_IMPL_API="extern \"C\"")
endif()

target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS=1)

target_link_libraries(imgui PUBLIC SDL3-shared)

set_target_properties(imgui PROPERTIES PREFIX "")

# CIMGUI
project(cimgui LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
file(READ cimgui/cimgui_impl.h cimgui_impl)

string(FIND "${cimgui_impl}" CIMGUI_USE_SDL3 rendbk_position)
string(FIND "${cimgui_impl}" CIMGUI_USE_SDLGPU3 platbk_position)
# If we don't find it, rendbk_position will be -1
if(rendbk_position EQUAL -1 OR platbk_position EQUAL -1)
    #get compiler name
    cmake_path(GET CMAKE_C_COMPILER FILENAME C_COMP)
    cmake_path(REMOVE_EXTENSION C_COMP)
    #get includes string
    execute_process(
    COMMAND luajit generator.lua ${C_COMP} "internal noimstrv" sdl3 sdlgpu3 "-I${CMAKE_CURRENT_SOURCE_DIR}/sdl/include"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cimgui/generator
    ERROR_VARIABLE error_command
    RESULT_VARIABLE build_command_result
    )
    if(NOT ${build_command_result} EQUAL 0)
        message(STATUS "cimgui generation failed: Do you have LuaJIT?")
        message(STATUS "cimgui generation failed: ${build_command_result}")
        message(STATUS "error_command: ${error_command}")
    endif()
endif()

add_library(cimgui SHARED 
    cimgui/cimgui.cpp
    cimgui/cimgui_impl.cpp
    cimgui/imgui/backends/imgui_impl_sdl3.cpp
    cimgui/imgui/backends/imgui_impl_sdlgpu3.cpp
)

target_include_directories(cimgui INTERFACE cimgui)
target_include_directories(cimgui PRIVATE cimgui/imgui)
target_include_directories(cimgui PRIVATE cimgui/generator/output/)
target_include_directories(cimgui PUBLIC cimgui/imgui/backends/)

#target_compile_definitions(cimgui PUBLIC CIMGUI_USE_SDL3 CIMGUI_USE_SDLGPU3)

#target_compile_definitions(cimgui PUBLIC "-DIMGUI_USER_CONFIG=\"cimgui/cimconfig.h\"")

if (WIN32)
    target_link_libraries(imgui PRIVATE imm32)
    target_compile_definitions(cimgui PUBLIC IMGUI_IMPL_API=extern\t\"C\"\t__declspec\(dllexport\))
else()
    target_compile_definitions(cimgui PUBLIC IMGUI_IMPL_API=extern\t\"C\"\t)
endif()

target_compile_definitions(cimgui PUBLIC IMGUI_USE_WCHAR32)

target_link_libraries(cimgui PUBLIC imgui)
target_link_libraries(cimgui PRIVATE stdc++ m)

set_target_properties(cimgui PROPERTIES PREFIX "")

#CIMPLOT
project(cimplot LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB CIMPLOT_SOURCES
    cimplot/cimplot.cpp
    cimplot/implot/implot_demo.cpp
    cimplot/implot/implot_items.cpp
    cimplot/implot/implot.cpp
)

add_library(cimplot SHARED ${CIMPLOT_SOURCES})

target_include_directories(cimplot PUBLIC cimplot)
target_include_directories(cimplot PUBLIC cimgui)
target_include_directories(cimplot PRIVATE cimgui/imgui)

target_link_libraries(cimplot PUBLIC cimgui imgui)

set_target_properties(cimplot PROPERTIES PREFIX "")

# LUA / LUAJIT
set(USE_LUAJIT 0 CACHE BOOL "Enable LuaJIT scripting" FORCE)
set(LUA_LIBS "" CACHE STRING "Lua libraries to link" FORCE)
set(LUA_INCLUDE "" CACHE STRING "Lua include directory" FORCE)
set(LUA_BUILD_EXE 0 CACHE BOOL "Build Lua executable" FORCE)
set(LUAJIT_BUILD_EXE 0 CACHE BOOL "Build LuaJIT executable" FORCE)

set(LUAJIT_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/luajit-cmake")
set(LUAJIT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/LuaJIT")

message(STATUS "Building LuaJIT from subprojects: ext/luajit-cmake + ext/LuaJIT")
set(LUAJIT_DIR "${LUAJIT_SOURCE_DIR}" CACHE PATH "LuaJIT source root" FORCE)
add_subdirectory(luajit-cmake)
set(USE_LUAJIT 1 CACHE BOOL "Enable LuaJIT scripting" FORCE)
set(LUA_LIBS luajit::lib CACHE STRING "Lua libraries to link" FORCE)
set(LUA_INCLUDE "${LUAJIT_SOURCE_DIR}/src" CACHE STRING "Lua include directory" FORCE)