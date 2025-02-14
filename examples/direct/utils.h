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

#include <memory>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <atomic>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdlib> // for std::getenv
#include <cstdio>  // for FILE, fopen, fclose
#include <fstream>
#include <sstream>
#include <future>
#include <algorithm>
#include <cstdlib> // for getenv

#include <sys/socket.h>
#include <netinet/tcp.h>

#include "rtc_base/logging.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/rtc_certificate_generator.h"

// Function to parse IP address and port from a string in the format "IP:PORT"
bool ParseIpAndPort(const std::string& ip_port, std::string& ip, int& port);

// Function to create a self-signed certificate
rtc::scoped_refptr<rtc::RTCCertificate> CreateCertificate();

// Function to load a certificate from PEM files
rtc::scoped_refptr<rtc::RTCCertificate> LoadCertificate(const std::string& cert_path, const std::string& key_path);

// Function to load certificate from environment variables or fall back to CreateCertificate
rtc::scoped_refptr<rtc::RTCCertificate> LoadCertificateFromEnv();

// Command line options
struct Options {
    bool is_caller = false; 
    bool encryption = true;
    bool whisper = true;
    bool video = false;
    bool help = false;
    std::string help_string;
    std::string whisper_model;
    std::string llama_model;
    std::string webrtc_cert_path = "cert.pem";
    std::string webrtc_key_path = "key.pem";
    std::string webrtc_speech_initial_playout_wav = "play.wav";
    std::string ip = "127.0.0.1";
    int port = 3456;
};

// Function to parse command line string to above options
Options parseOptions(int argc, char* argv[]);

// Function to get command line options to a string, to print or speak
std::string getUsage(const Options opts);