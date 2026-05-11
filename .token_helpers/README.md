# Overview
Examples of generating tokens

## gen_and_set.bash
Generate tokens and then set them as env vars for the current terminal session

## set_data_track_test_tokens.bash
Generate the two participant tokens required by the C++ SDK's integration
and stress test suites (data tracks, RPC, media multistream, etc.) and
export them as `LIVEKIT_TOKEN_A`, `LIVEKIT_TOKEN_B`, and `LIVEKIT_URL` for
the current terminal session.