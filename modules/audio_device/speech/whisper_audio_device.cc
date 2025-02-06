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

#include "modules/audio_device/speech/whisper_audio_device.h"

#include <string.h>
#include <cstdio>
#include <thread>
#include <iomanip>

#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/thread.h"
#include "system_wrappers/include/sleep.h"
#include "rtc_base/string_utils.h"
#include "api/task_queue/default_task_queue_factory.h"

#include "whisper.h"  // Whisper.cpp library header
#include "espeak_tts.h" // Epeak-ng tts
#include "whisper_helpers.h"  // Whisper helper code

//#define PLAY_WAV_ON_RECORD 1
//#define PLAY_WAV_ON_PLAY 1
//#define LLAMA_ENABLED 1

namespace webrtc {

const int kRecordingFixedSampleRate = 16000;  // Whisper typically uses 16kHz
const size_t kRecordingNumChannels = 1;       // Mono for Whisper
const int kPlayoutFixedSampleRate = 16000;
const size_t kPlayoutNumChannels = 1;
const size_t kPlayoutBufferSize =
    kPlayoutFixedSampleRate / 100 * kPlayoutNumChannels * 2;
const size_t kRecordingBufferSize =
    kRecordingFixedSampleRate / 100 * kRecordingNumChannels * 2;

WhisperAudioDevice::WhisperAudioDevice(
    TaskQueueFactory* task_queue_factory,
    absl::string_view whisperModelFilename,
    absl::string_view llamaModelFilename,
    absl::string_view wavFilename)
    : _task_queue_factory(task_queue_factory),
      _ptrAudioBuffer(nullptr),
      _recordingBuffer(nullptr),
      _playoutBuffer(nullptr),
      _recordingFramesLeft(0),
      _playoutFramesLeft(0),
      _recording(false),
      _playing(false),
      _whisperModelFilename(whisperModelFilename),
      _llamaModelFilename(llamaModelFilename),
      _wavFilename(wavFilename)
{
}

WhisperAudioDevice::~WhisperAudioDevice() {

  // Free buffers
  delete[] _recordingBuffer;
  delete[] _playoutBuffer;
}

int32_t WhisperAudioDevice::ActiveAudioLayer(
    AudioDeviceModule::AudioLayer& audioLayer) const {
  if(audioLayer == AudioDeviceModule::kSpeechAudio)
    return 0;

  return -1;  
}

AudioDeviceGeneric::InitStatus WhisperAudioDevice::Init() {

  return InitStatus::OK;
}

int32_t WhisperAudioDevice::Terminate() {
  return 0;
}

bool WhisperAudioDevice::Initialized() const {
  return true;
}

// Method to add text to the queue in a thread-safe manner
void WhisperAudioDevice::speakText(const std::string& text) {
  if(_tts) {
    std::lock_guard<std::mutex> lock(_queueMutex);
    std::string s(text);
    rtrim(s);
    ltrim(s);
    _textQueue.push(s);
  }
  _queueCondition.notify_one();  // Inform one waiting thread that an item is available
}

// Method to ask llama 
void WhisperAudioDevice::askLlama(const std::string& text) {
#if defined(LLAMA_ENABLED)
  if(_llama_device) {
     _llama_device->askLlama(text); // send to llama text queue
  }  
#endif  
}

//
// Recording
//

int16_t WhisperAudioDevice::RecordingDevices() {
  return 1;
}

int32_t WhisperAudioDevice::RecordingDeviceName(uint16_t index,
                                             char name[kAdmMaxDeviceNameSize],
                                             char guid[kAdmMaxGuidSize]) {
  const char* kName = "whisper_recording_device";
  const char* kGuid = "358f8c4d-9605-4d23-bf0a-17d346fafc6f";
  if (index < 1) {
    memset(name, 0, kAdmMaxDeviceNameSize);
    memset(guid, 0, kAdmMaxGuidSize);
    memcpy(name, kName, strlen(kName));
    memcpy(guid, kGuid, strlen(guid));
    return 0;
  }
  return -1;
}

int32_t WhisperAudioDevice::SetRecordingDevice(uint16_t index) {
  if (index == 0) {
    return 0;
  }
  return -1;
}

int32_t WhisperAudioDevice::SetRecordingDevice(
    AudioDeviceModule::WindowsDeviceType device) {
  return 0;
}

int32_t WhisperAudioDevice::RecordingIsAvailable(bool& available) {
  available = true;
  return 0;
}

int32_t WhisperAudioDevice::InitRecording() {
  MutexLock lock(&mutex_);

  if (_recording) {
    return -1;
  }

  _recordingFramesIn10MS = static_cast<size_t>(kRecordingFixedSampleRate / 100);

  if (_ptrAudioBuffer) {
    _ptrAudioBuffer->SetRecordingSampleRate(kRecordingFixedSampleRate);
    _ptrAudioBuffer->SetRecordingChannels(kRecordingNumChannels);
  }

  return 0;
}

bool WhisperAudioDevice::RecordingIsInitialized() const {
  return _recordingFramesIn10MS != 0;
}

int32_t WhisperAudioDevice::InitMicrophone() {
  return 0;
}

bool WhisperAudioDevice::MicrophoneIsInitialized() const {
  return true;
}

int32_t WhisperAudioDevice::StartRecording() {
  _recording = true;

  // Allocate recording buffer
  if (!_recordingBuffer) {
    _recordingBuffer = new int8_t[kRecordingBufferSize];
  }

  // "RECORDING"
  #if defined(PLAY_WAV_ON_RECORD)
  if (!_wavFilename.empty()) {
    _recFile = FileWrapper::OpenReadOnly(_wavFilename);
    if (!_recFile.is_open()) {
      RTC_LOG(LS_ERROR) << "Failed to open 'recording' file: " << _wavFilename;
      _recording = false;
      delete[] _recordingBuffer;
      _recordingBuffer = NULL;
      return -1;
    }
  }
  #endif // defined(PLAY_WAV_ON_RECORD)

  
  speakText("Started Whisper recording");
  _ptrThreadRec = rtc::PlatformThread::SpawnJoinable(
      [this] {
        while (RecThreadProcess()) {
        }
      },
      "whisper_audio_module_capture_thread",
      rtc::ThreadAttributes().SetPriority(rtc::ThreadPriority::kRealtime));

  RTC_LOG(LS_INFO) << "Started Whisper recording";

  return 0;
}

bool WhisperAudioDevice::Recording() const {
  return _recording;
}

int32_t WhisperAudioDevice::StopRecording() {
  {
    MutexLock lock(&mutex_);
    _recording = false;
  }

  if (!_ptrThreadRec.empty())
    _ptrThreadRec.Finalize();

  MutexLock lock(&mutex_);
  _recordingFramesLeft = 0;
  if (_recordingBuffer) {
    delete[] _recordingBuffer;
    _recordingBuffer = NULL;
  }

  _recFile.Close();

  RTC_LOG(LS_INFO) << "Stopped 'recording'!";
  return 0;
}

bool WhisperAudioDevice::RecThreadProcess() {
  if (!_recording) {
    return false;
  }

  int64_t currentTime = rtc::TimeMillis();
  mutex_.Lock();

  // Check if it's time to process another 10ms chunk
  if (_lastCallRecordMillis == 0 || currentTime - _lastCallRecordMillis >= 10) {
    
    // if transcribing, pop text
    bool shouldSynthesize = false;
    std::string textToSpeak;
    if(_tts) {
      std::unique_lock<std::mutex> lock(_queueMutex);
      if (!_textQueue.empty()) {
        textToSpeak = _textQueue.front();
        _textQueue.pop();
        shouldSynthesize = true;
      }
    }

    if (shouldSynthesize || !_ttsBuffer.empty()) {  // Handle both new synthesis and existing audio in _ttsBuffer
      if (shouldSynthesize && _ttsBuffer.empty()) {  // Synthesize only if there's new text and no remaining audio
        RTC_LOG(LS_INFO) << "TTS text: " << textToSpeak;
        _tts->synthesize(textToSpeak.c_str(), _ttsBuffer);  // Populate ttsBuffer here

        if (_ttsBuffer.empty()) {
          RTC_LOG(LS_WARNING) << "TTS buffer is empty after synthesis.";
        } else {
          RTC_LOG(LS_VERBOSE) << "TTS buffer size: " << _ttsBuffer.size();
        }
      }

      // Process audio from _ttsBuffer
      if (!_ttsBuffer.empty()) {
        size_t samplesToCopy = std::min(_recordingFramesIn10MS, _ttsBuffer.size() - _ttsIndex);
        memcpy(_recordingBuffer, &_ttsBuffer[_ttsIndex], samplesToCopy * sizeof(short));
        _ttsIndex += samplesToCopy;

        // Fill the rest of _recordingBuffer with zeros if needed
        if (samplesToCopy < _recordingFramesIn10MS) {
          memset(_recordingBuffer + samplesToCopy * sizeof(short), 0, (_recordingFramesIn10MS - samplesToCopy) * sizeof(short));
        }

        mutex_.Unlock();
        _ptrAudioBuffer->SetRecordedBuffer(_recordingBuffer, _recordingFramesIn10MS);
        _ptrAudioBuffer->DeliverRecordedData();
        mutex_.Lock();

        // Reset ttsIndex if we've gone through all the TTS audio
        if (_ttsIndex >= _ttsBuffer.size()) {
          _ttsIndex = 0;
          _ttsBuffer.clear();  // Clear for next synthesis
        }
      } else {
        // If no audio to send, send silence
        memset(_recordingBuffer, 0, _recordingFramesIn10MS * sizeof(short));
        mutex_.Unlock();
        _ptrAudioBuffer->SetRecordedBuffer(_recordingBuffer, _recordingFramesIn10MS);
        _ptrAudioBuffer->DeliverRecordedData();
        mutex_.Lock();
      }
    } else {
      // If no TTS audio and no new text, you might want to handle this case with silence or previous audio
      memset(_recordingBuffer, 0, _recordingFramesIn10MS * sizeof(short));
      mutex_.Unlock();
      _ptrAudioBuffer->SetRecordedBuffer(_recordingBuffer, _recordingFramesIn10MS);
      _ptrAudioBuffer->DeliverRecordedData();
      mutex_.Lock();
    }

    _lastCallRecordMillis = currentTime;
  } else {
    // Pacing for the next 10ms chunk
    int64_t sleepTime = 10 - (rtc::TimeMillis() - currentTime);
    if (sleepTime > 0) {
      mutex_.Unlock();
      SleepMs(sleepTime);
      mutex_.Lock();
    }
  }

  mutex_.Unlock();

  return true;
}

void WhisperAudioDevice::AttachAudioBuffer(AudioDeviceBuffer* audioBuffer) {
  MutexLock lock(&mutex_);
  _ptrAudioBuffer = audioBuffer;

  _ptrAudioBuffer->SetRecordingSampleRate(kRecordingFixedSampleRate);
  _ptrAudioBuffer->SetPlayoutSampleRate(kPlayoutFixedSampleRate);
  _ptrAudioBuffer->SetRecordingChannels(1);
  _ptrAudioBuffer->SetPlayoutChannels(1);
}

// 
// Playout block
// 

int16_t WhisperAudioDevice::PlayoutDevices() {
  return 1;
}

int32_t WhisperAudioDevice::PlayoutDeviceName(uint16_t index,
                                           char name[kAdmMaxDeviceNameSize],
                                           char guid[kAdmMaxGuidSize]) {
  const char* kName = "whisper_playout_device";
  const char* kGuid = "951ba178-fbd1-47d1-96be-965b17d56d5b";
  if (index < 1) {
    memset(name, 0, kAdmMaxDeviceNameSize);
    memset(guid, 0, kAdmMaxGuidSize);
    memcpy(name, kName, strlen(kName));
    memcpy(guid, kGuid, strlen(guid));
    return 0;
  }
  return -1;
}

int32_t WhisperAudioDevice::SetPlayoutDevice(uint16_t index) {
  if (index == 0) {
    return 0;
  }
  return -1;
}

int32_t WhisperAudioDevice::SetPlayoutDevice(
    AudioDeviceModule::WindowsDeviceType device) {
  return -1;
}

int32_t WhisperAudioDevice::InitPlayout() {
  MutexLock lock(&mutex_);

  if (_playing) {
    return -1;
  }

  if(!_whisperModelFilename.empty()) {
    RTC_LOG(LS_INFO) << "Whisper model: '" << _whisperModelFilename << "'";
    _whisper_transcriber.reset(new WhisperTranscriber(this, _task_queue_factory, _whisperModelFilename));
    _whisper_transcriber->Start();
    _whispering = true;

    #if defined (LLAMA_ENABLED)
    _llama_device.reset(new LlamaDeviceBase(this, _llamaModelFilename));
    _llama_device->Start();
    _llaming = true;
    #else
    _llaming = false;
    #endif // LLAMA ENABLED

    _tts.reset(new ESpeakTTS());
  }

  _playoutFramesIn10MS = static_cast<size_t>(kPlayoutFixedSampleRate / 100);

  if (_ptrAudioBuffer) {
    // Update webrtc audio buffer with the selected parameters
    _ptrAudioBuffer->SetPlayoutSampleRate(kPlayoutFixedSampleRate);
    _ptrAudioBuffer->SetPlayoutChannels(kPlayoutNumChannels);
  }

  return 0;
}

int32_t WhisperAudioDevice::PlayoutIsAvailable(bool& available) {
  available = true;
  return 0;
}

bool WhisperAudioDevice::PlayoutIsInitialized() const {
  return _playoutFramesIn10MS != 0;
}

int32_t WhisperAudioDevice::StartPlayout() {
  if (_playing) {
    return 0;
  }

  _playing = true;
  _playoutFramesLeft = 0;

  if (!_playoutBuffer) {
    _playoutBuffer = new int8_t[kPlayoutBufferSize];
  }
  if (!_playoutBuffer) {
    _playing = false;
    return -1;
  }

  #if defined(PLAY_WAV_ON_PLAY)
  if (!_wavFilename.empty()) {
    _playFile = FileWrapper::OpenReadOnly(_wavFilename);
    if (!_playFile.is_open()) {
      RTC_LOG(LS_ERROR) << "Failed to open 'playout' file: " << _wavFilename;
      _playing = false;
      delete[] _playoutBuffer;
      _playoutBuffer = NULL;
      return -1;
    }
  }
  #endif // defined(PLAY_WAV_ON_PLAY)

  // "PLAYOUT"
  _ptrThreadPlay = rtc::PlatformThread::SpawnJoinable(
      [this] {
        while (PlayThreadProcess()) {
        }
      },
      "webrtc_audio_module_play_thread",
      rtc::ThreadAttributes().SetPriority(rtc::ThreadPriority::kRealtime));

  RTC_LOG(LS_INFO) << "Started playout...";
  return 0;
}

int32_t WhisperAudioDevice::StopPlayout() {
  {
    MutexLock lock(&mutex_);
    _playing = false;
  }

  // stop playout thread first
  if (!_ptrThreadPlay.empty())
    _ptrThreadPlay.Finalize();

  if(_llama_device) {
    _llama_device->Stop();    
  }

  if (_whisper_transcriber) {
      _whisper_transcriber->Stop();
  }  

  MutexLock lock(&mutex_);

  _playoutFramesLeft = 0;
  delete[] _playoutBuffer;
  _playoutBuffer = NULL;

  RTC_LOG(LS_INFO) << "Stopped playout capture from file: "
                   << _wavFilename;
  return 0;
}

bool WhisperAudioDevice::PlayThreadProcess() {
  if (!_playing) {
    return false;
  }

  int64_t currentTime = rtc::TimeMillis();
  mutex_.Lock();

  if (_lastCallPlayoutMillis == 0 ||
      currentTime - _lastCallPlayoutMillis >= 10) {
    mutex_.Unlock();
    _ptrAudioBuffer->RequestPlayoutData(_playoutFramesIn10MS);
    mutex_.Lock();

    _playoutFramesLeft = _ptrAudioBuffer->GetPlayoutData(_playoutBuffer);
    RTC_DCHECK_EQ(_playoutFramesIn10MS, _playoutFramesLeft);

    #if defined(PLAY_WAV_ON_PLAY)
    if (_playFile.is_open()) {
      if (_playFile.Read(_playoutBuffer, kPlayoutBufferSize) > 0) {
        #if defined(DUMP_WAV_ON_PLAY)
        HexPrinter::Dump((const uint8_t*) _playoutBuffer, kPlayoutBufferSize);
        #endif
      } else {
        _playFile.Rewind();
      }
      if(_playFile.ReadEof())
        _playFile.Close();
    }
    #endif // defined(PLAY_WAV_ON_PLAY)

    if(_whisper_transcriber)
      _whisper_transcriber->ProcessAudioBuffer((uint8_t*)_playoutBuffer, kPlayoutBufferSize);

    _lastCallPlayoutMillis = currentTime;
  }

  _playoutFramesLeft = 0;
  mutex_.Unlock();

  int64_t deltaTimeMillis = rtc::TimeMillis() - currentTime;
  if (deltaTimeMillis < 10) {
    SleepMs(10 - deltaTimeMillis);
  }

  return true;
}

bool WhisperAudioDevice::Playing() const {
  return _playing;
}

int32_t WhisperAudioDevice::InitSpeaker() {
  return 0;
}

bool WhisperAudioDevice::SpeakerIsInitialized() const {
  return true;
}

//
// Pure virtual ooverrides
//

// Other required methods remain the same as in previous implementation
// (Dummy implementations for methods not specifically required)
int32_t WhisperAudioDevice::SpeakerVolumeIsAvailable(bool& /* available */) {
  return -1;
}
int32_t WhisperAudioDevice::SetSpeakerVolume(uint32_t /* volume */) {
  return -1;
}
int32_t WhisperAudioDevice::SpeakerVolume(uint32_t& /* volume */) const {
  return -1;
}
int32_t WhisperAudioDevice::MaxSpeakerVolume(uint32_t& /* maxVolume */) const {
  return -1;
}
int32_t WhisperAudioDevice::MinSpeakerVolume(uint32_t& /* minVolume */) const {
  return -1;
}
int32_t WhisperAudioDevice:: MicrophoneVolumeIsAvailable(bool& /* available */) {
  return -1;
}
int32_t WhisperAudioDevice::SetMicrophoneVolume(uint32_t /* volume */) {
  return -1;
}
int32_t WhisperAudioDevice::MicrophoneVolume(uint32_t& /* volume */) const {
  return -1;
}
int32_t WhisperAudioDevice::MaxMicrophoneVolume(uint32_t& /* maxVolume */) const {
  return -1;
}
int32_t WhisperAudioDevice::MinMicrophoneVolume(uint32_t& /* minVolume */) const {
  return -1;
}
int32_t WhisperAudioDevice::SpeakerMuteIsAvailable(bool& /* available */) {
  return -1;
}
int32_t WhisperAudioDevice::SetSpeakerMute(bool /* enable */) {
  return -1;
}
int32_t WhisperAudioDevice::SpeakerMute(bool& /* enabled */) const {
  return -1;
}
int32_t WhisperAudioDevice::MicrophoneMuteIsAvailable(bool& /* available */) {
  return -1;
}
int32_t WhisperAudioDevice::SetMicrophoneMute(bool /* enable */) {
  return -1;
}
int32_t WhisperAudioDevice::MicrophoneMute(bool& /* enabled */) const {
  return -1;
}
int32_t WhisperAudioDevice::StereoPlayoutIsAvailable(bool& /* available */) {
  return -1;
}
int32_t WhisperAudioDevice::SetStereoPlayout(bool /* enable */) {
  return -1;
}
int32_t WhisperAudioDevice::StereoPlayout(bool& /* enabled */) const {
  return -1;
}
int32_t WhisperAudioDevice::StereoRecordingIsAvailable(bool& /* available */) {
  return -1;
}
int32_t WhisperAudioDevice::SetStereoRecording(bool /* enable */) {
  return -1;
}
int32_t WhisperAudioDevice::StereoRecording(bool& /* enabled */) const {
  return -1;
}
int32_t WhisperAudioDevice::PlayoutDelay(uint16_t& delayMS) const {
  delayMS = _lastCallPlayoutMillis;
  return 0;
}
}  // namespace webrtc
