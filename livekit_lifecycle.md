# LiveKit room lifecycle — C++ integration sketch

This example shows how an application owns a LiveKit room from connection through media/data exchange and shutdown. Application-specific identifiers, payload schemas, media counts, dimensions, encoding limits, and deployment details are replaced with generic configuration and helpers.

A control layer supplies a room URL and token. The client joins with those credentials; room provisioning and token issuance happen outside this code. One single-thread executor owns room construction, media capture, status publication, and destruction. SDK room-delegate callbacks copy event values and enqueue work onto that executor. Named data-track callbacks run on an SDK reader thread and call a separately synchronized consumer.

This variant uses **room-scoped SDK ownership**: each room wrapper initializes the SDK, then directly calls `livekit::shutdown()` after disconnect cleanup. It assumes exclusive SDK use by one room wrapper at a time because SDK initialization and shutdown affect shared global state.

This is **illustrative C++ pseudocode, not a standalone buildable program**. Includes, member declarations, logging internals, and repetitive exception handling are omitted. `app.*`, application classes, and configuration fields are placeholders. Verify API availability and repeated initialization/shutdown support in the SDK build being discussed. Configuration fields represent generalized application settings.

```cpp
// 1. SDK INITIALIZATION OWNED BY THE ROOM WRAPPER
// RoomSession members include sdk_initialized_{false} and closed_{false}.
// A new wrapper initializes the SDK after the previous wrapper has closed.
void RoomSession::initializeSdk() {
    livekit::initialize(livekit::LogLevel::Info);
    sdk_initialized_ = true;
    app.attach_sdk_logger();
    livekit::setLogCallback(
        [](livekit::LogLevel level, const std::string& name,
           const std::string& message) {
            app.forward_sdk_log_to_attached_logger(level, name, message);
        });
}

// 2. OWNER: WAIT FOR CREDENTIALS, THEN CONSTRUCT A ROOM
// Owner members: one livekit_executor_{1}, one unique_ptr<RoomSession> room_,
// atomic connected/requested flags, and the active URL/token.
void SessionOwner::requestConnect(const ConnectionRequest& command) {
    if (command.livekit_url().empty() || command.livekit_token().empty()) return;
    livekit_connect_requested_ = true;
    livekit_executor_.add([this, url = command.livekit_url(),
                          token = command.livekit_token()] {
        if (room_connected_ && active_livekit_url_ == url &&
            active_livekit_token_ == token) return; // Same active auth: no-op.

        disconnectOnExecutor(); // Replace old room first.
        app.reset_optional_stream_state();
        auto consumer = app.make_shared_synchronized_data_consumer();
        try {
            // The room factory passes these callbacks to the wrapper.
            room_ = app.make_RoomSession(
                url, token, livekit_executor_,
                [consumer](const auto& bytes, auto sender_timestamp_ms) {
                    consumer->consume(bytes, sender_timestamp_ms);
                },
                [consumer] { consumer->resetForNewSession(); },
                [this](bool enabled, const std::string& destination) {
                    // Update media production and enqueue application status.
                    app.update_stream_state_and_enqueue_status(enabled, destination);
                },
                [this](livekit::DisconnectReason reason) {
                    // This callback has already hopped to livekit_executor_.
                    // RoomSession::close() has already shut down the SDK.
                    livekit_connect_requested_ = false;
                    disconnectOnExecutor();
                });
        } catch (...) {
            app.log_connect_failure();
            return; // Constructor rolled back. No application retry loop here.
        }
        data_consumer_ = consumer;
        active_livekit_url_ = url;
        active_livekit_token_ = token;
        room_connected_ = true; // Only after connect + all publications succeed.
    });
}

// 3. ROOM CONSTRUCTOR BODY (runs on livekit_executor_)
// The constructor stores callbacks/config/executor before this body.
// Each wrapper has its own shared_ptr<atomic_bool> alive_ initialized true.
void RoomSession::constructor_body() {
    // Install rollback before initialization. A throwing constructor does not
    // run ~RoomSession(), so its guard explicitly performs the same cleanup.
    auto rollback = app.make_scope_guard([this] { close(); });
    initializeSdk();

    room_ = std::make_unique<livekit::Room>();
    room_->setDelegate(this); // Installed BEFORE connect.
    const livekit::RoomOptions options; // Default options; no local overrides.
    if (!room_->connect(config_.livekit_url(), config_.livekit_token(), options))
        throw std::runtime_error("Room::connect failed");

    // Media count, names, and dimensions are application configuration.
    for (const auto& spec : config_.video_streams) {
        video_streams_[spec.id].source =
            std::make_shared<livekit::VideoSource>(spec.width, spec.height);
    }

    audio_source_ = std::make_shared<livekit::AudioSource>(
        config_.audio_sample_rate, config_.audio_channels, /*queue_size_ms=*/0);
    auto participant = room_->localParticipant().lock();
    if (!participant) throw std::runtime_error("No local participant");
    audio_track_ = participant->publishAudioTrack(
        config_.audio_track_name, audio_source_, livekit::TrackSource::SOURCE_MICROPHONE);
    if (!audio_track_) throw std::runtime_error("Audio publication failed");

    for (const auto& spec : config_.video_streams)
        publishOneTrack(spec);

    // Discover eligible participants already present when this client joined.
    for (const auto& weak : room_->remoteParticipants()) {
        if (auto remote = weak.lock()) {
            subscribeToDataFromParticipant(remote->identity());
            app.enqueue_initial_status_if_eligible(remote->identity());
        }
    }
    rollback.dismiss();
}

void RoomSession::publishOneTrack(const StreamConfig& spec) {
    auto& stream = video_streams_.at(spec.id);
    auto name = spec.track_name;
    auto track = livekit::LocalVideoTrack::createLocalVideoTrack(name, stream.source);
    livekit::TrackPublishOptions options;
    options.source = livekit::TrackSource::SOURCE_CAMERA;
    options.video_codec = app.map_configured_codec_to_livekit();
    options.stream = name;
    options.simulcast = false;
    if (app.configured_codec_is_av1())
        options.degradation_preference = livekit::DegradationPreference::MaintainResolution;
    livekit::VideoEncodingOptions encoding;
    encoding.max_bitrate = spec.max_bitrate;
    encoding.max_framerate = spec.max_framerate;
    options.video_encoding = encoding;
    options.red = false;
    options.dtx = false;
    options.preconnect_buffer = false;
    auto participant = room_->localParticipant().lock();
    if (!participant) throw std::runtime_error("No local participant");
    participant->publishTrack(track, options);
    if (spec.initially_muted) track->mute();
    stream.track = track;
}

// 4. PARTICIPANT DISCOVERY AND NAMED DATA TRACK
void RoomSession::onParticipantConnected(
    livekit::Room&, const livekit::ParticipantConnectedEvent& event) {
    if (!event.participant) return;
    // SDK FFI dispatch thread: copy values, then return without blocking.
    executor_->add([this, alive = alive_, identity = event.participant->identity()] {
        if (!alive->load()) return;
        subscribeToDataFromParticipant(identity);
        app.enqueue_initial_status_if_eligible(identity);
    });
}

void RoomSession::subscribeToDataFromParticipant(const std::string& identity) {
    if (!data_callback_ || !app.accepts_data_sender(identity)) return;
    if (data_callback_id_) return; // Keep the first eligible sender.
    data_session_started_callback_(); // Reset consumer before first delivery.
    data_callback_id_ = room_->addOnDataFrameCallback(
        identity, config_.data_track_name,
        [consume = data_callback_](const std::vector<std::uint8_t>& payload,
                                       std::optional<std::uint64_t> sender_timestamp_ms) {
            // SDK data-reader thread, NOT the home executor.
            // Consumer owns synchronization and application-specific payload handling.
            consume(payload, sender_timestamp_ms);
        });
    subscribed_identity_ = identity;
}

void RoomSession::onParticipantDisconnected(
    livekit::Room&, const livekit::ParticipantDisconnectedEvent& event) {
    if (!event.participant) return;
    executor_->add([this, alive = alive_, identity = event.participant->identity()] {
        if (!alive->load()) return;
        if (data_callback_id_ && identity == subscribed_identity_)
            clearDataSubscriptions(); // A later join may register again.
    });
}

// 5. LEGACY RELIABLE USER PACKETS: CONTROL IN, STATUS OUT
void RoomSession::onUserPacketReceived(
    livekit::Room&, const livekit::UserDataPacketEvent& event) {
    if (event.topic != config_.control_topic || !event.participant) return;
    auto identity = event.participant->identity();
    if (!app.accepts_control_sender(identity) ||
        event.kind != livekit::DataPacketKind::Reliable) return;
    executor_->add([this, alive = alive_, identity, payload = event.data] {
        if (!alive->load()) return;
        auto enabled = app.parse_stream_control(payload);
        if (!enabled.has_value()) return; // Malformed/unspecified: ignore.
        if (*enabled != optional_stream_enabled_) {
            auto track = video_streams_.at(config_.controlled_stream_id).track;
            if (*enabled) track->unmute(); else track->mute();
            optional_stream_enabled_ = *enabled;
        }
        app.update_stream_state_and_enqueue_status(*enabled, identity);
    });
}

void RoomSession::publishStatus(
    const StatusMessage& status, std::optional<std::string> destination_identity) {
    auto destination = destination_identity.value_or(subscribed_identity_);
    if (!room_ || destination.empty()) return;
    auto payload = app.serialize_status(status); // Opaque byte vector.
    auto participant = room_->localParticipant().lock();
    if (!participant) throw std::runtime_error("No local participant");
    participant->publishData(payload, /*reliable=*/true,
                             std::vector<std::string>{destination},
                             config_.status_topic);
}

// 6. STEADY-STATE MEDIA (runs on livekit_executor_)
// Media workers prepare frames, then submit them to the home executor:
//   folly::via(&livekit_executor_, [owned_frame] { room_->pushFrame(...); }).get();
// Audio uses the same submit-and-wait pattern. No room => drop the frame.
// Status is enqueued without waiting, periodically or after a control update.
void RoomSession::pushFrame(StreamId id, VideoFrame frame, int64_t capture_time_us) {
    auto type = app.map_validated_pixel_format(frame.format); // I420 or NV12.
    livekit::VideoFrame sdk_frame(frame.width, frame.height, type, std::move(frame.data));
    video_streams_.at(id).source->captureFrame(sdk_frame, capture_time_us);
    // Convert the application capture timestamp to microseconds before this call.
}

void RoomSession::pushAudio(const AudioChunk& chunk) {
    // Validate format, configured sample rate/channel count, and byte length.
    if (!app.valid_audio_chunk(chunk)) return;
    // Realtime capture (queue_size_ms=0) uses SDK-required frame durations.
    // Split incoming chunks as needed; preserve samples per channel.
    for (auto frame : app.split_into_realtime_audio_frames(chunk)) {
        try {
            audio_source_->captureFrame(
                livekit::AudioFrame(std::move(frame.samples), config_.audio_sample_rate,
                                    config_.audio_channels, frame.samples_per_channel),
                config_.audio_capture_timeout_ms);
        } catch (...) {
            app.log_dropped_audio_chunk();
            return;
        }
    }
}

// 7. FINAL SDK DISCONNECT (transient reconnects are left to the SDK)
void RoomSession::onDisconnected(
    livekit::Room&, const livekit::DisconnectedEvent& event) {
    // Never shut down the SDK from its own FFI dispatch thread.
    // Copy the owner callback; it may destroy this wrapper after close().
    executor_->add([this, alive = alive_, reason = event.reason,
                    notify = room_disconnected_callback_] {
        if (!alive->load()) return;
        close(); // Release room resources, then directly call livekit::shutdown().
        if (notify) notify(reason); // Owner clears its reference and connection state.
        // Do not touch this wrapper after notifying its owner.
    });
}

// An application disconnect request follows the same teardown path.
void SessionOwner::requestDisconnect() {
    livekit_connect_requested_ = false;
    livekit_executor_.add([this] { disconnectOnExecutor(); });
}

void SessionOwner::disconnectOnExecutor() {
    room_connected_ = false;
    active_livekit_url_.clear();
    active_livekit_token_.clear();
    if (data_consumer_) data_consumer_->disable();
    room_.reset(); // Runs the wrapper destructor below on the home executor.
    data_consumer_.reset();
}

// 8. ROOM TEARDOWN ORDER (also used by constructor rollback)
void RoomSession::clearDataSubscriptions() {
    if (room_ && data_callback_id_)
        room_->removeOnDataFrameCallback(*data_callback_id_);
    // Removal joins the data-reader thread; no more consumer calls after return.
    data_callback_id_.reset();
    subscribed_identity_.clear();
}

template <typename LocalTrack>
void unpublishLocalTrack(const std::shared_ptr<LocalTrack>& track,
                         const std::shared_ptr<livekit::LocalParticipant>& participant) {
    if (!track) return;
    auto publication = track->publication();
    if (participant && publication && !publication->sid().empty()) {
        try { participant->unpublishTrack(publication->sid()); }
        catch (...) { app.log_unpublish_failure(); } // Continue releasing resources.
    }
    track->setPublication(nullptr); // Break Track <-> LocalTrackPublication cycle.
}

void RoomSession::close() {
    // Called only on the home executor. Disconnect, rollback, and destruction
    // share this idempotent path, so SDK shutdown happens once per wrapper.
    if (closed_) return;
    closed_ = true;
    app.disable_data_consumer();
    alive_->store(false); // Already-queued callbacks skip this dead wrapper.
    clearDataSubscriptions();
    if (room_) {
        room_->setDelegate(nullptr);
        std::this_thread::sleep_for(config_.delegate_detach_grace_period);
        // Existing TEMPORARY workaround: not a proven callback-dispatch barrier.
    }
    auto participant = app.try_lock_local_participant(room_); // Catch errors.
    unpublishLocalTrack(audio_track_, participant);
    for (auto& [id, stream] : video_streams_)
        unpublishLocalTrack(stream.track, participant);
    participant.reset();
    audio_track_.reset();
    audio_source_.reset();
    video_streams_.clear();
    room_.reset(); // Relies on ~livekit::Room() to disconnect.
    if (sdk_initialized_) {
        livekit::shutdown(); // Directly owned by this room's disconnect cleanup.
        sdk_initialized_ = false;
    }
    app.detach_sdk_logger();
}

RoomSession::~RoomSession() {
    close(); // No-op if the disconnect callback already closed this wrapper.
}

// 9. OWNER CLEANUP
SessionOwner::~SessionOwner() {
    app.stop_and_join_producer_workers(); // Stop new media/control submissions.
    livekit_executor_.add([this] { disconnectOnExecutor(); });
    livekit_executor_.join(); // Drain room cleanup while owner state stays valid.
}
```

Lifecycle details relevant to SDK review:

- **Room-scoped SDK ownership:** each wrapper initializes the SDK and calls `livekit::shutdown()` from `close()` after releasing the room and all SDK-backed media objects. Explicit disconnect, final SDK disconnect, constructor rollback, and destruction share this cleanup path. Only one wrapper may own the SDK at a time.
- **Callback lifetimes:** delegate callbacks copy identities and payloads before hopping to the home executor. The shared `alive_` flag invalidates queued work after teardown. Data callback removal joins the reader before the consumer is released.
- **Delegate detachment limitation:** a short grace delay follows `setDelegate(nullptr)` because an SDK dispatch may already hold the raw delegate pointer. This existing workaround is probabilistic; it is not a callback-drain guarantee. The `alive_` flag alone does not protect an in-flight raw-pointer dispatch.
- **Two data receive paths:** named data-track frames use `addOnDataFrameCallback`. Reliable user packets use `onUserPacketReceived`; responses use `publishData`. Application policy chooses eligible senders, track names, topics, and payload formats.
- **Disconnect behavior:** a participant leaving clears its data subscription without ending the room. A final SDK room disconnect schedules room cleanup and SDK shutdown on the home executor, then notifies the owner. Explicit disconnect destroys the wrapper and performs the same cleanup. Transient reconnects are left to the SDK. A failed initial construction also cleans up any initialized SDK state.
- **Teardown order:** disable the consumer, invalidate queued work, remove data callbacks, detach the delegate, unpublish tracks, break publication ownership cycles, release tracks/sources, destroy the room, call `livekit::shutdown()`, then detach the logger. Constructor failure follows the same resource-cleanup order for resources created so far. Repeated cleanup calls are no-ops.

This sketch models room-scoped initialization and shutdown with generalized application details. It has not been compiled or exercised against a live room.
