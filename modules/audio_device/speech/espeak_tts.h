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

#ifndef ESPEAK_TTS_H
#define ESPEAK_TTS_H

#include <vector>
#include <functional>
#include <espeak-ng/speak_lib.h>

class ESpeakTTS {
private:
    std::function<void(std::vector<short>, void*, bool)> synthCallback;
    static int internalSynthCallback(short *wav, int numsamples, espeak_EVENT *events);

public:
    ESpeakTTS();
    ~ESpeakTTS();

    // Set the callback function for receiving synthesized audio
    void setSynthCallback(std::function<void(std::vector<short>, void*, bool)> callback);

    // Synthesize a given text into audio
    void synthesize(const char* text, std::vector<short>& buffer);

    // Method to get the sample rate used for synthesis
    int getSampleRate() const;
};

#endif // ESPEAK_TTS_H