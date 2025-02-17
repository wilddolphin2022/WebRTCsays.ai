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

#if defined(WEBRTC_LINUX)
extern "C" { void __libc_csu_init() {} void __libc_csu_fini() {} }
#endif

// Function to parse IP address and port from a string in the format "IP:PORT"
bool ParseIpAndPort(std::string& input, Options& opts) {
    
    // Check if it's a URL
    if (input.find("http://") == 0) {
        input = input.substr(7);
        opts.is_url = true;
    } else if (input.find("https://") == 0) {
        input = input.substr(8);
        opts.port = 443;  // Default HTTPS port
        opts.is_url = true;
    }

    // If it's a URL, handle path and query parameters
    if (opts.is_url && opts.room.empty()) {
        size_t slash_pos = input.find('/');
        if (slash_pos != std::string::npos) {
            std::string path = input.substr(slash_pos);
            input = input.substr(0, slash_pos);
            
            // Parse room ID from query parameters
            size_t room_pos = path.find("roomId=");
            if (room_pos != std::string::npos) {
                opts.room = path.substr(room_pos + 7);  // Skip "roomId="
                size_t end_pos = opts.room.find('&');
                if (end_pos != std::string::npos) {
                    opts.room = opts.room.substr(0, end_pos);
                }
            }
        }
    }

    // Extract host and port
    size_t colon_pos = input.find(':');
    if (colon_pos != std::string::npos) {
        opts.ip = input.substr(0, colon_pos);
        std::string port_str = input.substr(colon_pos + 1);
        if (!port_str.empty()) {
            char* end;
            long port_val = strtol(port_str.c_str(), &end, 10);
            if (*end != '\0' || port_val < 0 || port_val > 65535) {
                RTC_LOG(LS_ERROR) << "Invalid port number: " << port_str;
                return false;
            }
            opts.port = static_cast<int>(port_val);
        }
    } else {
        opts.ip = input;
        // Keep existing port if it's a URL with port 443, otherwise use default
        if (!opts.is_url) {
            opts.port = 3456;  // Default port for non-URL connections
        }
    }

    // Clean up hostname
    if (opts.ip.empty()) {
        opts.ip = "127.0.0.1";
    }

    // Validate port
    if (opts.port < 0 || opts.port > 65535) {
        RTC_LOG(LS_ERROR) << "Invalid port: " << opts.port;
        return false;
    }

    RTC_LOG(LS_INFO) << "Parsed address - IP: " << opts.ip << ", Port: " << opts.port  
                     << (opts.is_url ? " (URL)" : " (Direct)");
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
rtc::scoped_refptr<rtc::RTCCertificate> LoadCertificateFromEnv(Options opts) {
  // Get paths from environment variables
  const char* cert_path = opts.webrtc_cert_path.empty() ? std::getenv("WEBRTC_CERT_PATH") : opts.webrtc_cert_path.c_str();
  const char* key_path = opts.webrtc_key_path.empty() ? std::getenv("WEBRTC_KEY_PATH") : opts.webrtc_key_path.c_str();

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
    opts.is_caller = false;
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
        "  --webrtc_cert_path=<path>          Path to WebRTC certificate (default: cert.pem)\n"
        "  --webrtc_key_path=<path>           Path to WebRTC key (default: key.pem)\n"
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
            std::string mode = arg.substr(7);
            if(mode == "caller")
                opts.is_caller = true;
            else if(mode == "callee")
                opts.is_caller = false;
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
        else if (arg.find("--room=") == 0) {
            opts.room = arg.substr(7);
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
        else if (arg == "--video") {
            opts.video = true;
        }
        else if (arg == "--no-video") {
            opts.video = false;
        }
        // Handle address in any position
        else if (isAddress(arg)) {
            std::string address = arg;
            opts.ip = "127.0.0.1";
            opts.port = 3456;
            if (!ParseIpAndPort(address, opts)) {
                RTC_LOG(LS_ERROR) << "address:port combo is invalid";

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

  usage << "\nMode: " << (opts.is_caller ? "caller" : "callee") << "\n"; 
  usage << "Encryption: " << (opts.encryption ? "enabled" : "disabled") << "\n";
  usage << "Whisper: " << (opts.whisper ? "enabled" : "disabled") << "\n";
  usage << "Video: " << (opts.video ? "enabled" : "disabled") << "\n";
  usage << "Whisper Model: " << opts.whisper_model << "\n";
  usage << "Llama Model: " << opts.llama_model << "\n";
  usage << "WebRTC Cert Path: " << opts.webrtc_cert_path << "\n";
  usage << "WebRTC Key Path: " << opts.webrtc_key_path << "\n";
  usage << "WebRTC Speech Initial Playout WAV: " << opts.webrtc_speech_initial_playout_wav << "\n";
  usage << "IP Address: " << opts.ip << "\n";
  usage << "Port: " << opts.port << "\n";
  usage << "Room: " << opts.room << "\n";
  return usage.str();
}
