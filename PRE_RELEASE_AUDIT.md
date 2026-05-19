# Pre-Release Low-Hanging Fruit Audit

> **Context:** Audit performed on `feature/unified_method_style_tmp_participant`
> branch in preparation for a major release. Breaking changes are acceptable.
> Constraint: **do not remove deprecated code in this PR** — that is scheduled
> for a separate follow-up.
>
> Date generated: Friday, May 15, 2026

---

## Category A — Quick TODO resolutions (≤ a few lines each)

### A1. `src/ffi_client.cpp:257` — silent integer truncation in FFI callback

```cpp
event.ParseFromArray(buf,
                     static_cast<int>(len)); // TODO: this fixes for now, what if len exceeds int?
```

`size_t → int` cast can wrap for buffers > 2 GB. Cheap fix: bounds check +
`LK_LOG_ERROR` + early return. ~3 lines.

### A2. `src/audio_stream.cpp:145, 174` — dead-code TODOs

```cpp
// TODO, sample_rate and num_channels are not useful in AudioStream, remove it from FFI.
//  new_audio_stream->set_sample_rate(options_.sample_rate);
//  new_audio_stream->set_num_channels(options.num_channels);
```

Two duplicate TODOs with commented-out FFI fields. The Rust side ignores them.
Either delete both comment blocks (resolution: "won't do") or actually drop the
fields from the proto. Cheapest path is just remove the dead `//` lines —
they've been dead long enough that we know we don't need them.

### A3. `src/room_proto_converter.cpp:103` — `toDisconnectReason` is a stub

```cpp
DisconnectReason toDisconnectReason(proto::DisconnectReason /*in*/) {
  // TODO: map each proto::DisconnectReason to your DisconnectReason enum
  return DisconnectReason::Unknown;
}
```

This is **functionally broken** — every `DisconnectReason` event delivered to
consumers comes through as `Unknown`. Real fix is a switch over the proto enum
(~20 lines, mechanical). High-impact, low-effort. **Strong candidate for
pre-release fix.**

### A4. `src/video_stream.cpp:151` — stale TODO

```cpp
// TODO, do we need to cache the metadata from stream.info ?
```

`AudioStream` doesn't cache it either, and no caller asks for it. Resolve by
deleting the comment.

### A5. `src/local_participant.cpp:91` — `publishDtmf` ignores destinations

```cpp
// TODO, should we take destination as inputs?
const std::vector<std::string> destination_identities;
```

This is a public-API design decision. For a major release where breaks are OK,
adding an optional `destinations` parameter to `publishDtmf` is the right time.
~5 lines + 1 doc-comment update.

### A6. `src/room.cpp:1087` — `kConnectionStateChanged` doesn't update `connection_state_`

```cpp
// TODO, maybe we should update our |connection_state_| correspoindingly,
// but the this kConnectionStateChanged event is never triggered in my local test.
```

This is a real correctness gap (typo "correspoindingly" too). Either: (a) write
the assignment under the lock (already held), or (b) verify the event is
actually unused and delete the case. Both are tiny.

### A7. `src/room_proto_converter.cpp:287, 295` — unmapped reader handles

```cpp
ByteStreamOpenedEvent fromProto(const proto::ByteStreamOpened& in) {
  // TODO: map reader handle once OwnedByteStreamReader is known
```

The `reader_handle` field is being silently dropped from
`ByteStreamOpenedEvent` / `TextStreamOpenedEvent`. Worth flagging for product
owner: is the reader handle exposed through the C++ API? If not, drop the
comments and the unused field on the event struct. If it is, wire it up.

### A8. `src/tests/integration/test_data_track.cpp:490` — flaky test

```cpp
// TODO(BOT-347): this sometimes fails with a timeout.
```

Has a tracking ticket. Not low-hanging in the code sense, but worth keeping on
the radar for the release sign-off.

### A9. `bridge/README.md:78` — TODO from another author

```
TODO(sderosa): add instructions on how to use the bridge in your own CMake project.
```

Bridge is being deleted on 06/01/2026 anyway (see B1). Safe to drop the TODO.

### A10. `.github/workflows/make-release.yml:247` — Windows fix-up

```
# TODO(sxian): fix it after getting access to a windows machine
```

Probably blocked on hardware. Leave but worth confirming if it's been resolved.

---

## Category B — Approaching deprecation deadlines

### B1. Two deprecation deadlines fall on `06/01/2026` (~16 days from audit date)

`README.md:624-625` and supporting deprecation messages in
`include/livekit/room.h` and `include/livekit/subscription_thread_dispatcher.h`:

- `bridge/` (`livekit_bridge`) folder removal
- `setOnAudioFrameCallback(TrackSource)` / `setOnVideoFrameCallback(TrackSource)`
  overloads removal

Action: **either** push the date out to align with the major release window,
**or** keep the date and queue a follow-up PR to actually remove them on
06/01/2026. Right now the dates will silently slip with no enforcement. Just
flag — don't change without a product call.

### B2. Internal callers of soon-to-be-removed callback overloads

`src/tests/unit/test_room_callbacks.cpp` and
`src/tests/unit/test_subscription_thread_dispatcher.cpp` call
`setOnAudioFrameCallback(participant, TrackSource, …)` ~30+ times. When the
overload is removed, those tests break. They need migration to the track-name
overload before B1's deadline. **Not** low-hanging — substantial test refactor
— but worth scheduling now so it isn't a blocker on deletion day.

---

## Category C — Stale doc comments / minor hygiene

### C1. `include/livekit/participant.h:63-99` — references to no-longer-existent siblings

Each deprecated `set_*` carries the comment
`// Deprecated - see setName() (also deprecated; see notes above).` But
`setName()` etc. on `Participant` were removed in this branch (the WIP that's
currently uncommitted), and there are no "notes above" anymore (the long
comment block was deleted too). Now reads as a dangling cross-reference.
~7 single-line comments to update or remove. Trivial.

### C2. `include/livekit/track.h:91-94` — TODO-style code comment masquerading as documentation

```cpp
// std::string can actually throw, suppressing for now to maintain API
// compatibility
// NOLINTNEXTLINE(bugprone-exception-escape)
std::optional<std::string> mimeType() const noexcept { return mime_type_; }
```

This is the kind of `noexcept` lie that shows up as undefined behavior in the
wild. For a major release where breaks are OK, the right move is to drop
`noexcept` from `mimeType()` (and `mime_type()`). It's a one-token API change;
only effect on consumers is they can no longer use it in `noexcept`-required
contexts (rare). Worth raising for the breaking-change list.

### C3. `src/ffi_client.h:23` — unused `<iostream>`

```cpp
#include <iostream>
```

No `std::cout` / `std::cerr` in the file. Drop the include. Tiny.

### C4. Per-AGENTS.md include conventions — clean

Public headers all use `"livekit/foo.h"` (good); internal headers use bare
names (good). No issues found.

---

## Category D — NOLINT suppressions worth a second look

Most suppressions are intentional and well-scoped. A handful that could be
tightened:

### D1. `src/ffi_client.cpp:151` — `// NOLINTNEXTLINE(modernize-use-equals-default)`

Worth checking if the destructor body still needs to be non-trivial. If it's
just suppressing `~FfiClient() {}` vs `~FfiClient() = default;`, the lint is
right and the fix is `= default`.

### D2. `src/data_track_stream.cpp:63` — `// NOLINT(bugprone-unchecked-optional-access)`

Suppression is correct (the `cv_.wait` predicate plus the `if` above prove
`frame_.has_value()`), but a one-line `assert(frame_.has_value());` or
`[[assume]]` makes the invariant self-documenting. Optional polish.

### D3. NOLINT span in `src/subscription_thread_dispatcher.cpp:511-537, 574-602`

`bugprone-lambda-function-name` and `bugprone-exception-escape` suppressed
across ~30-line lambdas. These are real (the lambdas catch-all and log via
`__func__`). Worth a brief comment near the `NOLINTBEGIN` explaining *why*,
not just *what*. Tiny but improves auditability.

### D4. `src/trace/trace_event.h` and `src/trace/event_tracer.h` — file-wide NOLINT

Both wrap the entire file in `// NOLINTBEGIN ... // NOLINTEND` (no specific
check listed). This globally disables clang-tidy. If these were ported in from
Chromium and we're not the upstream, that's defensible — note it inline.
Otherwise scope to specific checks.

The remaining `readability-identifier-naming` suppressions are all on the
deprecated snake_case wrappers — those have to stay as long as the deprecated
symbols stay.

---

## Category E — Things to flag, not fix in this pass

These need product/architecture sign-off before changing:

### E1. Public-API integers using bare `int`

`AudioFrame`, `AudioSource`, `VideoSource` ctors use bare `int` for
`sample_rate`, `num_channels`, `samples_per_channel`. AGENTS.md prefers
fixed-width but explicitly says don't change existing public APIs without a
compat review. Flag for the breaking-changes list.

### E2. `Participant::Participant(...)` ctor takes 8 positional args

`include/livekit/participant.h:34-43`. Easy to misorder. A builder/options
struct would be more durable. Major-release scope.

### E3. `local_participant.h:117` Python-idiom doc comment

References `LocalParticipant.set_name` as the Python idiom — accurate but might
confuse C++ readers since our API is `setName`. Worth a brief parenthetical.

### E4. `bridge/README.md:5` removal date

Warns the folder is being removed on 06/01. Sync with B1 — same date.

---

## Recommended quick-win batch (a single small PR)

If you want a single low-risk PR that just clears the easy stuff before the
release branch cuts:

- **A1** — FFI buffer length bounds-check
- **A3** — `toDisconnectReason` real mapping
- **A4, A9** — delete two stale TODO comments
- **C1** — clean up dangling `// see setName()` references in `participant.h`
- **C3** — drop unused `<iostream>`
- **D1** — `= default` the `FfiClient` dtor if applicable

That gets visibly cleaner code, fixes one real correctness bug (A3), and
leaves all the deprecated API surface untouched per the constraint.
