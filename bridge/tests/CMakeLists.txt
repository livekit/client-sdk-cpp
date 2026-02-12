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
# Bridge Unit Tests
# ============================================================================

file(GLOB BRIDGE_TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

if(BRIDGE_TEST_SOURCES)
  add_executable(livekit_bridge_tests
    ${BRIDGE_TEST_SOURCES}
  )

  target_link_libraries(livekit_bridge_tests
    PRIVATE
      livekit_bridge
      GTest::gtest_main
  )

  # Copy shared libraries to test executable directory
  if(WIN32)
    add_custom_command(TARGET livekit_bridge_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit_bridge>
        $<TARGET_FILE_DIR:livekit_bridge_tests>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit>
        $<TARGET_FILE_DIR:livekit_bridge_tests>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:livekit>/livekit_ffi.dll"
        $<TARGET_FILE_DIR:livekit_bridge_tests>
      COMMENT "Copying DLLs to bridge test directory"
    )
  elseif(APPLE)
    add_custom_command(TARGET livekit_bridge_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit_bridge>
        $<TARGET_FILE_DIR:livekit_bridge_tests>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit>
        $<TARGET_FILE_DIR:livekit_bridge_tests>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:livekit>/liblivekit_ffi.dylib"
        $<TARGET_FILE_DIR:livekit_bridge_tests>
      COMMENT "Copying dylibs to bridge test directory"
    )
  else()
    add_custom_command(TARGET livekit_bridge_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit_bridge>
        $<TARGET_FILE_DIR:livekit_bridge_tests>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:livekit>
        $<TARGET_FILE_DIR:livekit_bridge_tests>
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:livekit>/liblivekit_ffi.so"
        $<TARGET_FILE_DIR:livekit_bridge_tests>
      COMMENT "Copying shared libraries to bridge test directory"
    )
  endif()

  # Register tests with CTest
  gtest_discover_tests(livekit_bridge_tests
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    PROPERTIES
      LABELS "bridge_unit"
  )
endif()
