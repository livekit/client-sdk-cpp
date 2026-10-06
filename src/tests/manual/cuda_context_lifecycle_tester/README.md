# CUDA context lifecycle tester

This manual tester measures repeated CUDA context creation and destruction
without initializing LiveKit or any video codec resources. It calls `cuInit()`
and `cuDeviceGet()` once, then calls only `cuCtxCreate()` and `cuCtxDestroy()`
inside the measured loop.

Build it with the other test targets:

```bash
./build.sh release-tests
```

Run ten iterations directly:

```bash
./build-release/bin/livekit_cuda_context_lifecycle_tester 10
```

Or run it through the memory tracker:

```bash
python3 ./scripts/track_process_memory.py --interval 0.1 -- \
  ./build-release/bin/livekit_cuda_context_lifecycle_tester 10
```
