/*
 *  (c) 2025, wilddolphin2022 
 *  For WebRTCsays.ai project
 *  https://github.com/wilddolphin2022
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <chrono>
#include <fstream>

#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/platform_thread.h"
#include "rtc_base/system/file_wrapper.h"
#include "rtc_base/logging.h"
#include "api/task_queue/default_task_queue_factory.h"

#include "llama_device_base.h"
#include "whisper_helpers.h"
#include "silence_finder.h"

#include "speech_audio_device.h"

struct whisper_context;

class WhisperTranscriber {
 private:
  SpeechAudioDevice* _speech_audio_device  = nullptr;
  std::unique_ptr<webrtc::TaskQueueFactory> _task_queue_factory;
  std::unique_ptr<TaskQueuePool> _task_queue_pool;

  std::string _modelFilename;
  whisper_context* _whisperContext;
  AudioRingBuffer _audioBuffer; // Replace ThreadSafeQueue with AudioRingBuffer

  rtc::PlatformThread _processingThread;
  std::atomic<bool> _running;
  std::atomic<bool> _processingActive;

  // Constants for audio processing
  static constexpr int kSampleRate = 16000;       // 16 kHz
  static constexpr int kChannels = 1;             // Mono
  static constexpr int kBufferDurationMs = 10;    // 10ms buffer
  static constexpr int kTargetDurationSeconds = 3; // 3-second segments for Whisper
  static constexpr int kRingBufferSizeIncrement = kSampleRate * kTargetDurationSeconds * 2 * 10; // 10-seconds increment for ring buffer size

  static constexpr size_t kTargetSamples = kSampleRate * 12 * 2; // 12 seconds of audio
  static constexpr size_t kSilenceSamples = 16000; // 1 second of silence at 16kHz

  // Accumulated buffer for Whisper processing
  std::vector<uint8_t> _accumulatedByteBuffer;
  std::atomic<size_t> _overflowCount;
  std::atomic<size_t> _ringBufferSize; // 10 segments buffer size

  #if defined(PCM_FILE_DUMP)
  webrtc::FileWrapper _pcm_file;
  #endif

  bool TranscribeAudioNonBlocking(const std::vector<float>& pcmf32);
  bool RunProcessingThread();
  bool ValidateWhisperModel(const std::string& modelPath);
  bool InitializeWhisperModel(const std::string& modelPath);
  whisper_context* TryAlternativeInitMethods(const std::string& modelPath);

  // State to keep track if we're in a voice segment
  bool _inVoiceSegment = false;
  size_t _samplesSinceVoiceStart = 0;
  size_t _silentSamplesCount = 0; // New: Count of silent samples
  void handleOverflow();

  int64_t _timeMarker = 0;
  
  std::vector<int16_t> _processingBuffer;
  std::unique_ptr<SilenceFinder<int16_t>> _silenceFinder;
  
    // Add new members for voice detection state
    struct VoiceDetectionState {
      float lastThreshold = 0.0f;
      size_t consecutiveVoiceFrames = 0;
      size_t consecutiveSilenceFrames = 0;
  } _voiceState;

  // Updated constants
  static constexpr float voiceStartThreshold = 0.12f;  // Higher threshold to start voice
  static constexpr float voiceEndThreshold = 0.08f;    // Lower threshold to end voice
  static constexpr size_t kMinVoiceFrames = 3;
  static constexpr size_t kMinSilenceFrames = 5;

 public:
  WhisperTranscriber(
      SpeechAudioDevice* _speech_audio_device,
      webrtc::TaskQueueFactory* task_queue_factory, 
      const std::string& inputFilename);
  
  ~WhisperTranscriber();

  void ProcessAudioBuffer(uint8_t* playoutBuffer, size_t kPlayoutBufferSize);

  bool Start();
  void Stop();
};