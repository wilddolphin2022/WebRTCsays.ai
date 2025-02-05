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

#include "espeak_tts.h"

#include <iostream>

#include "rtc_base/logging.h"

ESpeakTTS::ESpeakTTS() {
  espeak_AUDIO_OUTPUT output = AUDIO_OUTPUT_SYNCHRONOUS;  // No audio playback
  int Buflength = 10;       // Buffer length in milliseconds
  const char* path = NULL;  // Default path for espeak data
  int Options = 0;          // No special options
  char Voice[] = {"English"};

  if (espeak_Initialize(output, Buflength, path, Options) ==
      EE_INTERNAL_ERROR) {
    RTC_LOG(LS_ERROR) << "ESpeakTTS initialization failed!";
  }
  espeak_SetVoiceByName(Voice);
  const char* langNativeString = "en";
  espeak_VOICE voice;
  memset(&voice, 0, sizeof(espeak_VOICE));
  voice.languages = langNativeString;
  voice.name = "US";
  voice.variant = 1;
  voice.gender = 1;
  espeak_SetVoiceByProperties(&voice);

  //  espeakRATE:    speaking speed in word per minute.  Values 80 to 450.
  //  espeakVOLUME:  volume in range 0-200 or more.
  //                0=silence, 100=normal full volume, greater values may
  //                produce amplitude compression or distortion
  //  espeakPITCH:   base pitch, range 0-100.  50=normal
  //  espeakRANGE:   pitch range, range 0-100. 0-monotone, 50=normal

  espeak_SetParameter(espeakRATE, 200, 0);
  espeak_SetParameter(espeakVOLUME, 75, 0);
  espeak_SetParameter(espeakPITCH, 150, 0);
  espeak_SetParameter(espeakRANGE, 100, 0);

  // Turn translation off
  espeak_SetParameter((espeak_PARAMETER) 11, 0, 0);
}

ESpeakTTS::~ESpeakTTS() {
  espeak_Terminate();
}

void ESpeakTTS::setSynthCallback(
    std::function<void(std::vector<short>, void*, bool)> callback) {
  this->synthCallback = callback;
  espeak_SetSynthCallback(&ESpeakTTS::internalSynthCallback);
}

void ESpeakTTS::synthesize(const char* text, std::vector<short>& buffer) {
  std::vector<short>
      tempBuffer;  // Temporary buffer to capture synthesized audio
  setSynthCallback(
      [&tempBuffer](std::vector<short> wav, void* events, bool success) {
        if (success && !wav.empty()) {
          tempBuffer.insert(tempBuffer.end(), wav.begin(), wav.end());
        }
      });

  if (!synthCallback) {
    RTC_LOG(LS_ERROR) << "ESpeakTTS Synth callback not set!";
    return;
  }

  size_t size = strlen(text) + 1;  // Include null terminator
  unsigned int position = 0, end_position = 0, flags = espeakCHARS_AUTO;
  buffer.clear();  // Clear previous audio data

  espeak_ERROR result =
      espeak_Synth(text, size, position, POS_CHARACTER, end_position, flags,
                   NULL, reinterpret_cast<void*>(this));
  if (result != EE_OK) {
    RTC_LOG(LS_ERROR) << "ESpeakTTS espeak_Synth error " << result;
    return;
  }

  result = espeak_Synchronize();  // Wait for synthesis to complete
  if (result != EE_OK) {
    RTC_LOG(LS_ERROR) << "ESpeakTTS espeak_Synchronize error " << result;
    return;
  }

  // After synthesis, move the tempBuffer content to the passed buffer
  buffer = std::move(tempBuffer);
  return;
}

int ESpeakTTS::getSampleRate() const {
  return 16000;  // Default sample rate for espeak-ng
}

// Static member to act as an intermediary callback
int ESpeakTTS::internalSynthCallback(short* wav,
                                     int numsamples,
                                     espeak_EVENT* events) {
  ESpeakTTS* context = static_cast<ESpeakTTS*>(events->user_data);
  while (events->type != espeakEVENT_LIST_TERMINATED) {
    if (events->type == espeakEVENT_MSG_TERMINATED) {
      switch (events->type) {
        case espeakEVENT_SAMPLERATE:
          RTC_LOG(LS_INFO) << "ESpeakTTS Sample rate event";
          break;
        case espeakEVENT_WORD:
          RTC_LOG(LS_INFO) << "ESpeakTTS Word event";
          break;
        // ... handle other event types
        default:
          RTC_LOG(LS_INFO) << "ESpeakTTS Unhandled event type: "
                           << events->type;
      }
    }
    ++events;  // Examine the next event.
  }

  if (context->synthCallback) {
    std::vector<short> data(wav, wav + numsamples);
    context->synthCallback(data, events, true);
  }
  return 0;
}
