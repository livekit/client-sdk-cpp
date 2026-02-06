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
# Integration Tests
# ============================================================================

# Collect all integration test sources
file(GLOB INTEGRATION_TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cpp"
)

if(INTEGRATION_TEST_SOURCES)
  add_executable(livekit_integration_tests
    ${INTEGRATION_TEST_SOURCES}
  )

  target_link_libraries(livekit_integration_tests
    PRIVATE
      livekit
      GTest::gtest_main
      GTest::gmock
  )

  target_include_directories(livekit_integration_tests
    PRIVATE
      ${LIVEKIT_ROOT_DIR}/include
      ${LIVEKIT_ROOT_DIR}/src
  )

  # Define LIVEKIT_ROOT_DIR for tests to find data files
  target_compile_definitions(livekit_integration_tests
    PRIVATE
      LIVEKIT_ROOT_DIR="${LIVEKIT_ROOT_DIR}"
  )

  # Copy shared libraries to test executable directory
  if(WIN32)
    add_custom_command(TARGET livekit_integration_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit>
        $<TARGET_FILE_DIR:livekit_integration_tests>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:livekit>/livekit_ffi.dll"
        $<TARGET_FILE_DIR:livekit_integration_tests>
      COMMENT "Copying DLLs to integration test directory"
    )
  elseif(APPLE)
    add_custom_command(TARGET livekit_integration_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit>
        $<TARGET_FILE_DIR:livekit_integration_tests>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:livekit>/liblivekit_ffi.dylib"
        $<TARGET_FILE_DIR:livekit_integration_tests>
      COMMENT "Copying dylibs to integration test directory"
    )
  else()
    add_custom_command(TARGET livekit_integration_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit>
        $<TARGET_FILE_DIR:livekit_integration_tests>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:livekit>/liblivekit_ffi.so"
        $<TARGET_FILE_DIR:livekit_integration_tests>
      COMMENT "Copying shared libraries to integration test directory"
    )
  endif()

  # Register tests with CTest
  gtest_discover_tests(livekit_integration_tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    PROPERTIES
      LABELS "integration"
  )
endif()

# ============================================================================
# Stress Tests
# ============================================================================

# Collect all stress test sources
file(GLOB STRESS_TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/stress/*.cpp"
)

if(STRESS_TEST_SOURCES)
  add_executable(livekit_stress_tests
    ${STRESS_TEST_SOURCES}
  )

  target_link_libraries(livekit_stress_tests
    PRIVATE
      livekit
      GTest::gtest_main
      GTest::gmock
  )

  target_include_directories(livekit_stress_tests
    PRIVATE
      ${LIVEKIT_ROOT_DIR}/include
      ${LIVEKIT_ROOT_DIR}/src
  )

  # Copy shared libraries to test executable directory
  if(WIN32)
    add_custom_command(TARGET livekit_stress_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit>
        $<TARGET_FILE_DIR:livekit_stress_tests>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:livekit>/livekit_ffi.dll"
        $<TARGET_FILE_DIR:livekit_stress_tests>
      COMMENT "Copying DLLs to stress test directory"
    )
  elseif(APPLE)
    add_custom_command(TARGET livekit_stress_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit>
        $<TARGET_FILE_DIR:livekit_stress_tests>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:livekit>/liblivekit_ffi.dylib"
        $<TARGET_FILE_DIR:livekit_stress_tests>
      COMMENT "Copying dylibs to stress test directory"
    )
  else()
    add_custom_command(TARGET livekit_stress_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit>
        $<TARGET_FILE_DIR:livekit_stress_tests>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:livekit>/liblivekit_ffi.so"
        $<TARGET_FILE_DIR:livekit_stress_tests>
      COMMENT "Copying shared libraries to stress test directory"
    )
  endif()

  # Register tests with CTest (longer timeout for stress tests)
  gtest_discover_tests(livekit_stress_tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    PROPERTIES
      LABELS "stress"
      TIMEOUT 300
  )
endif()

# ============================================================================
# Combined test target
# ============================================================================

add_custom_target(run_all_tests
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running all tests"
)

if(TARGET livekit_integration_tests)
  add_dependencies(run_all_tests livekit_integration_tests)
endif()

if(TARGET livekit_stress_tests)
  add_dependencies(run_all_tests livekit_stress_tests)
endif()
