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
// DirectCallee Implementation
DirectCallee::DirectCallee(Options opts) : DirectPeer(opts) {}

DirectCallee::~DirectCallee() {
  if (tcp_socket_) {
    tcp_socket_->Close();
  }
  if (listen_socket_) {
    listen_socket_.reset();
  }
  CleanupSocketServer();
}

bool DirectCallee::StartListening() {
  auto task = [this]() -> bool {
    // Create raw socket
    int raw_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (raw_socket < 0) {
      RTC_LOG(LS_ERROR) << "Failed to create socket, errno: " << errno;
      return false;
    }

    // Setup server address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(opts_.port);
    addr.sin_addr.s_addr = INADDR_ANY;

    // Bind
    if (::bind(raw_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      RTC_LOG(LS_ERROR) << "Failed to bind, errno: " << errno;
      ::close(raw_socket);
      return false;
    }

    // Listen
    if (::listen(raw_socket, 5) < 0) {
      RTC_LOG(LS_ERROR) << "Failed to listen, errno: " << errno;
      ::close(raw_socket);
      return false;
    }

    // Wrap the listening socket
    auto wrapped_socket = pss()->WrapSocket(raw_socket);
    if (!wrapped_socket) {
      RTC_LOG(LS_ERROR) << "Failed to wrap socket";
      ::close(raw_socket);
      return false;
    }

    listen_socket_ = std::make_unique<rtc::AsyncTcpListenSocket>(
        std::unique_ptr<rtc::Socket>(wrapped_socket));

    listen_socket_->SignalNewConnection.connect(this,
                                                &DirectCallee::OnNewConnection);

    RTC_LOG(LS_INFO) << "Server listening on port " << opts_.port;
    return true;
  };
  return network_thread()->BlockingCall(std::move(task));
}

void DirectCallee::OnNewConnection(rtc::AsyncListenSocket* socket,
                                   rtc::AsyncPacketSocket* new_socket) {
  if (!new_socket) {
    RTC_LOG(LS_ERROR) << "New socket is null";
    return;
  }

  // If we have an existing connection, clean it up properly
  if (tcp_socket_) {
    RTC_LOG(LS_INFO) << "Closing existing connection for new one";
    tcp_socket_->DeregisterReceivedPacketCallback();
    tcp_socket_->UnsubscribeCloseEvent(this);
    tcp_socket_->Close();
    tcp_socket_ = nullptr;
  }

  // Reset all connection state
  ice_candidates_sent_ = 0;
  ice_candidates_received_ = 0;
  sdp_fragments_sent_ = 0;
  sdp_fragments_received_ = 0;
  is_disconnected_ = false;  // Reset disconnect state for new connection

  // new_socket is already an AsyncTCPSocket
  tcp_socket_ = std::unique_ptr<rtc::AsyncTCPSocket>(
      static_cast<rtc::AsyncTCPSocket*>(new_socket));
  SetupSocket(tcp_socket_.get());

  RTC_LOG(LS_INFO) << "New connection accepted from "
                   << tcp_socket_->GetRemoteAddress().ToString()
                   << ", waiting for HELLO";
}

void DirectCallee::OnMessage(rtc::AsyncPacketSocket* socket,
                             const unsigned char* data,
                             size_t len,
                             const rtc::SocketAddress& remote_addr) {
  if (!CheckConnection(socket)) {
    return;
  }
  
  std::string message((const char*)data, len);
  RTC_LOG(LS_INFO) << "Callee received: " << message;

  if (message == "HELLO") {
    RTC_LOG(LS_INFO) << "Received HELLO, sending WELCOME";
    SendMessage("WELCOME");
  } else if (message == "INIT") {
    RTC_LOG(LS_INFO) << "Received INIT, starting WebRTC";
    signaling_thread()->PostTask([this]() {
      Start();  // Start WebRTC on signaling thread
      SendMessage("WAITING");
    });
  } else if (message == "BYE") {
    RTC_LOG(LS_INFO) << "Received BYE, cleaning up connection";
    SendMessage("OK");
    
    // Clean up on appropriate threads
    signaling_thread()->PostTask([this]() {
      Shutdown();  // Clean up WebRTC
      
      // Then clean up socket on network thread
      network_thread()->PostTask([this]() {
        if (tcp_socket_) {
          tcp_socket_->DeregisterReceivedPacketCallback();
          tcp_socket_->UnsubscribeCloseEvent(this);
          tcp_socket_->Close();
          tcp_socket_ = nullptr;
        }
        RTC_LOG(LS_INFO) << "Ready for new connections";
      });
    });
  } else {
    HandleMessage(socket, message, remote_addr);
  }
}
