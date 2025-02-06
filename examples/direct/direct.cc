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

#include "direct.h"

#include "utils.h"

// DirectApplication Implementation
DirectApplication::DirectApplication() {
  pss_ = std::make_unique<rtc::PhysicalSocketServer>();

  main_thread_ = rtc::Thread::CreateWithSocketServer();
  main_thread_->socketserver()->SetMessageQueue(main_thread_.get());
  main_thread_->SetName("Main", nullptr);
  main_thread_->WrapCurrent();

  worker_thread_ = rtc::Thread::Create();
  signaling_thread_ = rtc::Thread::Create();
  network_thread_ = std::make_unique<rtc::Thread>(pss_.get());
  network_thread_->socketserver()->SetMessageQueue(network_thread_.get());
}

void DirectApplication::CleanupSocketServer() {
  if (rtc::Thread::Current() != main_thread_.get()) {
    main_thread_->PostTask([this]() { CleanupSocketServer(); });
    return;
  }

  // Stop threads in reverse order
  if (network_thread_) {
    network_thread_->Stop();
    network_thread_.reset();
  }
  if (worker_thread_) {
    worker_thread_->Stop();
    worker_thread_.reset();
  }
  if (signaling_thread_) {
    signaling_thread_->Stop();
    signaling_thread_.reset();
  }
  if (main_thread_) {
    main_thread_->UnwrapCurrent();
    main_thread_.reset();
  }
}

void DirectApplication::Run() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  rtc::Event quit_event;

  // Set up quit handler on network thread
  network_thread_->PostTask([this, &quit_event]() {
    while (!should_quit_) {
      rtc::Thread::Current()->ProcessMessages(100);
    }
    quit_event.Set();
  });

  // Process messages on main thread until network thread signals quit
  while (!quit_event.Wait(webrtc::TimeDelta::Millis(0))) {
    rtc::Thread::Current()->ProcessMessages(100);
  }

  // Final cleanup
  CleanupSocketServer();
}

DirectApplication::~DirectApplication() {
  CleanupSocketServer();
}

bool DirectApplication::Initialize() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  if (!worker_thread_->Start() || !signaling_thread_->Start() ||
      !network_thread_->Start()) {
    RTC_LOG(LS_ERROR) << "Failed to start threads";
    return false;
  }
  return true;
}

void DirectApplication::HandleMessage(rtc::AsyncPacketSocket* socket,
                                      const std::string& message,
                                      const rtc::SocketAddress& remote_addr) {
  if (message.find("ICE:") == 0) {
    ice_candidates_received_++;
    SendMessage("ICE_ACK:" + std::to_string(ice_candidates_received_));

    // Send our ICE candidate if we haven't sent all
    if (ice_candidates_sent_ < kMaxIceCandidates) {
      ice_candidates_sent_++;
      SendMessage("ICE:" + std::to_string(ice_candidates_sent_));
    }
    // Start SDP exchange when ICE is complete
    else if (ice_candidates_received_ >= kMaxIceCandidates &&
             sdp_fragments_sent_ == 0) {
      sdp_fragments_sent_++;
      SendMessage("SDP:" + std::to_string(sdp_fragments_sent_));
    }
  } else if (message.find("SDP:") == 0) {
    sdp_fragments_received_++;
    SendMessage("SDP_ACK:" + std::to_string(sdp_fragments_received_));

    // Send our SDP fragment if we haven't sent all
    if (sdp_fragments_sent_ < kMaxSdpFragments) {
      sdp_fragments_sent_++;
      SendMessage("SDP:" + std::to_string(sdp_fragments_sent_));
    }
    // Send BYE when all exchanges are complete
    else if (sdp_fragments_received_ >= kMaxSdpFragments &&
             ice_candidates_received_ >= kMaxIceCandidates) {
      SendMessage("BYE");
    }
  }
}

bool DirectApplication::SendMessage(const std::string& message) {
  if (!tcp_socket_) {
    RTC_LOG(LS_ERROR) << "Cannot send message, socket is null";
    return false;
  }
  RTC_LOG(LS_INFO) << "Sending message: " << message;
  size_t sent = tcp_socket_->Send(message.c_str(), message.length(),
                                  rtc::PacketOptions());
  if (sent <= 0) {
    RTC_LOG(LS_ERROR) << "Failed to send message, error: " << errno;
    return false;
  }
  RTC_LOG(LS_INFO) << "Successfully sent " << sent << " bytes";
  return true;
}

int main(int argc, char* argv[]) {
  Options opts = parseOptions(argc, argv);

  if (argc==1||opts.help) {
    std::string usage = opts.help_string;
    RTC_LOG(LS_ERROR) << usage;
    return 1;
  }

  RTC_LOG(LS_INFO) << getUsage(opts);

  std::string ip = "127.0.0.1";
  int port = 3456;

  if (!ParseIpAndPort(opts.address, ip, port)) {
    RTC_LOG(LS_ERROR) << "address:port combo is invalid";
    return 1;
  }

  std::cout << "starting direct..." << std::endl;
  rtc::InitializeSSL();

  if (opts.mode == "caller") {
    std::cout << "mode is caller..." << std::endl;
    DirectCaller caller(rtc::SocketAddress(ip, port), opts.encryption);
    if (!caller.Initialize()) {
      RTC_LOG(LS_ERROR) << "failed to initialize caller";
      return 1;
    }
    if (!caller.Connect()) {
      RTC_LOG(LS_ERROR) << "failed to connect";
      return 1;
    }
    caller.Run();
  } else if (opts.mode == "callee") {
    std::cout << "mode is callee..." << std::endl;
    DirectCallee callee(port, opts.encryption);
    callee.SetEnableWhisper(opts.whisper);
    if (!callee.Initialize()) {
      RTC_LOG(LS_ERROR) << "Failed to initialize callee";
      return 1;
    }
    if (!callee.StartListening()) {
      RTC_LOG(LS_ERROR) << "Failed to start listening";
      return 1;
    }
    callee.Run();
  } else {
    RTC_LOG(LS_ERROR) << "Invalid mode: " << opts.mode;
    return 1;
  }

  rtc::CleanupSSL();  // Changed from rtc::CleanupSSL()
  return 0;
}