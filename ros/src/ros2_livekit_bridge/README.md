# ros2_livekit_bridge

A ROS2 node that bridges the ROS2 topic graph to the LiveKit ecosystem. The node
dynamically discovers and subscribes to ROS2 topics matching user-defined patterns,
using publisher-matched QoS profiles, and forwards them to a LiveKit room.

## Prerequisites

Ensure you are able to run the examples from the bridge
[README](../../bridge/README.md).

## Architecture

The bridge is implemented as a single ROS2 node (`Ros2LiveKitBridge`) that:

1. **Parses parameters** from a YAML config declaring which topics to watch.
2. **Polls the ROS2 graph** at a configurable interval using
   `get_topic_names_and_types()` -- a lightweight DDS graph-cache lookup that
   does not add traffic to the network or affect other nodes.
3. **Matches discovered topics** against the configured list of ECMAScript
   regular expressions.
4. **Creates subscriptions** for each newly matched topic, using a QoS profile
   aggregated from all active publishers (see [QoS Determination](#qos-determination)
   below). For recognized message types (see below), a **typed subscription** is
   created so the message fields are directly accessible; all other topics use a
   **generic subscription** (`rclcpp::GenericSubscription`) that works with
   serialized bytes.

### Typed (restricted) message support

| ROS2 message type          | LiveKit track type | Wire format | Behaviour |
|----------------------------|--------------------|-------------|-----------|
| `sensor_msgs/msg/Image`   | Video track        | RGBA pixels | A `BridgeVideoTrack` is created lazily on the first received frame (using the image dimensions). Each callback converts the image to RGBA and calls `pushFrame()` directly. Supported encodings: `rgba8`, `rgb8`, `bgr8`, `bgra8`, `mono8`. |
| `nav_msgs/msg/Odometry`   | Data track | Foxglove protobuf (`PoseInFrame`) | Converted via `ros2_foxglove_adapters`. Twist + covariance are dropped. |
| `nav_msgs/msg/Path`       | Data track | Foxglove protobuf (`PosesInFrame`) | Per-pose timestamps are dropped; only the path header timestamp is used. |
| `nav_msgs/msg/OccupancyGrid` | Data track | Foxglove protobuf (`Grid`) | Occupancy data stored as INT8 packed field. |
| `geometry_msgs/msg/TransformStamped` | Data track | Foxglove protobuf (`FrameTransform`) | Direct field mapping. |
| `geometry_msgs/msg/Pose2D` | Data track | Foxglove protobuf (`Pose`) | 2D pose promoted to 3D (z=0, quaternion encodes rotation about z). |
| `geometry_msgs/msg/PolygonStamped` | Data track | Foxglove protobuf (`Log`) | Vertices serialized as structured text. |
| `geometry_msgs/msg/PoseWithCovarianceStamped` | Data track | Foxglove protobuf (`PoseInFrame`) | Covariance matrix is dropped. |
| `sensor_msgs/msg/PointCloud2` | Data track | Foxglove protobuf (`PointCloud`) | Binary point data copied verbatim with mapped field descriptors. |
| `sensor_msgs/msg/Imu`     | Data track | Foxglove protobuf (`PoseInFrame`) | Only orientation preserved; angular velocity and linear acceleration are dropped. |
| `sensor_msgs/msg/Joy`     | Data track | Foxglove protobuf (`Log`) | Axes and buttons serialized as structured text. |
| `sensor_msgs/msg/BatteryState` | Data track | Foxglove protobuf (`Log`) | Key battery metrics serialized as structured text. |
| `std_msgs/msg/String`     | Data track | Foxglove protobuf (`Log`) | String placed in Log.message field. |

Data track consumers only need the [Foxglove protobuf schema definitions](https://github.com/foxglove/schemas)
to decode messages -- no ROS2 dependency is required. Unsupported message types
are skipped with a warning logged at discovery time.

```
┌───────────────────────────────────────────────────────────────────────┐
│                        Ros2LiveKitBridge Node                         │
│                                                                       │
│  ┌──────────┐    ┌──────────────┐    ┌─────────────────────────────┐  │
│  │ Params   │───>│ Regex Engine │    │ Subscription Map            │  │
│  │ (YAML)   │    │ (compiled    │    │ (topic -> sub)              │  │
│  └──────────┘    │  patterns)   │    └──────────────┬──────────────┘  │
│                  └──────┬───────┘                   │                 │
│                         │                           │                 │
│  ┌──────────────────────▼───────────────────────────▼──────────────┐  │
│  │                 Poll Timer (wall clock)                         │  │
│  │  1. get_topic_names_and_types()                                 │  │
│  │  2. regex match against patterns                                │  │
│  │  3. skip already-subscribed topics                              │  │
│  │  4. get_publishers_info_by_topic() for QoS                      │  │
│  │  5a. sensor_msgs/msg/Image  → typed sub → pushFrame(RGBA)      │  │
│  │  5b. supported data types   → typed sub → toFoxglove() →       │  │
│  │      SerializeToString() → pushFrame(protobuf bytes)            │  │
│  │  5c. unsupported types      → warn + skip                      │  │
│  └──────────────────────────────────────────────────┬──────────────┘  │
│                                                     │                 │
│  ┌──────────────────────────────────────────────────▼──────────────┐  │
│  │                  LiveKitBridge (livekit_bridge)                  │  │
│  │  ┌─────────────────┐                                            │  │
│  │  │ BridgeVideoTrack│── pushFrame(RGBA) ───> LiveKit Room        │  │
│  │  │ (per image topic)│                                           │  │
│  │  └─────────────────┘                                            │  │
│  │  ┌─────────────────┐                                            │  │
│  │  │ BridgeDataTrack │── pushFrame(protobuf) ──> LiveKit Room     │  │
│  │  │ (per data topic) │                                           │  │
│  │  └─────────────────┘                                            │  │
│  │  TODO: BridgeAudioTrack                                         │  │
│  └─────────────────────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────────────────┘
```

## Configuration

Parameters are loaded from `config/ros2_livekit_bridge_params.yaml`:

| Parameter                  | Type             | Default | Description |
|----------------------------|------------------|---------|-------------|
| `room_name`                | string           | `""`    | LiveKit room name (unused for now). |
| `topic_polling_period_ms`  | int              | `500`   | Interval in milliseconds between graph polls. |
| `ros_threads`              | int              | `0`     | Number of threads for the ROS2 executor. The default 0 will use the number of cpu cores found instead|
| `ros_topics`               | list of strings  | `[]`    | ECMAScript regex patterns matched against full topic names. |
| `min_qos_depth`            | int              | `1`     | Lower bound for subscriber history depth. |
| `max_qos_depth`            | int              | `25`    | Upper bound for subscriber history depth. |
| `best_effort_qos_topics`   | list of strings  | `[]`    | Regex patterns for topics forced to BEST_EFFORT reliability. |

### Topic pattern examples

Patterns are full ECMAScript regular expressions tested with `std::regex_match`
(i.e. the pattern must match the **entire** topic name):

| Pattern              | Matches                                      |
|----------------------|----------------------------------------------|
| `/lidar/points`      | Exactly `/lidar/points`                      |
| `/tf.*`              | `/tf`, `/tf_static`, `/tf_anything`          |
| `/camera/.*`         | `/camera/image_raw`, `/camera/camera_info`   |
| `/robot[0-9]+/odom`  | `/robot1/odom`, `/robot42/odom`              |

## QoS Determination

The bridge determines subscriber QoS by aggregating all publisher endpoints for a
topic, following the same approach as
[`ros2 topic echo`](https://github.com/ros2/ros2cli/blob/619b3d1c9/ros2topic/ros2topic/verb/echo.py#L137-L194)
and the [Foxglove bridge](https://github.com/foxglove/foxglove-sdk/tree/main/ros/src/foxglove_bridge):

- **Depth**: Sum of each publisher's history depth (minimum 1 per publisher to
  handle RMW implementations that report 0), clamped to
  `[min_qos_depth, max_qos_depth]`. This correctly handles multiple
  `TRANSIENT_LOCAL` publishers (e.g. several `tf_static` broadcasters) whose
  latched messages all need to fit in the subscriber queue.
- **Reliability**: `RELIABLE` only when **all** publishers advertise `RELIABLE`.
  If publishers have mixed policies, falls back to `BEST_EFFORT` so the
  subscriber can connect to every publisher. Topics matching
  `best_effort_qos_topics` are unconditionally forced to `BEST_EFFORT`.
- **Durability**: `TRANSIENT_LOCAL` only when **all** publishers advertise
  `TRANSIENT_LOCAL`; otherwise `VOLATILE`.
- **Incompatible QoS callback**: Each subscription registers an event callback
  that logs an error if the chosen QoS is incompatible with a publisher.

## Building

**1. Build the LiveKit SDK** (from the repo root) with system spdlog so the
ROS2 node and rcl share a single spdlog implementation (avoids SIGBUS in
rcl logging when two spdlog/fmt copies are loaded):

```bash
# From client-sdk-cpp (repo root)
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug -DLIVEKIT_USE_SYSTEM_SPDLOG=ON
cmake --build build-debug
```

**2. Build the ROS2 workspace** (do *not* pass `LIVEKIT_USE_SYSTEM_SPDLOG` to colcon—that option is only for the SDK build in step 1):

```bash
cd ros/
source /opt/ros/humble/setup.bash
colcon build --packages-select ros2_livekit_bridge --cmake-args -DLIVEKIT_SDK_DIR=/path/to/cpp-client-sdk/build-debug
```

## Running

```bash
source ros/install/setup.bash

# With parameters from the default config file:
ros2 run ros2_livekit_bridge ros2_livekit_bridge_node \
  --ros-args --params-file \
  $(ros2 pkg prefix ros2_livekit_bridge)/share/ros2_livekit_bridge/config/ros2_livekit_bridge_params.yaml

# Or via the launch file:
ros2 launch ros2_livekit_bridge ros2_livekit_bridge_launch.xml
```

```bash
# launch with gdb
   gdb --args /home/jetson/workspaces/client-sdk-cpp/ros/install/ros2_livekit_bridge/lib/ros2_livekit_bridge/ros2_livekit_bridge_node --ros-args -r __node:=ros2_livekit_bridge --params-file /home/jetson/workspaces/client-sdk-cpp/ros/install/ros2_livekit_bridge/share/ros2_livekit_bridge/config/ros2_livekit_bridge_params.yaml
```

## Current Limitations
### Must dos
1. video track efficiency
2. audio track impl
3. cleaner compilation in CMakeLists.txt
4. test/validation sub to multiple camera/audio and forward

### Video tracks

- **Single `SOURCE_CAMERA` source.** All `sensor_msgs/msg/Image` topics are
  published as `livekit::TrackSource::SOURCE_CAMERA`. LiveKit allows only one
  track per source type per participant, so publishing multiple image topics
  simultaneously (e.g. `/camera/color` and `/camera/depth`) will conflict. A
  future change should assign distinct sources (`SOURCE_CAMERA`,
  `SOURCE_SCREENSHARE`, ...) or allow per-topic source configuration.
- **Lazy track creation uses first-frame dimensions.** The `BridgeVideoTrack` is
  created from the width/height of the first received `Image` message. If the
  camera resolution changes mid-stream the track is **not** recreated.
- **Limited encoding support.** Only `rgba8`, `rgb8`, `bgr8`, `bgra8`, and
  `mono8` encodings are handled. Other encodings (e.g. `16UC1`, `bayer_*`,
  compressed) are silently dropped with a throttled warning.
- **CPU pixel conversion.** Encoding conversion (e.g. `bgr8` -> RGBA) is done
  per-pixel on the CPU inside the subscription callback. For high-resolution or
  high-framerate streams this may become a bottleneck.
- **North Star** we shouldn be able to have the conversion done on the rust side of
  things and just pass in the correcct type, therefore the bridge is really just a pass through.

### Audio tracks

- **TODO:** No ROS2 message type is currently mapped to a LiveKit audio track.
  Candidates include `audio_common_msgs/msg/AudioData` and raw PCM topics. A
  typed subscription similar to the Image path should create a `BridgeAudioTrack`
  and call `pushFrame()` in the callback.

### Data tracks

- **Foxglove protobuf only.** Data tracks serialize using Foxglove protobuf
  schemas via `ros2_foxglove_adapters`. Only the ~12 message types listed in
  [Typed message support](#typed-restricted-message-support) are forwarded;
  all other types are silently skipped. To add new types, implement a
  `toFoxglove()` overload in the `ros2_foxglove_adapters` package.
- **Schema information not sent out-of-band.** The consumer must know which
  Foxglove protobuf type to deserialize for each topic. A future improvement
  could include a schema negotiation or metadata channel.

### General

- **No subscriber removal.** Once a subscription is created it is never removed,
  even if the publisher disappears. Stale subscriptions remain in the map.
- **Config changes require rebuild.** The YAML config is copied into the colcon
  install space at build time. Edits to the source YAML do not take effect until
  `colcon build` is re-run (or use `--symlink-install` during development).

### Linking
Cmake is fragile and requires specific relative locations to the livekit bridge libs. Needs to be more flexible.

## Examples
Instructions for running the Ignition Gazebo Livekit Demo

### Prerequisits
- Gazebo installation. See the [ROS2 docs](https://docs.ros.org/en/humble/Tutorials/Advanced/Simulators/Gazebo/Gazebo.html) for instructions. __NOTE: ensure correct ROS distro__.
- the ros_gz repos: https://github.com/gazebosim/ros_gz/tree/ros2

```
# Source the env
source /opt/ros/humble/setup.bash

# Run the sim
ros2 launch ros2_livekit_bridge image_bridge.launch.py image_topic:=/rgbd_camera/image

# view the ignition topics
ign topic -l

# List the ros2 topics
ros2 topic list

# Optional: Launch the foxglove bridge to validate sim
ros2 launch foxglove_bridge foxglove_bridge_launch.xml

# Launch the livekit bridge
export LIVEKIT_TOKEN=<token>
export LIVEKIT_URL=<url>
ros2 launch ros2_livekit_bridge ros2_livekit_bridge_launch.xml
```

### Debugging
## Test a think LiveKit/ROS2 integration
```
./install/ros2_livekit_bridge/lib/ros2_livekit_bridge/livekit_connect_node \
--ros-args \
-p livekit_url:=<url> \
-p livekit_token:=<token>
```

### Provide the bridge with a ros2 stream from your usb cam
```
python3 test/scripts/usb_camera_publisher.py
```
__NOTE__: to launch the python node you must have a video camera and opencv for python3 installed