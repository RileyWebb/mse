# -----------------------------------------------------------------------------
# 1. FETCH FREETYPE
# -----------------------------------------------------------------------------
FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-14-3
)
FetchContent_MakeAvailable(freetype)

# -----------------------------------------------------------------------------
# 2. FETCH CIMGUI (Modern CMake 4.x Approach)
# -----------------------------------------------------------------------------
FetchContent_Declare(
    cimgui_fetch
    GIT_REPOSITORY https://github.com/cimgui/cimgui.git
    GIT_TAG        docking_inter # The correct docking branch
    SOURCE_SUBDIR  "prevent_auto_build" # Prevents FetchContent_MakeAvailable from calling add_subdirectory()
)

# Safely download the content without executing its native CMakeLists.txt
FetchContent_MakeAvailable(cimgui_fetch)

# Setup handy variables for the fetched paths
set(CIMGUI_DIR "${cimgui_fetch_SOURCE_DIR}")
set(IMGUI_DIR "${CIMGUI_DIR}/imgui")

# ==============================================================================
# IMGUI
# ==============================================================================
project(imgui LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
)

add_library(imgui SHARED ${IMGUI_SOURCES})

set(FETCHED_SDL3_INCLUDE "${sdl3_SOURCE_DIR}/include")

target_include_directories(imgui PUBLIC "${IMGUI_DIR}")
target_include_directories(imgui PUBLIC "${IMGUI_DIR}/backends")
target_include_directories(imgui PUBLIC "${FETCHED_SDL3_INCLUDE}")

if (WIN32)
    target_link_libraries(imgui PRIVATE imm32)
endif()

target_compile_definitions(imgui PUBLIC IMGUI_USE_WCHAR32)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS=1)
target_link_libraries(imgui PUBLIC SDL3-shared)
set_target_properties(imgui PROPERTIES PREFIX "")

# ==============================================================================
# CIMGUI
# ==============================================================================
project(cimgui LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Read the file directly from the repo root
file(READ "${CIMGUI_DIR}/cimgui_impl.h" cimgui_impl)
string(FIND "${cimgui_impl}" CIMGUI_USE_SDL3 rendbk_position)
string(FIND "${cimgui_impl}" CIMGUI_USE_SDLGPU3 platbk_position)

if(rendbk_position EQUAL -1 OR platbk_position EQUAL -1)
    cmake_path(GET CMAKE_C_COMPILER FILENAME C_COMP)
    cmake_path(REMOVE_EXTENSION C_COMP)

    set(GENERATOR_DIR "${CIMGUI_DIR}/generator")
    
    set(GENERATED_BINDINGS
        "${CIMGUI_DIR}/cimgui.cpp"
        "${CIMGUI_DIR}/cimgui_impl.cpp"
    )

    add_custom_command(
        OUTPUT ${GENERATED_BINDINGS}
        COMMAND $<TARGET_FILE:luajit> generator.lua ${C_COMP} "internal noimstrv" sdl3 sdlgpu3 "-I${FETCHED_SDL3_INCLUDE}"
        WORKING_DIRECTORY "${GENERATOR_DIR}"
        DEPENDS luajit
        COMMENT "Generating cimgui bindings using internal LuaJIT..."
        VERBATIM
    )
endif()

add_library(cimgui SHARED 
    "${CIMGUI_DIR}/cimgui.cpp"
    "${CIMGUI_DIR}/cimgui_impl.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp"
)

target_include_directories(cimgui INTERFACE "${CIMGUI_DIR}")
target_include_directories(cimgui PRIVATE "${IMGUI_DIR}")
target_include_directories(cimgui PRIVATE "${CIMGUI_DIR}/generator/output/")
target_include_directories(cimgui PUBLIC "${IMGUI_DIR}/backends/")

if (WIN32)
    target_link_libraries(imgui PRIVATE imm32)
    target_compile_definitions(cimgui PUBLIC IMGUI_IMPL_API=extern\t\"C\"\t__declspec\(dllexport\))
else()
    target_compile_definitions(cimgui PUBLIC IMGUI_IMPL_API=extern\t\"C\"\t)
endif()

target_compile_definitions(cimgui PUBLIC IMGUI_USE_WCHAR32)
target_link_libraries(cimgui PUBLIC imgui SDL3-shared)
target_link_libraries(cimgui PRIVATE stdc++ m)
set_target_properties(cimgui PROPERTIES PREFIX "")

set(CIMGUI_OUTPUT_DIR "${CIMGUI_DIR}/generator/output" CACHE PATH "Directory for generated Lua definitions" FORCE)

# ==============================================================================
# CIMPLOT
# ==============================================================================
FetchContent_Declare(
    cimplot_fetch
    GIT_REPOSITORY https://github.com/cimgui/cimplot.git
    GIT_TAG        master
    SOURCE_SUBDIR  "prevent_auto_build" # Prevents FetchContent_MakeAvailable from calling add_subdirectory()
)

# Safely download the content without executing its native CMakeLists.txt
FetchContent_MakeAvailable(cimplot_fetch)

set(CIMPLOT_DIR "${cimplot_fetch_SOURCE_DIR}")

project(cimplot LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB CIMPLOT_SOURCES
    "${CIMPLOT_DIR}/cimplot.cpp"
    "${CIMPLOT_DIR}/implot/implot_demo.cpp"
    "${CIMPLOT_DIR}/implot/implot_items.cpp"
    "${CIMPLOT_DIR}/implot/implot.cpp"
)

add_library(cimplot SHARED ${CIMPLOT_SOURCES})
target_include_directories(cimplot PUBLIC "${CIMPLOT_DIR}")
target_include_directories(cimplot PUBLIC "${CIMGUI_DIR}")
target_include_directories(cimplot PRIVATE "${IMGUI_DIR}")

target_link_libraries(cimplot PUBLIC cimgui imgui SDL3-shared)
set_target_properties(cimplot PROPERTIES PREFIX "")

set(CIMPLOT_OUTPUT_DIR "${CIMPLOT_DIR}/generator/output" CACHE PATH "Directory for generated cimplot Lua definitions" FORCE)