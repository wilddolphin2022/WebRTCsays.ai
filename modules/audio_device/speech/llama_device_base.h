/*
 *  (c) 2025, wilddolphin2022 
 *  For WebRTCsays.ai project
 *  https://github.com/wilddolphin2022/ringrtc
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
 
#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>
#include <queue>

#include "absl/strings/string_view.h"
#include "rtc_base/platform_thread.h"
#include "speech_audio_device.h"

struct llama_model;
struct llama_context;
struct llama_sampler;
struct llama_vocab;
typedef int32_t llama_token;

class LlamaSimpleChat {
public:
  LlamaSimpleChat();
  ~LlamaSimpleChat();

  bool SetModelPath(const std::string& path);
  bool SetNGL(int layers);
  bool SetContextSize(int size);
  void StopGeneration();

  bool Initialize(SpeechAudioDevice* speech_audio_device);
  std::string generate(const std::string& request);

  bool InitializeContext();
  void FreeContext();

  private:
  bool LoadModel();

  std::string model_path_;
  int ngl_ = 99; // Number of GPU layers to offload
  int n_predict_ = 2048; // Number of tokens to predict
  std::string prompt_;

  llama_model* model_ = nullptr;
  const llama_vocab* vocab_ = nullptr;
  llama_context* ctx_ = nullptr;
  llama_sampler* smpl_ = nullptr;
  
  std::atomic<bool> continue_ = true;
  SpeechAudioDevice* _speech_audio_device = nullptr;

  bool isRepetitive(const std::string& text, size_t minPatternLength = 4);
  bool hasConfirmationPattern(const std::string& text);


};

class LlamaDeviceBase {
public:
  LlamaDeviceBase( 
    SpeechAudioDevice* speech_audio_device,
    const std::string& llamaModelFilename);
  virtual ~LlamaDeviceBase();

  // Send text to recording queue
  virtual void askLlama(const std::string& text);
  
  bool Start();
  void Stop();

private:
  rtc::PlatformThread _processingThread;
  std::atomic<bool> _running;
  bool RunProcessingThread();

  SpeechAudioDevice* _speech_audio_device = nullptr;
  std::string _llamaModelFilename;
  std::unique_ptr<LlamaSimpleChat> _llama_chat;

  // Incoming ask text queue
  std::queue<std::string> _textQueue;
  std::mutex _queueMutex;
  std::condition_variable _queueCondition;

  // Add these new members
  std::vector<llama_token> context_tokens_;
  const size_t max_context_tokens_ = 2048; // Adjust based on your model   
  
  bool TrimContext();
  bool AppendToContext(const std::vector<llama_token>& new_tokens);
};
