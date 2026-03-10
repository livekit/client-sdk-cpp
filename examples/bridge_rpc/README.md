# Bridge RPC Example

A minimal example of custom user-registered RPC methods using the
`SessionManager` high-level API.

Two headless executables — **BridgeRpcReceiver** and **BridgeRpcCaller** —
connect to the same LiveKit room. The receiver registers a `"print"` RPC
method that logs the caller's message and sleeps for a variable duration
before responding. The caller sends a numbered message every ~1 second and
prints the round-trip time.

## Sleep schedule

The receiver picks a sleep duration based on the call number:

| Call number   | Sleep   |
|---------------|---------|
| `%10 == 0`   | 20 s    |
| `%5 == 0`    | 10 s    |
| otherwise     | 1 s     |

Because the default LiveKit RPC timeout is 15 seconds, the caller sets a
30-second timeout so the 20-second sleeps can complete. The 10-second and
20-second cases demonstrate how long-running handlers affect the caller's
blocking `performRpc` call.

## Running

Generate two tokens for the same room with different identities:

```bash
lk token create --join --room my-room --identity receiver --valid-for 24h
lk token create --join --room my-room --identity caller   --valid-for 24h
```

Start the receiver first, then the caller:

```bash
# Terminal 1
LIVEKIT_URL=wss://... LIVEKIT_TOKEN=<receiver-token> ./build-release/bin/BridgeRpcReceiver

# Terminal 2
LIVEKIT_URL=wss://... LIVEKIT_TOKEN=<caller-token>   ./build-release/bin/BridgeRpcCaller
```

## Sample output

### Receiver

```
[receiver] Connecting to wss://example.livekit.cloud ...
[receiver] Connected.
[receiver] Registered RPC method "print".
[receiver]   call %10==0 -> 20s sleep
[receiver]   call %5==0  -> 10s sleep
[receiver]   otherwise   ->  1s sleep
[receiver] Waiting for calls...
[receiver] Call #1 from caller: "Hello from caller #1" (sleeping 1s)
[receiver] Call #1 done.
[receiver] Call #2 from caller: "Hello from caller #2" (sleeping 1s)
[receiver] Call #2 done.
[receiver] Call #3 from caller: "Hello from caller #3" (sleeping 1s)
[receiver] Call #3 done.
[receiver] Call #4 from caller: "Hello from caller #4" (sleeping 1s)
[receiver] Call #4 done.
[receiver] Call #5 from caller: "Hello from caller #5" (sleeping 10s)
[receiver] Call #5 done.
[receiver] Call #6 from caller: "Hello from caller #6" (sleeping 1s)
[receiver] Call #6 done.
[receiver] Call #7 from caller: "Hello from caller #7" (sleeping 1s)
[receiver] Call #7 done.
[receiver] Call #8 from caller: "Hello from caller #8" (sleeping 1s)
[receiver] Call #8 done.
[receiver] Call #9 from caller: "Hello from caller #9" (sleeping 1s)
[receiver] Call #9 done.
[receiver] Call #10 from caller: "Hello from caller #10" (sleeping 20s)
[receiver] Call #10 done.
```

### Caller

```
[caller] Connecting to wss://example.livekit.cloud ...
[caller] Connected.
[caller] #1 Sending: "Hello from caller #1" ...
[caller] #1 Response: "ok (slept 1s)" (1159ms)
[caller] #2 Sending: "Hello from caller #2" ...
[caller] #2 Response: "ok (slept 1s)" (1174ms)
[caller] #3 Sending: "Hello from caller #3" ...
[caller] #3 Response: "ok (slept 1s)" (1152ms)
[caller] #4 Sending: "Hello from caller #4" ...
[caller] #4 Response: "ok (slept 1s)" (1135ms)
[caller] #5 Sending: "Hello from caller #5" ...
[caller] #5 Response: "ok (slept 10s)" (10139ms)
[caller] #6 Sending: "Hello from caller #6" ...
[caller] #6 Response: "ok (slept 1s)" (1138ms)
[caller] #7 Sending: "Hello from caller #7" ...
[caller] #7 Response: "ok (slept 1s)" (1143ms)
[caller] #8 Sending: "Hello from caller #8" ...
[caller] #8 Response: "ok (slept 1s)" (1115ms)
[caller] #9 Sending: "Hello from caller #9" ...
[caller] #9 Response: "ok (slept 1s)" (1123ms)
[caller] #10 Sending: "Hello from caller #10" ...
[caller] #10 Response: "ok (slept 20s)" (20119ms)
```
