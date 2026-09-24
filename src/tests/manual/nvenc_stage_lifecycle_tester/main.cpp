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

#include <cuda.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "NvEncoder/NvEncoderCuda.h"

namespace {

using namespace std::chrono_literals;

constexpr int kDefaultIterations = 6;
constexpr int kWidth = 640;
constexpr int kHeight = 360;
constexpr int kFrameRate = 15;
constexpr int kBitrate = 1'000'000;

enum class Stage {
  kContext,
  kSession,
  kInitialize,
  kReconfigure,
  kCopy,
  kMap,
  kEncode
};

struct Options {
  int iteration_count = kDefaultIterations;
  Stage stage = Stage::kEncode;
  bool worker_thread = false;
  bool all_worker = false;
  bool reuse_context = false;
};

class TestNvEncoderCuda final : public NvEncoderCuda {
 public:
  using NvEncoderCuda::NvEncoderCuda;

  void mapFirstInput() { MapResources(0); }
};

void requireSuccess(CUresult result, const char* operation) {
  if (result != CUDA_SUCCESS) {
    const char* name = nullptr;
    cuGetErrorName(result, &name);
    throw std::runtime_error(std::string(operation) + " failed: " +
                             (name != nullptr ? name : "unknown"));
  }
}

int parsePositive(const std::string& value) {
  std::size_t parsed_length = 0;
  const int parsed = std::stoi(value, &parsed_length);
  if (parsed <= 0 || parsed_length != value.size()) {
    throw std::runtime_error("iteration count must be a positive integer");
  }
  return parsed;
}

Stage parseStage(const std::string& value) {
  if (value == "context") return Stage::kContext;
  if (value == "session") return Stage::kSession;
  if (value == "initialize") return Stage::kInitialize;
  if (value == "reconfigure") return Stage::kReconfigure;
  if (value == "copy") return Stage::kCopy;
  if (value == "map") return Stage::kMap;
  if (value == "encode") return Stage::kEncode;
  throw std::runtime_error("invalid stage");
}

Options parseOptions(int argc, char* argv[]) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--worker-thread") {
      options.worker_thread = true;
      continue;
    }
    if (argument == "--all-worker") {
      options.all_worker = true;
      continue;
    }
    if (argument == "--reuse-context") {
      options.reuse_context = true;
      continue;
    }
    if (index + 1 >= argc) {
      throw std::runtime_error(
          "usage: tester [--stage STAGE] [--iterations N] [--worker-thread]");
    }
    const std::string value = argv[++index];
    if (argument == "--stage") {
      options.stage = parseStage(value);
    } else if (argument == "--iterations") {
      options.iteration_count = parsePositive(value);
    } else {
      throw std::runtime_error("unknown option: " + argument);
    }
  }
  return options;
}

const char* stageName(Stage stage) {
  switch (stage) {
    case Stage::kContext:
      return "context";
    case Stage::kSession:
      return "session";
    case Stage::kInitialize:
      return "initialize";
    case Stage::kReconfigure:
      return "reconfigure";
    case Stage::kCopy:
      return "copy";
    case Stage::kMap:
      return "map";
    case Stage::kEncode:
      return "encode";
  }
  return "unknown";
}

std::int64_t rssKib() {
  std::ifstream smaps("/proc/self/smaps_rollup");
  std::string line;
  while (std::getline(smaps, line)) {
    if (line.rfind("Rss:", 0) == 0) {
      return std::stoll(line.substr(4));
    }
  }
  throw std::runtime_error("cannot read process RSS");
}

void initializeEncoder(NvEncoder& encoder) {
  NV_ENC_INITIALIZE_PARAMS initialize_params = {};
  NV_ENC_CONFIG encode_config = {};
  initialize_params.version = NV_ENC_INITIALIZE_PARAMS_VER;
  encode_config.version = NV_ENC_CONFIG_VER;
  initialize_params.encodeConfig = &encode_config;

  encoder.CreateDefaultEncoderParams(
      &initialize_params, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P4_GUID,
      NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY);
  initialize_params.frameRateNum = kFrameRate;
  initialize_params.frameRateDen = 1;
  initialize_params.bufferFormat = NV_ENC_BUFFER_FORMAT_IYUV;
  encode_config.profileGUID = NV_ENC_H264_PROFILE_BASELINE_GUID;
  encode_config.gopLength = NVENC_INFINITE_GOPLENGTH;
  encode_config.frameIntervalP = 1;
  encode_config.encodeCodecConfig.h264Config.idrPeriod =
      NVENC_INFINITE_GOPLENGTH;
  encode_config.rcParams.version = NV_ENC_RC_PARAMS_VER;
  encode_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
  encode_config.rcParams.averageBitRate = kBitrate;
  encode_config.rcParams.vbvBufferSize = kBitrate * 5 / kFrameRate;
  encode_config.rcParams.vbvInitialDelay =
      encode_config.rcParams.vbvBufferSize;
  encoder.CreateEncoder(&initialize_params);
}

void copyFrame(CUcontext context, NvEncoder& encoder) {
  std::vector<std::uint8_t> frame(kWidth * kHeight * 3 / 2, 0x7f);
  const NvEncInputFrame* input = encoder.GetNextInputFrame();
  NvEncoderCuda::CopyToDeviceFrame(
      context, frame.data(), kWidth,
      reinterpret_cast<CUdeviceptr>(input->inputPtr), input->pitch, kWidth,
      kHeight, CU_MEMORYTYPE_HOST, input->bufferFormat, input->chromaOffsets,
      input->numChromaPlanes);
}

void exerciseEncoder(CUcontext context, Stage stage) {
  TestNvEncoderCuda encoder(context, kWidth, kHeight,
                            NV_ENC_BUFFER_FORMAT_IYUV, 0);
  if (stage >= Stage::kInitialize) initializeEncoder(encoder);
  if (stage >= Stage::kReconfigure &&
      !encoder.SetRates(kFrameRate, kBitrate)) {
    throw std::runtime_error("NVENC reconfiguration failed");
  }
  if (stage >= Stage::kCopy) copyFrame(context, encoder);
  if (stage == Stage::kMap) encoder.mapFirstInput();
  if (stage == Stage::kEncode) {
    for (int frame_index = 0; frame_index < kFrameRate; ++frame_index) {
      if (frame_index > 0) copyFrame(context, encoder);
      std::vector<std::vector<std::uint8_t>> packets;
      encoder.EncodeFrame(packets);
      if (packets.empty()) {
        throw std::runtime_error("encode returned no packet");
      }
    }
  }
  encoder.DestroyEncoder();
}

void runIteration(CUdevice device, Stage stage, bool worker_thread) {
  CUcontext context = nullptr;
#if CUDA_VERSION >= 13000
  requireSuccess(cuCtxCreate(&context, nullptr, 0, device), "cuCtxCreate");
#else
  requireSuccess(cuCtxCreate(&context, 0, device), "cuCtxCreate");
#endif

  try {
    if (stage != Stage::kContext) {
      if (worker_thread) {
        std::exception_ptr worker_error;
        std::thread worker([&] {
          try {
            requireSuccess(cuCtxSetCurrent(context), "cuCtxSetCurrent");
            exerciseEncoder(context, stage);
          } catch (...) {
            worker_error = std::current_exception();
          }
        });
        worker.join();
        if (worker_error) {
          std::rethrow_exception(worker_error);
        }
      } else {
        exerciseEncoder(context, stage);
      }
    }
  } catch (...) {
    cuCtxDestroy(context);
    throw;
  }

  requireSuccess(cuCtxDestroy(context), "cuCtxDestroy");
}

void run(const Options& options) {
  requireSuccess(cuInit(0), "cuInit");
  CUdevice device = 0;
  requireSuccess(cuDeviceGet(&device, 0), "cuDeviceGet");

  if (options.reuse_context &&
      (options.worker_thread || options.all_worker ||
       options.stage == Stage::kContext)) {
    throw std::runtime_error(
        "--reuse-context requires a codec stage and no worker mode");
  }

  CUcontext reused_context = nullptr;
  if (options.reuse_context) {
#if CUDA_VERSION >= 13000
    requireSuccess(cuCtxCreate(&reused_context, nullptr, 0, device),
                   "cuCtxCreate");
#else
    requireSuccess(cuCtxCreate(&reused_context, 0, device), "cuCtxCreate");
#endif
  }

  const std::int64_t initial_rss = rssKib();
  std::cout << "stage,iteration,rss_mib,delta_mib\n";
  try {
    for (int iteration = 1; iteration <= options.iteration_count; ++iteration) {
      if (options.reuse_context) {
        exerciseEncoder(reused_context, options.stage);
      } else if (options.all_worker) {
        std::exception_ptr worker_error;
        std::thread worker([&] {
          try {
            runIteration(device, options.stage, false);
          } catch (...) {
            worker_error = std::current_exception();
          }
        });
        worker.join();
        if (worker_error) {
          std::rethrow_exception(worker_error);
        }
      } else {
        runIteration(device, options.stage, options.worker_thread);
      }
      std::this_thread::sleep_for(250ms);
      const std::int64_t rss = rssKib();
      std::cout
          << stageName(options.stage)
          << (options.reuse_context
                  ? "-reuse-context"
                  : (options.all_worker
                         ? "-all-worker"
                         : (options.worker_thread ? "-worker" : "")))
          << ',' << iteration << ',' << rss / 1024.0 << ','
          << (rss - initial_rss) / 1024.0 << '\n';
    }
  } catch (...) {
    if (reused_context) cuCtxDestroy(reused_context);
    throw;
  }
  if (reused_context) {
    requireSuccess(cuCtxDestroy(reused_context), "cuCtxDestroy");
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    run(parseOptions(argc, argv));
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "NVENC stage lifecycle tester failed: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
