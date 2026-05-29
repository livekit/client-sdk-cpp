# Logging

The SDK uses [spdlog](https://github.com/gabime/spdlog) internally but does
**not** expose it in public headers. All log output goes through a thin public
API in `<livekit/logging.h>`.

## Two-tier filtering

| Tier | When | How | Cost |
|------|------|-----|------|
| **Compile-time** | CMake configure | `-DLIVEKIT_LOG_LEVEL=WARN` | Zero — calls below the level are stripped from the binary |
| **Runtime** | Any time after `initialize()` | `livekit::setLogLevel(LogLevel::Warn)` | Minimal — a level check before formatting |

### Compile-time level (`LIVEKIT_LOG_LEVEL`)

Set once when you configure CMake. Calls below this threshold are completely
removed by the preprocessor — no format-string evaluation, no function call.

```bash
# Development (default): keep everything available
cmake -DLIVEKIT_LOG_LEVEL=TRACE ..

# Release: strip TRACE / DEBUG / INFO
cmake -DLIVEKIT_LOG_LEVEL=WARN ..

# Production: only ERROR and CRITICAL survive
cmake -DLIVEKIT_LOG_LEVEL=ERROR ..
```

Valid values: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `CRITICAL`, `OFF`.

### Runtime level (`setLogLevel`)

Among the levels that survived compilation you can still filter at runtime
without rebuilding:

```cpp
#include <livekit/livekit.h>

livekit::initialize();                           // default level: Info
livekit::setLogLevel(livekit::LogLevel::Debug);  // show more detail
livekit::setLogLevel(livekit::LogLevel::Warn);   // suppress info chatter
```

## Custom log callback

Replace the default stderr sink with your own handler. This is the integration
point for frameworks like ROS2 (`RCLCPP_*` macros), Android logcat, or any
structured-logging pipeline:

```cpp
#include <livekit/livekit.h>

livekit::initialize();
livekit::setLogLevel(livekit::LogLevel::Trace);

livekit::setLogCallback(
    [](livekit::LogLevel level,
       const std::string &logger_name,
       const std::string &message) {
      // Route to your framework, e.g.:
      //   RCLCPP_INFO(get_logger(), "[%s] %s", logger_name.c_str(), message.c_str());
      myLogger.log(level, logger_name, message);
    });

// Pass nullptr to restore the default stderr sink:
livekit::setLogCallback(nullptr);
```

See the [`logging_levels/custom_sinks.cpp`](https://github.com/livekit-examples/cpp-example-collection/blob/main/logging_levels/custom_sinks.cpp)
example for three copy-paste-ready patterns: a **file logger**, **JSON
structured lines**, and a **ROS2 bridge** that maps `LogLevel` to `RCLCPP_*`
macros.

## Available log levels

| Level | Typical use |
|-------|-------------|
| `Trace` | Per-frame / per-packet detail (very noisy) |
| `Debug` | Diagnostic info useful during development |
| `Info` | Normal operational messages (connection, track events) |
| `Warn` | Unexpected but recoverable situations |
| `Error` | Failures that affect functionality |
| `Critical` | Unrecoverable errors |
| `Off` | Suppress all output |
