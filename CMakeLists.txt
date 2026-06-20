cmake_minimum_required(VERSION 4.1)
project(mse C)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

enable_testing()

if(MINGW)
    add_link_options(-static-libgcc -static-libstdc++)
    add_link_options(-Wl,-Bstatic -lpthread -Wl,-Bdynamic)
endif()

add_subdirectory(ext)
#add_subdirectory(examples/test_backend)
add_subdirectory(backends/cnes)

add_subdirectory(libmse)
add_subdirectory(frontend)

add_custom_target(copy_info
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENCE
    ${CMAKE_CURRENT_SOURCE_DIR}/CREDITS
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

add_custom_target(copy_data
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/data/
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data/)

add_dependencies(mse copy_info copy_data)

# IGNORE AFTER THIS WIP
#find_program(GLSLC_EXECUTABLE glslc)
#if(GLSLC_EXECUTABLE)
#    message(STATUS "Found glslc: ${GLSLC_EXECUTABLE}")
#    
#    set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/glsl")
#    set(SHADER_OUTPUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/data/shaders")
#    
#    file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})
#    
#    file(GLOB SHADER_SOURCES 
#        "${SHADER_SOURCE_DIR}/*.vert"
#        "${SHADER_SOURCE_DIR}/*.frag"
#        "${SHADER_SOURCE_DIR}/*.comp"
#    )
#    
#    set(SPV_FILES "")
#    
#    foreach(SHADER ${SHADER_SOURCES})
#        # Use NAME to get the full filename (e.g., passthrough.vert)
#        get_filename_component(FILENAME_FULL ${SHADER} NAME)
#        
#        # Append .spv to the full original filename (e.g., passthrough.vert.spv)
#        set(SPV_FILE "${SHADER_OUTPUT_DIR}/${FILENAME_FULL}.spv")
#        
#        add_custom_command(
#            OUTPUT ${SPV_FILE}
#            COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${SPV_FILE}
#            DEPENDS ${SHADER}
#            COMMENT "Compiling shader ${FILENAME_FULL}"
#        )
#        
#        list(APPEND SPV_FILES ${SPV_FILE})
#    endforeach()
#    
#    add_custom_target(compile_shaders ALL DEPENDS ${SPV_FILES})
#
#else()
#    message(WARNING "glslc not found! Shaders will not be compiled.")
#endif()