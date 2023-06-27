#include "../include/livekit/livekit.h"

using namespace livekit;

int main(int argc, char *argv[])
{
    Room room{};
    room.Connect("ws://localhost:7880", "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjE2ODc1NTY5NTgsImlzcyI6ImRldmtleSIsIm5hbWUiOiJ1c2VyMSIsIm5iZiI6MTY4NzQ3MDU1OCwic3ViIjoidXNlcjEiLCJ2aWRlbyI6eyJyb29tIjoibXktZmlyc3Qtcm9vbSIsInJvb21Kb2luIjp0cnVlfX0.1K7pvuq1Lr-Ph_DLOUun60Zbm5A1Fm9eKl754GMtDHE");

    ParticipantInfo participantInfo;
    participantInfo.set_identity("userId1");
    participantInfo.set_sid("id1");
    participantInfo.set_metadata("my-first-room");
    participantInfo.set_name("user1");
    LocalParticipant participant(participantInfo, room);
    VideoFrameBuffer videoFrameBuffer = VideoFrameBuffer::Create();
    VideoFrame videoFrame;
    VideoSource videoSource;
    std::unique_ptr<LocalVideoTrack> videoTrack = LocalVideoTrack::CreateVideoTrack("video1", "camera");
    participant.PublishTrack();

    // Should we implement a mechanism to PollEvents/WaitEvents? Like SDL2/glfw
    //   - So we can remove the useless loop here
    // Or is it better to use callback based events?

    while(true) {

    }

    return 0;
}
