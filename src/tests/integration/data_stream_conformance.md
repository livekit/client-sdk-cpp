# Data stream conformance tests

The `test_data_stream_conformance_*.cpp` integration tests port the unit tests
of the Rust `livekit-data-stream` crate
(`client-sdk-rust/livekit-data-stream/src`) to end-to-end C++ tests against a
real SFU. They check that the C++ SDK sends and receives data streams the way
every other LiveKit SDK does, including data streams v2 (inline single-packet
streams and deflate-compressed streams).

| File | What it covers | Needs API added with the FFI port |
|------|----------------|-----------------------------------|
| `test_data_stream_conformance_receive_v1.cpp` | Receiving v1 streams: hand-built packets, remote writers, disconnects | No |
| `test_data_stream_conformance_receive_v2.cpp` | Receiving v2 streams: inline, compressed, attachments | No |
| `test_data_stream_conformance_receive_api.cpp` | Receive payload cap (`RoomOptions::data_stream`), `reader->close()` | Yes |
| `test_data_stream_conformance_send.cpp` | Sending through `sendText` / `sendBytes` / `sendFile` / `streamText` / `streamBytes` | Yes |
| `test_data_stream_conformance_text_header.cpp` | Text operation type / version / generated, both directions | Yes |
| `test_data_stream_conformance_write_to_file.cpp` | `ByteStreamReader::writeToFile` | Yes |

Shared helpers live in `src/tests/common/data_stream_test_utils.h`:

- `FfiPeer` drives a room's participant through the raw FFI, so it sends
  exactly what another FFI-based SDK sends. Its one-shot sends and writers go
  through the Rust outgoing manager, and its raw header / chunk / trailer
  requests send hand-built v1 packets.
- `RawPacketSniffer` records the raw stream packets a room receives. The FFI's
  raw header event carries neither `inline_content` nor `compression`, so an
  inline stream shows up as a header with no chunk or trailer packets, and
  compression shows up as chunk bytes that differ from, and are smaller than,
  the payload.
- `TextStreamCollector` / `ByteStreamCollector` receive through the public C++
  API.

The receive-side v1 and v2 files use only API that predates the FFI port, so
they also build against the old C++ data stream implementation. There, 22 of
their 27 tests fail.

Tests with a third participant (`SenderDisconnectOnlyAffectsItsOwnStreams`,
`TargetedStreamsReachOnlyTheirDestinations`) need `LIVEKIT_TOKEN_C`, which
`scripts/set-test-tokens.sh` mints, and skip without it.

## What cannot be tested from the C++ layer

- **Progress reporting.** The FFI exposes no send or receive progress.
- **Recipients without v2 support.** Every FFI client advertises data streams
  v2 with deflate-raw compression, so a test can't create a pre-v2 recipient or
  one without compression. The Rust pre-v2 / mixed-room routing tests therefore
  have no end-to-end equivalent.
- **Malformed v2 packets.** The FFI can send hand-built v1 packets only; v2
  packets come from the Rust outgoing manager, which never produces gaps,
  unknown compression types, inline/length mismatches or compressed duplicates.
- **Per-packet encryption mismatches.** A sender can't change encryption
  mid-stream.
- **Manager internals.** Open-stream counts, `StreamClosed` events, transport
  batching and the internal-topic filtering of raw events aren't exposed to the
  C++ SDK.
- **The outgoing `sender_identity` stamp.** The SFU attributes packets to the
  authenticated participant unless its token allows impersonation, which the
  test tokens don't.

The C++ SDK delegates all of these to the Rust crate, whose own unit tests
cover them.

## Rust test → C++ test

"Partial" means the C++ test checks what is observable end to end, and the
note says what is not.

### `outgoing/manager.rs` (60)

| Rust test | C++ test (`DataStreamSendConformanceTest.*` unless noted) |
|-----------|----------------------------------------------------------|
| one_shot_progress_follows_acknowledgement | N/A: no progress API |
| pre_v2_short_text_is_legacy_multipacket | N/A: no pre-v2 recipient |
| pre_v2_long_text_splits_at_mtu | N/A: no pre-v2 recipient (MTU split: SendTextCompressFalseLargeIsUncompressedMultipacket) |
| pre_v2_bytes_is_legacy_multipacket | N/A: no pre-v2 recipient |
| pre_v2_empty_text_sends_header_and_trailer | N/A: no pre-v2 recipient |
| v2_short_compressible_text_inlines_compressed | SendTextShortCompressibleInlines (partial: compression flag not visible) |
| v2_short_incompressible_text_inlines_raw | SendTextShortIncompressibleInlines |
| v2_no_compression_cap_inlines_raw | N/A: no recipient without compression |
| v2_large_highly_compressible_text_still_inlines | SendTextLargeHighlyCompressibleStillInlines |
| v2_somewhat_compressible_text_is_compressed_multipacket | SendTextSomewhatCompressibleIsCompressedMultipacket |
| v2_compress_false_short_inlines_raw | SendTextCompressFalseShortInlines (partial: compression flag not visible) |
| v2_compress_false_large_is_uncompressed_multipacket | SendTextCompressFalseLargeIsUncompressedMultipacket |
| v2_send_text_with_attachments_never_inlines | SendTextWithAttachmentsNeverInlines |
| v2_large_text_to_uncapable_recipient_is_uncompressed_multipacket | N/A: no recipient without compression |
| v2_empty_text_sends_single_inline_packet | SendTextEmptyIsSingleInlinePacket |
| v2_send_bytes_short_incompressible_inlines_raw | SendBytesShortIncompressibleInlines |
| v2_send_bytes_no_compression_cap_inlines_raw | N/A: no recipient without compression |
| v2_send_bytes_large_highly_compressible_inlines | SendBytesLargeHighlyCompressibleInlines |
| v2_send_bytes_somewhat_compressible_is_compressed_multipacket | SendBytesSomewhatCompressibleIsCompressedMultipacket |
| v2_send_bytes_compress_false_large_is_uncompressed_multipacket | SendBytesCompressFalseLargeIsUncompressedMultipacket |
| v2_send_bytes_short_compressible_inlines_compressed | SendBytesShortCompressibleInlinesAndReturnsInfo |
| send_file_never_inlines_and_compresses_when_eligible | SendFileNeverInlinesAndCompresses |
| send_file_uncompressed_splits_at_mtu | SendFileUncompressedSplitsAtMtu |
| send_file_incompressible_compressed_expands | SendFileIncompressibleCompressedExpands |
| send_file_to_no_compression_recipient_is_uncompressed | N/A: no recipient without compression |
| send_file_reports_progress_after_each_chunk | N/A: no progress API |
| compressed_send_file_reports_rising_progress_and_the_total_last | N/A: no progress API |
| compressed_send_file_without_its_last_chunk_never_reports_the_total | N/A: no progress API or transport fault injection |
| send_file_reports_progress_for_empty_file | SendFileEmptyFile (partial: no progress API) |
| send_file_empty_file | SendFileEmptyFile |
| v2_broadcast_all_capable_inlines_compressed | SendTextShortCompressibleInlines (broadcast; partial: compression flag not visible) |
| v2_broadcast_with_uncapable_member_inlines_raw | N/A: no recipient without compression |
| empty_room_broadcast_is_v2_eligible | BroadcastToEmptyRoomSucceeds (partial: with nobody listening, no packet can be inspected) |
| mixed_broadcast_falls_back_to_legacy | N/A: no pre-v2 recipient |
| mixed_targeted_v2_subset_inlines_compressed | TargetedStreamsReachOnlyTheirDestinations (partial) |
| mixed_targeted_subset_missing_cap_inlines_uncompressed | N/A: no recipient without compression |
| stream_text_with_sender_identity_stamps_every_packet | SenderIdentityOptionIsAcceptedAndDelivers (partial: SFU attribution) |
| send_text_inline_with_sender_identity_stamps_packet | SenderIdentityOptionIsAcceptedAndDelivers (partial: SFU attribution) |
| packets_carry_no_identity_when_sender_identity_unset | PacketsAreAttributedToTheSender |
| one_shot_send_text_targets_every_packet | TargetedStreamsReachOnlyTheirDestinations |
| stream_text_targets_every_packet | TargetedStreamsReachOnlyTheirDestinations |
| send_file_targets_every_packet | TargetedStreamsReachOnlyTheirDestinations |
| compressed_send_file_progress_targets_every_packet | TargetedStreamsReachOnlyTheirDestinations (partial: no progress API) |
| stream_text_never_compresses_or_inlines | StreamTextSendsEachPacketAsItIsWritten |
| stream_text_declares_total_length | N/A: text stream options carry no total length over the FFI (byte streams: StreamBytesNeverCompressesOrInlines) |
| stream_bytes_never_compresses_or_inlines | StreamBytesNeverCompressesOrInlines |
| oversized_attributes_on_chunked_path_errors | SendTextOversizedAttributesThrows |
| enforce_header_size_accepts_small_header | Implied by every send test (internal function) |
| enforce_header_size_rejects_large_header | SendTextOversizedAttributesThrows, StreamTextOversizedAttributesThrows |
| drop_raw_stream_on_non_tokio_thread_does_not_panic | DestroyedWriterSendsTrailer |
| one_shot_send_text_is_a_single_transport_request | N/A: transport batching is internal |
| one_shot_send_bytes_is_a_single_transport_request | N/A: transport batching is internal |
| one_shot_send_with_sender_identity_stamps_every_packet | SenderIdentityOptionIsAcceptedAndDelivers (partial: SFU attribution) |
| incremental_writer_sends_per_write | StreamTextSendsEachPacketAsItIsWritten |
| send_file_streams_one_packet_per_request | SendFileUncompressedSplitsAtMtu (partial: batching is internal) |
| stream_bytes_multi_write_splits_at_mtu | StreamBytesMultiWriteSplitsAtMtu |
| close_with_options_sends_trailer_attributes | CloseWithAttributesSendsTrailerAttributes |
| close_with_options_sends_reason_and_attributes | CloseWithReasonSendsReasonAndAttributes |
| stream_text_oversized_attributes_errors | StreamTextOversizedAttributesThrows |
| stream_text_splits_on_utf8_boundaries_at_mtu | StreamTextSplitsOnUtf8BoundariesAtMtu |

### `incoming/manager.rs` (54)

| Rust test | C++ test |
|-----------|----------|
| v1_text_stream_round_trips | DataStreamReceiveV1Test.V1TextStreamRoundTrips |
| v1_byte_stream_round_trips | DataStreamReceiveV1Test.V1ByteStreamRoundTrips |
| v1_merges_trailer_attributes | DataStreamReceiveV1Test.V1TrailerAttributesMergedAfterClose, .RemoteTextWriterRoundTripsWithTrailerAttributes |
| v1_errors_when_too_few_bytes | DataStreamReceiveV1Test.V1ErrorsWhenTooFewBytes, .RemoteWriterTooFewBytesErrors |
| v1_errors_when_too_many_bytes | DataStreamReceiveV1Test.V1ErrorsWhenTooManyBytes, .RemoteWriterTooManyBytesErrors |
| v1_max_payload_size_breached_with_unknown_total | DataStreamReceiveApiCapTest.BreachedWithUnknownTotal |
| v1_max_payload_size_fast_fails_on_declared_total | DataStreamReceiveApiCapTest.FastFailsOnDeclaredTotal |
| v1_payload_exactly_at_max_payload_size_succeeds | DataStreamReceiveApiCapTest.PayloadExactlyAtCapSucceeds |
| v1_drops_on_encryption_type_mismatch | N/A: per-packet encryption mismatch |
| v1_drops_on_trailer_encryption_type_mismatch | N/A: per-packet encryption mismatch |
| v1_trailer_attributes_merged_after_close | DataStreamReceiveV1Test.V1TrailerAttributesMergedAfterClose |
| v2_inline_uncompressed_text | DataStreamReceiveV2Test.V2InlineUncompressedText |
| v2_inline_uncompressed_byte | DataStreamReceiveV2Test.V2InlineUncompressedByte |
| v2_inline_compressed_text | DataStreamReceiveV2Test.V2InlineCompressedText |
| v2_inline_compressed_byte | DataStreamReceiveV2Test.V2InlineCompressedByte |
| v2_inline_compressed_max_payload_size_breached | DataStreamReceiveApiCapTest.InlineCompressedBreached |
| v2_inline_uncompressed_max_payload_size_breached | DataStreamReceiveApiCapTest.InlineUncompressedBreached |
| v2_inline_payload_is_held_to_its_declared_length | N/A: malformed v2 packet (consistent lengths are covered by the inline tests) |
| v2_inline_zero_length_text | DataStreamReceiveV2Test.V2InlineZeroLengthText |
| v2_multipacket_compressed_text | DataStreamReceiveV2Test.V2MultipacketCompressedText |
| errors_open_streams_on_sender_disconnect | DataStreamReceiveV1Test.SenderDisconnectErrorsOpenStreams |
| abort_only_affects_matching_sender | DataStreamReceiveV1Test.SenderDisconnectOnlyAffectsItsOwnStreams |
| v2_compressed_gap_errors | N/A: malformed v2 packet (v1 gap: V1MissedChunkErrors) |
| v2_max_payload_size_breached | DataStreamReceiveApiCapTest.CompressedMultipacketBreached |
| v2_multipacket_compressed_byte_stream | DataStreamReceiveV2Test.V2MultipacketCompressedByteStream |
| v2_compressed_errors_when_too_few_bytes | N/A: malformed v2 packet (v1: V1ErrorsWhenTooFewBytes) |
| v2_compressed_errors_when_too_many_bytes | N/A: malformed v2 packet (v1: V1ErrorsWhenTooManyBytes) |
| v1_duplicate_chunk_dropped | DataStreamReceiveV1Test.V1DuplicateChunkDropped |
| v2_compressed_duplicate_chunk_dropped | N/A: malformed v2 packet |
| v2_compressed_text_reframes_multibyte_utf8 | DataStreamReceiveV2Test.V2CompressedTextReframesMultibyteUtf8 (high-entropy multibyte text instead of a hand-split stream) |
| v2_unknown_compression_type_is_ignored | N/A: malformed v2 packet |
| v2_compressed_merges_trailer_attributes | N/A: no sender puts trailer attributes on a compressed stream (v1: V1TrailerAttributesMergedAfterClose) |
| progress_reports_completion_uncompressed_bytes | N/A: no progress API |
| progress_reports_completion_compressed_text | N/A: no progress API (content: V2MultipacketCompressedText) |
| progress_reports_completion_inline | N/A: no progress API |
| progress_stream_outlives_a_consuming_read | N/A: no progress API |
| closer_after_shutdown_does_nothing | DataStreamReceiveApiCloseTest.CloseAfterDisconnectIsHarmless |
| closer_ends_an_open_stream_once | DataStreamReceiveApiCloseTest.CloseEndsAnOpenStreamOnce |
| closer_discards_packets_still_queued | DataStreamReceiveApiCloseTest.CloseDiscardsQueuedChunks |
| closer_spares_a_later_stream_with_the_same_id | DataStreamReceiveApiCloseTest.CloseSparesALaterStreamWithTheSameId |
| counts_streams_across_their_lifecycle | N/A: open-stream count is internal |
| inline_streams_are_never_counted | N/A: open-stream count is internal (inline delivery: V2InlineUncompressedText) |
| shutdown_errors_open_streams | DataStreamReceiveV1Test.ReceiverDisconnectErrorsOpenStreams |
| dropping_all_inputs_errors_open_streams | DataStreamReceiveV1Test.ReceiverDisconnectErrorsOpenStreams (closest C++ equivalent) |
| trailer_close_emits_stream_closed | N/A: StreamClosed is internal (the reader's end is checked everywhere) |
| inline_stream_emits_stream_closed | N/A: StreamClosed is internal (V2InlineUncompressedText) |
| error_close_emits_stream_closed | DataStreamReceiveV1Test.V1MissedChunkErrors |
| abort_emits_stream_closed | DataStreamReceiveV1Test.SenderDisconnectErrorsOpenStreams |
| trailer_for_unopened_stream_emits_no_stream_closed | DataStreamReceiveV1Test.OrphanChunkAndTrailerAreIgnored |
| empty_chunks_are_ignored | DataStreamReceiveV1Test.EmptyChunksAreIgnored |
| trailer_with_reason_errors_abnormal_end | DataStreamReceiveV1Test.V1TrailerWithReasonErrorsAbnormalEnd, .RemoteWriterCloseWithReasonErrorsAbnormalEnd |
| text_stream_with_attachments_round_trips | DataStreamReceiveV2Test.TextStreamWithAttachmentsRoundTrips |
| chunk_and_trailer_report_the_topic_of_their_stream | N/A: internal-topic filtering of raw events is host policy the C++ SDK doesn't consume |
| chunk_for_an_unopened_stream_reports_no_topic | DataStreamReceiveV1Test.OrphanChunkAndTrailerAreIgnored |

### `incoming/stream_reader.rs` (6)

| Rust test | C++ test (`DataStreamWriteToFileTest.*`) |
|-----------|------------------------------------------|
| write_to_file_accepts_plain_name | AcceptsPlainName |
| write_to_file_rejects_traversal_in_stream_name | RejectsTraversalInStreamName |
| write_to_file_rejects_traversal_in_name_override | RejectsTraversalInNameOverride |
| write_to_file_rejects_absolute_path_in_stream_name | RejectsAbsolutePathInStreamName |
| write_to_file_rejects_nested_path_in_stream_name | RejectsNestedPathInStreamName |
| write_to_file_rejects_empty_stream_name | RejectsEmptyStreamName |

### `utf8_chunk.rs` (4)

`test_string_chunking`, `test_empty_string_chunking`,
`test_single_character_string_chunking` and `test_mixed_string_chunking` unit
test an internal chunking helper. The C++ SDK never chunks text itself; the
behavior they protect is checked end to end by
`DataStreamSendConformanceTest.StreamTextSplitsOnUtf8BoundariesAtMtu` and
`DataStreamReceiveV2Test.V2CompressedTextReframesMultibyteUtf8`.
