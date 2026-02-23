# cmake/spdlog.cmake
#
# Windows: use vcpkg spdlog
# macOS/Linux: vendored spdlog via FetchContent (static library)
#
# Exposes:
#   - Target spdlog::spdlog
#   - LIVEKIT_SPDLOG_ACTIVE_LEVEL compile definition
#
# Compile-time log level
# ----------------------
# LIVEKIT_LOG_LEVEL controls which LK_LOG_* calls survive compilation.
# Calls below this level are stripped entirely (zero overhead).
# Levels that survive can still be filtered at runtime via setLogLevel().
#
# Valid values: TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL, OFF
# Default: TRACE  (nothing stripped -- all levels available at runtime)
#
# Example: cmake -DLIVEKIT_LOG_LEVEL=WARN ...
#   -> TRACE/DEBUG/INFO calls are compiled out; WARN/ERROR/CRITICAL remain.

set(LIVEKIT_LOG_LEVEL "TRACE" CACHE STRING
  "Compile-time minimum log level (TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL, OFF)")

# Map the user-facing name to the spdlog integer constant.
string(TOUPPER "${LIVEKIT_LOG_LEVEL}" _LK_LOG_LEVEL_UPPER)
if(_LK_LOG_LEVEL_UPPER STREQUAL "TRACE")
  set(_SPDLOG_ACTIVE_LEVEL 0)
elseif(_LK_LOG_LEVEL_UPPER STREQUAL "DEBUG")
  set(_SPDLOG_ACTIVE_LEVEL 1)
elseif(_LK_LOG_LEVEL_UPPER STREQUAL "INFO")
  set(_SPDLOG_ACTIVE_LEVEL 2)
elseif(_LK_LOG_LEVEL_UPPER STREQUAL "WARN")
  set(_SPDLOG_ACTIVE_LEVEL 3)
elseif(_LK_LOG_LEVEL_UPPER STREQUAL "ERROR")
  set(_SPDLOG_ACTIVE_LEVEL 4)
elseif(_LK_LOG_LEVEL_UPPER STREQUAL "CRITICAL")
  set(_SPDLOG_ACTIVE_LEVEL 5)
elseif(_LK_LOG_LEVEL_UPPER STREQUAL "OFF")
  set(_SPDLOG_ACTIVE_LEVEL 6)
else()
  message(FATAL_ERROR
    "Invalid LIVEKIT_LOG_LEVEL='${LIVEKIT_LOG_LEVEL}'. "
    "Must be one of: TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL, OFF")
endif()

message(STATUS "LiveKit compile-time log level: ${_LK_LOG_LEVEL_UPPER} (SPDLOG_ACTIVE_LEVEL=${_SPDLOG_ACTIVE_LEVEL})")

include(FetchContent)

set(LIVEKIT_SPDLOG_VERSION "1.15.1" CACHE STRING "Vendored spdlog version")

option(LIVEKIT_USE_SYSTEM_SPDLOG
  "Use system spdlog instead of vendored (set ON when building for ROS 2 to avoid ABI conflicts with rcl_logging_spdlog)"
  OFF)

# ---------------------------------------------------------------------------
# Windows: use vcpkg
# ---------------------------------------------------------------------------
if(WIN32 AND LIVEKIT_USE_VCPKG)
  find_package(spdlog CONFIG REQUIRED)
  message(STATUS "Windows: using vcpkg spdlog")
  return()
endif()

# ---------------------------------------------------------------------------
# System spdlog (required for ROS 2 nodes to avoid dual-spdlog SIGBUS)
# ---------------------------------------------------------------------------
if(LIVEKIT_USE_SYSTEM_SPDLOG)
  find_package(spdlog REQUIRED)
  message(STATUS "Using system spdlog (LIVEKIT_USE_SYSTEM_SPDLOG=ON)")
  return()
endif()

# ---------------------------------------------------------------------------
# macOS/Linux: vendored spdlog via FetchContent
# ---------------------------------------------------------------------------
FetchContent_Declare(
  livekit_spdlog
  URL "https://github.com/gabime/spdlog/archive/refs/tags/v${LIVEKIT_SPDLOG_VERSION}.tar.gz"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(livekit_spdlog)

message(STATUS "macOS/Linux: using vendored spdlog v${LIVEKIT_SPDLOG_VERSION}")
