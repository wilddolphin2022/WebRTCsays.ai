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
// DirectCaller Implementation
DirectCaller::DirectCaller(Options opts)
    : DirectPeer(opts) {}

DirectCaller::~DirectCaller() {
    if (tcp_socket_) {
        tcp_socket_->Close();
    }
}

bool DirectCaller::Connect() {
    auto task = [this]() -> bool {
        // Create raw socket
        int raw_socket = ::socket(AF_INET, SOCK_STREAM, 0);
        if (raw_socket < 0) {
            RTC_LOG(LS_ERROR) << "Failed to create raw socket, errno: " << errno;
            return false;
        }

        // Setup local address
        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = 0;
        local_addr.sin_addr.s_addr = INADDR_ANY;

        // Bind
        if (::bind(raw_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
            RTC_LOG(LS_ERROR) << "Failed to bind raw socket, errno: " << errno;
            ::close(raw_socket);
            return false;
        }

        // Setup remote address
        struct sockaddr_in remote_addr;
        memset(&remote_addr, 0, sizeof(remote_addr));
        remote_addr.sin_family = AF_INET;
        remote_addr.sin_port = htons(opts_.port);
        inet_pton(AF_INET, opts_.ip.c_str(), &remote_addr.sin_addr);

        // Store remote address for later use
        remote_addr_ = rtc::SocketAddress(opts_.ip, opts_.port);

        RTC_LOG(LS_INFO) << "Attempting to connect to " << remote_addr_.ToString(); 

        // Connect
        if (::connect(raw_socket, (struct sockaddr*)&remote_addr, sizeof(remote_addr)) < 0) {
            RTC_LOG(LS_ERROR) << "Failed to connect raw socket, errno: " << errno;
            ::close(raw_socket);
            return false;
        }

        RTC_LOG(LS_INFO) << "Raw socket connected successfully";

        // Wrap the connected socket
        auto wrapped_socket = pss()->WrapSocket(raw_socket);
        if (!wrapped_socket) {
            RTC_LOG(LS_ERROR) << "Failed to wrap socket, errno: " << errno;
            ::close(raw_socket);
            return false;
        }

        tcp_socket_ = std::make_unique<rtc::AsyncTCPSocket>(wrapped_socket);
        SetupSocket(tcp_socket_.get());
        
        OnConnect(tcp_socket_.get());
        return true;
    };
    return network_thread()->BlockingCall(std::move(task));
}

void DirectCaller::OnConnect(rtc::AsyncPacketSocket* socket) {
    // Now remote_addr_ will be properly set
    RTC_LOG(LS_INFO) << "Connected to " << remote_addr_.ToString();
    
    // Start the message sequence
    network_thread()->PostTask([this]() {
        SendMessage("HELLO");
    });
}

void DirectCaller::OnMessage(rtc::AsyncPacketSocket* socket,
                           const unsigned char* data,
                           size_t len,
                           const rtc::SocketAddress& remote_addr) {
  if(!CheckConnection(socket)) {
    return;
  }

  std::string message((const char*)data, len);
  RTC_LOG(LS_INFO) << "Caller received: " << message;

  if (message == "WELCOME") {
      SendMessage("INIT");
  } 
  else if (message == "WAITING") {
      Start();
  } 
  else if (message == "OK") {
      Shutdown();
      QuitThreads();
  } else {
      HandleMessage(socket, message, remote_addr);
  }
}
