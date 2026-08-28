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

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      running_.store(false);
      throw std::runtime_error(socketError("failed to create proxy listener"));
    }
    int reuse_address = 1;
    (void)::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address));
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
    socklen_t address_length = sizeof(address);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &address_length) != 0) {
      const auto error = socketError("failed to resolve proxy listener port");
      closeSocket(listen_fd_);
      running_.store(false);
      throw std::runtime_error(error);
    }
    listen_port_ = ntohs(address.sin_port);
    const int listener = listen_fd_;
    accept_thread_ = std::thread([this, listener]() { acceptLoop(listener); });
  }

  /// Stop the listener and all active forwarding workers.
  void stop() {
    if (!running_.exchange(false)) return;
    paused_.store(false);
    pause_cv_.notify_all();
    closeSocket(listen_fd_);
    if (accept_thread_.joinable()) accept_thread_.join();
    const auto connections = connectionSnapshot();
    for (const auto& connection : connections) connection->close();
    for (const auto& connection : connections) connection->join();
    const std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.clear();
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
  class Connection {
  public:
    Connection(TcpFaultProxy& owner, int client_fd, int upstream_fd)
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
    void pump(int source_fd, int destination_fd) {
      std::array<std::uint8_t, 16U * 1024U> buffer{};
      while (open_.load() && owner_.running_.load()) {
        if (!owner_.waitUntilResumed(open_)) break;
        const auto bytes_read = ::recv(source_fd, buffer.data(), buffer.size(), 0);
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
    int client_fd_{-1};
    int upstream_fd_{-1};
    std::atomic_bool open_{true};
    std::thread client_to_upstream_;
    std::thread upstream_to_client_;
  };

  static std::string socketError(const std::string& message) { return message + ": " + std::strerror(errno); }
  static void closeSocket(int& socket_fd) {
    if (socket_fd < 0) return;
    (void)::shutdown(socket_fd, SHUT_RDWR);
    (void)::close(socket_fd);
    socket_fd = -1;
  }
  static ssize_t sendNoSignal(int socket_fd, const void* data, std::size_t size) {
#ifdef MSG_NOSIGNAL
    return ::send(socket_fd, data, size, MSG_NOSIGNAL);
#else
    return ::send(socket_fd, data, size, 0);
#endif
  }
  static void configureSocket(int socket_fd) {
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
  int connectUpstream() const {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const auto port = std::to_string(upstream_port_);
    if (::getaddrinfo(upstream_host_.c_str(), port.c_str(), &hints, &addresses) != 0) return -1;
    int upstream_fd = -1;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
      upstream_fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
      if (upstream_fd < 0) continue;
      configureSocket(upstream_fd);
      if (::connect(upstream_fd, address->ai_addr, address->ai_addrlen) == 0) break;
      closeSocket(upstream_fd);
    }
    ::freeaddrinfo(addresses);
    return upstream_fd;
  }
  void acceptLoop(int listener) {
    while (running_.load()) {
      sockaddr_storage client_address{};
      socklen_t client_address_length = sizeof(client_address);
      const int client_fd = ::accept(listener, reinterpret_cast<sockaddr*>(&client_address), &client_address_length);
      if (client_fd < 0) {
        if (running_.load() && errno == EINTR) continue;
        break;
      }
      configureSocket(client_fd);
      const int upstream_fd = connectUpstream();
      if (upstream_fd < 0) {
        int fd = client_fd;
        closeSocket(fd);
        continue;
      }
      auto connection = std::make_shared<Connection>(*this, client_fd, upstream_fd);
      {
        const std::lock_guard<std::mutex> lock(connections_mutex_);
        connections_.push_back(connection);
      }
      ++accepted_connection_count_;
      connection->start();
    }
  }
  std::vector<std::shared_ptr<Connection>> connectionSnapshot() const {
    const std::lock_guard<std::mutex> lock(connections_mutex_);
    return connections_;
  }
  std::string upstream_host_;
  std::uint16_t upstream_port_;
  int listen_fd_{-1};
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
