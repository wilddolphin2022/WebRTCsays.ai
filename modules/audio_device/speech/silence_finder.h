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

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>

template<typename T>
class SilenceFinder {
public:
    SilenceFinder(T * data, uint size, uint samples) : 
        d(data), 
        sBegin(0), 
        s(size), 
        samp(samples), 
        status(Undefined) 
    {
        // Calculate average amplitude here for relative threshold
        avgAmplitude = calculateAverageAmplitude(data, size);
    }

    std::vector<std::pair<uint, uint>> find(const float relativeThreshold, const uint window) {
        const T threshold = static_cast<T>(avgAmplitude * relativeThreshold);
        auto r = findSilence(d, s, threshold, window);
        regionsToTime(r);
        return r;
    }

private:
    enum Status {
        Silent, Loud, Undefined
    };

    void toggleSilence(Status st, uint pos, std::vector<std::pair<uint, uint>> & res) {
        if (st == Silent) {
            if (status != Silent) sBegin = pos;  // Start of silence
            status = Silent;
        } else {
            if (status == Silent) {  // End of silence detected
                res.push_back(std::pair<uint, uint>(sBegin, pos));
                status = Loud;
            }
        }
    }

    void end(Status st, uint pos, std::vector<std::pair<uint, uint>> & res) {
        if (status == Silent) {  // If we're in silence at the end
            res.push_back(std::pair<uint, uint>(sBegin, pos));
        }
    }

    T calculateAverageAmplitude(T* data, uint size) {
        // Use RMS (Root Mean Square) instead of arithmetic mean
        double sum_squares = 0.0;
        for (uint i = 0; i < size; ++i) {
            double sample = static_cast<double>(data[i]);
            sum_squares += sample * sample;
        }
        return static_cast<T>(std::sqrt(sum_squares / size));
    }

    static T delta(T * data, const uint window) {
        // Improve noise immunity by using RMS of window
        double sum_squares = 0.0;
        T max_amplitude = 0;
        
        for (uint i = 0; i < window; ++i) {
            double sample = static_cast<double>(std::abs(data[i]));
            sum_squares += sample * sample;
            max_amplitude = std::max(max_amplitude, static_cast<T>(sample));
        }
        
        T rms = static_cast<T>(std::sqrt(sum_squares / window));
        return std::max(rms, static_cast<T>(max_amplitude / 4));  // Consider both RMS and peak
    }

    std::vector<std::pair<uint, uint>> findSilence(T * data, const uint size, const T threshold, const uint win) {
        std::vector<std::pair<uint, uint>> regions;
        if (size == 0 || win == 0) {
            return regions; // Return empty vector for invalid input
        }

        uint window = win;
        uint pos = 0;
        Status s = Undefined;

        while (pos < size) {  // Changed from <= to <
            // Use the minimum of window size or remaining data size
            uint checkSize = std::min(window, size - pos);
            if (delta(data + pos, checkSize) < threshold) {
                s = Silent;
            } else {
                s = Loud;
            }
            toggleSilence(s, pos, regions);
            pos += window;
            
            // Ensure we don't go past the buffer end
            if (pos > size) {
                pos = size;  // This will make the next iteration fail the loop condition
            }
        }

        // Handle the case where the last segment might not be a full window
        if (pos == size && status == Silent) {
            end(s, pos, regions); // Only call end if we were in a silent state at the end
        }

        return regions;
    }

    void regionsToTime(std::vector<std::pair<uint, uint>> & regions) {
        for (auto & r : regions) {
            r.first /= samp;
            r.second /= samp;
        }
    }

    T * d;
    uint sBegin, s, samp;
    Status status;

public:    
    T avgAmplitude = 0; // Store average amplitude for relative threshold calculation
};

// Usage:
// SilenceFinder<int16_t> silenceFinder(int16Buffer.data(), int16Buffer.size(), kSampleRate);
// const float relativeThreshold = 0.05f; // 5% of average amplitude
// const uint windowSize = kSampleRate / 10; // 100ms window
// auto silenceRegions = silenceFinder.find(relativeThreshold, windowSize);