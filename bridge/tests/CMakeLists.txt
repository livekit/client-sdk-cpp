cmake_minimum_required(VERSION 3.20)

# ============================================================================
# Google Test Setup via FetchContent
# ============================================================================

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)

# Prevent overriding the parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Don't install gtest when installing this project
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Enable CTest
enable_testing()
include(GoogleTest)

# ============================================================================
# Helper: copy shared libraries to a test executable's output directory
# ============================================================================
function(bridge_test_copy_libs target)
  if(WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit_bridge>
        $<TARGET_FILE_DIR:${target}>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit>
        $<TARGET_FILE_DIR:${target}>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:livekit>/livekit_ffi.dll"
        $<TARGET_FILE_DIR:${target}>
      COMMENT "Copying DLLs to ${target} directory"
    )
  elseif(APPLE)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit_bridge>
        $<TARGET_FILE_DIR:${target}>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit>
        $<TARGET_FILE_DIR:${target}>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:livekit>/liblivekit_ffi.dylib"
        $<TARGET_FILE_DIR:${target}>
      COMMENT "Copying dylibs to ${target} directory"
    )
  else()
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit_bridge>
        $<TARGET_FILE_DIR:${target}>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit>
        $<TARGET_FILE_DIR:${target}>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:livekit>/liblivekit_ffi.so"
        $<TARGET_FILE_DIR:${target}>
      COMMENT "Copying shared libraries to ${target} directory"
    )
  endif()
endfunction()

# ============================================================================
# Bridge Unit Tests (existing — files in tests/ root)
# ============================================================================

file(GLOB BRIDGE_TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp"
)

if(BRIDGE_TEST_SOURCES)
  add_executable(livekit_bridge_unit_tests
    ${BRIDGE_TEST_SOURCES}
  )

  target_link_libraries(livekit_bridge_unit_tests
    PRIVATE
      livekit_bridge
      GTest::gtest_main
  )

  bridge_test_copy_libs(livekit_bridge_unit_tests)

  gtest_discover_tests(livekit_bridge_unit_tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    PROPERTIES
      LABELS "bridge_unit_tests"
  )
endif()

# ============================================================================
# Bridge Integration Tests (tests/integration/)
# ============================================================================

file(GLOB BRIDGE_INTEGRATION_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cpp"
)

if(BRIDGE_INTEGRATION_SOURCES)
  add_executable(livekit_bridge_integration_tests
    ${BRIDGE_INTEGRATION_SOURCES}
  )

  target_link_libraries(livekit_bridge_integration_tests
    PRIVATE
      livekit_bridge
      GTest::gtest_main
  )

  bridge_test_copy_libs(livekit_bridge_integration_tests)

  gtest_discover_tests(livekit_bridge_integration_tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    PROPERTIES
      LABELS "bridge_integration"
  )
endif()

# ============================================================================
# Bridge Stress Tests (tests/stress/)
# ============================================================================

file(GLOB BRIDGE_STRESS_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/stress/*.cpp"
)

if(BRIDGE_STRESS_SOURCES)
  add_executable(livekit_bridge_stress_tests
    ${BRIDGE_STRESS_SOURCES}
  )

  target_link_libraries(livekit_bridge_stress_tests
    PRIVATE
      livekit_bridge
      GTest::gtest_main
  )

  bridge_test_copy_libs(livekit_bridge_stress_tests)

  gtest_discover_tests(livekit_bridge_stress_tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    PROPERTIES
      LABELS "bridge_stress"
      TIMEOUT 300
  )
endif()

# ============================================================================
# Combined target
# ============================================================================

add_custom_target(run_bridge_tests
  COMMAND ${CMAKE_CTEST_COMMAND} -L "bridge_unit|bridge_integration|bridge_stress" --output-on-failure
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running all bridge tests"
)

if(TARGET livekit_bridge_unit_tests)
  add_dependencies(run_bridge_tests livekit_bridge_unit_tests)
endif()

if(TARGET livekit_bridge_integration_tests)
  add_dependencies(run_bridge_tests livekit_bridge_integration_tests)
endif()

if(TARGET livekit_bridge_stress_tests)
  add_dependencies(run_bridge_tests livekit_bridge_stress_tests)
endif()
