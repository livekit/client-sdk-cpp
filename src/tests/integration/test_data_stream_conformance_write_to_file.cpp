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

// ByteStreamReader::writeToFile, end to end. Ports the write_to_file tests of
// the Rust `livekit-data-stream` crate
// (client-sdk-rust/livekit-data-stream/src/incoming/stream_reader.rs): the
// file name comes from the sender, so it must be a plain file name. Also
// covers the one-consumer rule (read or write to a file, not both) and
// readers that are dropped without being consumed.
//
// Run with:
//   ./build-debug/bin/livekit_integration_tests --gtest_filter=DataStreamWriteToFile*

#include <livekit/data_stream.h>

#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../common/data_stream_test_utils.h"

namespace livekit::test::ds {

namespace {

constexpr char kInvalidFileName[] = "file name must be a plain file name";

/// Captures byte readers as they open, without consuming them.
class ByteReaderCapture {
public:
  ByteReaderCapture() = default;
  ByteReaderCapture(const ByteReaderCapture&) = delete;
  ByteReaderCapture& operator=(const ByteReaderCapture&) = delete;
  ByteReaderCapture(ByteReaderCapture&&) = delete;
  ByteReaderCapture& operator=(ByteReaderCapture&&) = delete;
  ~ByteReaderCapture() {
    const std::scoped_lock<std::mutex> lock(mutex_);
    for (auto& r : readers_) {
      r->close();
    }
  }

  void registerOn(Room& room, const std::string& topic) {
    room.registerByteStreamHandler(topic, [this](std::shared_ptr<ByteStreamReader> reader, const std::string&) {
      {
        const std::scoped_lock<std::mutex> lock(mutex_);
        readers_.push_back(std::move(reader));
      }
      cv_.notify_all();
    });
  }

  std::shared_ptr<ByteStreamReader> waitForReader(std::size_t index = 0) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!cv_.wait_for(lock, kStreamTimeout, [&] { return readers_.size() > index; })) {
      return nullptr;
    }
    return readers_[index];
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::shared_ptr<ByteStreamReader>> readers_;
};

/// A fresh, empty directory, removed on destruction.
class TempDir {
public:
  TempDir() : path_(std::filesystem::temp_directory_path() / uniqueStreamId("ds_conf_dir")) {
    std::filesystem::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;

  const std::filesystem::path& path() const { return path_; }
  std::string str() const { return path_.string(); }
  bool empty() const { return std::filesystem::is_empty(path_); }

private:
  std::filesystem::path path_;
};

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace

class DataStreamWriteToFileTest : public DataStreamConformanceTest {
protected:
  /// Sends `content` from the peer as a one-shot byte stream named `name`
  /// (unset: the writer default, an empty name) and returns its reader.
  std::shared_ptr<ByteStreamReader> receive(ByteReaderCapture& capture, const std::optional<std::string>& name,
                                            const std::string& content = "hello") {
    const auto topic = uniqueTopic("to_file");
    capture.registerOn(receiverRoom(), topic);
    FfiPeer peer(senderRoom());
    if (name) {
      PeerByteOptions opts;
      opts.topic = topic;
      opts.name = *name;
      EXPECT_TRUE(peer.sendBytes(content, opts).ok);
    } else {
      // Stream writers default to an empty name.
      PeerByteOptions opts;
      opts.topic = topic;
      const auto writer = peer.openBytes(opts);
      EXPECT_TRUE(writer.ok) << writer.error;
      (void)peer.writeBytes(writer.writer_handle, content);
      (void)peer.closeBytes(writer.writer_handle);
    }
    return capture.waitForReader();
  }

  /// Expects writeToFile to reject the stream's name and write nothing.
  void expectRejected(const std::shared_ptr<ByteStreamReader>& reader, const TempDir& dir,
                      const std::optional<std::string>& name_override = std::nullopt) {
    ASSERT_NE(reader, nullptr);
    try {
      const auto path = reader->writeToFile(dir.str(), name_override);
      FAIL() << "unsafe file name accepted; wrote " << path;
    } catch (const DataStreamError& e) {
      EXPECT_NE(std::string(e.what()).find(kInvalidFileName), std::string::npos) << e.what();
    }
    EXPECT_TRUE(dir.empty()) << "nothing may be written for a rejected name";
    EXPECT_FALSE(std::filesystem::exists(dir.path().parent_path() / "evil.txt"));
  }
};

// Rust: write_to_file_accepts_plain_name
TEST_F(DataStreamWriteToFileTest, AcceptsPlainName) {
  TempDir dir;
  ByteReaderCapture capture;
  auto reader = receive(capture, "file.txt");
  ASSERT_NE(reader, nullptr);

  const auto path = reader->writeToFile(dir.str());

  EXPECT_EQ(std::filesystem::path(path), dir.path() / "file.txt");
  EXPECT_EQ(readFile(path), "hello");
  std::vector<std::uint8_t> chunk;
  EXPECT_FALSE(reader->readNext(chunk)) << "a stream written to a file has nothing left to read";
}

// Rust: write_to_file_rejects_traversal_in_stream_name
TEST_F(DataStreamWriteToFileTest, RejectsTraversalInStreamName) {
  TempDir dir;
  ByteReaderCapture capture;
  expectRejected(receive(capture, "../evil.txt"), dir);
}

// Rust: write_to_file_rejects_traversal_in_name_override
TEST_F(DataStreamWriteToFileTest, RejectsTraversalInNameOverride) {
  TempDir dir;
  ByteReaderCapture capture;
  expectRejected(receive(capture, "safe.txt"), dir, "../evil.txt");
}

// Rust: write_to_file_rejects_absolute_path_in_stream_name
TEST_F(DataStreamWriteToFileTest, RejectsAbsolutePathInStreamName) {
  TempDir dir;
  ByteReaderCapture capture;
  const auto absolute = (dir.path().parent_path() / "evil.txt").string();
  expectRejected(receive(capture, absolute), dir);
}

// Rust: write_to_file_rejects_nested_path_in_stream_name
TEST_F(DataStreamWriteToFileTest, RejectsNestedPathInStreamName) {
  TempDir dir;
  ByteReaderCapture capture;
  expectRejected(receive(capture, "nested/evil.txt"), dir);
}

// Rust: write_to_file_rejects_empty_stream_name
TEST_F(DataStreamWriteToFileTest, RejectsEmptyStreamName) {
  TempDir dir;
  ByteReaderCapture capture;
  expectRejected(receive(capture, std::nullopt), dir);
}

TEST_F(DataStreamWriteToFileTest, NameOverrideIsUsed) {
  TempDir dir;
  ByteReaderCapture capture;
  auto reader = receive(capture, "original.txt");
  ASSERT_NE(reader, nullptr);

  const auto path = reader->writeToFile(dir.str(), "renamed.bin");

  EXPECT_EQ(std::filesystem::path(path), dir.path() / "renamed.bin");
  EXPECT_EQ(readFile(path), "hello");
}

TEST_F(DataStreamWriteToFileTest, DefaultsToTheTempDirectory) {
  ByteReaderCapture capture;
  const auto name = uniqueStreamId("default_dir") + ".txt";
  auto reader = receive(capture, name);
  ASSERT_NE(reader, nullptr);

  const auto path = reader->writeToFile();

  EXPECT_EQ(std::filesystem::path(path).filename(), name);
  EXPECT_EQ(readFile(path), "hello");
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

// A file sent with C++ sendFile (compressed, multi-packet) lands intact.
TEST_F(DataStreamWriteToFileTest, WritesACompressedMultipacketFile) {
  TempDir dir;
  ByteReaderCapture capture;
  const auto topic = uniqueTopic("to_file_large");
  capture.registerOn(receiverRoom(), topic);
  const auto content = randomText(60'000);
  const auto source = dir.path() / "source.txt";
  {
    std::ofstream out(source, std::ios::binary);
    out << content;
  }
  StreamByteOptions opts;
  opts.topic = topic;
  opts.name = "received.txt";
  (void)lockLocalParticipant(senderRoom())->sendFile(source.string(), opts);

  auto reader = capture.waitForReader();
  ASSERT_NE(reader, nullptr);
  const auto path = reader->writeToFile(dir.str());
  EXPECT_EQ(readFile(path), content);
}

TEST_F(DataStreamWriteToFileTest, CannotWriteAfterReading) {
  ByteReaderCapture capture;
  auto reader = receive(capture, "file.txt");
  ASSERT_NE(reader, nullptr);
  EXPECT_EQ(fromBytes(reader->readAll()), "hello");
  EXPECT_THROW((void)reader->writeToFile(), DataStreamError) << "a stream can only be consumed once";
}

TEST_F(DataStreamWriteToFileTest, CannotWriteFromTheStreamHandler) {
  const auto topic = uniqueTopic("to_file_handler");
  std::mutex mutex;
  std::condition_variable cv;
  std::optional<std::string> error;
  receiverRoom().registerByteStreamHandler(topic, [&](std::shared_ptr<ByteStreamReader> reader, const std::string&) {
    std::string message;
    try {
      (void)reader->writeToFile();
      message = "no error";
    } catch (const DataStreamError& e) {
      message = e.what();
    }
    const std::scoped_lock<std::mutex> lock(mutex);
    error = message;
    cv.notify_all();
  });
  FfiPeer peer(senderRoom());
  PeerByteOptions opts;
  opts.topic = topic;
  opts.name = "file.txt";
  ASSERT_TRUE(peer.sendBytes("hello", opts).ok);

  std::unique_lock<std::mutex> lock(mutex);
  ASSERT_TRUE(cv.wait_for(lock, kStreamTimeout, [&] { return error.has_value(); }));
  EXPECT_NE(error->find("SDK event thread"), std::string::npos)
      << "writeToFile on the event thread would deadlock and must throw: " << *error;
  lock.unlock();
  receiverRoom().unregisterByteStreamHandler(topic);
}

// A handler that ignores its reader releases the stream; later streams on the
// topic are unaffected.
TEST_F(DataStreamWriteToFileTest, IgnoredReaderDoesNotBlockLaterStreams) {
  const auto topic = uniqueTopic("ignored_reader");
  std::atomic<int> opened{0};
  receiverRoom().registerByteStreamHandler(topic, [&](std::shared_ptr<ByteStreamReader>, const std::string&) {
    ++opened; // Drop the reader without consuming it.
  });
  FfiPeer peer(senderRoom());
  PeerByteOptions opts;
  opts.topic = topic;
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(peer.sendBytes(randomText(40'000, static_cast<std::uint64_t>(i) + 1), opts).ok);
  }
  const auto deadline = std::chrono::steady_clock::now() + kStreamTimeout;
  while (opened < 3 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(20ms);
  }
  EXPECT_EQ(opened, 3);
  receiverRoom().unregisterByteStreamHandler(topic);

  ByteStreamCollector collector;
  collector.registerOn(receiverRoom(), topic);
  ASSERT_TRUE(peer.sendBytes("after", opts).ok);
  ASSERT_TRUE(collector.waitFor(1));
  EXPECT_EQ(collector.only().content, "after");
}

} // namespace livekit::test::ds
