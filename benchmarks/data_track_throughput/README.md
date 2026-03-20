# Data Track Throughput Experiment

Coordinated producer and consumer for benchmarking `LocalDataTrack` /
`RemoteDataTrack` throughput across a sweep of payload sizes and publish rates.

## What It Does

- `producer.cpp`
  - Publishes a data track named `data-track-throughput`
  - Runs a default sweep of payload sizes and publish rates (see
    **Test Bounds** below)
  - Calls the consumer over RPC before and after each scenario

- `consumer.cpp`
  - Registers a room data-frame callback for the producer's data track
  - Receives every frame and records arrival timestamps
  - Logs validation warnings (size mismatches, header mismatches, etc.) to stderr
  - Tracks duplicates and missing messages
  - Appends raw data to scenario-level and per-message CSV files

## Design Principles

- **Raw data only in CSV.** The consumer writes only directly measured values
  (counts, byte totals, microsecond timestamps). All derived metrics (throughput,
  latency percentiles, delivery ratio, etc.) are computed at analysis time by
  `scripts/plot_throughput.py`.
- **Fixed packet size per scenario.** Each scenario uses a single
  `packet_size_bytes`. This ensures every message in a run is the same size,
  making aggregate measurements unambiguous.
- **Minimal measurement overhead.** The hot `onDataFrame` callback captures the
  arrival timestamp first, then appends to an in-memory vector under a brief
  mutex. File I/O happens only at finalization after all data is collected.

## Test Bounds

All bounds are defined in `common.h`. A scenario is any combination of
(payload size, publish rate) that passes all three constraints below.

### Hard Limits

| Parameter | Min | Max |
|-----------|-----|-----|
| Packet size | 1 KiB | 256 MiB |
| Publish rate | 1 Hz | 50k Hz |

### Data-Rate Budget

Every scenario must satisfy:

```
packet_size_bytes * desired_rate_hz <= 10 Gbps (1.25 GB/s)
```

This naturally allows small messages at very high rates and large messages at
low rates while preventing any single scenario from attempting an unreasonable
throughput that would destabilize the connection.

### Default Sweep Grid

The default sweep iterates over 13 payload sizes and 13 publish rates, skipping
any combination that exceeds the data-rate budget:

**Payload sizes:** 1 KiB, 4 KiB, 16 KiB, 64 KiB, 128 KiB, 256 KiB, 512 KiB,
1 MiB, 2 MiB, 4 MiB, 16 MiB, 64 MiB, 256 MiB

**Publish rates:** 1, 5, 10, 25, 50, 100, 200, 500, 1k, 5k, 10k, 20k, 50k Hz

The budget clips larger payloads to lower rates. For example:

| Payload | Max rate allowed |
|---------|-----------------|
| 1 KiB | 50k Hz (all rates) |
| 16 KiB | 50k Hz (all rates) |
| 64 KiB | 10k Hz |
| 256 KiB | 1k Hz |
| 1 MiB | 1k Hz |
| 4 MiB | 200 Hz |
| 64 MiB | 10 Hz |
| 256 MiB | 1 Hz |

The budget clips larger payloads to lower rates. For example:

| Payload | Max rate allowed |
|---------|-----------------|
| 1 KiB | 50k Hz (all rates) |
| 16 KiB | 50k Hz (all rates) |
| 64 KiB | 10k Hz |
| 256 KiB | 1k Hz |
| 1 MiB | 1k Hz |
| 4 MiB | 200 Hz |
| 64 MiB | 10 Hz |
| 256 MiB | 1 Hz |

Single-scenario mode (`--rate-hz`, `--packet-size`, `--num-msgs`) bypasses the
default grid and only enforces the hard limits and data-rate budget, allowing
any valid combination to be tested explicitly.

## CSV Output

The consumer writes raw measurement data only. All derived metrics are computed
at analysis time by `scripts/plot_throughput.py`.

### `throughput_summary.csv`

One row per scenario. Contains only raw counts, byte totals, and microsecond
timestamps:

| Column | Description |
|--------|-------------|
| `run_id` | Unique scenario identifier |
| `scenario_name` | Human-readable scenario label |
| `desired_rate_hz` | Requested publish rate |
| `packet_size_bytes` | Fixed packet size for this scenario |
| `messages_requested` | Number of messages the producer was told to send |
| `messages_attempted` | Number of messages the producer tried to send |
| `messages_enqueued` | Number of messages successfully enqueued |
| `messages_enqueue_failed` | Number of enqueue failures |
| `messages_received` | Unique messages received by consumer |
| `messages_missed` | `messages_requested - messages_received` |
| `duplicate_messages` | Number of duplicate frames received |
| `attempted_bytes` | Total bytes the producer attempted to send |
| `enqueued_bytes` | Total bytes successfully enqueued |
| `received_bytes` | Total bytes received by consumer |
| `first_send_time_us` | Timestamp of first send (microseconds since epoch) |
| `last_send_time_us` | Timestamp of last send |
| `first_arrival_time_us` | Timestamp of first arrival at consumer |
| `last_arrival_time_us` | Timestamp of last arrival at consumer |

### `throughput_messages.csv`

One row per received frame. Raw observation data only:

| Column | Description |
|--------|-------------|
| `run_id` | Scenario identifier |
| `sequence` | Message sequence number |
| `payload_bytes` | Actual payload size received |
| `send_time_us` | Producer send timestamp (microseconds since epoch) |
| `arrival_time_us` | Consumer arrival timestamp (microseconds since epoch) |
| `is_duplicate` | 1 if this sequence was already seen, 0 otherwise |

## Prerequisites

- CMake 3.20+
- C++17 compiler
- The LiveKit C++ SDK, built and installed (see below)

## Building

All commands below assume you are in **this directory**
(`data_track_throughput/`).

### 1. Build and install the SDK

From the SDK repository root:

```bash
./build.sh          # builds the SDK (debug by default)
cmake --install build-debug --prefix local-install
```

### 2. Configure this experiment

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$(cd ../../local-install && pwd)"
```

> Adjust the `CMAKE_PREFIX_PATH` to wherever the SDK was installed. The path
> above assumes this directory lives two levels below the repository root; it
> works regardless of the parent directory's name.

### 3. Build

```bash
cmake --build build
```

The executables and required shared libraries are placed in `build/`.

## Build Targets

- `DataTrackThroughputConsumer`
- `DataTrackThroughputProducer`

## Running

## Generate Tokens

```bash
# producer
lk token create \
  --api-key devkey \
  --api-secret secret \
  -i producer \
  --join \
  --valid-for 99999h \
  --room robo_room \
  --grant '{"canPublish":true,"canSubscribe":true,"canPublishData":true}'

# consumer
lk token create \
  --api-key devkey \
  --api-secret secret \
  -i consumer \
  --join \
  --valid-for 99999h \
  --room robo_room \
  --grant '{"canPublish":true,"canSubscribe":true,"canPublishData":true}'
```

Start the local server:
```bash
LIVEKIT_CONFIG="enable_data_tracks: true" livekit-server --dev
```

Start the consumer first:

```bash
./build/DataTrackThroughputConsumer <ws-url> <consumer-token>
```

Then start the producer:

```bash
./build/DataTrackThroughputProducer <ws-url> <producer-token> --consumer consumer
```

If you omit `--consumer`, the producer expects exactly one remote participant
to already be in the room.

## Single Scenario

Instead of the full sweep, you can run one scenario:

```bash
./build/DataTrackThroughputProducer \
  <ws-url> <producer-token> \
  --consumer <consumer-identity> \
  --rate-hz 50 \
  --packet-size 1mb \
  --num-msgs 25
```

## Plotting

Generate plots from a benchmark output directory:

```bash
python3 scripts/plot_throughput.py data_track_throughput_results
```

By default the script writes PNGs into `data_track_throughput_results/plots/`.
Pass `--output-dir <path>` to override the output location.

All derived metrics (throughput, latency percentiles, delivery ratio, receive
rate, interarrival times) are computed from the raw CSV timestamps and counts
at plot time.

### Generated Plots

From `throughput_summary.csv` + `throughput_messages.csv`:

| File | Description |
|------|-------------|
| `expected_vs_actual_throughput.png` | Scatter plot comparing expected vs actual receive throughput (Mbps). Points are colored by desired publish rate and sized by payload. An ideal y=x reference line is overlaid. |
| `dropped_messages_vs_expected_throughput.png` | Scatter plot of missed/dropped message count vs expected throughput, colored by payload size (log scale). |
| `actual_throughput_heatmap.png` | Heatmap of actual receive throughput (Mbps) with payload size on the y-axis and desired rate on the x-axis. |
| `delivery_ratio_heatmap.png` | Heatmap of delivery ratio (received / requested) over the same payload-size x rate grid. |
| `p50_latency_heatmap.png` | Heatmap of median (P50) send-to-receive latency (ms) over the same grid. |
| `p95_latency_heatmap.png` | Heatmap of P95 send-to-receive latency (ms) over the same grid. |
| `message_latency_histogram.png` | Histogram of per-message latency (ms) across all received frames. |
| `message_interarrival_series.png` | Time-series line plot of inter-arrival gaps (ms) for every received message, ordered by run then arrival time. |
