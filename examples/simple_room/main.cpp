#include "../include/livekit/livekit.h"

#include <cstdio>
#include <vector>
#include <thread>
#include <cmath>
#include <future>

using namespace livekit;

const std::string URL = "ws://localhost:7880";
const std::string TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjE2ODg3NDYzNTgsImlzcyI6ImRldmtleSIsIm5hbWUiOiJoZW5nc3RhciIsIm5iZiI6MTY4ODY1OTk1OCwic3ViIjoiaGVuZ3N0YXIiLCJ2aWRlbyI6eyJyb29tIjoibXktZmlyc3Qtcm9vbSIsInJvb21Kb2luIjp0cnVlfX0.BKKLppcGWeaDD-PFP83mGVtnT8vgx0bZneluuZbfjkc";

std::vector<int> hsv_to_rgb(float H, float S,float V) {
    if(H>360 || H<0 || S>100 || S<0 || V>100 || V<0){
        std::cout<< "The givem HSV values are not in valid range" << std::endl;
        return {};
    }
    float s = S/100;
    float v = V/100;
    float C = s*v;
    float X = C*(1 - abs(std::fmod(H/60.0, 2)-1));
    float m = v-C;
    float r,g,b;
    if(H >= 0 && H < 60){
        r = C,g = X,b = 0;
    }
    else if(H >= 60 && H < 120){
        r = X,g = C,b = 0;
    }
    else if(H >= 120 && H < 180){
        r = 0,g = C,b = X;
    }
    else if(H >= 180 && H < 240){
        r = 0,g = X,b = C;
    }
    else if(H >= 240 && H < 300){
        r = X,g = 0,b = C;
    }
    else{
        r = C,g = 0,b = X;
    }
    int R = (r+m)*255;
    int G = (g+m)*255;
    int B = (b+m)*255;
    // cout<<"R : "<<R<<endl;
    // cout<<"G : "<<G<<endl;
    // cout<<"B : "<<B<<endl;
    return {R, G, B};
}

void publish_frames(const VideoSource& source) {
    std::cout << "publish_frames" << std::endl;
    ArgbFrame argb_frame(FORMAT_ARGB, 1280, 720);
    std::vector<uint8_t> arr = argb_frame.data;
    double framerate = 1.0 / 30;
    double hue = 0.0;
    while (true) {
        VideoFrame frame(0, VIDEO_ROTATION_0, argb_frame.ToI420());
        std::vector<int> rgb = std::move(hsv_to_rgb(hue, 1.0, 1.0));
        std::vector<int> argb_color;
        argb_color.push_back(255);
        for (int i = 0; i < arr.size(); i += 4) {
            arr[i] = argb_color[0];
            arr[i + 1] = argb_color[1];
            arr[i + 2] = argb_color[2];
            arr[i + 3] = argb_color[3];
        }
        source.CaptureFrame(frame);
        hue += framerate / 3;
        if (hue >= 1.0) {
            hue = 0.0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(int(framerate * 1000)));
    }
}


int main(int argc, char *argv[])
{
    std::shared_ptr<Room> room(new Room());
    room->Connect(URL, TOKEN);

    // ParticipantInfo participantInfo;
    // participantInfo.set_identity("hengstar");
    // participantInfo.set_sid("id1");
    // participantInfo.set_metadata("my-first-room");
    // participantInfo.set_name("user2");
    // LocalParticipant participant(participantInfo, room);

    livekit::ArgbFrame argbFrame(FORMAT_ARGB, 1280, 720);

    // printf("Successfully read frame of size %d\n", frameSize);

    VideoSource videoSource;

    // livekit::ARGBBufferInfo videoFrameBufferInfo;
    // videoFrameBufferInfo.set_width(640);
    // videoFrameBufferInfo.set_height(480);
    // videoFrameBufferInfo.set_stride(640);
    // FFIRequest request;
    // livekit::ToI420Request* toI420Request = request.mutable_to_i420();
    // toI420Request->mutable_argb->set_ptr()
    // livekit::FFIResponse response = FfiClient::getInstance().SendRequest(request);
    // response.to_i420();
    // VideoFrameBuffer videoFrameBuffer = VideoFrameBuffer::Create(std::move(videoFrameHandle), std::move(videoFrameBufferInfo));
    
    //VideoFrame videoFrame(0, VIDEO_ROTATION_0, argbFrame.ToI420());
    //videoSource.CaptureFrame(videoFrame);
    // publish_frames(videoSource);
    auto sourceTask = std::async(std::launch::async, publish_frames, videoSource);
    // track = livekit::LocalVideoTrack::create_video_track("hue", source);
    // options.source = livekit::TrackSource::SOURCE_CAMERA;
    std::cout << "[Hengstar] Main Publishing track" << std::endl;
    std::shared_ptr<LocalVideoTrack> videoTrack = LocalVideoTrack::CreateVideoTrack("hue", videoSource);
    livekit::TrackPublishOptions options;
    options.set_source(SOURCE_CAMERA);
    while (!room->GetLocalParticipant()) {}
    std::cout << "[Hengstar] Main Publishing track 2" << std::endl;
    room->GetLocalParticipant()->PublishTrack(videoTrack, options);

    sourceTask.wait();

    // Should we implement a mechanism to PollEvents/WaitEvents? Like SDL2/glfw
    //   - So we can remove the useless loop here
    // Or is it better to use callback based events?

    while(true) {

    }

    return 0;
}
