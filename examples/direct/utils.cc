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

#include "utils.h"
#include <espeak-ng/espeak_ng.h>
#include <espeak-ng/speak_lib.h>

// Function to parse IP address and port from a string in the format "IP:PORT"
bool ParseIpAndPort(const std::string& ip_port, std::string& ip, int& port) {
    size_t colon_pos = ip_port.find(':');
    if (colon_pos == std::string::npos) {
        RTC_LOG(LS_ERROR) << "Invalid IP:PORT format: " << ip_port;
        return false;
    }

    ip = ip_port.substr(0, colon_pos);
    std::string port_str = ip_port.substr(colon_pos + 1);
    port = std::stoi(port_str);

    if (port < 0 || port > 65535) {
        RTC_LOG(LS_ERROR) << "Invalid port: " << port;
        return false;
    }

    return true;
}

// Function to create a self-signed certificate

rtc::scoped_refptr<rtc::RTCCertificate> CreateCertificate() {
  auto key_params = rtc::KeyParams::RSA(2048); // Use RSA with 2048-bit key
  auto identity = rtc::SSLIdentity::Create("webrtc", key_params);
  if (!identity) {
    RTC_LOG(LS_ERROR) << "Failed to create SSL identity";
    return nullptr;
  }
  return rtc::RTCCertificate::Create(std::move(identity));
}


// Function to read a file into a string
std::string ReadFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) {
    RTC_LOG(LS_ERROR) << "Failed to open file: " << path;
    return "";
  }
  std::ostringstream oss;
  oss << file.rdbuf();
  return oss.str();
}

// Function to load a certificate from PEM files
rtc::scoped_refptr<rtc::RTCCertificate> LoadCertificate(const std::string& cert_path, const std::string& key_path) {
  // Read the certificate and key files
  std::string cert_pem = ReadFile(cert_path);
  std::string key_pem = ReadFile(key_path);

  if (cert_pem.empty() || key_pem.empty()) {
    RTC_LOG(LS_ERROR) << "Failed to read certificate or key file";
    return nullptr;
  }

  // Log the PEM strings for debugging
  RTC_LOG(LS_VERBOSE) << "Certificate PEM:\n" << cert_pem;
  RTC_LOG(LS_VERBOSE) << "Private Key PEM:\n" << key_pem;

  // Create an SSL identity from the PEM strings
  auto identity = rtc::SSLIdentity::CreateFromPEMStrings(key_pem, cert_pem);
  if (!identity) {
    RTC_LOG(LS_ERROR) << "Failed to create SSL identity from PEM strings";
    return nullptr;
  }

  return rtc::RTCCertificate::Create(std::move(identity));
}

// Function to load certificate from environment variables or fall back to CreateCertificate
rtc::scoped_refptr<rtc::RTCCertificate> LoadCertificateFromEnv() {
  // Get paths from environment variables
  const char* cert_path = std::getenv("WEBRTC_CERT_PATH");
  const char* key_path = std::getenv("WEBRTC_KEY_PATH");

  if (cert_path && key_path) {
    RTC_LOG(LS_INFO) << "Loading certificate from " << cert_path << " and " << key_path;
    auto certificate = LoadCertificate(cert_path, key_path);
    if (certificate) {
      return certificate;
    }
    RTC_LOG(LS_WARNING) << "Failed to load certificate from files; falling back to CreateCertificate";
  } else {
    RTC_LOG(LS_WARNING) << "Environment variables WEBRTC_CERT_PATH and WEBRTC_KEY_PATH not set; falling back to CreateCertificate";
  }

  // Fall back to CreateCertificate
  return CreateCertificate();
}

Options parseOptions(int argc, char* argv[]) {
    Options opts;
    // Initialize defaults
    opts.encryption = false;
    opts.whisper = false;
    opts.mode = "";
    opts.webrtc_cert_path = "cert.pem";
    opts.webrtc_key_path = "key.pem";
    opts.webrtc_speech_initial_playout_wav = "play.wav";
    opts.help = false;
    opts.help_string = "Usage:\n"
        "direct [options] [address] [options]\n\n"
        "Options:\n"
        "  --mode <caller|callee>              Set operation mode (default: caller)\n"
        "  --encryption, --no-encryption       Enable/disable encryption (default: disabled)\n"
        "  --whisper, --no-whisper            Enable/disable whisper (default: disabled)\n"
        "  --whisper_model=<path>             Path to whisper model\n"
        "  --llama_model=<path>               Path to llama model\n"
        "  --webrtc_cert_path <path>          Path to WebRTC certificate (default: cert.pem)\n"
        "  --webrtc_key_path <path>           Path to WebRTC key (default: key.pem)\n"
        "  --help                             Show this help message\n"
        "\nExamples:\n"
        "  direct --mode=caller 192.168.1.100:3478 --encryption\n"
        "  direct --mode=callee :3478 --no-encryption\n"
        "  direct 192.168.1.100:3478 --encryption --whisper --whisper_model=model.bin\n";

    // Helper function to check if string is an address
    auto isAddress = [](const std::string& str) {
        return str.find(':') != std::string::npos && 
               (str.find('.') != std::string::npos || str[0] == ':');
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        // Handle parameters with values
        if (arg.find("--mode=") == 0) {
            opts.mode = arg.substr(7);
        } else if (arg == "--encryption") {
            opts.encryption = true;
        } else if (arg == "--whisper") {
            opts.whisper = true;
        } else if (arg.find("--whisper_model=") == 0) {
            opts.whisper_model = arg.substr(16);  // Length of "-whisper_model="
            RTC_LOG(LS_INFO) << "Whisper model path: " << opts.whisper_model;
            if(!opts.whisper) opts.whisper = true;
        } else if (arg.find("--llama_model=") == 0) {
            opts.llama_model = arg.substr(14);  // Length of "-llama_model="
            RTC_LOG(LS_INFO) << "LLAMA model path: " << opts.llama_model;
        } else if (arg.find("--webrtc_cert_path=") == 0) {
            opts.webrtc_cert_path = arg.substr(19);
        }
        else if (arg.find("--webrtc_key_path=") == 0) {
            opts.webrtc_key_path = arg.substr(18);
        }
        else if (arg.find("--webrtc_speech_initial_playout_wav=") == 0) {
            opts.webrtc_speech_initial_playout_wav = arg.substr(36);
        }
        // Handle flags
        else if (arg == "--help") {
            opts.help = true;
        }
        else if (arg == "--encryption") {
            opts.encryption = true;
        }
        else if (arg == "--no-encryption") {
            opts.encryption = false;
        }
        else if (arg == "--whisper") {
            opts.whisper = true;
        }
        else if (arg == "--no-whisper") {
            opts.whisper = false;
        } 
        // Handle address in any position
        else if (isAddress(arg)) {
            opts.address = arg;
            // Only guess mode if not explicitly set
            if (opts.mode.empty()) {
                opts.mode = (arg.find('.') == std::string::npos) ? "callee" : "caller";
            }
        }
    }

    // Load environment variables if paths not provided
    if(opts.webrtc_cert_path.empty()) {
    if (const char* env_cert = std::getenv("WEBRTC_CERT_PATH")) {
        opts.webrtc_cert_path = env_cert;
    }}
    if(opts.webrtc_key_path.empty()) {
    if (const char* env_key = std::getenv("WEBRTC_KEY_PATH")) {
        opts.webrtc_key_path = env_key;
    }}
    if(opts.webrtc_speech_initial_playout_wav.empty()) {
    if (const char* env_wav = std::getenv("WEBRTC_SPEECH_INITIAL_PLAYOUT_WAV")) {
        opts.webrtc_speech_initial_playout_wav = env_wav;
    }}
    if(opts.whisper_model.empty()) {
    if (const char* env_whisper = std::getenv("WHISPER_MODEL")) {
        opts.whisper_model = env_whisper;
    }}
    if(opts.llama_model.empty()) {
    if (const char* env_llama = std::getenv("LLAMA_MODEL")) {
        opts.llama_model = env_llama;
    }}

    return opts;
}

std::string getUsage(const Options opts) {
  std::stringstream usage;

  usage << "\nMode: " << opts.mode << "\n";
  usage << "Encryption: " << (opts.encryption ? "enabled" : "disabled") << "\n";
  usage << "Whisper: " << (opts.whisper ? "enabled" : "disabled") << "\n";
  usage << "Whisper Model: " << opts.whisper_model << "\n";
  usage << "Llama Model: " << opts.llama_model << "\n";
  usage << "WebRTC Cert Path: " << opts.webrtc_cert_path << "\n";
  usage << "WebRTC Key Path: " << opts.webrtc_key_path << "\n";
  usage << "WebRTC Speech Initial Playout WAV: " << opts.webrtc_speech_initial_playout_wav << "\n";
  usage << "IP Address: " << opts.address << "\n";

  return usage.str();
}

// Global promise for speech completion
static std::promise<void>* g_completion_promise = nullptr;

int SynthCallback(short* wav, int numsamples, espeak_EVENT* events) {
    if (events && events->type == espeakEVENT_MSG_TERMINATED) {
        if (g_completion_promise) {
            g_completion_promise->set_value();
        }
    }
    return 0;  // Continue synthesis
}

void speakString(const std::string& s) {
    std::promise<void> completion_promise;
    std::future<void> completion_future = completion_promise.get_future();
    g_completion_promise = &completion_promise;

    int esOK = espeak_Initialize(AUDIO_OUTPUT_PLAYBACK, 0, nullptr, 0);
    if(esOK == EE_INTERNAL_ERROR) {
        RTC_LOG(LS_ERROR) << "Failed to initialize espeak-ng";
        g_completion_promise = nullptr;
        return;
    }

    espeak_SetSynthCallback(SynthCallback);

    espeak_VOICE voice;
    memset(&voice, 0, sizeof(espeak_VOICE));
    voice.languages = "en";
    voice.name = "US";
    voice.variant = 1;
    voice.gender = 1;
    espeak_SetVoiceByProperties(&voice);

    espeak_SetParameter(espeakRATE, 150, 0);
    espeak_SetParameter(espeakVOLUME, 100, 0);
    espeak_SetParameter(espeakPITCH, 50, 0);
    espeak_SetParameter(espeakRANGE, 50, 0);

    int synth_flags = espeakCHARS_AUTO | espeakENDPAUSE;
    esOK = espeak_Synth(s.c_str(), s.length() + 1, 0, POS_CHARACTER, 0, synth_flags, nullptr, nullptr);
    
    if (esOK != EE_OK) {
        RTC_LOG(LS_ERROR) << "Speech synthesis failed";
        g_completion_promise = nullptr;
        return;
    }

    // Wait for completion
    completion_future.wait();
    g_completion_promise = nullptr;
}
