/*
 * Copyright 2025 LiveKit, Inc.
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

#include "yuv_source.h"

#include <chrono>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
#define close_socket closesocket
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCKET_VALUE (-1)
#define close_socket close
#endif

namespace publish_yuv {

namespace {

socket_t connectTcp(const std::string &host, std::uint16_t port) {
#ifdef _WIN32
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return INVALID_SOCKET_VALUE;
#endif
  struct addrinfo hints = {}, *res = nullptr;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  std::string portStr = std::to_string(port);
  if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0) {
    std::cerr << "YuvSource: getaddrinfo failed for " << host << ":" << port << "\n";
    return INVALID_SOCKET_VALUE;
  }
  socket_t fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd == INVALID_SOCKET_VALUE) {
    freeaddrinfo(res);
    return INVALID_SOCKET_VALUE;
  }
  if (connect(fd, res->ai_addr, static_cast<socklen_t>(res->ai_addrlen)) != 0) {
    close_socket(fd);
    freeaddrinfo(res);
    return INVALID_SOCKET_VALUE;
  }
  freeaddrinfo(res);
  return fd;
}

} // namespace

YuvSource::YuvSource(const std::string &host,
                     std::uint16_t port,
                     int width,
                     int height,
                     int fps,
                     YuvFrameCallback callback)
    : host_(host),
      port_(port),
      width_(width),
      height_(height),
      fps_(fps),
      callback_(std::move(callback)) {
  if (width_ > 0 && height_ > 0) {
    frame_size_ =
        static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 3 / 2;
  }
}

YuvSource::~YuvSource() { stop(); }

void YuvSource::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread(&YuvSource::loop, this);
}

void YuvSource::stop() {
  running_.store(false);
  if (thread_.joinable()) thread_.join();
}

void YuvSource::loop() {
  if (frame_size_ == 0) {
    std::cerr << "YuvSource: invalid frame size\n";
    running_.store(false);
    return;
  }

  socket_t fd = connectTcp(host_, port_);
  if (fd == INVALID_SOCKET_VALUE) {
    std::cerr << "YuvSource: failed to connect to " << host_ << ":" << port_ << "\n";
    running_.store(false);
    return;
  }

  std::cout << "YuvSource: connected to " << host_ << ":" << port_ << " (" << width_
            << "x" << height_ << "@" << fps_ << "fps, frame=" << frame_size_
            << " bytes)\n";

  auto t0 = std::chrono::steady_clock::now();

  while (running_.load()) {
    std::vector<std::uint8_t> frame(frame_size_);
    std::size_t filled = 0;
    while (filled < frame_size_ && running_.load()) {
#ifdef _WIN32
      int n = recv(fd, reinterpret_cast<char *>(frame.data() + filled),
                   static_cast<int>(frame_size_ - filled), 0);
#else
      ssize_t n = recv(fd, frame.data() + filled, frame_size_ - filled, 0);
#endif
      if (n <= 0) {
        running_.store(false);
        break;
      }
      filled += static_cast<std::size_t>(n);
    }
    if (!running_.load() || filled < frame_size_) break;

    std::int64_t ts_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0)
            .count();
    if (callback_) {
      YuvFrame out;
      out.data = std::move(frame);
      out.timestamp_us = ts_us;
      callback_(std::move(out));
    }
  }

  close_socket(fd);
  running_.store(false);
}

} // namespace publish_yuv
