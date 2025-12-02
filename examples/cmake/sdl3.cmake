# cmake/sdl3.cmake
include(FetchContent)

# Only fetch/build SDL3 once, even if this file is included multiple times
if (NOT TARGET SDL3::SDL3)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.2.26
    )

    FetchContent_MakeAvailable(SDL3)
endif()

