/*
 * Copyright 2023 LiveKit
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

#include "livekit/logging.h"

#include <spdlog/sinks/callback_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <mutex>

namespace livekit {
namespace {

const char* kLoggerName = "livekit";

spdlog::level::level_enum toSpdlogLevel(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return spdlog::level::trace;
    case LogLevel::Debug:
      return spdlog::level::debug;
    case LogLevel::Info:
      return spdlog::level::info;
    case LogLevel::Warn:
      return spdlog::level::warn;
    case LogLevel::Error:
      return spdlog::level::err;
    case LogLevel::Critical:
      return spdlog::level::critical;
    case LogLevel::Off:
      return spdlog::level::off;
  }
  return spdlog::level::info;
}

LogLevel fromSpdlogLevel(spdlog::level::level_enum level) {
  switch (level) {
    case spdlog::level::trace:
      return LogLevel::Trace;
    case spdlog::level::debug:
      return LogLevel::Debug;
    case spdlog::level::info:
      return LogLevel::Info;
    case spdlog::level::warn:
      return LogLevel::Warn;
    case spdlog::level::err:
      return LogLevel::Error;
    case spdlog::level::critical:
      return LogLevel::Critical;
    case spdlog::level::off: // NOLINT(bugprone-branch-clone)
      return LogLevel::Off;
    default:
      return LogLevel::Info;
  }
}

std::mutex& loggerMutex() {
  static std::mutex mtx;
  return mtx;
}

std::shared_ptr<spdlog::logger>& loggerStorage() {
  static std::shared_ptr<spdlog::logger> logger;
  return logger;
}

std::shared_ptr<spdlog::logger> createDefaultLogger() {
  auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>(kLoggerName, sink);
  logger->set_level(spdlog::level::info);
  logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
  return logger;
}

} // namespace

namespace detail {

std::shared_ptr<spdlog::logger> getLogger() {
  const std::scoped_lock<std::mutex> lock(loggerMutex());
  auto& logger = loggerStorage();
  if (!logger) {
    logger = createDefaultLogger();
    spdlog::register_logger(logger);
  }
  return logger;
}

void shutdownLogger() {
  const std::scoped_lock<std::mutex> lock(loggerMutex());
  auto& logger = loggerStorage();
  if (logger) {
    spdlog::drop(kLoggerName);
    logger.reset();
  }
}

} // namespace detail

void setLogLevel(LogLevel level) { detail::getLogger()->set_level(toSpdlogLevel(level)); }

LogLevel getLogLevel() { return fromSpdlogLevel(detail::getLogger()->level()); }

void setLogCallback(LogCallback callback) {
  const std::scoped_lock<std::mutex> lock(loggerMutex());
  auto& logger = loggerStorage();
  auto current_level = logger ? logger->level() : spdlog::level::info;

  if (logger) {
    spdlog::drop(kLoggerName);
  }

  if (callback) {
    auto sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
        [cb = std::move(callback)](const spdlog::details::log_msg& msg) {
          cb(fromSpdlogLevel(msg.level), std::string(msg.logger_name.data(), msg.logger_name.size()),
             std::string(msg.payload.data(), msg.payload.size()));
        });
    logger = std::make_shared<spdlog::logger>(kLoggerName, sink);
  } else {
    logger = createDefaultLogger();
  }

  logger->set_level(current_level);
  spdlog::register_logger(logger);
}

} // namespace livekit
