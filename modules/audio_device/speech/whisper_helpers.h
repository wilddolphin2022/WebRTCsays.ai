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

#pragma once

#include <mutex>
#include <iomanip>
#include <algorithm> 
#include <cctype>
#include <locale>

class AudioRingBuffer {
private:
    std::vector<uint8_t> buffer;
    std::atomic<size_t> bufferSize;
    std::atomic<size_t> writeIndex;
    std::atomic<size_t> readIndex;

    void resizeBuffer(size_t newSize) {
        std::vector<uint8_t> newBuffer(newSize);
        
        // Copy existing data to new buffer, considering wrap-around
        size_t available = availableToRead();
        if (available > 0) {
            size_t readFrom = readIndex % bufferSize.load();
            size_t firstPart = std::min(available, bufferSize.load() - readFrom);
            
            std::copy(buffer.begin() + readFrom, buffer.begin() + readFrom + firstPart, newBuffer.begin());
            if (firstPart < available) {
                std::copy(buffer.begin(), buffer.begin() + (available - firstPart), newBuffer.begin() + firstPart);
            }
        }
        
        // Swap buffers
        buffer.swap(newBuffer);
        
        // Reset indices in case of size change
        size_t oldSize = bufferSize.load();
        if (newSize > oldSize) {
            writeIndex = available;
        } else {
            writeIndex = std::min(writeIndex.load(), newSize);
        }
        readIndex = 0;
        bufferSize = newSize;
    }

public:
    AudioRingBuffer(size_t initialSize) : buffer(initialSize), bufferSize(initialSize), writeIndex(0), readIndex(0) {}

    size_t availableToRead() const {
        size_t result = writeIndex - readIndex;
        if (result > bufferSize.load()) return 0; // wrap around
        return result;
    }

    size_t spaceAvailable() const {
        return bufferSize.load() - availableToRead();
    }

    bool write(const uint8_t* data, size_t size) {
        if (size > spaceAvailable()) {
            // Attempt to resize if there's not enough space
            size_t newSize = bufferSize.load() * 2; // Double the size as an example strategy
            if (newSize < size + availableToRead()) {
                newSize = size + availableToRead() + (bufferSize.load() / 2); // Ensure enough space for current write + some extra
            }
            resizeBuffer(newSize);
        }

        size_t writeTo = writeIndex % bufferSize.load();
        size_t canWrite = std::min(size, bufferSize.load() - writeTo);

        std::copy(data, data + canWrite, buffer.data() + writeTo);
        if (canWrite < size) {
            std::copy(data + canWrite, data + size, buffer.data());
        }
        
        writeIndex.fetch_add(size, std::memory_order_relaxed);
        if (writeIndex >= bufferSize.load() * 2) { // Check for wrap around
            writeIndex -= bufferSize.load();
            readIndex -= bufferSize.load(); // Adjust readIndex if needed
        }
        
        return true;
    }

    bool read(uint8_t* data, size_t size) {
        if (size > availableToRead()) return false; // Not enough data

        size_t readFrom = readIndex % bufferSize.load();
        size_t canRead = std::min(size, bufferSize.load() - readFrom);

        std::copy(buffer.data() + readFrom, buffer.data() + readFrom + canRead, data);
        if (canRead < size) {
            std::copy(buffer.data(), buffer.data() + (size - canRead), data + canRead);
        }

        readIndex.fetch_add(size, std::memory_order_relaxed);
        if (readIndex >= bufferSize.load() * 2) { // Check for wrap around
            readIndex -= bufferSize.load();
            writeIndex -= bufferSize.load(); // Adjust writeIndex if needed
        }

        return true;
    }

    // New method for shrinking the buffer if desired
    void shrinkToFit(size_t minSize) {
        size_t currentSize = bufferSize.load();
        size_t newSize = std::max(minSize, availableToRead());
        
        if (newSize < currentSize) {
            resizeBuffer(newSize * 2); // Keep some extra capacity to avoid frequent resizing
        }
    }

    // New method to increase buffer if desired
    void increaseWith(size_t incSize) {
        size_t currentSize = bufferSize.load();
        resizeBuffer(currentSize + incSize);
    }

    size_t bufSize() const { return bufferSize.load(); }
};

// Usage example:
// Assuming you have access to a TaskQueueFactory instance
// webrtc::TaskQueueFactory* factory = ...;
// TaskQueuePool pool(factory, 4); // Create with 4 task queues
// pool.enqueue([](){ std::cout << "Task executed!" << std::endl; });
#include "rtc_base/logging.h"
#include "rtc_base/buffer.h"

#include "api/task_queue/task_queue_factory.h"

class TaskQueuePool {
private:
  std::vector<std::unique_ptr<webrtc::TaskQueueBase, webrtc::TaskQueueDeleter>> queues_;
  size_t next_queue_{0};
  std::mutex queue_mutex_;

public:
   TaskQueuePool(webrtc::TaskQueueFactory* task_queue_factory, size_t threads) {
       queues_.reserve(threads);
       for(size_t i = 0; i < threads; ++i) {
           queues_.emplace_back(task_queue_factory->CreateTaskQueue(
               "TaskQueuePool_" + std::to_string(i),
               webrtc::TaskQueueFactory::Priority::NORMAL));
        RTC_LOG(LS_INFO) << "TaskQueuePool queues_.size() " << queues_.size();
       }
   }
 
   template<class F, class... Args>
   void enqueue(F&& f, Args&&... args) {
       auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
       std::unique_lock<std::mutex> lock(queue_mutex_);
       size_t queue_index = next_queue_++;
       if (next_queue_ >= queues_.size()) {
           next_queue_ = 0;
       }
       queues_[queue_index]->PostTask([task]() mutable { task(); });
   }

  size_t poolSize() const { return queues_.size(); }

   ~TaskQueuePool() = default;
};

class HexPrinter {
public:
    HexPrinter() = default;
    ~HexPrinter() = default;

    static void Dump(const uint8_t* buffer, size_t length, size_t bytes_per_line = 16) {
        if (!buffer || length == 0) {
            RTC_LOG(LS_WARNING) << "Invalid buffer or length";
            return;
        }

        std::stringstream output;
        output << std::hex << std::setfill('0');
        size_t display_length = std::max(length, bytes_per_line);

        for (size_t i = 0; i < display_length; ++i) {
            if (i < length) {
                uint8_t byte = buffer[i];
                if (std::isalnum(byte)) {
                    output << ' ' << static_cast<char>(byte) << ' ';
                } else {
                    output << std::setw(2) << static_cast<int>(byte) << ' ';
                }
            } else {
                output << ".. "; // Padding
            }

            if ((i + 1) % bytes_per_line == 0 && i < display_length - 1) {
                output << '\n';
            }
        }

        RTC_LOG(LS_INFO) << "Buffer Dump (" << length << " bytes):\n" << output.str();
    }

    // Overload for rtc::Buffer
    static void PrintBufferHex(const rtc::Buffer& buffer, size_t bytes_per_line = 16) {
        Dump(buffer.data(), buffer.size(), bytes_per_line);
    }
};

template<typename T>
std::vector<T> convertDatatype(std::vector<float> float_vec)
{
    std::vector<T> vec;
    vec.reserve( float_vec.size() );    //  avoids unnecessary reallocations
    std::transform( float_vec.begin(), float_vec.end(),
        std::back_inserter( vec ),
        [](const float &arg) { return static_cast<T>(arg); } );
    return vec;
}

// trim from start (in place)
inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}