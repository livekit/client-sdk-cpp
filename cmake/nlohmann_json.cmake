# cmake/nlohmann_json.cmake
#
# Windows: use vcpkg nlohmann-json
# macOS/Linux: vendored nlohmann/json via FetchContent (header-only)
#
# Exposes:
#   - Target nlohmann_json::nlohmann_json (INTERFACE, header-only)
#
# nlohmann/json is a PRIVATE dependency of liblivekit: it is only included from
# implementation files under src/ and must never appear in a public header.
# Its include directories are marked SYSTEM so its single ~25k-line header does
# not trip -Wall/-Wextra/-Wpedantic or clang-tidy.

include(FetchContent)
include(warnings)

set(LIVEKIT_NLOHMANN_JSON_VERSION "3.12.0" CACHE STRING "Vendored nlohmann/json version")

# ---------------------------------------------------------------------------
# Windows: use vcpkg
# ---------------------------------------------------------------------------
if(WIN32 AND LIVEKIT_USE_VCPKG)
  find_package(nlohmann_json CONFIG REQUIRED)
  if(TARGET nlohmann_json::nlohmann_json)
    livekit_treat_as_external(nlohmann_json::nlohmann_json)
  endif()
  message(STATUS "Windows: using vcpkg nlohmann-json")
  return()
endif()

# ---------------------------------------------------------------------------
# macOS/Linux: vendored nlohmann/json via FetchContent
# ---------------------------------------------------------------------------
FetchContent_Declare(
  livekit_nlohmann_json
  URL "https://github.com/nlohmann/json/releases/download/v${LIVEKIT_NLOHMANN_JSON_VERSION}/json.tar.xz"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")

livekit_fetchcontent_makeavailable(livekit_nlohmann_json)

# Header-only INTERFACE target: nothing to compile, but mark its includes as
# SYSTEM so warnings from json.hpp are suppressed in consuming targets.
if(TARGET nlohmann_json::nlohmann_json)
  livekit_treat_as_external(nlohmann_json::nlohmann_json)
endif()

message(STATUS "macOS/Linux: using vendored nlohmann/json v${LIVEKIT_NLOHMANN_JSON_VERSION}")
