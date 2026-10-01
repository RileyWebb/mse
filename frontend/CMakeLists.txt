cmake_minimum_required(VERSION 4.1)
project(mse C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/SDL.cmake)
include(cmake/cimgui.cmake)

set(MSE_FRONTEND_SOURCES
    src/main.c
    src/frontend_args.c
    src/frontend_app.c
    src/frontend_imgui.c
    src/frontend_theme.c
    src/frontend_widgets.c
    src/frontend_help.c
    src/frontend_ui.c
    src/frontend_terminal.c
    src/frontend_settings.c
    src/frontend_input.c
    src/frontend_controller.c
    src/cimgui_markdown.c
    src/frontend_lua.c
    src/frontend_lua_ui.c
    src/frontend_file_dialog.c
    #src/cimgui_hex.c
    src/frontend_library.c
    src/frontend_covers.c
    src/frontend_profiler.c
    src/frontend_screenshot.c
    src/frontend_startup_log.c
    src/frontend_logo.c
    src/frontend_chrome.c
)

add_executable(mse ${MSE_FRONTEND_SOURCES})
set_target_properties(mse PROPERTIES PREFIX "")

if(WIN32)
    set(PLATFORM_LIBS mingw32)
    set_target_properties(mse PROPERTIES WIN32_EXECUTABLE TRUE)

    # The executable's own icon: what Explorer, the Start menu, a pinned
    # taskbar button and the installer's shortcuts show. The running window's
    # icon is set separately at startup (frontend_logo.c). Configured rather
    # than written with a relative path, because windres resolves one against
    # wherever it happens to be run from, not against the .rc file.
    #
    # Its own object library, because windres runs the C preprocessor and a
    # source of mse would inherit every definition mse and its dependencies
    # carry -- __declspec(dllexport), quoted Windows paths -- and windres's
    # command line mangles those into "No such file or directory".
    enable_language(RC)
    set(MSE_ICON_PATH "${CMAKE_SOURCE_DIR}/packaging/windows/mse.ico")
    configure_file(mse.rc.in "${CMAKE_CURRENT_BINARY_DIR}/mse.rc" @ONLY)
    add_library(mse_icon OBJECT "${CMAKE_CURRENT_BINARY_DIR}/mse.rc")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/mse.rc" PROPERTIES OBJECT_DEPENDS "${MSE_ICON_PATH}")
    target_sources(mse PRIVATE $<TARGET_OBJECTS:mse_icon>)
endif()

include(cmake/LuaDeps.cmake)

add_dependencies(mse imgui_lua_bindings)

target_link_libraries(mse PRIVATE ${PLATFORM_LIBS})
target_link_libraries(mse PUBLIC libmse cimgui cimplot SDL3-shared)
target_link_libraries(mse PRIVATE stb)

target_compile_options(mse PRIVATE
    $<$<CONFIG:Debug>:
    #-pedantic
    -Wall
    -Wextra
    -Wconversion
    -Wundef
    #-Werror
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-unused-but-set-variable>
)

target_compile_definitions(mse PRIVATE CIMGUI_USE_SDL3 CIMGUI_USE_SDLGPU3 CIMGUI_DEFINE_ENUMS_AND_STRUCTS=1)

if(MINGW)
    get_filename_component(MINGW_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)

    set(MINGW_RUNTIME_DLLS
        "${MINGW_BIN_DIR}/libstdc++-6.dll"
        "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
        "${MINGW_BIN_DIR}/libwinpthread-1.dll"
    )

    add_custom_command(TARGET mse POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
            ${MINGW_RUNTIME_DLLS}
            $<TARGET_FILE_DIR:mse>
        COMMENT "Copying MinGW runtime DLLs to output directory"
    )
endif()

#if(TARGET libluajit)
#
#    add_dependencies(mse)
#endif()

# Themes are configuration, not code: each is a list of cvar assignments the
# console reads. Shipped ones sit next to the executable; a user's own go in
# themes/ under the app data directory and are picked up alongside them.
add_custom_target(mse_themes ALL
    COMMENT "Deploying themes..."
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/themes"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/themes")

add_dependencies(mse mse_themes)
