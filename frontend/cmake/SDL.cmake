include(FetchContent)

# Disable internal extras to drastically speed up your compilation time
set(SDL_TESTS OFF CACHE BOOL "Disable SDL3 internal tests" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "Disable SDL3 examples" FORCE)
set(SDL_INSTALL_DOCS OFF CACHE BOOL "Disable SDL3 documentation installation" FORCE)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
# set(SDL_STATIC ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.10 # Or pinpoint a specific release tag (e.g., preview-3.1.8)
)

FetchContent_GetProperties(SDL3)
if(NOT SDL3_POPULATED)
    FetchContent_MakeAvailable(SDL3)
endif()