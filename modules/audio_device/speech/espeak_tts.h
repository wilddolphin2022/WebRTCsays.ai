#ifndef ESPEAK_TTS_H_
#define ESPEAK_TTS_H_

#include <espeak-ng/espeak_ng.h>
#include <functional>
#include <vector>
#include <mutex>

class ESpeakTTS {
public:
    ESpeakTTS();
    ~ESpeakTTS();

    void synthesize(const char* text, std::vector<short>& buffer);
    int getSampleRate() const;

private:
    static int internalSynthCallback(short* wav, int numsamples, espeak_EVENT* events);
    
    std::recursive_mutex mutex_;
    std::vector<short> synthesis_buffer_;
    
    static constexpr size_t SAMPLE_RATE = 16000;
};

#endif