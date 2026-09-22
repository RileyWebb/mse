include(FetchContent)

# xml2lua
FetchContent_Declare(
    xml2lua_repo
    GIT_REPOSITORY https://github.com/manoelcampos/xml2lua.git
    GIT_TAG        master
)

FetchContent_GetProperties(xml2lua_repo)
if(NOT xml2lua_repo_POPULATED)
    FetchContent_MakeAvailable(xml2lua_repo)
endif()

add_custom_target(xml2lua ALL
    COMMENT "Deploying xml2lua scripts to runtime directory..."
    
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cnes/data/lua"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cnes/data/lua/xmlhandler"
    
    COMMAND ${CMAKE_COMMAND} -E copy "${xml2lua_repo_SOURCE_DIR}/xml2lua.lua" "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cnes/data/lua/"
    COMMAND ${CMAKE_COMMAND} -E copy "${xml2lua_repo_SOURCE_DIR}/XmlParser.lua" "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cnes/data/lua/"
    
    COMMAND ${CMAKE_COMMAND} -E copy "${xml2lua_repo_SOURCE_DIR}/xmlhandler/dom.lua" "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cnes/data/lua/xmlhandler/"
    COMMAND ${CMAKE_COMMAND} -E copy "${xml2lua_repo_SOURCE_DIR}/xmlhandler/tree.lua"    "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cnes/data/lua/xmlhandler/"
    COMMAND ${CMAKE_COMMAND} -E copy "${xml2lua_repo_SOURCE_DIR}/xmlhandler/print.lua"    "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cnes/data/lua/xmlhandler/"
)