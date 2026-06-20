cmake_minimum_required(VERSION 4.1)
project(mse C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(MSE_FRONTEND_SOURCES
    src/main.c
    src/frontend_app.c
    src/frontend_imgui.c
    src/frontend_theme.c
    src/frontend_help.c
    src/frontend_logs.c
    src/frontend_ui.c
    src/frontend_terminal.c
    src/frontend_settings.c
    src/frontend_input.c
    src/cimgui_markdown.c
    #src/cimgui_hex.c
)

add_executable(mse ${MSE_FRONTEND_SOURCES})
set_target_properties(mse PROPERTIES PREFIX "")

if(WIN32)
    set(PLATFORM_LIBS mingw32)
    set_target_properties(mse PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(mse PRIVATE ${PLATFORM_LIBS})
target_link_libraries(mse PUBLIC libmse cimgui cimplot)

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