# DataStamping

Minimal two-process example:

- `DataStampingProducer` publishes a synthetic camera track with
  `metadata.user_timestamp_us` set on every frame.
- `DataStampingProducer` also publishes a data track named `imu` with JSON
  payloads shaped like:

```json
{
  "angular": {"x": 0.0, "y": 0.0, "z": 0.0},
  "linear": {"x": 0.0, "y": 0.0, "z": 0.0}
}
```

The IMU values are simulated sine waves.

- `DataStampingConsumer` only registers:
  - `addOnDataFrameCallback()`
  - `setOnVideoFrameCallback()`

Run them in the same room with different participant identities:

```sh
LIVEKIT_URL=ws://localhost:7880 LIVEKIT_TOKEN=<producer-token> ./DataStampingProducer
LIVEKIT_URL=ws://localhost:7880 LIVEKIT_TOKEN=<consumer-token> ./DataStampingConsumer
```
