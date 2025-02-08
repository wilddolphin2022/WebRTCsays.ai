#ifndef ESPEAK_TTS_H_
#define ESPEAK_TTS_H_

#include <espeak-ng/espeak_ng.h>
#include <functional>
#include <vector>
#include <mutex>
#include <deque>
#include <chrono>

class ESpeakTTS {
public:
    ESpeakTTS();
    ~ESpeakTTS();

    void synthesize(const char* text, std::vector<short>& buffer);
    int getSampleRate() const;

private:
    static int internalSynthCallback(short* wav, int numsamples, espeak_EVENT* events);
    
    // Ring buffer implementation
    class AudioRingBuffer {
    public:
        explicit AudioRingBuffer(size_t capacity = 48000) // 3 seconds at 16kHz
            : capacity_(capacity) {}

        void write(const short* data, size_t size) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (size_t i = 0; i < size; i++) {
                if (buffer_.size() >= capacity_) {
                    buffer_.pop_front();
                }
                buffer_.push_back(data[i]);
            }
        }

        size_t read(short* data, size_t size) {
            std::lock_guard<std::mutex> lock(mutex_);
            size_t read_size = std::min(size, buffer_.size());
            for (size_t i = 0; i < read_size; i++) {
                data[i] = buffer_.front();
                buffer_.pop_front();
            }
            return read_size;
        }

        void clear() {
            std::lock_guard<std::mutex> lock(mutex_);
            buffer_.clear();
        }

        size_t size() const {
            std::lock_guard<std::mutex> lock(mutex_);
            return buffer_.size();
        }

    private:
        std::deque<short> buffer_;
        mutable std::mutex mutex_;
        const size_t capacity_;
    };

    AudioRingBuffer ring_buffer_;
    std::chrono::steady_clock::time_point last_read_time_;
    static constexpr size_t SAMPLE_RATE = 16000;
};

#endif