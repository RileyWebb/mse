# Lua bindings for ImGui.
#
# These are generated, not vendored. LuaJIT-ImGui ships pre-generated bindings,
# but they describe its own pinned cimgui; struct layouts and enum values have
# to match the DLL we actually load or every field read lands somewhere else.
# So we take its generator (class_gen.lua, which turns cimgui's metadata into
# wrappers with default arguments and overload dispatch) and run it over the
# metadata our own cimgui build emits.
#
# Only the bindings are used. LuaJIT-ImGui's runtime modules create their own
# window and backend; here the frontend owns both, and Lua draws inside the
# frame it is already in.

include(FetchContent)

FetchContent_Declare(
    luajit_imgui
    GIT_REPOSITORY https://github.com/sonoro1234/LuaJIT-ImGui.git
    # Pinned: class_gen.lua has to agree with the shape of the metadata our
    # cimgui emits, and that is a property of a specific commit, not of master.
    GIT_TAG        9d157e7727748b592c6d013390b13df090b5daf4
    # Its submodules are whole copies of cimgui, cimplot, cimnodes and friends;
    # we need one Lua file, so skip them.
    GIT_SUBMODULES ""
    # Keeps FetchContent_MakeAvailable from running its CMakeLists.
    SOURCE_SUBDIR  "prevent_auto_build"
)

FetchContent_MakeAvailable(luajit_imgui)

set(IMGUI_BINDINGS_WORK "${CMAKE_CURRENT_BINARY_DIR}/imgui_bindings")
set(IMGUI_BINDINGS_OUT "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/data/lua")

set(IMGUI_BINDINGS_FILES
    "${IMGUI_BINDINGS_OUT}/imgui/base.lua"
    "${IMGUI_BINDINGS_OUT}/imgui/cdefs.lua"
)

add_custom_command(
    OUTPUT ${IMGUI_BINDINGS_FILES}

    # class_gen.lua resolves ../cimgui/generator/output against its own working
    # directory, so the metadata has to be staged where it expects to find it.
    COMMAND ${CMAKE_COMMAND} -E make_directory "${IMGUI_BINDINGS_WORK}/lua/imgui"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${IMGUI_BINDINGS_OUT}/imgui"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CIMGUI_DIR}/generator/output"
            "${IMGUI_BINDINGS_WORK}/cimgui/generator/output"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CIMGUI_DIR}/generator/cpp2ffi.lua"
            "${IMGUI_BINDINGS_WORK}/cimgui/generator/cpp2ffi.lua"

    COMMAND ${CMAKE_COMMAND} -E chdir "${IMGUI_BINDINGS_WORK}/lua"
            $<TARGET_FILE:luajit>
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_imgui_bindings.lua"
            "${IMGUI_BINDINGS_WORK}"
            "${CIMGUI_DIR}"
            "${luajit_imgui_SOURCE_DIR}/lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/lua/imgui_prelude.lua"
            "${IMGUI_BINDINGS_OUT}"
            "${CMAKE_C_COMPILER}"

    DEPENDS
        luajit
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_imgui_bindings.lua"
        "${CMAKE_CURRENT_SOURCE_DIR}/lua/imgui_prelude.lua"
        "${CIMGUI_DIR}/generator/output/definitions.lua"

    COMMENT "Generating Lua ImGui bindings from this build's cimgui"
    VERBATIM
)

add_custom_target(imgui_lua_bindings ALL DEPENDS ${IMGUI_BINDINGS_FILES})

# The frontend's own Lua modules: the panel registry backends register with, and
# the shim that binds the generated bindings to this build's cimgui.
# imgui_prelude.lua is deliberately not deployed -- it is an input to the
# generator, not something to load at runtime.
add_custom_target(mse_lua_modules ALL
    COMMENT "Deploying frontend Lua modules..."
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/lua/mse"
        "${IMGUI_BINDINGS_OUT}/mse"
)

add_dependencies(imgui_lua_bindings mse_lua_modules)

# Draws the backend's panels headlessly against a real ImGui context and a real
# ROM. Catches the failures that do not show up as build errors: bindings that
# no longer match cimgui, a panel calling something that does not exist, or the
# debug API's struct layout drifting from the Lua cdef.
set(PANEL_SMOKE_ROM
    "${CMAKE_SOURCE_DIR}/backends/cnes/tests/nestest/nestest.nes")

if(EXISTS ${PANEL_SMOKE_ROM})
    add_test(
        NAME mse_lua_panels
        COMMAND $<TARGET_FILE:luajit>
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/panel_smoke.lua"
                ${PANEL_SMOKE_ROM}
    )

    set_tests_properties(mse_lua_panels PROPERTIES
        # The modules, the bindings and the backend are all resolved relative to
        # the runtime directory.
        WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        TIMEOUT 60
        LABELS "frontend;lua;ui"
    )
endif()
