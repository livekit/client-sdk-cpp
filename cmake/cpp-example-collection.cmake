# Copyright 2026 LiveKit, Inc.
#
# LiveKit examples integration helpers.

include_guard(GLOBAL)

function(livekit_configure_cpp_example_collection)
  set(LIVEKIT_CPP_EXAMPLES_SOURCE_DIR
      "${CMAKE_SOURCE_DIR}/cpp-example-collection")
  set(LIVEKIT_CPP_EXAMPLES_INSTALL_PREFIX
      "${CMAKE_BINARY_DIR}/../local-install"
      CACHE PATH "Install prefix used by cpp-example-collection")
  set(LIVEKIT_CPP_EXAMPLES_BINARY_DIR
      "${CMAKE_BINARY_DIR}/cpp-example-collection-build"
      CACHE PATH "Build directory for cpp-example-collection")
  set(LIVEKIT_CPP_EXAMPLES_LIVEKIT_DIR
      "${LIVEKIT_CPP_EXAMPLES_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/cmake/LiveKit")

  if(NOT EXISTS "${LIVEKIT_CPP_EXAMPLES_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
      "cpp-example-collection submodule is missing. Run: "
      "git submodule sync --recursive && "
      "git submodule update --init --recursive --checkout")
  endif()

  add_custom_target(install_livekit_sdk_for_examples
    COMMAND ${CMAKE_COMMAND} -E make_directory "${LIVEKIT_CPP_EXAMPLES_INSTALL_PREFIX}"
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
            --prefix "${LIVEKIT_CPP_EXAMPLES_INSTALL_PREFIX}"
            --config "$<CONFIG>"
    DEPENDS livekit
    COMMENT "Installing LiveKit SDK for cpp-example-collection"
    VERBATIM
  )

  add_custom_target(cpp_example_collection ALL
    COMMAND ${CMAKE_COMMAND} -S "${LIVEKIT_CPP_EXAMPLES_SOURCE_DIR}"
            -B "${LIVEKIT_CPP_EXAMPLES_BINARY_DIR}"
            -DCMAKE_PREFIX_PATH="${LIVEKIT_CPP_EXAMPLES_INSTALL_PREFIX}"
            -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
            -DLiveKit_DIR="${LIVEKIT_CPP_EXAMPLES_LIVEKIT_DIR}"
    COMMAND ${CMAKE_COMMAND} --build "${LIVEKIT_CPP_EXAMPLES_BINARY_DIR}"
            --config "$<CONFIG>"
    DEPENDS install_livekit_sdk_for_examples
    COMMENT "Configuring and building cpp-example-collection"
    VERBATIM
  )
endfunction()
