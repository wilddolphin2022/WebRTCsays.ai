#include "espeak_tts.h"
#include "rtc_base/logging.h"

ESpeakTTS::ESpeakTTS() {
    espeak_AUDIO_OUTPUT output = AUDIO_OUTPUT_SYNCHRONOUS;
    int Buflength = 500;
    const char* path = NULL;
    int Options = 0;
    char Voice[] = {"English"};

    if (espeak_Initialize(output, Buflength, path, Options) == EE_INTERNAL_ERROR) {
        RTC_LOG(LS_ERROR) << "ESpeakTTS initialization failed!";
        return;
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

    espeak_SetParameter(espeakRATE, 200, 0);
    espeak_SetParameter(espeakVOLUME, 75, 0);
    espeak_SetParameter(espeakPITCH, 150, 0);
    espeak_SetParameter(espeakRANGE, 100, 0);
    espeak_SetParameter((espeak_PARAMETER)11, 0, 0);

    espeak_SetSynthCallback(&ESpeakTTS::internalSynthCallback);
}

ESpeakTTS::~ESpeakTTS() {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        synthesis_buffer_.clear();
    }
    espeak_Terminate();
}

void ESpeakTTS::synthesize(const char* text, std::vector<short>& buffer) {
    if (!text) return;

    RTC_LOG(LS_INFO) << "ESpeakTTS: Starting synthesis of text: '" << text << "'";
    
    // Clear any previous synthesis data
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        synthesis_buffer_.clear();
    }

    size_t size = strlen(text) + 1;
    unsigned int position = 0, end_position = 0, flags = espeakCHARS_AUTO;

    espeak_ERROR result = espeak_Synth(text, size, position, POS_CHARACTER, 
                                     end_position, flags, NULL, 
                                     reinterpret_cast<void*>(this));
    
    if (result != EE_OK) {
        RTC_LOG(LS_ERROR) << "ESpeakTTS: espeak_Synth error " << result;
        return;
    }

    result = espeak_Synchronize();
    if (result != EE_OK) {
        RTC_LOG(LS_ERROR) << "ESpeakTTS: espeak_Synchronize error " << result;
        return;
    }

    // Copy the synthesized audio to the output buffer
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        buffer = synthesis_buffer_;
    }

    RTC_LOG(LS_INFO) << "ESpeakTTS: Synthesis complete, buffer size: " << buffer.size();
}

int ESpeakTTS::internalSynthCallback(short* wav, int numsamples, espeak_EVENT* events) {
    if (wav == nullptr || numsamples <= 0) {
        return 0;
    }

    ESpeakTTS* context = static_cast<ESpeakTTS*>(events->user_data);
    if (!context) return 0;

    // Directly append to synthesis buffer
    {
        std::lock_guard<std::recursive_mutex> lock(context->mutex_);
        size_t current_size = context->synthesis_buffer_.size();
        context->synthesis_buffer_.resize(current_size + numsamples);
        memcpy(context->synthesis_buffer_.data() + current_size, wav, numsamples * sizeof(short));
    }

    return 0;
}

int ESpeakTTS::getSampleRate() const {
    return SAMPLE_RATE;
}