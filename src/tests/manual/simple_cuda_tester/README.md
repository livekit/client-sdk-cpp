# Simple CUDA tester

Connects one room, publishes three H264 video tracks through the NVIDIA
encoder path, captures a short gray I420 sequence, then disconnects.

## Build

```bash
./build.sh release-tests
```

## Run

```bash
source scripts/set-test-tokens.sh
export RUST_LOG=libwebrtc=debug

./build-release/bin/livekit_simple_cuda_tester --iterations 10
```

`LIVEKIT_URL` and `LIVEKIT_TOKEN_A` come from `set-test-tokens.sh`. Pass `--iterations N`
or a bare integer to repeat the connect/publish/teardown cycle; the default is 1.
sets `LIVEKIT_PREFERRED_HW_ENCODER=nvenc` if it is unset, and publishes H264
because NVENC does not encode the default VP8 codec.

Confirm stderr includes `Using NVIDIA HW encoder (NVENC) for H264`.
