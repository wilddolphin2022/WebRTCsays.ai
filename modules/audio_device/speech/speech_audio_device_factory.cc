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


#include <stdio.h>
#include <cstdlib>
#include <mutex>

#include "absl/strings/string_view.h"
#include "rtc_base/logging.h"
#include "rtc_base/string_utils.h"

#include "modules/audio_device/speech/speech_audio_device_factory.h"
#include "modules/audio_device/speech/whisper_audio_device.h"

namespace webrtc {

std::string SpeechAudioDeviceFactory::_whisperModelFilename;
std::string SpeechAudioDeviceFactory::_llamaModelFilename;
std::string SpeechAudioDeviceFactory::_wavFilename;

AudioDeviceGeneric* SpeechAudioDeviceFactory::CreateSpeechAudioDevice(TaskQueueFactory* task_queue_factory) {

  WhisperAudioDevice* whisper_audio_device = nullptr;
  if(!whisper_audio_device) {

    SpeechAudioDeviceFactory::_whisperModelFilename = std::getenv("WHISPER_MODEL") ? \
      std::getenv("WHISPER_MODEL") : ""; // Must be ggml
    if(SpeechAudioDeviceFactory::_whisperModelFilename.empty())
      RTC_LOG(LS_WARNING)
        << "WHISPER_MODEL enviroment variable is empty! Did you mean it?";
        
    SpeechAudioDeviceFactory::_llamaModelFilename = std::getenv("LLAMA_MODEL") ? \
      std::getenv("LLAMA_MODEL") : ""; // Must be gguf
    if(SpeechAudioDeviceFactory::_llamaModelFilename.empty())
      RTC_LOG(LS_WARNING)
        << "LLAMA_MODEL enviroment variable is empty! Did you mean it?";

    SpeechAudioDeviceFactory::_wavFilename = std::getenv("WEBRTC_SPEECH_INITIAL_PLAYOUT_WAV") ? \
      std::getenv("WEBRTC_SPEECH_INITIAL_PLAYOUT_WAV") : ""; // Must be .wav
    if(!SpeechAudioDeviceFactory::_wavFilename.empty())
      RTC_LOG(LS_INFO)
        << "WEBRTC_SPEECH_INITIAL_PLAYOUT_WAV is '" << SpeechAudioDeviceFactory::_wavFilename << "'";

    whisper_audio_device = new WhisperAudioDevice(task_queue_factory, 
                                                   _whisperModelFilename, 
                                                   _llamaModelFilename, 
                                                   _wavFilename);
    RTC_LOG(LS_INFO) << "Initialized WhisperAudioDevice instance.";
  }

  return static_cast<AudioDeviceGeneric*>(whisper_audio_device);
}

}  // namespace webrtc
