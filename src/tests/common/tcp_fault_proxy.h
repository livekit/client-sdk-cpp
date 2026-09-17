/*
 * Copyright 2026 LiveKit
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

#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace livekit::test {

/// Test-only TCP proxy whose established connections can be frozen or reset.
class TcpFaultProxy {
public:
  TcpFaultProxy(std::string upstream_host, std::uint16_t upstream_port)
      : upstream_host_(std::move(upstream_host)), upstream_port_(upstream_port) {}

  TcpFaultProxy(const TcpFaultProxy&) = delete;
  TcpFaultProxy& operator=(const TcpFaultProxy&) = delete;

  ~TcpFaultProxy() { stop(); }

  /// Bind an ephemeral loopback port and begin accepting connections.
  void start() {
    if (running_.exchange(true)) return;
    initializeSocketLibrary();
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (isInvalidSocket(listen_fd_)) {
      running_.store(false);
      throw std::runtime_error(socketError("failed to create proxy listener"));
    }
    enableAddressReuse(listen_fd_);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listen_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
      const auto error = socketError("failed to bind proxy listener");
      closeSocket(listen_fd_);
      running_.store(false);
      throw std::runtime_error(error);
    }
    if (::listen(listen_fd_, 16) != 0) {
      const auto error = socketError("failed to listen on proxy socket");
      closeSocket(listen_fd_);
      running_.store(false);
      throw std::runtime_error(error);
    }
    SocketLength address_length = sizeof(address);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &address_length) != 0) {
      const auto error = socketError("failed to resolve proxy listener port");
      closeSocket(listen_fd_);
      running_.store(false);
      throw std::runtime_error(error);
    }
    listen_port_ = ntohs(address.sin_port);
    const SocketHandle listener = listen_fd_;
    accept_thread_ = std::thread([this, listener]() { acceptLoop(listener); });
  }

  /// Stop the listener and all active forwarding workers.
  void stop() noexcept {
    if (!running_.exchange(false)) return;
    paused_.store(false);
    pause_cv_.notify_all();
    closeSocket(listen_fd_);
    if (accept_thread_.joinable()) accept_thread_.join();
    std::vector<std::shared_ptr<Connection>> connections;
    {
      const std::scoped_lock<std::mutex> lock(connections_mutex_);
      connections.swap(connections_);
    }
    for (const auto& connection : connections) connection->close();
    for (const auto& connection : connections) connection->join();
  }

  /// Freeze traffic in both directions without closing sockets.
  void pause() { paused_.store(true); }

  /// Resume traffic on existing and newly accepted connections.
  void resume() {
    paused_.store(false);
    pause_cv_.notify_all();
  }

  /// Abruptly close every currently accepted connection while retaining the listener.
  void resetConnections() {
    for (const auto& connection : connectionSnapshot()) connection->close();
  }

  /// Return the loopback port selected by start().
  std::uint16_t listenPort() const { return listen_port_; }
  /// Return the total number of client connections accepted by this proxy.
  std::uint64_t acceptedConnectionCount() const { return accepted_connection_count_.load(); }

private:
#if defined(_WIN32)
  using SocketHandle = SOCKET;
  using SocketLength = int;
  static constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
  using SocketHandle = int;
  using SocketLength = socklen_t;
  static constexpr SocketHandle kInvalidSocket = -1;
#endif

  class Connection {
  public:
    Connection(TcpFaultProxy& owner, SocketHandle client_fd, SocketHandle upstream_fd)
        : owner_(owner), client_fd_(client_fd), upstream_fd_(upstream_fd) {}
    ~Connection() {
      close();
      join();
    }
    void start() {
      client_to_upstream_ = std::thread([this]() { pump(client_fd_, upstream_fd_); });
      upstream_to_client_ = std::thread([this]() { pump(upstream_fd_, client_fd_); });
    }
    void close() {
      if (!open_.exchange(false)) return;
      owner_.pause_cv_.notify_all();
      closeSocket(client_fd_);
      closeSocket(upstream_fd_);
    }
    void join() {
      if (client_to_upstream_.joinable()) client_to_upstream_.join();
      if (upstream_to_client_.joinable()) upstream_to_client_.join();
    }

  private:
    void pump(SocketHandle source_fd, SocketHandle destination_fd) {
      constexpr std::size_t kBufferSize = static_cast<std::size_t>(16) * 1024U;
      std::array<std::uint8_t, kBufferSize> buffer{};
      while (open_.load() && owner_.running_.load()) {
        if (!owner_.waitUntilResumed(open_)) break;
        const auto bytes_read = receive(source_fd, buffer.data(), buffer.size());
        if (bytes_read <= 0) break;
        if (!owner_.waitUntilResumed(open_)) break;
        std::size_t bytes_sent = 0;
        while (bytes_sent < static_cast<std::size_t>(bytes_read) && open_.load() && owner_.running_.load()) {
          const auto result = sendNoSignal(destination_fd, buffer.data() + bytes_sent,
                                           static_cast<std::size_t>(bytes_read) - bytes_sent);
          if (result <= 0) {
            close();
            return;
          }
          bytes_sent += static_cast<std::size_t>(result);
        }
      }
      close();
    }
    TcpFaultProxy& owner_;
    SocketHandle client_fd_{kInvalidSocket};
    SocketHandle upstream_fd_{kInvalidSocket};
    std::atomic_bool open_{true};
    std::thread client_to_upstream_;
    std::thread upstream_to_client_;
  };

  static void initializeSocketLibrary() {
#if defined(_WIN32)
    static std::once_flag initialized;
    std::call_once(initialized, []() {
      WSADATA data{};
      const int error = ::WSAStartup(MAKEWORD(2, 2), &data);
      if (error != 0) {
        throw std::runtime_error("failed to initialize Winsock: " + std::to_string(error));
      }
    });
#endif
  }

  static bool isInvalidSocket(SocketHandle socket_fd) { return socket_fd == kInvalidSocket; }

  static std::string socketError(const std::string& message) {
#if defined(_WIN32)
    return message + ": WSA error " + std::to_string(::WSAGetLastError());
#else
    return message + ": " + std::strerror(errno);
#endif
  }

  static void enableAddressReuse(SocketHandle socket_fd) {
    int reuse_address = 1;
#if defined(_WIN32)
    (void)::setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse_address),
                       sizeof(reuse_address));
#else
    (void)::setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address));
#endif
  }

  static void closeSocket(SocketHandle& socket_fd) {
    if (isInvalidSocket(socket_fd)) return;
#if defined(_WIN32)
    (void)::shutdown(socket_fd, SD_BOTH);
    (void)::closesocket(socket_fd);
#else
    (void)::shutdown(socket_fd, SHUT_RDWR);
    (void)::close(socket_fd);
#endif
    socket_fd = kInvalidSocket;
  }

  static int receive(SocketHandle socket_fd, std::uint8_t* buffer, std::size_t size) {
#if defined(_WIN32)
    return ::recv(socket_fd, reinterpret_cast<char*>(buffer), static_cast<int>(size), 0);
#else
    return static_cast<int>(::recv(socket_fd, buffer, size, 0));
#endif
  }

  static int sendNoSignal(SocketHandle socket_fd, const void* data, std::size_t size) {
#ifdef MSG_NOSIGNAL
    return static_cast<int>(::send(socket_fd, data, size, MSG_NOSIGNAL));
#elif defined(_WIN32)
    return ::send(socket_fd, reinterpret_cast<const char*>(data), static_cast<int>(size), 0);
#else
    return static_cast<int>(::send(socket_fd, data, size, 0));
#endif
  }

  static void configureSocket(SocketHandle socket_fd) {
#ifdef SO_NOSIGPIPE
    int suppress_sigpipe = 1;
    (void)::setsockopt(socket_fd, SOL_SOCKET, SO_NOSIGPIPE, &suppress_sigpipe, sizeof(suppress_sigpipe));
#else
    (void)socket_fd;
#endif
  }
  bool waitUntilResumed(const std::atomic_bool& connection_open) {
    std::unique_lock<std::mutex> lock(pause_mutex_);
    pause_cv_.wait(
        lock, [this, &connection_open]() { return !paused_.load() || !running_.load() || !connection_open.load(); });
    return running_.load() && connection_open.load();
  }
  SocketHandle connectUpstream() const {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const auto port = std::to_string(upstream_port_);
    if (::getaddrinfo(upstream_host_.c_str(), port.c_str(), &hints, &addresses) != 0) return kInvalidSocket;
    SocketHandle upstream_fd = kInvalidSocket;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
      upstream_fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
      if (isInvalidSocket(upstream_fd)) continue;
      configureSocket(upstream_fd);
      if (::connect(upstream_fd, address->ai_addr, address->ai_addrlen) == 0) break;
      closeSocket(upstream_fd);
    }
    ::freeaddrinfo(addresses);
    return upstream_fd;
  }
  void acceptLoop(SocketHandle listener) {
    while (running_.load()) {
      sockaddr_storage client_address{};
      SocketLength client_address_length = sizeof(client_address);
      const SocketHandle client_fd =
          ::accept(listener, reinterpret_cast<sockaddr*>(&client_address), &client_address_length);
      if (isInvalidSocket(client_fd)) {
#if !defined(_WIN32)
        if (running_.load() && errno == EINTR) continue;
#endif
        break;
      }
      configureSocket(client_fd);
      const SocketHandle upstream_fd = connectUpstream();
      if (isInvalidSocket(upstream_fd)) {
        SocketHandle fd = client_fd;
        closeSocket(fd);
        continue;
      }
      auto connection = std::make_shared<Connection>(*this, client_fd, upstream_fd);
      {
        const std::scoped_lock<std::mutex> lock(connections_mutex_);
        connections_.push_back(connection);
      }
      ++accepted_connection_count_;
      connection->start();
    }
  }
  std::vector<std::shared_ptr<Connection>> connectionSnapshot() const {
    const std::scoped_lock<std::mutex> lock(connections_mutex_);
    return connections_;
  }
  std::string upstream_host_;
  std::uint16_t upstream_port_;
  SocketHandle listen_fd_{kInvalidSocket};
  std::uint16_t listen_port_{0};
  std::atomic_bool running_{false};
  std::atomic_bool paused_{false};
  std::atomic<std::uint64_t> accepted_connection_count_{0};
  std::thread accept_thread_;
  mutable std::mutex connections_mutex_;
  std::vector<std::shared_ptr<Connection>> connections_;
  std::mutex pause_mutex_;
  std::condition_variable pause_cv_;
};

} // namespace livekit::test
