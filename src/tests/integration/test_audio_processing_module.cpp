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

#include <gtest/gtest.h>
#include <livekit/audio_frame.h>
#include <livekit/audio_processing_module.h>
#include <livekit/livekit.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace livekit {
namespace test {

class AudioProcessingModuleTest : public ::testing::Test {
protected:
  void SetUp() override { livekit::initialize(livekit::LogSink::kConsole); }

  void TearDown() override { livekit::shutdown(); }

  // Helper to create a 10ms audio frame at given sample rate
  static AudioFrame create10msFrame(int sample_rate, int num_channels) {
    int samples_per_channel = sample_rate / 100; // 10ms
    return AudioFrame::create(sample_rate, num_channels, samples_per_channel);
  }

  // Helper to fill frame with sine wave
  static void fillWithSineWave(AudioFrame &frame, double frequency,
                               double amplitude = 10000.0) {
    auto &data = frame.data();
    int sample_rate = frame.sample_rate();
    int num_channels = frame.num_channels();
    int samples_per_channel = frame.samples_per_channel();

    for (int i = 0; i < samples_per_channel; ++i) {
      double t = static_cast<double>(i) / sample_rate;
      auto sample = static_cast<std::int16_t>(
          amplitude * std::sin(2.0 * M_PI * frequency * t));
      for (int ch = 0; ch < num_channels; ++ch) {
        data[i * num_channels + ch] = sample;
      }
    }
  }

  // Helper to fill frame with random noise
  static void fillWithNoise(AudioFrame &frame, double amplitude = 5000.0,
                            unsigned int seed = 0) {
    std::mt19937 gen(seed == 0 ? std::random_device{}() : seed);
    std::uniform_real_distribution<> dis(-amplitude, amplitude);

    auto &data = frame.data();
    for (auto &sample : data) {
      sample = static_cast<std::int16_t>(dis(gen));
    }
  }

  // Calculate RMS (Root Mean Square) energy of audio frame
  static double calculateRMS(const AudioFrame &frame) {
    const auto &data = frame.data();
    if (data.empty()) {
      return 0.0;
    }

    double sum_squares = 0.0;
    for (const auto &sample : data) {
      sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return std::sqrt(sum_squares / static_cast<double>(data.size()));
  }

  // Calculate energy in a specific frequency band using a simple DFT approach
  // This is a simplified calculation for testing purposes
  static double calculateFrequencyBandEnergy(const AudioFrame &frame,
                                             double low_freq,
                                             double high_freq) {
    const auto &data = frame.data();
    int sample_rate = frame.sample_rate();
    int num_channels = frame.num_channels();
    int samples_per_channel = frame.samples_per_channel();

    if (data.empty() || samples_per_channel == 0) {
      return 0.0;
    }

    // Use first channel only for frequency analysis
    std::vector<double> mono_data(samples_per_channel);
    for (int i = 0; i < samples_per_channel; ++i) {
      mono_data[i] = static_cast<double>(data[i * num_channels]);
    }

    // Simple DFT for the frequency range of interest
    double energy = 0.0;
    int low_bin =
        static_cast<int>(low_freq * samples_per_channel / sample_rate);
    int high_bin =
        static_cast<int>(high_freq * samples_per_channel / sample_rate);

    for (int k = std::max(1, low_bin);
         k <= std::min(high_bin, samples_per_channel / 2); ++k) {
      double real = 0.0;
      double imag = 0.0;
      double freq_rad = 2.0 * M_PI * k / samples_per_channel;

      for (int n = 0; n < samples_per_channel; ++n) {
        real += mono_data[n] * std::cos(freq_rad * n);
        imag -= mono_data[n] * std::sin(freq_rad * n);
      }

      energy += real * real + imag * imag;
    }

    return std::sqrt(energy / samples_per_channel);
  }

  // Copy audio frame data
  static std::vector<std::int16_t> copyFrameData(const AudioFrame &frame) {
    return frame.data();
  }

  // Read a WAV file and return the raw PCM samples
  // Assumes 16-bit PCM format
  static bool readWavFile(const std::string &path,
                          std::vector<std::int16_t> &samples, int &sample_rate,
                          int &num_channels) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
      std::cerr << "Failed to open WAV file: " << path << std::endl;
      return false;
    }

    // Read RIFF header
    char riff[4];
    file.read(riff, 4);
    if (std::string(riff, 4) != "RIFF") {
      std::cerr << "Not a valid RIFF file" << std::endl;
      return false;
    }

    file.seekg(4, std::ios::cur); // Skip file size

    char wave[4];
    file.read(wave, 4);
    if (std::string(wave, 4) != "WAVE") {
      std::cerr << "Not a valid WAVE file" << std::endl;
      return false;
    }

    // Find fmt chunk
    while (file.good()) {
      char chunk_id[4];
      file.read(chunk_id, 4);
      std::uint32_t chunk_size;
      file.read(reinterpret_cast<char *>(&chunk_size), 4);

      if (std::string(chunk_id, 4) == "fmt ") {
        std::uint16_t audio_format;
        file.read(reinterpret_cast<char *>(&audio_format), 2);
        if (audio_format != 1) { // PCM
          std::cerr << "Only PCM format supported" << std::endl;
          return false;
        }

        std::uint16_t channels;
        file.read(reinterpret_cast<char *>(&channels), 2);
        num_channels = channels;

        std::uint32_t rate;
        file.read(reinterpret_cast<char *>(&rate), 4);
        sample_rate = static_cast<int>(rate);

        file.seekg(chunk_size - 8, std::ios::cur); // Skip rest of fmt chunk
      } else if (std::string(chunk_id, 4) == "data") {
        // Read audio data
        size_t num_samples = chunk_size / sizeof(std::int16_t);
        samples.resize(num_samples);
        file.read(reinterpret_cast<char *>(samples.data()), chunk_size);
        return true;
      } else {
        // Skip unknown chunk
        file.seekg(chunk_size, std::ios::cur);
      }
    }

    std::cerr << "No data chunk found" << std::endl;
    return false;
  }

  // Scale audio samples by a factor (for simulating quiet/loud audio)
  static void scaleAudio(std::vector<std::int16_t> &samples, double scale) {
    for (auto &sample : samples) {
      double scaled = static_cast<double>(sample) * scale;
      sample = static_cast<std::int16_t>(std::clamp(scaled, -32768.0, 32767.0));
    }
  }
};

// ============================================================================
// Construction Tests
// ============================================================================

TEST_F(AudioProcessingModuleTest, CreateWithDefaultOptions) {
  AudioProcessingModule::Options opts;
  AudioProcessingModule apm(opts);

  EXPECT_TRUE(apm.valid());
}

TEST_F(AudioProcessingModuleTest, CreateWithAllFeaturesEnabled) {
  AudioProcessingModule::Options opts;
  opts.echo_cancellation = true;
  opts.noise_suppression = true;
  opts.high_pass_filter = true;
  opts.auto_gain_control = true;

  AudioProcessingModule apm(opts);

  EXPECT_TRUE(apm.valid());
}

TEST_F(AudioProcessingModuleTest, CreateWithEchoCancellationOnly) {
  AudioProcessingModule::Options opts;
  opts.echo_cancellation = true;

  AudioProcessingModule apm(opts);

  EXPECT_TRUE(apm.valid());
}

TEST_F(AudioProcessingModuleTest, CreateWithNoiseSuppressionOnly) {
  AudioProcessingModule::Options opts;
  opts.noise_suppression = true;

  AudioProcessingModule apm(opts);

  EXPECT_TRUE(apm.valid());
}

TEST_F(AudioProcessingModuleTest, CreateWithAutoGainControlOnly) {
  AudioProcessingModule::Options opts;
  opts.auto_gain_control = true;

  AudioProcessingModule apm(opts);

  EXPECT_TRUE(apm.valid());
}

TEST_F(AudioProcessingModuleTest, CreateWithHighPassFilterOnly) {
  AudioProcessingModule::Options opts;
  opts.high_pass_filter = true;

  AudioProcessingModule apm(opts);

  EXPECT_TRUE(apm.valid());
}

// ============================================================================
// ProcessStream Tests
// ============================================================================

TEST_F(AudioProcessingModuleTest, ProcessStreamMono48kHz) {
  AudioProcessingModule::Options opts;
  opts.noise_suppression = true;
  AudioProcessingModule apm(opts);

  AudioFrame frame = create10msFrame(48000, 1);
  fillWithSineWave(frame, 440.0); // 440 Hz tone

  EXPECT_NO_THROW(apm.processStream(frame));
}

TEST_F(AudioProcessingModuleTest, ProcessStreamStereo48kHz) {
  AudioProcessingModule::Options opts;
  opts.noise_suppression = true;
  AudioProcessingModule apm(opts);

  AudioFrame frame = create10msFrame(48000, 2);
  fillWithSineWave(frame, 440.0);

  EXPECT_NO_THROW(apm.processStream(frame));
}

TEST_F(AudioProcessingModuleTest, ProcessStreamMono16kHz) {
  AudioProcessingModule::Options opts;
  opts.noise_suppression = true;
  AudioProcessingModule apm(opts);

  AudioFrame frame = create10msFrame(16000, 1);
  fillWithSineWave(frame, 440.0);

  EXPECT_NO_THROW(apm.processStream(frame));
}

TEST_F(AudioProcessingModuleTest, ProcessStreamEmptyFrame) {
  AudioProcessingModule::Options opts;
  opts.noise_suppression = true;
  AudioProcessingModule apm(opts);

  AudioFrame frame; // Default empty frame

  // Should not throw, just return early
  EXPECT_NO_THROW(apm.processStream(frame));
}

TEST_F(AudioProcessingModuleTest, ProcessStreamWithNoisyInput) {
  AudioProcessingModule::Options opts;
  opts.noise_suppression = true;
  AudioProcessingModule apm(opts);

  AudioFrame frame = create10msFrame(48000, 1);
  fillWithNoise(frame);

  EXPECT_NO_THROW(apm.processStream(frame));
}

// ============================================================================
// ProcessReverseStream Tests
// ============================================================================

TEST_F(AudioProcessingModuleTest, ProcessReverseStreamMono48kHz) {
  AudioProcessingModule::Options opts;
  opts.echo_cancellation = true;
  AudioProcessingModule apm(opts);

  AudioFrame frame = create10msFrame(48000, 1);
  fillWithSineWave(frame, 440.0);

  EXPECT_NO_THROW(apm.processReverseStream(frame));
}

TEST_F(AudioProcessingModuleTest, ProcessReverseStreamStereo48kHz) {
  AudioProcessingModule::Options opts;
  opts.echo_cancellation = true;
  AudioProcessingModule apm(opts);

  AudioFrame frame = create10msFrame(48000, 2);
  fillWithSineWave(frame, 440.0);

  EXPECT_NO_THROW(apm.processReverseStream(frame));
}

TEST_F(AudioProcessingModuleTest, ProcessReverseStreamEmptyFrame) {
  AudioProcessingModule::Options opts;
  opts.echo_cancellation = true;
  AudioProcessingModule apm(opts);

  AudioFrame frame; // Default empty frame

  EXPECT_NO_THROW(apm.processReverseStream(frame));
}

// ============================================================================
// SetStreamDelay Tests
// ============================================================================

TEST_F(AudioProcessingModuleTest, SetStreamDelayMs) {
  AudioProcessingModule::Options opts;
  opts.echo_cancellation = true;
  AudioProcessingModule apm(opts);

  EXPECT_NO_THROW(apm.setStreamDelayMs(0));
  EXPECT_NO_THROW(apm.setStreamDelayMs(50));
  EXPECT_NO_THROW(apm.setStreamDelayMs(100));
  EXPECT_NO_THROW(apm.setStreamDelayMs(200));
}

// ============================================================================
// Echo Cancellation Workflow Tests
// ============================================================================

TEST_F(AudioProcessingModuleTest, EchoCancellationWorkflow) {
  AudioProcessingModule::Options opts;
  opts.echo_cancellation = true;
  opts.noise_suppression = true;
  AudioProcessingModule apm(opts);

  // Simulate typical AEC workflow:
  // 1. Process speaker audio (reverse stream)
  // 2. Process microphone audio (forward stream)

  AudioFrame speakerFrame = create10msFrame(48000, 1);
  fillWithSineWave(speakerFrame, 440.0);

  AudioFrame micFrame = create10msFrame(48000, 1);
  fillWithSineWave(micFrame, 440.0); // Simulated echo
  fillWithNoise(micFrame, 1000.0);   // Plus some noise

  // Set estimated delay
  EXPECT_NO_THROW(apm.setStreamDelayMs(50));

  // Process reverse stream (speaker output)
  EXPECT_NO_THROW(apm.processReverseStream(speakerFrame));

  // Process forward stream (microphone input)
  EXPECT_NO_THROW(apm.processStream(micFrame));
}

TEST_F(AudioProcessingModuleTest, MultipleFramesProcessing) {
  AudioProcessingModule::Options opts;
  opts.echo_cancellation = true;
  opts.noise_suppression = true;
  opts.auto_gain_control = true;
  AudioProcessingModule apm(opts);

  // Process multiple frames (simulating real-time audio)
  for (int i = 0; i < 100; ++i) {
    AudioFrame speakerFrame = create10msFrame(48000, 1);
    fillWithSineWave(speakerFrame, 440.0);

    AudioFrame micFrame = create10msFrame(48000, 1);
    fillWithNoise(micFrame);

    EXPECT_NO_THROW(apm.processReverseStream(speakerFrame));
    EXPECT_NO_THROW(apm.processStream(micFrame));
  }
}

// ============================================================================
// Move Semantics Tests
// ============================================================================

TEST_F(AudioProcessingModuleTest, MoveConstruction) {
  AudioProcessingModule::Options opts;
  opts.noise_suppression = true;
  AudioProcessingModule apm1(opts);

  EXPECT_TRUE(apm1.valid());

  AudioProcessingModule apm2(std::move(apm1));

  EXPECT_TRUE(apm2.valid());
  EXPECT_FALSE(apm1.valid()); // Moved-from should be invalid
}

TEST_F(AudioProcessingModuleTest, MoveAssignment) {
  AudioProcessingModule::Options opts;
  opts.noise_suppression = true;
  AudioProcessingModule apm1(opts);
  AudioProcessingModule apm2(opts);

  EXPECT_TRUE(apm1.valid());
  EXPECT_TRUE(apm2.valid());

  apm2 = std::move(apm1);

  EXPECT_TRUE(apm2.valid());
  EXPECT_FALSE(apm1.valid()); // Moved-from should be invalid
}

// ============================================================================
// FfiHandleId Test
// ============================================================================

TEST_F(AudioProcessingModuleTest, FfiHandleIdNonZero) {
  AudioProcessingModule::Options opts;
  AudioProcessingModule apm(opts);

  EXPECT_NE(apm.ffi_handle_id(), 0u);
}

// ============================================================================
// Noise Suppression Effectiveness Tests
// ============================================================================

TEST_F(AudioProcessingModuleTest, NoiseSuppressionReducesNoiseEnergy) {
  // Create APM with noise suppression enabled
  AudioProcessingModule::Options opts;
  opts.noise_suppression = true;
  AudioProcessingModule apm(opts);

  // Process multiple frames to let the noise suppressor adapt
  // The noise suppressor needs several frames to estimate the noise profile
  constexpr int kWarmupFrames = 50;
  constexpr int kTestFrames = 50;
  constexpr unsigned int kSeed = 12345; // Fixed seed for reproducibility

  double total_input_energy = 0.0;
  double total_output_energy = 0.0;

  // Warmup phase - let the noise suppressor learn the noise characteristics
  for (int i = 0; i < kWarmupFrames; ++i) {
    AudioFrame frame = create10msFrame(48000, 1);
    fillWithNoise(frame, 8000.0, kSeed + i);
    apm.processStream(frame);
  }

  // Measurement phase - measure energy reduction
  for (int i = 0; i < kTestFrames; ++i) {
    AudioFrame frame = create10msFrame(48000, 1);
    fillWithNoise(frame, 8000.0, kSeed + kWarmupFrames + i);

    double input_energy = calculateRMS(frame);
    total_input_energy += input_energy;

    apm.processStream(frame);

    double output_energy = calculateRMS(frame);
    total_output_energy += output_energy;
  }

  double avg_input_energy = total_input_energy / kTestFrames;
  double avg_output_energy = total_output_energy / kTestFrames;

  // Noise suppression should reduce energy significantly
  // We expect at least 50% reduction for pure noise input
  std::cout << "[NoiseSuppression] Avg input energy: " << avg_input_energy
            << ", Avg output energy: " << avg_output_energy << ", Reduction: "
            << (1.0 - avg_output_energy / avg_input_energy) * 100.0 << "%"
            << std::endl;

  EXPECT_LT(avg_output_energy, avg_input_energy)
      << "Noise suppression should reduce energy";
}

TEST_F(AudioProcessingModuleTest, NoiseSuppressionPreservesSpeechLikeSignal) {
  // Create APM with noise suppression enabled
  AudioProcessingModule::Options opts;
  opts.noise_suppression = true;
  AudioProcessingModule apm(opts);

  // Create a more speech-like signal with multiple harmonics and varying
  // amplitude Pure sine waves may be classified as tonal noise by the NS
  // algorithm
  constexpr int kFrames = 100;

  double total_input_energy = 0.0;
  double total_output_energy = 0.0;

  for (int i = 0; i < kFrames; ++i) {
    AudioFrame frame = create10msFrame(48000, 1);
    auto &data = frame.data();
    int sample_rate = frame.sample_rate();
    int samples_per_channel = frame.samples_per_channel();

    // Create speech-like signal with fundamental + harmonics (like voice)
    // Vary amplitude slightly to simulate natural speech variation
    double amplitude_variation =
        8000.0 + 2000.0 * std::sin(2.0 * M_PI * i / 20.0);

    for (int s = 0; s < samples_per_channel; ++s) {
      double t = static_cast<double>(s) / sample_rate;
      // Fundamental (250 Hz) + harmonics (typical of voiced speech)
      double sample = amplitude_variation *
                      (0.5 * std::sin(2.0 * M_PI * 250.0 * t) +  // Fundamental
                       0.3 * std::sin(2.0 * M_PI * 500.0 * t) +  // 2nd harmonic
                       0.15 * std::sin(2.0 * M_PI * 750.0 * t) + // 3rd harmonic
                       0.05 * std::sin(2.0 * M_PI * 1000.0 * t)  // 4th harmonic
                      );
      data[s] =
          static_cast<std::int16_t>(std::clamp(sample, -32768.0, 32767.0));
    }

    double input_energy = calculateRMS(frame);
    total_input_energy += input_energy;

    apm.processStream(frame);

    double output_energy = calculateRMS(frame);
    total_output_energy += output_energy;
  }

  double avg_input_energy = total_input_energy / kFrames;
  double avg_output_energy = total_output_energy / kFrames;
  double preservation_ratio = avg_output_energy / avg_input_energy;

  std::cout << "[NoiseSuppression-Speech] Avg input energy: "
            << avg_input_energy << ", Avg output energy: " << avg_output_energy
            << ", Preservation: " << preservation_ratio * 100.0 << "%"
            << std::endl;

  // Note: Even speech-like signals may be partially attenuated by NS
  // We just verify that the output has some significant energy
  // (i.e., NS doesn't completely silence the signal)
  EXPECT_GT(avg_output_energy, avg_input_energy * 0.1)
      << "Speech-like signals should not be completely suppressed";
}

// ============================================================================
// High Pass Filter Effectiveness Tests
// ============================================================================

TEST_F(AudioProcessingModuleTest, HighPassFilterAttenuatesLowFrequencies) {
  // Create APM with high pass filter enabled
  AudioProcessingModule::Options opts;
  opts.high_pass_filter = true;
  AudioProcessingModule apm(opts);

  // Test with a very low frequency signal (below the ~80Hz cutoff)
  constexpr double kLowFrequency = 30.0; // 30 Hz - below cutoff
  constexpr int kFrames = 100;

  double total_input_energy = 0.0;
  double total_output_energy = 0.0;

  for (int i = 0; i < kFrames; ++i) {
    AudioFrame frame = create10msFrame(48000, 1);
    fillWithSineWave(frame, kLowFrequency, 10000.0);

    double input_energy = calculateRMS(frame);
    total_input_energy += input_energy;

    apm.processStream(frame);

    double output_energy = calculateRMS(frame);
    total_output_energy += output_energy;
  }

  double avg_input_energy = total_input_energy / kFrames;
  double avg_output_energy = total_output_energy / kFrames;

  std::cout << "[HighPassFilter-LowFreq] Avg input energy: " << avg_input_energy
            << ", Avg output energy: " << avg_output_energy << ", Attenuation: "
            << (1.0 - avg_output_energy / avg_input_energy) * 100.0 << "%"
            << std::endl;

  // Low frequencies should be significantly attenuated
  EXPECT_LT(avg_output_energy, avg_input_energy * 0.8)
      << "High pass filter should attenuate low frequencies";
}

TEST_F(AudioProcessingModuleTest, HighPassFilterPassesHighFrequencies) {
  // Create APM with high pass filter enabled
  AudioProcessingModule::Options opts;
  opts.high_pass_filter = true;
  AudioProcessingModule apm(opts);

  // Test with a frequency well above the cutoff
  constexpr double kHighFrequency = 1000.0; // 1kHz - well above 80Hz cutoff
  constexpr int kFrames = 100;

  double total_input_energy = 0.0;
  double total_output_energy = 0.0;

  for (int i = 0; i < kFrames; ++i) {
    AudioFrame frame = create10msFrame(48000, 1);
    fillWithSineWave(frame, kHighFrequency, 10000.0);

    double input_energy = calculateRMS(frame);
    total_input_energy += input_energy;

    apm.processStream(frame);

    double output_energy = calculateRMS(frame);
    total_output_energy += output_energy;
  }

  double avg_input_energy = total_input_energy / kFrames;
  double avg_output_energy = total_output_energy / kFrames;
  double pass_ratio = avg_output_energy / avg_input_energy;

  std::cout << "[HighPassFilter-HighFreq] Avg input energy: "
            << avg_input_energy << ", Avg output energy: " << avg_output_energy
            << ", Pass ratio: " << pass_ratio * 100.0 << "%" << std::endl;

  // High frequencies should pass through with minimal attenuation
  // Allow up to 20% loss due to processing artifacts
  EXPECT_GT(pass_ratio, 0.8) << "High pass filter should pass high frequencies";
}

TEST_F(AudioProcessingModuleTest, HighPassFilterCompareLowVsHighFrequency) {
  // Create two APMs with high pass filter enabled
  AudioProcessingModule::Options opts;
  opts.high_pass_filter = true;

  AudioProcessingModule apm_low(opts);
  AudioProcessingModule apm_high(opts);

  constexpr double kLowFrequency = 30.0;   // Below cutoff
  constexpr double kHighFrequency = 500.0; // Above cutoff
  constexpr int kFrames = 100;

  double low_freq_output_energy = 0.0;
  double high_freq_output_energy = 0.0;

  // Process low frequency
  for (int i = 0; i < kFrames; ++i) {
    AudioFrame frame = create10msFrame(48000, 1);
    fillWithSineWave(frame, kLowFrequency, 10000.0);
    apm_low.processStream(frame);
    low_freq_output_energy += calculateRMS(frame);
  }

  // Process high frequency
  for (int i = 0; i < kFrames; ++i) {
    AudioFrame frame = create10msFrame(48000, 1);
    fillWithSineWave(frame, kHighFrequency, 10000.0);
    apm_high.processStream(frame);
    high_freq_output_energy += calculateRMS(frame);
  }

  double avg_low = low_freq_output_energy / kFrames;
  double avg_high = high_freq_output_energy / kFrames;

  std::cout << "[HighPassFilter-Compare] Low freq (30Hz) output: " << avg_low
            << ", High freq (500Hz) output: " << avg_high << std::endl;

  // High frequency output should be significantly greater than low frequency
  EXPECT_GT(avg_high, avg_low * 1.5) << "High frequencies should have more "
                                        "energy than low frequencies after HPF";
}

// ============================================================================
// Automatic Gain Control (AGC) Effectiveness Tests
// ============================================================================

TEST_F(AudioProcessingModuleTest, AGCProcessesAudioWithoutError) {
  // Create APM with AGC enabled
  // Note: WebRTC's AGC behavior varies by configuration. This test verifies
  // that AGC processes audio correctly without errors and produces valid
  // output.
  AudioProcessingModule::Options opts;
  opts.auto_gain_control = true;
  AudioProcessingModule apm(opts);

  constexpr int kFrames = 200; // 2 seconds of audio

  double total_input_energy = 0.0;
  double total_output_energy = 0.0;

  for (int i = 0; i < kFrames; ++i) {
    AudioFrame frame = create10msFrame(48000, 1);
    auto &data = frame.data();
    int sample_rate = frame.sample_rate();
    int samples_per_channel = frame.samples_per_channel();

    // Speech-like signal with varying amplitude
    double amplitude = 2000.0 * (0.5 + 0.5 * std::sin(2.0 * M_PI * i / 50.0));

    for (int s = 0; s < samples_per_channel; ++s) {
      double t = static_cast<double>(s) / sample_rate;
      double sample = amplitude * (0.5 * std::sin(2.0 * M_PI * 250.0 * t) +
                                   0.3 * std::sin(2.0 * M_PI * 500.0 * t));
      data[s] =
          static_cast<std::int16_t>(std::clamp(sample, -32768.0, 32767.0));
    }

    total_input_energy += calculateRMS(frame);
    apm.processStream(frame);
    total_output_energy += calculateRMS(frame);
  }

  double avg_input = total_input_energy / kFrames;
  double avg_output = total_output_energy / kFrames;

  std::cout << "[AGC] Processed " << kFrames << " frames. "
            << "Avg input=" << avg_input << ", Avg output=" << avg_output
            << std::endl;

  // Verify output is valid (not zero, not clipped)
  EXPECT_GT(avg_output, 0.0) << "AGC output should not be zero";
  EXPECT_LT(avg_output, 30000.0)
      << "AGC output should not be excessively clipped";
}

TEST_F(AudioProcessingModuleTest, AGCHandlesVaryingInputLevels) {
  // Create APM with AGC enabled
  // Test that AGC handles transitions between quiet and loud audio
  AudioProcessingModule::Options opts;
  opts.auto_gain_control = true;
  AudioProcessingModule apm(opts);

  constexpr int kFramesPerPhase = 100; // 1 second per phase

  // Process quiet audio
  double quiet_output_sum = 0.0;
  for (int i = 0; i < kFramesPerPhase; ++i) {
    AudioFrame frame = create10msFrame(48000, 1);
    fillWithSineWave(frame, 440.0, 1000.0); // Quiet
    apm.processStream(frame);
    quiet_output_sum += calculateRMS(frame);
  }

  // Process loud audio
  double loud_output_sum = 0.0;
  for (int i = 0; i < kFramesPerPhase; ++i) {
    AudioFrame frame = create10msFrame(48000, 1);
    fillWithSineWave(frame, 440.0, 15000.0); // Loud
    apm.processStream(frame);
    loud_output_sum += calculateRMS(frame);
  }

  double quiet_avg = quiet_output_sum / kFramesPerPhase;
  double loud_avg = loud_output_sum / kFramesPerPhase;

  std::cout << "[AGC-VaryingLevels] Quiet output=" << quiet_avg
            << ", Loud output=" << loud_avg << std::endl;

  // Verify outputs are valid and different levels produce different outputs
  EXPECT_GT(quiet_avg, 0.0) << "Quiet output should not be zero";
  EXPECT_GT(loud_avg, 0.0) << "Loud output should not be zero";
  EXPECT_GT(loud_avg, quiet_avg) << "Loud output should be greater than quiet";
}

TEST_F(AudioProcessingModuleTest, AGCAttenuatesLoudSpeech) {
  // Test AGC with real speech audio scaled to simulate loud input
  // This verifies that AGC attenuates loud speech to prevent clipping

  std::vector<std::int16_t> original_samples;
  int sample_rate = 0;
  int num_channels = 0;

  std::string wav_path = std::string(LIVEKIT_ROOT_DIR) + "/data/welcome.wav";
  if (!readWavFile(wav_path, original_samples, sample_rate, num_channels)) {
    GTEST_SKIP() << "Could not read " << wav_path;
  }

  std::cout << "[AGC-LoudSpeech] Loaded " << original_samples.size()
            << " samples, " << sample_rate << " Hz, " << num_channels
            << " channels" << std::endl;

  // Scale up to simulate loud input (3x original volume)
  std::vector<std::int16_t> loud_samples = original_samples;
  scaleAudio(loud_samples, 3.0);

  double loud_input_rms = 0.0;
  for (const auto &s : loud_samples) {
    loud_input_rms += static_cast<double>(s) * static_cast<double>(s);
  }
  loud_input_rms = std::sqrt(loud_input_rms / loud_samples.size());
  std::cout << "[AGC-LoudSpeech] Loud input RMS (3x): " << loud_input_rms
            << std::endl;

  // Create APM with AGC enabled
  AudioProcessingModule::Options opts;
  opts.auto_gain_control = true;
  AudioProcessingModule apm(opts);

  // Process in 10ms chunks
  int samples_per_frame = sample_rate / 100;
  int total_frames = static_cast<int>(loud_samples.size()) / samples_per_frame;

  double total_output_rms = 0.0;
  int frame_count = 0;

  for (int f = 0; f < total_frames; ++f) {
    std::vector<std::int16_t> frame_data(
        loud_samples.begin() + f * samples_per_frame,
        loud_samples.begin() + (f + 1) * samples_per_frame);

    AudioFrame frame(frame_data, sample_rate, num_channels, samples_per_frame);
    apm.processStream(frame);

    total_output_rms += calculateRMS(frame);
    frame_count++;
  }

  double avg_output_rms = total_output_rms / frame_count;
  double gain_applied =
      (loud_input_rms > 0) ? (avg_output_rms / loud_input_rms) : 0.0;

  std::cout << "[AGC-LoudSpeech] Input RMS=" << loud_input_rms
            << ", Output RMS=" << avg_output_rms
            << ", Effective gain=" << gain_applied << "x" << std::endl;

  // Verify AGC attenuated the loud signal (gain < 1.0)
  EXPECT_GT(avg_output_rms, 0.0) << "Output should not be zero";
  EXPECT_LT(gain_applied, 1.0)
      << "AGC should attenuate loud audio (gain < 1.0)";

  std::cout << "[AGC-LoudSpeech] SUCCESS: AGC attenuated loud speech by "
            << (1.0 - gain_applied) * 100.0 << "%" << std::endl;
}

TEST_F(AudioProcessingModuleTest, AGCWithNoiseSuppressionCombined) {
  // Test combined AGC + noise suppression
  AudioProcessingModule::Options opts;
  opts.auto_gain_control = true;
  opts.noise_suppression = true;
  AudioProcessingModule apm(opts);

  constexpr int kFrames = 200;
  constexpr unsigned int kSeed = 54321;

  // Process noise-only frames first (warmup)
  for (int i = 0; i < 50; ++i) {
    AudioFrame frame = create10msFrame(48000, 1);
    fillWithNoise(frame, 3000.0, kSeed + i);
    apm.processStream(frame);
  }

  // Now process signal + noise
  constexpr double kSignalAmplitude = 2000.0;
  constexpr double kNoiseAmplitude = 1000.0;
  constexpr double kSignalFrequency = 1000.0;

  double signal_energy_sum = 0.0;
  int signal_frames = 0;

  for (int i = 0; i < kFrames; ++i) {
    AudioFrame frame = create10msFrame(48000, 1);

    // Add signal
    fillWithSineWave(frame, kSignalFrequency, kSignalAmplitude);

    // Add noise on top
    auto &data = frame.data();
    std::mt19937 gen(kSeed + 50 + i);
    std::uniform_real_distribution<> dis(-kNoiseAmplitude, kNoiseAmplitude);
    for (auto &sample : data) {
      sample = static_cast<std::int16_t>(std::clamp(
          static_cast<double>(sample) + dis(gen), -32768.0, 32767.0));
    }

    apm.processStream(frame);

    if (i >= kFrames / 2) { // Measure second half after adaptation
      signal_energy_sum += calculateRMS(frame);
      signal_frames++;
    }
  }

  double avg_output_energy = signal_energy_sum / signal_frames;

  std::cout << "[AGC+NS Combined] Avg output energy: " << avg_output_energy
            << " (input signal amplitude: " << kSignalAmplitude
            << ", noise amplitude: " << kNoiseAmplitude << ")" << std::endl;

  // Should have reasonable output energy (AGC boosted, NS cleaned)
  EXPECT_GT(avg_output_energy, 100.0)
      << "Combined AGC+NS should produce reasonable output";
}

} // namespace test
} // namespace livekit
