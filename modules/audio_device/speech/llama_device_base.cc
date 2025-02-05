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

#include <llama.h>
#include <thread>

#include "llama_device_base.h"
#include "rtc_base/logging.h"
#include "whisper_helpers.h"

LlamaSimpleChat::LlamaSimpleChat() = default;

LlamaSimpleChat::~LlamaSimpleChat() {
    if (smpl_) {
        llama_sampler_free(smpl_);
    }
    if (ctx_) {
        llama_free(ctx_);
    }
    if (model_) {
        llama_model_free(model_);
    }
}

bool LlamaSimpleChat::SetModelPath(const std::string& path) {
    model_path_ = path;
  RTC_LOG(LS_INFO) << "SimpleChat model_path_ (4) " << model_path_;
    return true;
}

bool LlamaSimpleChat::SetNGL(int layers) {
    ngl_ = layers;
    return true;
}

bool LlamaSimpleChat::SetContextSize(int size) {
    n_predict_ = size;
    return true;
}

void LlamaSimpleChat::StopGeneration() {
    continue_ = false;
}

bool LlamaSimpleChat::Initialize(SpeechAudioDevice* speech_audio_device) {
    _speech_audio_device = speech_audio_device;
    ggml_backend_load_all();
    return LoadModel() && InitializeContext();
}

bool LlamaSimpleChat::LoadModel() {
    if (model_path_.empty()) {
        RTC_LOG(LS_ERROR) << "Model path not set.";
        return false;
    }

    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = ngl_;
    model_ = llama_model_load_from_file(model_path_.c_str(), model_params);
    if (!model_) {
        RTC_LOG(LS_ERROR) << "Unable to load model.";
        return false;
    }
    vocab_ = llama_model_get_vocab(model_);
    return true;
}

bool LlamaSimpleChat::InitializeContext() {
    if (!model_ || !vocab_) {
        RTC_LOG(LS_ERROR) << "Model or vocab not loaded.";
        return false;
    }

    // Tokenize the prompt
    const int n_prompt = -llama_tokenize(vocab_, prompt_.c_str(), prompt_.size(), NULL, 0, true, true);
    std::vector<llama_token> prompt_tokens(n_prompt);
    if (llama_tokenize(vocab_, prompt_.c_str(), prompt_.size(), prompt_tokens.data(), prompt_tokens.size(), true, true) < 0) {
        RTC_LOG(LS_ERROR) << "Failed to tokenize the prompt.";
        return false;
    }

    // Setup context parameters
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = n_prompt + n_predict_ - 1;
    ctx_params.n_batch = n_prompt;
    ctx_params.no_perf = false;

    ctx_ = llama_init_from_model(model_, ctx_params);
    if (!ctx_) {
        RTC_LOG(LS_ERROR) << "Failed to create the llama_context.";
        return false;
    }

    // Initialize sampler
    smpl_ = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl_, llama_sampler_init_min_p(0.05f, 1));
    llama_sampler_chain_add(smpl_, llama_sampler_init_temp(0.8f));
    llama_sampler_chain_add(smpl_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));


    return true;
}

std::string LlamaSimpleChat::generate(const std::string& prompt) {
    std::string response;
    std::string current_phrase;
    bool answer_started = false;
 
    const int n_prompt_tokens = -llama_tokenize(vocab_, prompt.c_str(), prompt.size(), NULL, 0, true, true);
    std::vector<llama_token> prompt_tokens(n_prompt_tokens);
    if (llama_tokenize(vocab_, prompt.c_str(), prompt.size(), prompt_tokens.data(), 
        prompt_tokens.size(), llama_get_kv_cache_used_cells(ctx_) == 0, true) < 0) {
        RTC_LOG(LS_ERROR) << "failed to tokenize the prompt";
        return "";
    }

    llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
    llama_token new_token_id;

    continue_ = true;
    int bos_found = 0;

    while (true) {
        if (!continue_) {
            if (_speech_audio_device && !current_phrase.empty()) {
                _speech_audio_device->speakText(current_phrase);
            }
            break;
        }

        int n_ctx = llama_n_ctx(ctx_);
        int n_ctx_used = llama_get_kv_cache_used_cells(ctx_);
        if (n_ctx_used + batch.n_tokens > n_ctx) {
            RTC_LOG(LS_ERROR) << "context size exceeded";
            break;
        }

        if (llama_decode(ctx_, batch)) {
            RTC_LOG(LS_ERROR) << "failed to decode";
            break;
        }

        new_token_id = llama_sampler_sample(smpl_, ctx_, -1);

        if (llama_vocab_is_eog(vocab_, new_token_id)) {
            break;
        }

        char buf[256];
        int n = llama_token_to_piece(vocab_, new_token_id, buf, sizeof(buf), 0, true);
        if (n < 0) {
            RTC_LOG(LS_ERROR) << "failed to convert token to piece";
            break;
        }

        if (buf[0] == 10) {
            bos_found++;
            batch = llama_batch_get_one(&new_token_id, 1);
            continue;
        }

        if (bos_found > 1) {
            if (!answer_started) {
                answer_started = true;
                bos_found = 0;
            } else if (response.find("Answer: ") != std::string::npos && 
                      response.back() == '.') {
                if (_speech_audio_device && !current_phrase.empty()) {
                    _speech_audio_device->speakText(current_phrase);
                }
                break;
            }
        }

        if (answer_started) {
            std::string s(buf, n);
            HexPrinter::Dump((const uint8_t*)buf, n);

            current_phrase += s;
            if (s.find('.') != std::string::npos) {
                if (_speech_audio_device) {
                    _speech_audio_device->speakText(current_phrase);
                }
                response += current_phrase;
                current_phrase.clear();
            }
        }

        batch = llama_batch_get_one(&new_token_id, 1);
    }

    if (!current_phrase.empty()) {
        response += current_phrase;
    }

    return response;
}

//
// Llama device base
//

#define USE_LLAMA_CHAT 1

LlamaDeviceBase::LlamaDeviceBase(SpeechAudioDevice* speech_audio_device, 
    const std::string& llamaModelFilename)
  : _speech_audio_device(speech_audio_device),
    _llamaModelFilename(llamaModelFilename) 
{
}

LlamaDeviceBase::~LlamaDeviceBase() {}

void LlamaDeviceBase::askLlama(const std::string& text) {

  std::string s(text);
  rtrim(s);
  ltrim(s);
  
  if(text.empty())
    return;

  if (_llama_chat)
    _llama_chat->StopGeneration();

  {
    std::lock_guard<std::mutex> lock(_queueMutex);
    _textQueue.push(s);
  }
  _queueCondition.notify_one();  // Inform one waiting thread that an item is available
}


bool LlamaDeviceBase::RunProcessingThread() {
    
  while(_running) {
    std::string textToAsk;
    bool shouldAsk = false;

    {
      std::unique_lock<std::mutex> lock(_queueMutex);
      if (!_textQueue.empty()) {
        textToAsk = _textQueue.front();
        _textQueue.pop();
        shouldAsk = true;
        RTC_LOG(LS_INFO) << "Llama was asked '" << textToAsk << "'";
      }
    }

    if (shouldAsk) {       
      std::string response = _llama_chat->generate(textToAsk);
      textToAsk.clear();

      RTC_LOG(LS_INFO) << "Llama answered '" << response << "'";

      if(_speech_audio_device) {
        _speech_audio_device->speakText(response); // send to text queue in audio device      
      }
    }
 
    // Sleep if no data available to read to prevent busy-waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  
  return true;
}

bool LlamaDeviceBase::Start() {
    if (!_running) {
        _llama_chat.reset(new LlamaSimpleChat());
        _llama_chat->SetModelPath(_llamaModelFilename);
        if(_llama_chat && _llama_chat->Initialize(_speech_audio_device)) {
          RTC_LOG(LS_INFO) << "Llama chat initialized!";
        }

        _running = true;
        _processingThread = rtc::PlatformThread::SpawnJoinable(
          [this] {
            while (RunProcessingThread()) {
            }
          },
          "llama_processing_thread",
          rtc::ThreadAttributes().SetPriority(rtc::ThreadPriority::kNormal));
    }

    return _running && !_processingThread.empty();
}

void LlamaDeviceBase::Stop() {
    if (_running) {
        _running = false;
        
        _processingThread.Finalize();
    }
}
