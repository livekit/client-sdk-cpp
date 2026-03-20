# Copyright 2026 LiveKit, Inc.
#
# Standalone CMake build for the data-track throughput experiment.
# All paths are relative to CMAKE_CURRENT_SOURCE_DIR so this directory
# can be moved or renamed freely.

cmake_minimum_required(VERSION 3.20)
project(DataTrackThroughput LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Dependencies --------------------------------------------------------

find_package(LiveKit CONFIG REQUIRED)

find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# ---- Targets -------------------------------------------------------------

set(_targets DataTrackThroughputProducer DataTrackThroughputConsumer)

add_executable(DataTrackThroughputProducer producer.cpp)
add_executable(DataTrackThroughputConsumer consumer.cpp)

foreach(_target ${_targets})
  target_include_directories(${_target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(${_target} PRIVATE LiveKit::livekit nlohmann_json::nlohmann_json)
endforeach()

# ---- RPATH ---------------------------------------------------------------

if(UNIX)
  if(APPLE)
    set_target_properties(${_targets} PROPERTIES
      BUILD_RPATH  "@loader_path"
      INSTALL_RPATH "@loader_path"
    )
  else()
    set_target_properties(${_targets} PROPERTIES
      BUILD_RPATH  "$ORIGIN"
      INSTALL_RPATH "$ORIGIN"
      BUILD_RPATH_USE_ORIGIN TRUE
    )
  endif()
endif()

# ---- Copy SDK shared libraries next to executables -----------------------

get_target_property(_lk_location LiveKit::livekit LOCATION)
if(_lk_location)
  get_filename_component(_lk_lib_dir "${_lk_location}" DIRECTORY)
else()
  get_target_property(_lk_location LiveKit::livekit IMPORTED_LOCATION)
  if(NOT _lk_location)
    get_target_property(_lk_location LiveKit::livekit IMPORTED_LOCATION_RELEASE)
  endif()
  if(NOT _lk_location)
    get_target_property(_lk_location LiveKit::livekit IMPORTED_LOCATION_DEBUG)
  endif()
  if(_lk_location)
    get_filename_component(_lk_lib_dir "${_lk_location}" DIRECTORY)
  endif()
endif()

if(_lk_lib_dir)
  if(WIN32)
    file(GLOB _sdk_shared_libs "${_lk_lib_dir}/../bin/*.dll" "${_lk_lib_dir}/*.dll")
  elseif(APPLE)
    file(GLOB _sdk_shared_libs "${_lk_lib_dir}/*.dylib")
  else()
    file(GLOB _sdk_shared_libs "${_lk_lib_dir}/*.so" "${_lk_lib_dir}/*.so.*")
  endif()

  foreach(_target ${_targets})
    foreach(_lib ${_sdk_shared_libs})
      get_filename_component(_lib_name "${_lib}" NAME)
      add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_lib}" "$<TARGET_FILE_DIR:${_target}>/${_lib_name}"
        COMMENT "Copying ${_lib_name} next to ${_target}"
      )
    endforeach()
  endforeach()
endif()
