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
4. **Creates generic subscriptions** (`rclcpp::GenericSubscription`) for each
   newly matched topic, using a QoS profile aggregated from all active publishers
   (see [QoS Determination](#qos-determination) below).

Because `GenericSubscription` works with serialized messages, the bridge does not
need compile-time knowledge of any message types.

```
┌─────────────────────────────────────────────────────────┐
│                  Ros2LiveKitBridge Node                 │
│                                                         │
│  ┌──────────┐    ┌──────────────┐    ┌───────────────┐  │
│  │ Params   │───>│ Regex Engine │    │ Subscription  │  │
│  │ (YAML)   │    │ (compiled    │    │ Map           │  │
│  └──────────┘    │  patterns)   │    │ (topic->sub)  │  │
│                  └──────┬───────┘    └───────┬───────┘  │
│                         │                    │          │
│  ┌──────────────────────▼────────────────────▼───────┐  │
│  │              Poll Timer (wall clock)              │  │
│  │  1. get_topic_names_and_types()                   │  │
│  │  2. regex match against patterns                  │  │
│  │  3. skip already-subscribed topics                │  │
│  │  4. get_publishers_info_by_topic() for QoS        │  │
│  │  5. create_generic_subscription()                 │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

## Configuration

Parameters are loaded from `config/ros2_livekit_bridge_params.yaml`:

| Parameter                  | Type             | Default | Description |
|----------------------------|------------------|---------|-------------|
| `room_name`                | string           | `""`    | LiveKit room name (unused for now). |
| `topic_polling_period_ms`  | int              | `500`   | Interval in milliseconds between graph polls. |
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

```bash
cd ros/
source /opt/ros/humble/setup.bash
colcon build --packages-select ros2_livekit_bridge
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

## Current Limitations

- **No LiveKit forwarding yet.** Subscriber callbacks currently only log the
  topic name and serialized message size. The LiveKit publish path is not
  implemented.
- **No subscriber removal.** Once a subscription is created it is never removed,
  even if the publisher disappears. Stale subscriptions remain in the map.

## Examples
Instructions for running the Ignition Gazebo Livekit Demo

# Prerequisits
- Gazebo installation. See the [ROS2 docs](https://docs.ros.org/en/humble/Tutorials/Advanced/Simulators/Gazebo/Gazebo.html) for instructions. __NOTE: ensure correct ROS distro__.
- the ros_gz repos: https://github.com/gazebosim/ros_gz/tree/ros2
# Run the Demo
```
# Source the env
source /opt/ros/humble/setup.bash

# Run the sim
ros2 launch ros2_livekit_bridge image_bridge.launch.py image_topic:=/rgbd_camera/image

# view the ignition topics
ign topic -l

# List the ros2 topics
ros2 topic list

# Launch the livekit bridge
ros2 launch ros2_livekit_bridge ros2_livekit_bridge_launch.xml


# Optional: Launch the foxglove bridge to validate data independent of the livekit bridge
ros2 launch ros2_livekit_bridge foxglove_bridge_launch.xml
```