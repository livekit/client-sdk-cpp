# 📘 SimpleRpc Example — Technical Overview

This README provides deeper technical details about the RPC (Remote Procedure Call) support demonstrated in the SimpleRpc example.
It complements the example instructions found in the root README.md.

If you're looking for how to run the example, see the root [README](https://github.com/livekit/client-sdk-cpp).

This document explains:
- How LiveKit RPC works in the C++ SDK
- Where the APIs are defined
- How senders call RPC methods
- How receivers register handlers
- What happens if the receiver is absent
- How long-running operations behave
- Timeouts, disconnects, and unsupported methods
- RPC lifecycle events and error propagation

## 🔧 Overview: How RPC Works
LiveKit RPC allows one participant (the caller) to invoke a method on another participant (the receiver) using the data channel transport.
It is:
- Peer-to-peer within the room (not server-executed RPC)
- Request/response (caller waits for a reply or an error)
- Asynchronous under the hood, synchronous or blocking from the caller’s perspective
- Delivery-guaranteed when using the reliable data channel

Each RPC call includes:
| Field                    | Meaning                                             |
|--------------------------|-----------------------------------------------------|
| **destination_identity** | Identity of the target participant                  |
| **method**               | Method name string (e.g., "square-root")            |
| **payload**              | Arbitrary UTF-8 text                                |
| **response_timeout**     | Optional timeout (seconds)                          |
| **invocation_id**        | Server-generated ID used internally for correlation |

##  📍 Location of APIs in C++
All public-facing RPC APIs live in:
[include/livekit/local_participant.h](https://github.com/livekit/client-sdk-cpp/blob/main/include/livekit/local_participant.h#L160)

### Key methods:

#### Sender-side APIs
```bash
std::string performRpc(
    const std::string& destination_identity,
    const std::string& method,
    const std::string& payload,
    std::optional<double> response_timeout_sec = std::nullopt
);

Receiver-side APIs
void registerRpcMethod(
    const std::string& method_name,
    RpcHandler handler
);

void unregisterRpcMethod(const std::string& method_name);

Handler signature
using RpcHandler =
  std::function<std::optional<std::string>(const RpcInvocationData&)>;
```

Handlers can:
- Return a string (the RPC response payload)
- Return std::nullopt (meaning “no return payload”)
- Throw exceptions (mapped to APPLICATION_ERROR)
- Throw a RpcError (mapped to specific error codes)

#### 🛰 Sender Behavior (performRpc)

When the caller invokes:
```bash
auto reply = lp->performRpc("math-genius", "square-root", "{\"number\":16}");
```

The following occurs:

A PerformRpcRequest is sent through FFI to the SDK core.

The SDK transmits the invocation to the target participant (if present).

The caller begins waiting for a matching RpcMethodInvocationResponse.

One of the following happens:
| Outcome                  | Meaning                                  |
|--------------------------|------------------------------------------|
| **Success**              | Receiver returned a payload              |
| **UNSUPPORTED_METHOD**   | Receiver did not register the method     |
| **RECIPIENT_NOT_FOUND**  | Target identity not present in room      |
| **RECIPIENT_DISCONNECTED** | Target left before replying            |
| **RESPONSE_TIMEOUT**     | Receiver took too long                   |
| **APPLICATION_ERROR**    | Handler threw an exception               |

#### 🔄 Round-trip time (RTT)

The caller can measure RTT externally (as SimpleRpc does), but the SDK does not measure RTT internally.

#### 📡 Receiver Behavior (registerRpcMethod)

A receiver must explicitly register handlers:
```bash
local_participant->registerRpcMethod("square-root",
  [](const RpcInvocationData& data) {
      double number = parse(data.payload);
      return make_json("result", std::sqrt(number));
  });
```

When an invocation arrives:
- Room receives a RpcMethodInvocationEvent
- Room forwards it to the corresponding LocalParticipant
- LocalParticipant::handleRpcMethodInvocation():
- Calls the handler
- Converts any exceptions into RpcError
- Sends back RpcMethodInvocationResponse

⚠ If no handler exists:

Receiver returns: UNSUPPORTED_METHOD


#### 🚨 What Happens if Receiver Is Absent?
| Case                                                | Behavior                                          |
|-----------------------------------------------------|---------------------------------------------------|
| Receiver identity is not in the room                | Caller immediately receives `RECIPIENT_NOT_FOUND` |
| Receiver is present but disconnects before replying | Caller receives `RECIPIENT_DISCONNECTED`          |
| Receiver joins later                                | Caller must retry manually (no automatic waiting) |

**Important**:
LiveKit does not queue RPC calls for offline participants.

#### ⏳ Timeout Behavior

If the caller specifies:

performRpc(..., /*response_timeout=*/10.0); 

Then:
- Receiver is given 10 seconds to respond.
- If the receiver handler takes longer (e.g., sleep 30s), caller receives:
RESPONSE_TIMEOUT

**If no response_timeout is provided explicitly, the default timeout is 15 seconds.**


This is by design and demonstrated in the example.

####  🧨 Errors & Failure Modes
| Error Code             | Cause                                      |
|------------------------|---------------------------------------------|
| **APPLICATION_ERROR**  | Handler threw a C++ exception               |
| **UNSUPPORTED_METHOD** | No handler registered for the method        |
| **RECIPIENT_NOT_FOUND** | Destination identity not in room          |
| **RECIPIENT_DISCONNECTED** | Participant left mid-flight            |
| **RESPONSE_TIMEOUT**   | Handler exceeded allowed response time      |
| **CONNECTION_TIMEOUT** | Transport-level issue                       |
| **SEND_FAILED**        | SDK failed to send invocation               |
