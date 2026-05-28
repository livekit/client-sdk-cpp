# Copyright 2026 LiveKit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Resolves target aliases, marks their exported include directories as system includes,
# and disables compiler warnings on compilable third-party targets.
function(_livekit_resolve_target target out_var)
  if(NOT TARGET ${target})
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  get_target_property(_aliased_target ${target} ALIASED_TARGET)
  if(_aliased_target)
    set(${out_var} "${_aliased_target}" PARENT_SCOPE)
  else()
    set(${out_var} "${target}" PARENT_SCOPE)
  endif()
endfunction()

function(livekit_disable_warnings target)
  _livekit_resolve_target(${target} _resolved_target)
  if(NOT _resolved_target)
    return()
  endif()

  get_target_property(_target_type ${_resolved_target} TYPE)
  get_target_property(_is_imported ${_resolved_target} IMPORTED)
  if(_is_imported OR _target_type STREQUAL "INTERFACE_LIBRARY")
    return()
  endif()
  if(NOT _target_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
    return()
  endif()

  target_compile_options(${_resolved_target} PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,MSVC>:/W0>
    $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/W0>
    $<$<COMPILE_LANG_AND_ID:C,AppleClang,Clang,GNU>:-w>
    $<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang,GNU>:-w>
  )
endfunction()

function(livekit_mark_system_includes target)
  _livekit_resolve_target(${target} _resolved_target)
  if(NOT _resolved_target)
    return()
  endif()

  get_target_property(_interface_includes ${_resolved_target} INTERFACE_INCLUDE_DIRECTORIES)
  if(_interface_includes)
    set_property(TARGET ${_resolved_target} APPEND PROPERTY
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ${_interface_includes}
    )
  endif()
endfunction()

function(livekit_get_interface_includes target out_var)
  _livekit_resolve_target(${target} _resolved_target)
  if(NOT _resolved_target)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  get_target_property(_interface_includes ${_resolved_target} INTERFACE_INCLUDE_DIRECTORIES)
  if(_interface_includes)
    set(${out_var} ${_interface_includes} PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

function(livekit_fetchcontent_makeavailable)
  set(CMAKE_WARN_DEPRECATED OFF)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
  FetchContent_MakeAvailable(${ARGV})
endfunction()

function(livekit_collect_targets_in_directory out_var directory)
  get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)

  set(_all_targets ${_targets})
  foreach(_subdirectory IN LISTS _subdirectories)
    livekit_collect_targets_in_directory(_subdirectory_targets "${_subdirectory}")
    list(APPEND _all_targets ${_subdirectory_targets})
  endforeach()

  set(${out_var} ${_all_targets} PARENT_SCOPE)
endfunction()

function(livekit_treat_as_external target)
  if(NOT TARGET ${target})
    return()
  endif()

  livekit_mark_system_includes(${target})
  livekit_disable_warnings(${target})
endfunction()
