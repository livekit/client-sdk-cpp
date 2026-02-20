/*
 * Copyright 2025 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Robot example -- streams real webcam video and microphone audio to a
 * LiveKit room using SDL3 for hardware capture.
 *
 * Usage:
 *   robot <ws-url> <token>
 *   LIVEKIT_URL=... LIVEKIT_TOKEN=... robot
 *
 * The token must grant identity "robot". Generate one with:
 *   lk token create --api-key <key> --api-secret <secret> \
 *       --join --room my-room --identity robot \
 *       --valid-for 24h
 *
 * Run alongside the "human" example (which displays the robot's feed).
 */

#include "livekit/audio_frame.h"
#include "livekit/track.h"
#include "livekit/video_frame.h"
#include "livekit_bridge/livekit_bridge.h"
#include "sdl_media.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---- Minimal 5x7 bitmap font for rendering text into RGBA buffers ----
// Each glyph is 5 columns wide, 7 rows tall, stored as 7 bytes (one per row,
// MSB = leftmost pixel). Only printable ASCII 0x20..0x7E are defined.
namespace bitmap_font {

constexpr int kGlyphW = 5;
constexpr int kGlyphH = 7;

// clang-format off
static const std::uint8_t kGlyphs[][kGlyphH] = {
  // 0x20 ' '
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  // 0x21 '!'
  {0x20,0x20,0x20,0x20,0x00,0x20,0x00},
  // 0x22 '"'
  {0x50,0x50,0x00,0x00,0x00,0x00,0x00},
  // 0x23 '#'
  {0x50,0xF8,0x50,0x50,0xF8,0x50,0x00},
  // 0x24 '$'
  {0x20,0x78,0xA0,0x70,0x28,0xF0,0x20},
  // 0x25 '%'
  {0xC8,0xC8,0x10,0x20,0x48,0x98,0x00},
  // 0x26 '&'
  {0x40,0xA0,0x40,0xA8,0x90,0x68,0x00},
  // 0x27 '\''
  {0x20,0x20,0x00,0x00,0x00,0x00,0x00},
  // 0x28 '('
  {0x10,0x20,0x40,0x40,0x20,0x10,0x00},
  // 0x29 ')'
  {0x40,0x20,0x10,0x10,0x20,0x40,0x00},
  // 0x2A '*'
  {0x00,0x50,0x20,0xF8,0x20,0x50,0x00},
  // 0x2B '+'
  {0x00,0x20,0x20,0xF8,0x20,0x20,0x00},
  // 0x2C ','
  {0x00,0x00,0x00,0x00,0x20,0x20,0x40},
  // 0x2D '-'
  {0x00,0x00,0x00,0xF8,0x00,0x00,0x00},
  // 0x2E '.'
  {0x00,0x00,0x00,0x00,0x00,0x20,0x00},
  // 0x2F '/'
  {0x08,0x08,0x10,0x20,0x40,0x80,0x00},
  // 0x30 '0'
  {0x70,0x88,0x98,0xA8,0xC8,0x70,0x00},
  // 0x31 '1'
  {0x20,0x60,0x20,0x20,0x20,0x70,0x00},
  // 0x32 '2'
  {0x70,0x88,0x08,0x30,0x40,0xF8,0x00},
  // 0x33 '3'
  {0x70,0x88,0x30,0x08,0x88,0x70,0x00},
  // 0x34 '4'
  {0x10,0x30,0x50,0x90,0xF8,0x10,0x00},
  // 0x35 '5'
  {0xF8,0x80,0xF0,0x08,0x08,0xF0,0x00},
  // 0x36 '6'
  {0x30,0x40,0xF0,0x88,0x88,0x70,0x00},
  // 0x37 '7'
  {0xF8,0x08,0x10,0x20,0x20,0x20,0x00},
  // 0x38 '8'
  {0x70,0x88,0x70,0x88,0x88,0x70,0x00},
  // 0x39 '9'
  {0x70,0x88,0x88,0x78,0x10,0x60,0x00},
  // 0x3A ':'
  {0x00,0x00,0x20,0x00,0x20,0x00,0x00},
  // 0x3B ';'
  {0x00,0x00,0x20,0x00,0x20,0x20,0x40},
  // 0x3C '<'
  {0x08,0x10,0x20,0x40,0x20,0x10,0x08},
  // 0x3D '='
  {0x00,0x00,0xF8,0x00,0xF8,0x00,0x00},
  // 0x3E '>'
  {0x80,0x40,0x20,0x10,0x20,0x40,0x80},
  // 0x3F '?'
  {0x70,0x88,0x10,0x20,0x00,0x20,0x00},
  // 0x40 '@'
  {0x70,0x88,0xB8,0xB8,0x80,0x70,0x00},
  // 0x41 'A'
  {0x70,0x88,0x88,0xF8,0x88,0x88,0x00},
  // 0x42 'B'
  {0xF0,0x88,0xF0,0x88,0x88,0xF0,0x00},
  // 0x43 'C'
  {0x70,0x88,0x80,0x80,0x88,0x70,0x00},
  // 0x44 'D'
  {0xF0,0x88,0x88,0x88,0x88,0xF0,0x00},
  // 0x45 'E'
  {0xF8,0x80,0xF0,0x80,0x80,0xF8,0x00},
  // 0x46 'F'
  {0xF8,0x80,0xF0,0x80,0x80,0x80,0x00},
  // 0x47 'G'
  {0x70,0x88,0x80,0xB8,0x88,0x70,0x00},
  // 0x48 'H'
  {0x88,0x88,0xF8,0x88,0x88,0x88,0x00},
  // 0x49 'I'
  {0x70,0x20,0x20,0x20,0x20,0x70,0x00},
  // 0x4A 'J'
  {0x08,0x08,0x08,0x08,0x88,0x70,0x00},
  // 0x4B 'K'
  {0x88,0x90,0xA0,0xC0,0xA0,0x90,0x88},
  // 0x4C 'L'
  {0x80,0x80,0x80,0x80,0x80,0xF8,0x00},
  // 0x4D 'M'
  {0x88,0xD8,0xA8,0x88,0x88,0x88,0x00},
  // 0x4E 'N'
  {0x88,0xC8,0xA8,0x98,0x88,0x88,0x00},
  // 0x4F 'O'
  {0x70,0x88,0x88,0x88,0x88,0x70,0x00},
  // 0x50 'P'
  {0xF0,0x88,0x88,0xF0,0x80,0x80,0x00},
  // 0x51 'Q'
  {0x70,0x88,0x88,0xA8,0x90,0x68,0x00},
  // 0x52 'R'
  {0xF0,0x88,0x88,0xF0,0xA0,0x90,0x00},
  // 0x53 'S'
  {0x70,0x80,0x70,0x08,0x88,0x70,0x00},
  // 0x54 'T'
  {0xF8,0x20,0x20,0x20,0x20,0x20,0x00},
  // 0x55 'U'
  {0x88,0x88,0x88,0x88,0x88,0x70,0x00},
  // 0x56 'V'
  {0x88,0x88,0x88,0x50,0x50,0x20,0x00},
  // 0x57 'W'
  {0x88,0x88,0x88,0xA8,0xA8,0x50,0x00},
  // 0x58 'X'
  {0x88,0x50,0x20,0x20,0x50,0x88,0x00},
  // 0x59 'Y'
  {0x88,0x50,0x20,0x20,0x20,0x20,0x00},
  // 0x5A 'Z'
  {0xF8,0x10,0x20,0x40,0x80,0xF8,0x00},
  // 0x5B '['
  {0x70,0x40,0x40,0x40,0x40,0x70,0x00},
  // 0x5C '\\'
  {0x80,0x40,0x20,0x10,0x08,0x08,0x00},
  // 0x5D ']'
  {0x70,0x10,0x10,0x10,0x10,0x70,0x00},
  // 0x5E '^'
  {0x20,0x50,0x88,0x00,0x00,0x00,0x00},
  // 0x5F '_'
  {0x00,0x00,0x00,0x00,0x00,0xF8,0x00},
  // 0x60 '`'
  {0x40,0x20,0x00,0x00,0x00,0x00,0x00},
  // 0x61 'a'
  {0x00,0x70,0x08,0x78,0x88,0x78,0x00},
  // 0x62 'b'
  {0x80,0x80,0xF0,0x88,0x88,0xF0,0x00},
  // 0x63 'c'
  {0x00,0x70,0x80,0x80,0x80,0x70,0x00},
  // 0x64 'd'
  {0x08,0x08,0x78,0x88,0x88,0x78,0x00},
  // 0x65 'e'
  {0x00,0x70,0x88,0xF8,0x80,0x70,0x00},
  // 0x66 'f'
  {0x30,0x40,0xF0,0x40,0x40,0x40,0x00},
  // 0x67 'g'
  {0x00,0x78,0x88,0x78,0x08,0x70,0x00},
  // 0x68 'h'
  {0x80,0x80,0xF0,0x88,0x88,0x88,0x00},
  // 0x69 'i'
  {0x20,0x00,0x60,0x20,0x20,0x70,0x00},
  // 0x6A 'j'
  {0x10,0x00,0x30,0x10,0x10,0x10,0x60},
  // 0x6B 'k'
  {0x80,0x90,0xA0,0xC0,0xA0,0x90,0x00},
  // 0x6C 'l'
  {0x60,0x20,0x20,0x20,0x20,0x70,0x00},
  // 0x6D 'm'
  {0x00,0xD0,0xA8,0xA8,0x88,0x88,0x00},
  // 0x6E 'n'
  {0x00,0xF0,0x88,0x88,0x88,0x88,0x00},
  // 0x6F 'o'
  {0x00,0x70,0x88,0x88,0x88,0x70,0x00},
  // 0x70 'p'
  {0x00,0xF0,0x88,0xF0,0x80,0x80,0x00},
  // 0x71 'q'
  {0x00,0x78,0x88,0x78,0x08,0x08,0x00},
  // 0x72 'r'
  {0x00,0xB0,0xC8,0x80,0x80,0x80,0x00},
  // 0x73 's'
  {0x00,0x78,0x80,0x70,0x08,0xF0,0x00},
  // 0x74 't'
  {0x40,0xF0,0x40,0x40,0x48,0x30,0x00},
  // 0x75 'u'
  {0x00,0x88,0x88,0x88,0x98,0x68,0x00},
  // 0x76 'v'
  {0x00,0x88,0x88,0x50,0x50,0x20,0x00},
  // 0x77 'w'
  {0x00,0x88,0x88,0xA8,0xA8,0x50,0x00},
  // 0x78 'x'
  {0x00,0x88,0x50,0x20,0x50,0x88,0x00},
  // 0x79 'y'
  {0x00,0x88,0x88,0x78,0x08,0x70,0x00},
  // 0x7A 'z'
  {0x00,0xF8,0x10,0x20,0x40,0xF8,0x00},
  // 0x7B '{'
  {0x18,0x20,0x60,0x20,0x20,0x18,0x00},
  // 0x7C '|'
  {0x20,0x20,0x20,0x20,0x20,0x20,0x00},
  // 0x7D '}'
  {0xC0,0x20,0x30,0x20,0x20,0xC0,0x00},
  // 0x7E '~'
  {0x00,0x00,0x48,0xB0,0x00,0x00,0x00},
};
// clang-format on

/// Draw a string into an RGBA buffer at the given pixel coordinate.
/// Each character is drawn at `scale` times the native 5x7 size.
static void drawString(std::uint8_t *buf, int buf_w, int buf_h, int x0, int y0,
                       const std::string &text, int scale, std::uint8_t r,
                       std::uint8_t g, std::uint8_t b) {
  int cx = x0;
  for (char ch : text) {
    int idx = static_cast<unsigned char>(ch) - 0x20;
    if (idx < 0 ||
        idx >= static_cast<int>(sizeof(kGlyphs) / sizeof(kGlyphs[0])))
      idx = 0; // fallback to space
    for (int row = 0; row < kGlyphH; ++row) {
      std::uint8_t bits = kGlyphs[idx][row];
      for (int col = 0; col < kGlyphW; ++col) {
        if (bits & (0x80 >> col)) {
          for (int sy = 0; sy < scale; ++sy) {
            for (int sx = 0; sx < scale; ++sx) {
              int px = cx + col * scale + sx;
              int py = y0 + row * scale + sy;
              if (px >= 0 && px < buf_w && py >= 0 && py < buf_h) {
                int off = (py * buf_w + px) * 4;
                buf[off + 0] = r;
                buf[off + 1] = g;
                buf[off + 2] = b;
                buf[off + 3] = 255;
              }
            }
          }
        }
      }
    }
    cx += (kGlyphW + 1) * scale; // 1px spacing between characters
  }
}

} // namespace bitmap_font

static std::atomic<bool> g_running{true};
static void handleSignal(int) { g_running.store(false); }

int main(int argc, char *argv[]) {
  // ----- Parse args / env -----
  std::string url, token;
  if (argc >= 3) {
    url = argv[1];
    token = argv[2];
  } else {
    const char *e = std::getenv("LIVEKIT_URL");
    if (e)
      url = e;
    e = std::getenv("LIVEKIT_TOKEN");
    if (e)
      token = e;
  }
  if (url.empty() || token.empty()) {
    std::cerr << "Usage: robot <ws-url> <token>\n"
              << "   or: LIVEKIT_URL=... LIVEKIT_TOKEN=... robot\n";
    return 1;
  }

  std::signal(SIGINT, handleSignal);

  // ----- Initialize SDL3 -----
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_CAMERA)) {
    std::cerr << "[robot] SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  // ----- Connect to LiveKit -----
  livekit_bridge::LiveKitBridge bridge;
  std::cout << "[robot] Connecting to " << url << " ...\n";
  livekit::RoomOptions options;
  options.auto_subscribe = true;
  if (!bridge.connect(url, token, options)) {
    std::cerr << "[robot] Failed to connect.\n";
    SDL_Quit();
    return 1;
  }
  std::cout << "[robot] Connected.\n";

  // ----- Create outgoing tracks -----
  constexpr int kSampleRate = 48000;
  constexpr int kChannels = 1;
  constexpr int kWidth = 1280;
  constexpr int kHeight = 720;

  constexpr int kSimWidth = 480;
  constexpr int kSimHeight = 320;

  auto mic = bridge.createAudioTrack("robot-mic", kSampleRate, kChannels,
                                     livekit::TrackSource::SOURCE_MICROPHONE);
  auto sim_audio =
      bridge.createAudioTrack("robot-sim-audio", kSampleRate, kChannels,
                              livekit::TrackSource::SOURCE_SCREENSHARE_AUDIO);
  auto cam = bridge.createVideoTrack("robot-cam", kWidth, kHeight,
                                     livekit::TrackSource::SOURCE_CAMERA);
  auto sim_cam =
      bridge.createVideoTrack("robot-sim-frame", kSimWidth, kSimHeight,
                              livekit::TrackSource::SOURCE_SCREENSHARE);
  std::cout << "[robot] Publishing mic + sim audio (" << kSampleRate << " Hz, "
            << kChannels << " ch), cam + sim frame (" << kWidth << "x"
            << kHeight << " / " << kSimWidth << "x" << kSimHeight << ").\n";

  // ----- SDL Mic capture -----
  // SDLMicSource pulls 10ms frames from the default recording device and
  // invokes our callback with interleaved int16 samples.
  bool mic_using_sdl = false;
  std::unique_ptr<SDLMicSource> sdl_mic;
  std::atomic<bool> mic_running{true};
  std::thread mic_thread;

  {
    int recCount = 0;
    SDL_AudioDeviceID *recDevs = SDL_GetAudioRecordingDevices(&recCount);
    bool has_mic = recDevs && recCount > 0;
    if (recDevs)
      SDL_free(recDevs);

    if (has_mic) {
      sdl_mic = std::make_unique<SDLMicSource>(
          kSampleRate, kChannels, kSampleRate / 100, // 10ms frames
          [&mic](const int16_t *samples, int num_samples_per_channel,
                 int /*sample_rate*/, int /*num_channels*/) {
            try {
              mic->pushFrame(samples, num_samples_per_channel);
            } catch (const std::exception &e) {
              std::cerr << "[robot] Mic push error: " << e.what() << "\n";
            }
          });

      if (sdl_mic->init()) {
        mic_using_sdl = true;
        std::cout << "[robot] Using SDL microphone.\n";
        mic_thread = std::thread([&]() {
          while (mic_running.load()) {
            sdl_mic->pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        });
      } else {
        std::cerr << "[robot] SDL mic init failed.\n";
        sdl_mic.reset();
      }
    }

    if (!mic_using_sdl) {
      std::cout << "[robot] No microphone found; sending silence.\n";
      mic_thread = std::thread([&]() {
        constexpr int kSamplesPerFrame = kSampleRate / 100;
        std::vector<std::int16_t> silence(kSamplesPerFrame * kChannels, 0);
        auto next = std::chrono::steady_clock::now();
        while (mic_running.load()) {
          try {
            mic->pushFrame(silence, kSamplesPerFrame);
          } catch (...) {
          }
          next += std::chrono::milliseconds(10);
          std::this_thread::sleep_until(next);
        }
      });
    }
  }

  // ----- SDL Camera capture -----
  // SDLCamSource grabs webcam frames and invokes our callback with raw pixels.
  bool cam_using_sdl = false;
  std::unique_ptr<SDLCamSource> sdl_cam;
  std::atomic<bool> cam_running{true};
  std::thread cam_thread;

  {
    int camCount = 0;
    SDL_CameraID *cams = SDL_GetCameras(&camCount);
    bool has_cam = cams && camCount > 0;
    if (cams)
      SDL_free(cams);

    if (has_cam) {
      sdl_cam = std::make_unique<SDLCamSource>(
          kWidth, kHeight, 30, SDL_PIXELFORMAT_RGBA32,
          [&cam](const uint8_t *pixels, int pitch, int width, int height,
                 SDL_PixelFormat /*fmt*/, Uint64 timestampNS) {
            // Copy row-by-row (pitch may differ from width*4)
            const int dstPitch = width * 4;
            std::vector<std::uint8_t> buf(dstPitch * height);
            for (int y = 0; y < height; ++y) {
              std::memcpy(buf.data() + y * dstPitch, pixels + y * pitch,
                          dstPitch);
            }
            try {
              cam->pushFrame(buf.data(), buf.size(),
                             static_cast<std::int64_t>(timestampNS / 1000));
            } catch (const std::exception &e) {
              std::cerr << "[robot] Cam push error: " << e.what() << "\n";
            }
          });

      if (sdl_cam->init()) {
        cam_using_sdl = true;
        std::cout << "[robot] Using SDL camera.\n";
        cam_thread = std::thread([&]() {
          while (cam_running.load()) {
            sdl_cam->pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        });
      } else {
        std::cerr << "[robot] SDL camera init failed.\n";
        sdl_cam.reset();
      }
    }

    if (!cam_using_sdl) {
      std::cout << "[robot] No camera found; sending solid green frames.\n";
      cam_thread = std::thread([&]() {
        std::vector<std::uint8_t> green(kWidth * kHeight * 4);
        for (int i = 0; i < kWidth * kHeight; ++i) {
          green[i * 4 + 0] = 0;
          green[i * 4 + 1] = 180;
          green[i * 4 + 2] = 0;
          green[i * 4 + 3] = 255;
        }
        std::int64_t ts = 0;
        while (cam_running.load()) {
          try {
            cam->pushFrame(green, ts);
            ts += 33333;
          } catch (...) {
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
      });
    }
  }

  // ----- Sim frame video track (red bg, white text with frame # and time)
  // -----
  std::atomic<bool> sim_running{true};
  std::thread sim_thread([&]() {
    const std::size_t buf_size = kSimWidth * kSimHeight * 4;
    std::vector<std::uint8_t> frame(buf_size);
    std::uint64_t frame_num = 0;
    auto start = std::chrono::steady_clock::now();

    while (sim_running.load()) {
      // Fill with red background
      for (int i = 0; i < kSimWidth * kSimHeight; ++i) {
        frame[i * 4 + 0] = 200; // R
        frame[i * 4 + 1] = 30;  // G
        frame[i * 4 + 2] = 30;  // B
        frame[i * 4 + 3] = 255; // A
      }

      // Compute elapsed time
      auto now = std::chrono::steady_clock::now();
      auto elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
              .count();
      int secs = static_cast<int>(elapsed_ms / 1000);
      int ms = static_cast<int>(elapsed_ms % 1000);

      // Build text lines
      std::string line1 = "FRAME " + std::to_string(frame_num);
      char time_buf[32];
      std::snprintf(time_buf, sizeof(time_buf), "T=%d.%03ds", secs, ms);
      std::string line2(time_buf);

      // Draw white text at scale=4 (each character is 20x28 pixels)
      constexpr int kScale = 4;
      constexpr int kCharW = (bitmap_font::kGlyphW + 1) * kScale;
      int line1_w = static_cast<int>(line1.size()) * kCharW;
      int line2_w = static_cast<int>(line2.size()) * kCharW;
      int y1 = (kSimHeight / 2) - (bitmap_font::kGlyphH * kScale) - 4;
      int y2 = (kSimHeight / 2) + 4;
      int x1 = (kSimWidth - line1_w) / 2;
      int x2 = (kSimWidth - line2_w) / 2;

      bitmap_font::drawString(frame.data(), kSimWidth, kSimHeight, x1, y1,
                              line1, kScale, 255, 255, 255);
      bitmap_font::drawString(frame.data(), kSimWidth, kSimHeight, x2, y2,
                              line2, kScale, 255, 255, 255);

      std::int64_t ts = static_cast<std::int64_t>(elapsed_ms) * 1000;
      try {
        sim_cam->pushFrame(frame, ts);
      } catch (...) {
      }
      ++frame_num;
      std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
  });
  std::cout << "[robot] Sim frame track started.\n";

  // ----- Sim audio track (siren: sine sweep 600-1200 Hz, 1s period) -----
  std::atomic<bool> sim_audio_running{true};
  std::thread sim_audio_thread([&]() {
    constexpr int kFrameSamples = kSampleRate / 100; // 10ms frames
    constexpr double kLoFreq = 600.0;
    constexpr double kHiFreq = 1200.0;
    constexpr double kSweepPeriod = 1.0; // seconds per full up-down cycle
    constexpr double kAmplitude = 16000.0;
    constexpr double kTwoPi = 2.0 * M_PI;

    std::vector<std::int16_t> buf(kFrameSamples * kChannels);
    double phase = 0.0;
    std::uint64_t sample_idx = 0;
    auto next = std::chrono::steady_clock::now();

    while (sim_audio_running.load()) {
      for (int i = 0; i < kFrameSamples; ++i) {
        double t = static_cast<double>(sample_idx) / kSampleRate;
        // Triangle sweep between kLoFreq and kHiFreq
        double sweep = std::fmod(t / kSweepPeriod, 1.0);
        double freq =
            (sweep < 0.5)
                ? kLoFreq + (kHiFreq - kLoFreq) * (sweep * 2.0)
                : kHiFreq - (kHiFreq - kLoFreq) * ((sweep - 0.5) * 2.0);
        phase += kTwoPi * freq / kSampleRate;
        if (phase > kTwoPi)
          phase -= kTwoPi;
        auto sample = static_cast<std::int16_t>(kAmplitude * std::sin(phase));
        for (int ch = 0; ch < kChannels; ++ch)
          buf[i * kChannels + ch] = sample;
        ++sample_idx;
      }
      try {
        sim_audio->pushFrame(buf, kFrameSamples);
      } catch (...) {
      }
      next += std::chrono::milliseconds(10);
      std::this_thread::sleep_until(next);
    }
  });
  std::cout << "[robot] Sim audio (siren) track started.\n";

  // ----- Main loop: keep alive + pump SDL events -----
  std::cout << "[robot] Streaming... press Ctrl-C to stop.\n";

  while (g_running.load()) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) {
        g_running.store(false);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // ----- Cleanup -----
  std::cout << "[robot] Shutting down...\n";

  mic_running.store(false);
  cam_running.store(false);
  sim_running.store(false);
  sim_audio_running.store(false);
  if (mic_thread.joinable())
    mic_thread.join();
  if (cam_thread.joinable())
    cam_thread.join();
  if (sim_thread.joinable())
    sim_thread.join();
  if (sim_audio_thread.joinable())
    sim_audio_thread.join();
  sdl_mic.reset();
  sdl_cam.reset();

  mic.reset();
  sim_audio.reset();
  cam.reset();
  sim_cam.reset();
  bridge.disconnect();

  SDL_Quit();
  std::cout << "[robot] Done.\n";
  return 0;
}
