# Turns the built bin/ tree into a clean install tree, and wraps that in an
# NSIS installer on Windows.
#
# Nothing in this build has an install step of its own: every target and every
# deploy rule writes straight into bin/, which is also the working directory the
# app is run from while developing. So bin/ holds the whole runtime *and*
# whatever a session left behind -- movies, the library database, imgui.ini, a
# config, the headless test binaries. What follows takes the runtime and leaves
# the rest, rather than shipping a snapshot of somebody's working directory.

set(MSE_RUNTIME_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

# One version for the whole project, read from the header rather than repeated
# here, so a release cannot be cut with a number that disagrees with the one the
# application prints.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/libmse/include/libmse/libmse_version.h" MSE_VERSION_HEADER)
string(REGEX MATCH "LIBMSE_VERSION_STRING[ \t]+\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" MSE_VERSION_MATCH
       "${MSE_VERSION_HEADER}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "Could not read LIBMSE_VERSION_STRING from libmse_version.h")
endif()
set(MSE_VERSION "${CMAKE_MATCH_1}")
message(STATUS "MSE version ${MSE_VERSION}")

# --- the install tree --------------------------------------------------------
#
# Everything here is in the "runtime" component, and the installer asks for that
# component by name. Not for the usual reason -- there is only one component --
# but because every fetched dependency brings its own install() rules with it.
# SDL, LuaJIT, LibreSSL, curl and minizip between them add headers, .pc files,
# CMake config packages and static libraries, and an uncomponented install
# collects the lot: 167MB of development files around a 25MB application.
install(TARGETS mse RUNTIME DESTINATION . COMPONENT runtime)

# Every shared library at the top of bin/ is a runtime dependency by
# construction -- the build puts them there and nothing else does -- so they are
# taken as a set rather than named one at a time, which would go stale the first
# time a dependency is added. Globbed when the install runs and not when this
# file is read, because on a clean tree none of them exist yet at configure
# time. The emptiness check is what turns "installed before building" from a
# silently broken package into an error.
install(CODE "
    file(GLOB MSE_RUNTIME_LIBRARIES \"${MSE_RUNTIME_DIR}/*${CMAKE_SHARED_LIBRARY_SUFFIX}\")
    if(NOT MSE_RUNTIME_LIBRARIES)
        message(FATAL_ERROR
            \"No shared libraries in ${MSE_RUNTIME_DIR} -- build before installing.\")
    endif()
    file(INSTALL \${MSE_RUNTIME_LIBRARIES} DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")

    # cmake --install --strip only reaches files installed through install()
    # rules it generated itself, and these came in through file(INSTALL). These
    # are MinGW DLLs carrying full DWARF; stripping them takes the package from
    # roughly 120MB to 25MB, so leaving them out of --strip would make the flag
    # look like it did nothing.
    if(\${CMAKE_INSTALL_DO_STRIP} AND NOT \"${CMAKE_STRIP}\" STREQUAL \"\")
        foreach(MSE_LIBRARY IN LISTS MSE_RUNTIME_LIBRARIES)
            get_filename_component(MSE_LIBRARY_NAME \"\${MSE_LIBRARY}\" NAME)
            execute_process(COMMAND \"${CMAKE_STRIP}\"
                            \"\${CMAKE_INSTALL_PREFIX}/\${MSE_LIBRARY_NAME}\")
        endforeach()
    endif()
" COMPONENT runtime)

# Paths inside the application are relative to the working directory, so the
# layout under the prefix has to match the layout under bin/ exactly. A shortcut
# that starts mse.exe somewhere else finds no backends and no Lua.
install(DIRECTORY "${MSE_RUNTIME_DIR}/data/"   DESTINATION data   COMPONENT runtime)
install(DIRECTORY "${MSE_RUNTIME_DIR}/themes/" DESTINATION themes COMPONENT runtime)
install(DIRECTORY "${MSE_RUNTIME_DIR}/cnes/"   DESTINATION cnes   COMPONENT runtime)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENCE"
    "${CMAKE_CURRENT_SOURCE_DIR}/CREDITS.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    DESTINATION . COMPONENT runtime)

# --- the installer -----------------------------------------------------------

if(WIN32)
    find_program(MSE_MAKENSIS_EXECUTABLE
        NAMES makensis
        PATHS "$ENV{PROGRAMFILES}/NSIS" "$ENV{PROGRAMFILES\(X86\)}/NSIS"
        DOC "NSIS compiler, for the Windows installer")

    set(MSE_STAGING_DIR "${CMAKE_BINARY_DIR}/dist/MSE")
    set(MSE_INSTALLER    "${CMAKE_BINARY_DIR}/dist/MSE-${MSE_VERSION}-setup.exe")

    if(MSE_MAKENSIS_EXECUTABLE)
        # NSIS takes Windows paths. CMake hands out forward slashes, and File /r
        # is one of the places NSIS does not forgive them.
        file(TO_NATIVE_PATH "${MSE_STAGING_DIR}" MSE_STAGING_NATIVE)
        file(TO_NATIVE_PATH "${MSE_INSTALLER}"   MSE_INSTALLER_NATIVE)
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/LICENCE" MSE_LICENCE_NATIVE)

        # Staged fresh each time: the staging directory is reused between runs,
        # and a file dropped from the build would otherwise stay in the package
        # forever because nothing ever removes it.
        add_custom_target(installer
            COMMENT "Building the Windows installer..."
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${MSE_STAGING_DIR}"
            COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
                    --prefix "${MSE_STAGING_DIR}" --component runtime --strip
            COMMAND ${MSE_MAKENSIS_EXECUTABLE}
                    "/DVERSION=${MSE_VERSION}"
                    "/DSTAGING=${MSE_STAGING_NATIVE}"
                    "/DOUTFILE=${MSE_INSTALLER_NATIVE}"
                    "/DLICENCE=${MSE_LICENCE_NATIVE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/mse.nsi"
            VERBATIM)

        add_dependencies(installer mse)
    else()
        message(STATUS "makensis not found; the 'installer' target is unavailable")
    endif()
endif()
