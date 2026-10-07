# Repository Documentation

Additional documentation for the SDK.

- [Building](building.md) — prerequisites, build scripts, CMake presets,
  vcpkg, integration into your CMake project, troubleshooting.
- [Built-in capture sources](capture-sources.md) — pattern and clock sources,
  publishing, and capture lifecycle.
- [Authentication](authentication.md) — generating tokens, token source types
  (initial connect only), and in-session token refresh.
- [Logging](logging.md) — compile-time vs runtime filtering, log levels,
  custom sinks (file, JSON, ROS2 `RCLCPP_*` macros).
- [Tracing](tracing.md) — Chromium-format performance traces, viewing in
  `chrome://tracing` and Perfetto.
- [Testing](testing.md) — unit, integration, and stress test suites; env
  vars; token-helper script for local `livekit-server --dev` runs.
- [Developer tools](tools.md) — `clang-tidy`, `clang-format`, `valgrind`,
  Doxygen, pre-commit hook, Rust submodule recovery tips.
