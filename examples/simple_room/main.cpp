#include "livekit/livekit.h"

using namespace livekit;

int main(int argc, char *argv[])
{
    Room room{};
    room.Connect("ws://localhost:7880", "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjQ5MjM1OTQ4MjMsImlzcyI6IkFQSVRzRWZpZFpqclFvWSIsIm5hbWUiOiJ3ZWIiLCJuYmYiOjE2ODM1OTQ4MjMsInN1YiI6IndlYiIsInZpZGVvIjp7InJvb20iOiJsaXZla2l0LWZmaS10ZXN0Iiwicm9vbUpvaW4iOnRydWV9fQ.voLT9RK3wNYEGdWovPLv1BzyN1v5tpJ59e0DIqIVfiU");


    // Should we implement a mechanism to PollEvents/WaitEvents? Like SDL2/glfw
    //   - So we can remove the useless loop here
    // Or is it better to use callback based events?

    while(true) {

    }

    return 0;
}
