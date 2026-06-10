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