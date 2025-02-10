/*
 *  (c) 2025, wilddolphin2022 
 *  For WebRTCsays.ai project
 *  https://github.com/wilddolphin2022/webrtcsays.ai
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AUDIO_DEVICE_SPEECH_AUDIO_DEVICE_FACTORY_H_
#define AUDIO_DEVICE_SPEECH_AUDIO_DEVICE_FACTORY_H_

#include <stdint.h>
#include <mutex>

#include "absl/strings/string_view.h"
#include "api/task_queue/task_queue_factory.h"
#include "modules/audio_device/audio_device_generic.h"

namespace webrtc {

// This class is used by audio_device_impl.cc when WebRTC is compiled with
// WEBRTC_SPEECH_DEVICES. The application must include this file and set the
// filenames to use before the audio device module is initialized. This is
// intended for test tools which use the audio device module.
class SpeechAudioDeviceFactory {
 public:
  static AudioDeviceGeneric* CreateSpeechAudioDevice(TaskQueueFactory* task_queue_factory);

 private:
  enum : uint32_t { MAX_FILENAME_LEN = 512 };

  // The input whisper model file must be a ggml file (https://github.com/ggerganov/whisper.cpp/blob/master/models/README.md)
  static std::string _whisperModelFilename;
  // The input llama model file must be a gguf file
  static std::string _llamaModelFilename;
  // This is a wav file, 16k samples, 16 bit PCM, to play out on beginning
  static std::string _wavFilename;
};

}  // namespace webrtc

#endif  // AUDIO_DEVICE_SPEECH_AUDIO_DEVICE_FACTORY_H_
