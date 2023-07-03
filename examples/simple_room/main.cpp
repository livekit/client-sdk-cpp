#include "../include/livekit/livekit.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

using namespace livekit;

enum STATUS {
    STATUS_SUCCESS,
    STATUS_NULL_ARG,
    STATUS_OPEN_FILE_FAILED,
    STATUS_BUFFER_TOO_SMALL,
    STATUS_READ_FILE_FAILED
};

using PCHAR = char*;
using BOOL = bool;
using PBYTE = char*;
using PUINT64 = unsigned long long*;
using UINT64 = unsigned long long;
using UINT32 = unsigned int;
using SIZE_T = size_t;

#define STATUS_SUCCESS 0
#define STATUS_NULL_ARG 1
#define STATUS_OPEN_FILE_FAILED 2
#define STATUS_BUFFER_TOO_SMALL 3
#define STATUS_READ_FILE_FAILED 4

#define FOPEN(file, mode) fopen(file, mode)
#define FCLOSE(fp) fclose(fp)
#define FSEEK(fp, offset, origin) fseek(fp, offset, origin)
#define FTELL(fp) ftell(fp)
#define FREAD(buffer, size, count, fp) fread(buffer, size, count, fp)

/**
 * Read a file from the given full/relative filePath into the memory area pointed to by pBuffer.
 * Specifying NULL in pBuffer will return the size of the file.
 *
 * Parameters:
 *     filePath - file path to read from
 *     binMode  - TRUE to read file stream as binary; FALSE to read as a normal text file
 *     pBuffer  - buffer to write contents of the file to. If NULL return the size in pSize.
 *     pSize    - destination PUINT64 to store the size of the file when pBuffer is NULL;
 */

// int readFile(const std::string& filePath, BOOL binMode, PBYTE pBuffer, UINT64* pSize)
// {
//     UINT64 fileLen;
//     int retStatus = STATUS_SUCCESS;
//     FILE* fp = NULL;
//     if (filePath.empty() || pSize == NULL) {
//         return STATUS_NULL_ARG;
//     }
//     fp = FOPEN(filePath.c_str(), binMode ? "rb" : "r");
//     if (fp == NULL) {
//         return STATUS_OPEN_FILE_FAILED;
//     }
//     // Get the size of the file
//     FSEEK(fp, 0, SEEK_END);
//     fileLen = FTELL(fp);
//     if (pBuffer == NULL) {
//         // requested the length - set and early return
//         *pSize = fileLen;
//         return STATUS_SUCCESS;
//     }
//     // Validate the buffer size
//     if (fileLen > *pSize) {
//         return STATUS_BUFFER_TOO_SMALL;
//     }
//     // Read the file into memory buffer
//     FSEEK(fp, 0, SEEK_SET);
//     if (FREAD(pBuffer, (size_t)fileLen, 1, fp) != 1) {
//         return STATUS_READ_FILE_FAILED;
//     }
//     CleanUp:
//     if (fp != NULL) {
//         FCLOSE(fp);
//         fp = NULL;
//     }
//     return retStatus;
// }

int readFile(const std::string& filePath, BOOL binMode, PBYTE pBuffer, UINT32* pSize)
{
    // Create an ifstream object and open the file
    std::ifstream rfile(filePath, binMode ? std::ios::binary : std::ios::in);
    // Check if the file is opened successfully
    if (!rfile.is_open())
    {
        return STATUS_OPEN_FILE_FAILED;
    }
    
    // Get the size of the file
    rfile.seekg(0, std::ios::end); // move to the end of the file
    if (rfile.fail())
    {
        return STATUS_READ_FILE_FAILED;
    }
    UINT64 fileLen = rfile.tellg(); // get the current position
    printf("readFile(): fileLen = %llu\n", fileLen);
    if (fileLen == -1)
    {
        return STATUS_READ_FILE_FAILED;
    }

    // Check if buffer is null or too small
    if (pBuffer == nullptr)
    {
        printf("returning fileLen = %llu\n", fileLen);;
        // requested the length - set and early return
        *pSize = fileLen;
        return STATUS_SUCCESS;
    }
    // Validate the buffer size
    if (fileLen > *pSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    // Read the file into memory buffer
    rfile.seekg(0, std::ios::beg); // move to the beginning of the file
    rfile.read(pBuffer, fileLen); // read fileLen bytes into pBuffer
    if (rfile.fail() || rfile.gcount() != fileLen)
    {
        return STATUS_READ_FILE_FAILED;
    }

    // Release the resources
    rfile.close(); // close the file
    return STATUS_SUCCESS;
}

int readFrameFromDisk(PBYTE pFrame, UINT32* pSize, const std::string& frameFilePath)
{
    printf("readFrameFromDisk(): frameFilePath = %s\n", frameFilePath.c_str());
    int retStatus = STATUS_SUCCESS;
    if (pSize == NULL) {
        printf("readFrameFromDisk(): operation returned status code: %d \n", STATUS_NULL_ARG);
        return retStatus;
    }
    // printf("readFile(): path = %s\n", frameFilePath.c_str());
    // Get the size and read into frame
    retStatus = readFile(frameFilePath, true, pFrame, pSize);
    if (retStatus != STATUS_SUCCESS) {
        printf("readFile(): operation returned status code: %d \n", retStatus);
        return retStatus;
    }

    return STATUS_SUCCESS;
}

#define MAX_PATH_LEN 4096

int main(int argc, char *argv[])
{
    std::shared_ptr<Room> room(new Room());
    room->Connect("ws://localhost:7880", "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJleHAiOjE2ODg0ODg2NTMsImlzcyI6ImRldmtleSIsIm5hbWUiOiJ1c2VyMSIsIm5iZiI6MTY4ODQwMjI1Mywic3ViIjoidXNlcjEiLCJ2aWRlbyI6eyJyb29tIjoibXktZmlyc3Qtcm9vbSIsInJvb21Kb2luIjp0cnVlfX0.0U5A8b7AfNyYYaCNDxQJe13mxWeV2RkgNn3VCoCCX1g");

    // ParticipantInfo participantInfo;
    // participantInfo.set_identity("userId1");
    // participantInfo.set_sid("id1");
    // participantInfo.set_metadata("my-first-room");
    // participantInfo.set_name("user1");
    // LocalParticipant participant(participantInfo, room);

    // int retStatus = STATUS_SUCCESS;
    // char filePathChars[MAX_PATH_LEN + 1];
    // UINT32 fileIndex = 1, frameSize = 0;
    // snprintf(filePathChars, MAX_PATH_LEN, "/home/xgong/Workplace/repos/client-sdk-cpp/examples/simple_room/h264SampleFrames/frame-%04d.h264", fileIndex);

    // const std::string filePath(filePathChars); 
    
    // retStatus = readFrameFromDisk(nullptr, &frameSize, filePath);
    // printf("frameSize: %d\n", frameSize);
    // if (retStatus != STATUS_SUCCESS) {
    //     printf("readFrameFromDisk(): operation returned status code: 0x%08x \n", retStatus);
    //     return 1;
    // }

    // livekit::ArgbFrame argbFrame(FORMAT_ARGB, 640, 480);

    // retStatus = readFrameFromDisk((PBYTE)argbFrame.data, &frameSize, filePath);
    // if (retStatus != STATUS_SUCCESS) {
    //     printf("readFrameFromDisk(): operation returned status code: 0x%08x \n", retStatus);
    //     return 2;
    // }

    // printf("Successfully read frame of size %d\n", frameSize);

    // VideoSource videoSource;

    // // livekit::ARGBBufferInfo videoFrameBufferInfo;
    // // videoFrameBufferInfo.set_width(640);
    // // videoFrameBufferInfo.set_height(480);
    // // videoFrameBufferInfo.set_stride(640);
    // // FFIRequest request;
    // // livekit::ToI420Request* toI420Request = request.mutable_to_i420();
    // // toI420Request->mutable_argb->set_ptr()
    // // livekit::FFIResponse response = FfiClient::getInstance().SendRequest(request);
    // // response.to_i420();
    // // VideoFrameBuffer videoFrameBuffer = VideoFrameBuffer::Create(std::move(videoFrameHandle), std::move(videoFrameBufferInfo));
    // VideoFrame videoFrame(0, VIDEO_ROTATION_0, argbFrame.ToI420());
    // videoSource.CaptureFrame(videoFrame);
    
    // std::shared_ptr<LocalVideoTrack> videoTrack = LocalVideoTrack::CreateVideoTrack("video1", videoSource);
    // livekit::TrackPublishOptions options;
    // options.set_source(SOURCE_UNKNOWN);
    // participant.PublishTrack(videoTrack, options);

    // Should we implement a mechanism to PollEvents/WaitEvents? Like SDL2/glfw
    //   - So we can remove the useless loop here
    // Or is it better to use callback based events?

    while(true) {

    }

    return 0;
}
