#include "espeak_tts.h"
#include "rtc_base/logging.h"

ESpeakTTS::ESpeakTTS() 
    : last_read_time_(std::chrono::steady_clock::now()) {
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

void ESpeakTTS::synthesize(const char* text, std::vector<short>& buffer) {
    if (!text) return;

    RTC_LOG(LS_INFO) << "ESpeakTTS: Starting synthesis of text: '" << text << "'";
    
    // Clear the ring buffer and output buffer
    ring_buffer_.clear();
    buffer.clear();

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

    // Read all available samples from the ring buffer
    size_t ring_buffer_size = ring_buffer_.size();
    if (ring_buffer_size > 0) {
        buffer.resize(ring_buffer_size);
        size_t read = ring_buffer_.read(buffer.data(), ring_buffer_size);
        if (read != ring_buffer_size) {
            // Resize buffer to actual read size if different
            buffer.resize(read);
        }
    }

    RTC_LOG(LS_INFO) << "ESpeakTTS: Synthesis complete, buffer size: " << buffer.size();
}

int ESpeakTTS::internalSynthCallback(short* wav, int numsamples, espeak_EVENT* events) {
    if (wav == nullptr || numsamples <= 0) {
        return 0;
    }

    ESpeakTTS* context = static_cast<ESpeakTTS*>(events->user_data);
    if (!context) return 0;

    // Write samples to ring buffer
    context->ring_buffer_.write(wav, numsamples);
    
    RTC_LOG(LS_INFO) << "ESpeakTTS: Received " << numsamples << " samples";
    return 0;
}

int ESpeakTTS::getSampleRate() const {
    return SAMPLE_RATE;
}

ESpeakTTS::~ESpeakTTS() {
    ring_buffer_.clear();
    espeak_Terminate();
}