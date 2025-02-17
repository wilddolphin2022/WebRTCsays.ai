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
#include <json/json.h>
#include "rtc_base/strings/json.h"

class SocketStream : public rtc::StreamInterface {
public:
    explicit SocketStream(rtc::Socket* socket) : socket_(socket) {}

    rtc::StreamState GetState() const override {
        return socket_ ? rtc::SS_OPEN : rtc::SS_CLOSED;
    }

    rtc::StreamResult Read(rtc::ArrayView<uint8_t> buffer,
                          size_t& read,
                          int& error) override {
        int64_t err = 0;
        int result = socket_->Recv(buffer.data(), buffer.size(), &err);
        if (result > 0) {
            read = result;
            return rtc::SR_SUCCESS;
        }
        error = err;
        if (err == EWOULDBLOCK) 
            return rtc::SR_BLOCK;
        return rtc::SR_ERROR;
    }

    rtc::StreamResult Write(rtc::ArrayView<const uint8_t> data,
                           size_t& written,
                           int& error) override {
        int result = socket_->Send(data.data(), data.size());
        if (result > 0) {
            written = result;
            return rtc::SR_SUCCESS;
        }
        if (error == EWOULDBLOCK)
            return rtc::SR_BLOCK;
        return rtc::SR_ERROR;
    }

    void Close() override {
        if (socket_) {
            socket_->Close();
        }
    }

private:
    rtc::Socket* socket_;
};

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

        // Setup remote address
        struct sockaddr_in remote_addr;
        memset(&remote_addr, 0, sizeof(remote_addr));
        remote_addr.sin_family = AF_INET;
        remote_addr.sin_port = htons(opts_.port);
        
        // Handle hostname resolution
        struct hostent* he = gethostbyname(opts_.ip.c_str());
        if (!he) {
            RTC_LOG(LS_ERROR) << "Failed to resolve hostname: " << opts_.ip;
            ::close(raw_socket);
            return false;
        }
        memcpy(&remote_addr.sin_addr, he->h_addr_list[0], he->h_length);

        // Store remote address for later use
        remote_addr_ = rtc::SocketAddress(opts_.ip, opts_.port);

        RTC_LOG(LS_INFO) << "Attempting to connect to " << remote_addr_.ToString();

        // Connect socket
        if (::connect(raw_socket, (struct sockaddr*)&remote_addr, sizeof(remote_addr)) < 0) {
            RTC_LOG(LS_ERROR) << "Failed to connect raw socket, errno: " << errno;
            ::close(raw_socket);
            return false;
        }

        auto wrapped_socket = pss()->WrapSocket(raw_socket);
        if (!wrapped_socket) {
            RTC_LOG(LS_ERROR) << "Failed to wrap socket";
            ::close(raw_socket);
            return false;
        }

        // Only use SSL for URLs (https://)
        if (opts_.port == 443 && opts_.ip.find("://") != std::string::npos) {
            // SSL setup code...
            auto stream = std::make_unique<SocketStream>(wrapped_socket);
                
            auto ssl_adapter = rtc::OpenSSLStreamAdapter::Create(
                std::move(stream),
                [](rtc::SSLHandshakeError error) {
                    if (static_cast<int>(error) != 0) {  // 0 means success
                        RTC_LOG(LS_ERROR) << "SSL Handshake failed with error: " 
                                        << static_cast<int>(error);
                    }
                });

            if (!ssl_adapter) {
                RTC_LOG(LS_ERROR) << "Failed to create SSL adapter";
                return false;
            }

            // SetMode is deprecated, but we'll keep it for now
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            ssl_adapter->SetMode(rtc::SSL_MODE_TLS);
            #pragma GCC diagnostic pop

            int result = ssl_adapter->StartSSL();
            if (result < 0) {
                RTC_LOG(LS_ERROR) << "Failed to start SSL with error: " << result;
                return false;
            }
            
            tcp_socket_ = std::make_unique<rtc::AsyncTCPSocket>(wrapped_socket);
        } else {
            // Regular non-SSL connection for direct IP:PORT connections
            tcp_socket_ = std::make_unique<rtc::AsyncTCPSocket>(wrapped_socket);
        }

        SetupSocket(tcp_socket_.get());
        OnConnect(tcp_socket_.get());
        return true;
    };
    return network_thread()->BlockingCall(std::move(task));
}

void DirectCaller::OnConnect(rtc::AsyncPacketSocket* socket) {
    RTC_LOG(LS_INFO) << "Connected to " << remote_addr_.ToString();

    // Setup socket before sending any messages
    SetupSocket(socket);

    // Use the URL connection flag from options
    if (!opts_.is_url) {
        // Send INIT message to start WebRTC signaling
        std::string init_msg = "INIT";
        rtc::PacketOptions options;
        int sent = socket->Send(init_msg.c_str(), init_msg.length(), options);
        if (sent != static_cast<int>(init_msg.length())) {
            RTC_LOG(LS_ERROR) << "Failed to send INIT message";
            return;
        }
        RTC_LOG(LS_INFO) << "Sent INIT message";
    } else {
        // For URL connections, start with JSON signaling
        Json::Value jmessage;
        jmessage["type"] = "join";
        jmessage["data"] = Json::Value(Json::objectValue);
        jmessage["data"]["displayName"] = "WebRTCsays.ai Direct";
        jmessage["data"]["device"] = Json::Value(Json::objectValue);
        jmessage["data"]["device"]["flag"] = "desktop";
        jmessage["data"]["device"]["name"] = "WebRTCsays.ai Direct";
        jmessage["data"]["device"]["version"] = "1.0";
        jmessage["data"]["rtpCapabilities"] = Json::Value(Json::objectValue);
        jmessage["data"]["sctpCapabilities"] = Json::Value(Json::objectValue);
        
        if (!opts_.room.empty()) {
            jmessage["data"]["roomId"] = opts_.room;
        }
        
        Json::FastWriter writer;
        std::string json_message = writer.write(jmessage);
        json_message += "\n";

        RTC_LOG(LS_INFO) << "Preparing to send join message: " << json_message;
        
        rtc::PacketOptions options;
        int sent = socket->Send(json_message.c_str(), json_message.length(), options);
        if (sent != static_cast<int>(json_message.length())) {
            RTC_LOG(LS_ERROR) << "Failed to send join message. Sent " << sent 
                             << " of " << json_message.length() << " bytes";
            return;
        }
        RTC_LOG(LS_INFO) << "Successfully sent join message for room: " << opts_.room 
                         << " (" << sent << " bytes)";
    }
}

void DirectCaller::OnMessage(rtc::AsyncPacketSocket* socket,
                           const unsigned char* data,
                           size_t len,
                           const rtc::SocketAddress& remote_addr) {
    std::string message(reinterpret_cast<const char*>(data), len);
    RTC_LOG(LS_INFO) << "Received message (" << len << " bytes): " << message;

    if (!opts_.is_url) {
        // Direct connection protocol
        if (message.find("WELCOME") == 0) {
            Start();
        } else if (message.find("WAITING") == 0) {
            Start();
        } else if (message.find("ANSWER:") == 0) {
            std::string sdp = message.substr(7);
            SetRemoteDescription(sdp);
        } else if (message.find("ICE:") == 0) {
            std::string candidate = message.substr(4);
            AddIceCandidate(candidate);
        } else if (message == "OK") {
            Shutdown();
            QuitThreads();
        }
    } else {
        // URL-based connection protocol
        Json::Reader reader;
        Json::Value jmessage;
        if (reader.parse(message, jmessage)) {
            std::string type = jmessage["type"].asString();
            RTC_LOG(LS_INFO) << "Parsed message type: " << type;

            if (type == "error") {
                std::string error = jmessage["error"].asString();
                RTC_LOG(LS_ERROR) << "Server error: " << error;
                if (jmessage.isMember("data")) {
                    RTC_LOG(LS_ERROR) << "Error details: " 
                                     << Json::FastWriter().write(jmessage["data"]);
                }
            } else if (type == "notification") {
                std::string method = jmessage["method"].asString();
                RTC_LOG(LS_INFO) << "Received notification method: " << method;
                if (method == "roomReady") {
                    RTC_LOG(LS_INFO) << "Room is ready, starting WebRTC...";
                    Start();
                }
                if (jmessage.isMember("data")) {
                    RTC_LOG(LS_INFO) << "Notification data: " 
                                    << Json::FastWriter().write(jmessage["data"]);
                }
            } else {
                RTC_LOG(LS_INFO) << "Unhandled message type: " << type 
                                << "\nFull message: " << Json::FastWriter().write(jmessage);
            }
        } else {
            RTC_LOG(LS_WARNING) << "Failed to parse JSON message: " << message;
        }
    }
}

bool DirectCaller::SendMessage(const std::string& message) {
    if (!tcp_socket_) {
        RTC_LOG(LS_ERROR) << "Socket not connected";
        return false;
    }

    rtc::PacketOptions options;
    int sent = tcp_socket_->Send(message.c_str(), message.length(), options);
    if (sent < 0) {
        RTC_LOG(LS_ERROR) << "Failed to send message";
        return false;
    }
    return true;
}
