cmake_minimum_required(VERSION 4.1)
project(mse C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

add_subdirectory(ext)
#add_subdirectory(examples/test_backend)
add_subdirectory(backends/cnes)

set(LIBMSE_SOURCES
    src/libmse/libmse.c
    src/libmse/libmse_cvar.c
    src/libmse/libmse_sync.c
    src/libmse/libmse_gfx.c
    src/libmse/debug.c
    src/libmse/file_registry.c
)

set(MSE_FRONTEND_SOURCES
    src/frontend/main.c
    src/frontend/frontend_app.c
    src/frontend/frontend_imgui.c
    src/frontend/frontend_theme.c
    src/frontend/frontend_help.c
    src/frontend/frontend_logs.c
    src/frontend/frontend_ui.c
    src/frontend/frontend_terminal.c
    src/frontend/frontend_settings.c
    src/frontend/frontend_input.c
    src/frontend/cimgui_markdown.c
    #src/frontend/cimgui_hex.c
)

add_library(libmse SHARED ${LIBMSE_SOURCES})
target_include_directories(libmse PUBLIC include)
set_target_properties(libmse PROPERTIES PREFIX "")
target_compile_definitions(libmse PRIVATE LIBMSE_EXPORTS)

add_executable(mse ${MSE_FRONTEND_SOURCES})
target_include_directories(mse PUBLIC include)
set_target_properties(mse PROPERTIES PREFIX "")

if(WIN32)
    set(PLATFORM_LIBS mingw32)
    set_target_properties(mse PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(libmse PRIVATE ${PLATFORM_LIBS})
target_link_libraries(libmse PUBLIC cjson minizip SDL3-shared)

if(NOT WIN32)
    target_link_libraries(libmse PRIVATE dl)
endif()

# Link optional stb target only if it was created in the ext subdirectory
if(TARGET stb)
    target_link_libraries(libmse PUBLIC stb)
endif()

target_link_libraries(mse PRIVATE ${PLATFORM_LIBS})
target_link_libraries(mse PUBLIC libmse cimgui cimplot)

# Link Lua/LuaJIT if found
if(USE_LUAJIT)
    if(LUA_LIBS)
        target_link_libraries(libmse PUBLIC ${LUA_LIBS})
    endif()
    if(LUA_INCLUDE)
        target_include_directories(libmse PUBLIC ${LUA_INCLUDE})
    endif()
    target_compile_definitions(libmse PUBLIC USE_LUAJIT=1)
    message(STATUS "Lua scripting ENABLED")
else()
    message(STATUS "Lua scripting DISABLED")
endif()

target_compile_options(libmse PRIVATE
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

add_custom_target(copy_data
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/data/
    ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/data/)

add_custom_target(copy_info
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENCE
#    ${CMAKE_CURRENT_SOURCE_DIR}/CREDITS
    ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

# Shader compilation
find_program(GLSLC_EXECUTABLE glslc)
if(GLSLC_EXECUTABLE)
    message(STATUS "Found glslc: ${GLSLC_EXECUTABLE}")
    
    set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/glsl")
    set(SHADER_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data/shaders")
    
    file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})
    
    file(GLOB SHADER_SOURCES 
        "${SHADER_SOURCE_DIR}/*.vert"
        "${SHADER_SOURCE_DIR}/*.frag"
        "${SHADER_SOURCE_DIR}/*.comp"
    )
    
    set(SPV_FILES "")
    
    foreach(SHADER ${SHADER_SOURCES})
        # Use NAME to get the full filename (e.g., passthrough.vert)
        get_filename_component(FILENAME_FULL ${SHADER} NAME)
        
        # Append .spv to the full original filename (e.g., passthrough.vert.spv)
        set(SPV_FILE "${SHADER_OUTPUT_DIR}/${FILENAME_FULL}.spv")
        
        add_custom_command(
            OUTPUT ${SPV_FILE}
            COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${SPV_FILE}
            DEPENDS ${SHADER}
            COMMENT "Compiling shader ${FILENAME_FULL}"
        )
        
        list(APPEND SPV_FILES ${SPV_FILE})
    endforeach()
    
    add_custom_target(compile_shaders ALL DEPENDS ${SPV_FILES})

else()
    message(WARNING "glslc not found! Shaders will not be compiled.")
endif()

add_dependencies(libmse compile_shaders)
add_dependencies(mse copy_info copy_data compile_shaders)