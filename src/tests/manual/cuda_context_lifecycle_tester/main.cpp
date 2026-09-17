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

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr int kDefaultIterations = 10;
constexpr const char* kUsage =
    "usage: livekit_cuda_context_lifecycle_tester [--interactive] "
    "[--iterations] N";

struct Options {
  int iteration_count = kDefaultIterations;
  bool interactive = false;
};

int parseIterationCount(const char* value) {
  try {
    const int parsed = std::stoi(value);
    if (parsed <= 0) {
      throw std::runtime_error("iteration count must be greater than zero");
    }
    return parsed;
  } catch (const std::invalid_argument&) {
    throw std::runtime_error("iteration count must be an integer");
  } catch (const std::out_of_range&) {
    throw std::runtime_error("iteration count is out of range");
  }
}

Options parseOptions(int argc, char* argv[]) {
  Options options;
  bool saw_iteration_count = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--interactive") {
      options.interactive = true;
      continue;
    }

    const char* iteration_value = nullptr;
    if (arg == "--iterations") {
      if (i + 1 >= argc) {
        throw std::runtime_error(kUsage);
      }
      iteration_value = argv[++i];
    } else if (!arg.empty() && arg[0] != '-') {
      iteration_value = argv[i];
    } else {
      throw std::runtime_error(kUsage);
    }

    if (saw_iteration_count) {
      throw std::runtime_error(kUsage);
    }
    options.iteration_count = parseIterationCount(iteration_value);
    saw_iteration_count = true;
  }

  return options;
}

void requireSuccess(CUresult result, const char* operation) {
  if (result != CUDA_SUCCESS) {
    throw std::runtime_error(std::string(operation) +
                             " failed with CUDA result " +
                             std::to_string(static_cast<int>(result)));
  }
}

void waitForEnter(bool interactive, const char* prompt) {
  if (!interactive) {
    return;
  }
  std::cout << prompt << std::endl;
  std::cin.get();
}

void logStep(bool interactive, const std::string& message) {
  if (!interactive) {
    return;
  }
  std::cout << message << std::endl;
}

void run(const Options& options) {
  if (!options.interactive) {
    std::cout << "Running " << options.iteration_count << " iterations automatically" << std::endl;
  }
  requireSuccess(cuInit(0), "cuInit");

  CUdevice device = 0;
  requireSuccess(cuDeviceGet(&device, 0), "cuDeviceGet");

  for (int iteration = 1; iteration <= options.iteration_count; ++iteration) {
    logStep(options.interactive,
            "################ iteration " + std::to_string(iteration) + '/' +
                std::to_string(options.iteration_count) +
                " starting ################");

    CUcontext context = nullptr;
    waitForEnter(options.interactive, "Press Enter to create context");
    requireSuccess(cuCtxCreate(&context, 0, device), "cuCtxCreate");
    logStep(options.interactive, "!!! Context created !!!");

    waitForEnter(options.interactive, "Press Enter to destroy context");
    requireSuccess(cuCtxDestroy(context), "cuCtxDestroy");
    logStep(options.interactive, "!!! Context destroyed !!!");
    logStep(options.interactive,
            "################ iteration " + std::to_string(iteration) + '/' +
                std::to_string(options.iteration_count) +
                " complete ################");
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    run(parseOptions(argc, argv));
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return EXIT_FAILURE;
  }
}
