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

// DirectCallee Implementation
DirectCallee::DirectCallee(
    const int local_port,
    const bool enable_encryption,
    const bool enable_video,
    const bool enable_whisper
    ) 
    : DirectPeer(false, enable_encryption, enable_video, enable_whisper), 
    local_port_(local_port) {}

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
        addr.sin_port = htons(local_port_);
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
        
        listen_socket_->SignalNewConnection.connect(this, &DirectCallee::OnNewConnection);

        RTC_LOG(LS_INFO) << "Server listening on port " << local_port_;
        return true;
    };
    return network_thread()->BlockingCall(std::move(task));
}

void DirectCallee::OnNewConnection(rtc::AsyncListenSocket* socket, 
                                 rtc::AsyncPacketSocket* new_socket) {
    RTC_LOG(LS_INFO) << "New connection received";
    
    if (!new_socket) {
        RTC_LOG(LS_ERROR) << "New socket is null";
        return;
    }

    tcp_socket_.reset(static_cast<rtc::AsyncTCPSocket*>(new_socket));
    RTC_LOG(LS_INFO) << "Connection accepted from " << tcp_socket_->GetRemoteAddress().ToString();

    tcp_socket_->RegisterReceivedPacketCallback(
        [this](rtc::AsyncPacketSocket* socket, const rtc::ReceivedPacket& packet) {
            RTC_LOG(LS_INFO) << "Received packet of size: " << packet.payload().size();
            OnMessage(socket, packet.payload().data(), packet.payload().size(), 
                     packet.source_address());
        });
    
    RTC_LOG(LS_INFO) << "Callback registered for incoming messages";
}

void DirectCallee::OnMessage(rtc::AsyncPacketSocket* socket,
                           const unsigned char* data,
                           size_t len,
                           const rtc::SocketAddress& remote_addr) {
    std::string message((const char*)data, len);
    RTC_LOG(LS_INFO) << "Callee received: " << message;

    if (message == "HELLO") {
        SendMessage("WELCOME");
    } else if (message == "BYE") {
        SendMessage("OK");
        Shutdown();
        QuitThreads();
    } else {
        HandleMessage(socket, message, remote_addr);
    }
}
