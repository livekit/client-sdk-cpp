# Tracing

The SDK includes built-in support for [Chromium tracing](https://www.chromium.org/developers/how-tos/trace-event-profiling-tool/),
allowing you to capture detailed performance traces for debugging and
optimization.

## Basic usage

```cpp
#include <livekit/livekit.h>

// Start tracing to a file
livekit::startTracing("trace.json");

// ... run your application ...

// Stop tracing and flush to file
livekit::stopTracing();
```

## Filtering by category

You can optionally filter which categories to trace:

```cpp
// Trace only specific categories (supports wildcards)
livekit::startTracing("trace.json", {"livekit.*", "webrtc.*"});
```

## Viewing traces

Open the generated trace file in one of these viewers:

- **Chrome**: navigate to `chrome://tracing` and click "Load" to open the trace file.
- **Perfetto**: open [ui.perfetto.dev](https://ui.perfetto.dev) and drag-drop your trace file.
