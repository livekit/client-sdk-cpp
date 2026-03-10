# Logging Examples

Demonstrates the LiveKit C++ SDK's two-tier logging system. No LiveKit server
is required -- the examples simply emit log messages at every severity level so
you can see how filtering works.

There are two executables:

| Target                       | Source              | What it shows |
|------------------------------|---------------------|---------------|
| `LoggingLevelsBasicUsage`    | `basic_usage.cpp`   | Runtime level cycling and a basic custom callback |
| `LoggingLevelsCustomSinks`   | `custom_sinks.cpp`  | Three practical custom sink patterns: file, JSON, and ROS2 bridge |

## Usage -- LoggingLevelsBasicUsage

```bash
# Full demo: cycles through every runtime level, then shows the callback API
./build/examples/LoggingLevelsBasicUsage

# Set a single runtime level and emit all messages
./build/examples/LoggingLevelsBasicUsage warn       # only WARN, ERROR, CRITICAL printed
./build/examples/LoggingLevelsBasicUsage trace      # everything printed
./build/examples/LoggingLevelsBasicUsage off        # nothing printed
```

## Usage -- LoggingLevelsCustomSinks

```bash
# Run all three sink demos in sequence
./build/examples/LoggingLevelsCustomSinks

# Run a single sink demo
./build/examples/LoggingLevelsCustomSinks file    # writes SDK logs to livekit.log
./build/examples/LoggingLevelsCustomSinks json    # emits JSON-lines to stdout
./build/examples/LoggingLevelsCustomSinks ros2    # mimics RCLCPP_* output format
```

## How log-level filtering works

The SDK filters log messages in two stages:

### 1. Compile-time (`LIVEKIT_LOG_LEVEL`)

Set at CMake configure time. Calls **below** this level are stripped from the
binary entirely -- the format string is never evaluated and no function is
called. This is zero-cost.

```bash
# Default: nothing stripped (all levels available at runtime)
cmake -DLIVEKIT_LOG_LEVEL=TRACE ..

# Strip TRACE, DEBUG, and INFO at compile time
cmake -DLIVEKIT_LOG_LEVEL=WARN ..

# Only ERROR and CRITICAL survive
cmake -DLIVEKIT_LOG_LEVEL=ERROR ..
```

Valid values: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `CRITICAL`, `OFF`.

Under the hood this sets `SPDLOG_ACTIVE_LEVEL`, which the `LK_LOG_*` macros
check with a preprocessor guard before emitting any code.

### 2. Runtime (`setLogLevel`)

Among the levels that survived compilation, `setLogLevel()` controls which
ones actually produce output. You can change it at any time after
`livekit::initialize()`:

```cpp
livekit::initialize();                           // default level: Info
livekit::setLogLevel(livekit::LogLevel::Debug);  // show more detail
livekit::setLogLevel(livekit::LogLevel::Error);  // only errors and above
```

### Interaction between the two tiers

| Compile-time level | Runtime level | TRACE | DEBUG | INFO | WARN | ERROR |
|--------------------|---------------|:-----:|:-----:|:----:|:----:|:-----:|
| TRACE              | Info          |       |       |  x   |  x   |   x   |
| TRACE              | Trace         |  x    |  x   |  x   |  x   |   x   |
| WARN               | Trace         |       |       |      |  x   |   x   |
| WARN               | Error         |       |       |      |      |   x   |

Cells marked **x** produce output. Empty cells are filtered out -- either
stripped at compile time (left columns when compile-time > level) or suppressed
at runtime.

## Custom log callbacks (`setLogCallback`)

`setLogCallback()` lets you redirect **all** SDK log output to your own handler
instead of the default stderr sink. This is the integration point for frameworks
like ROS2, Android logcat, or any structured-logging pipeline.

The basic API:

```cpp
livekit::setLogCallback(
    [](livekit::LogLevel level,
       const std::string &logger_name,
       const std::string &message) {
      // Your code here -- e.g. write to file, emit JSON, call RCLCPP_INFO, ...
    });

// Pass nullptr to restore the default stderr sink:
livekit::setLogCallback(nullptr);
```

`LoggingLevelsCustomSinks` (`custom_sinks.cpp`) provides three ready-to-copy patterns:

### File sink

Writes every SDK log line to a file with an ISO-8601 timestamp:

```cpp
auto file = std::make_shared<std::ofstream>("livekit.log", std::ios::trunc);
livekit::setLogCallback(
    [file](livekit::LogLevel level, const std::string &logger_name,
           const std::string &message) {
      *file << timestamp() << " [" << levelTag(level) << "] ["
            << logger_name << "] " << message << "\n";
    });
```

### JSON sink

Emits one JSON object per line -- ready for piping into `jq` or a log
aggregation service:

```
{"ts":"2025-07-01T12:00:00.123Z","level":"INFO","logger":"livekit","msg":"track published"}
```

### ROS2 bridge sink

Maps `livekit::LogLevel` to `RCLCPP_DEBUG` / `RCLCPP_INFO` / `RCLCPP_WARN` /
`RCLCPP_ERROR` so LiveKit logs appear in the standard ROS2 console output,
properly severity-tagged and namespaced under your node:

```cpp
livekit::setLogCallback(
    [node](livekit::LogLevel level, const std::string &logger_name,
           const std::string &message) {
      switch (level) {
      case livekit::LogLevel::Trace:
      case livekit::LogLevel::Debug:
        RCLCPP_DEBUG(node->get_logger(), "[%s] %s",
                     logger_name.c_str(), message.c_str());
        break;
      case livekit::LogLevel::Info:
        RCLCPP_INFO(node->get_logger(), "[%s] %s",
                    logger_name.c_str(), message.c_str());
        break;
      // ... Warn, Error, Critical ...
      }
    });
```

The example compiles without rclcpp by stubbing the output to match ROS2
formatting.
