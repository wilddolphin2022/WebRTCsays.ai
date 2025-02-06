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

#ifndef WEBRTC_DIRECT_DIRECT_H_
#define WEBRTC_DIRECT_DIRECT_H_

#include <memory>
#include <string>
#include <future>
#include <optional>

#include "rtc_base/async_tcp_socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/thread.h"
#include "rtc_base/physical_socket_server.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/thread.h"
#include "rtc_base/logging.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/openssl_identity.h"
#include "rtc_base/ref_counted_object.h"
#include "api/jsep.h"
#include "api/peer_connection_interface.h"
#include "api/peer_connection_interface.h"
#include "api/create_peerconnection_factory.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/environment/environment_factory.h"
#include "p2p/base/port_allocator.h"
#include "p2p/client/basic_port_allocator.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "pc/peer_connection.h"
#include "pc/peer_connection_factory.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "rtc_base/virtual_socket_server.h"
#include "system_wrappers/include/field_trial.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include "system_wrappers/include/clock.h"
#include "modules/audio_device/audio_device_impl.h"

#ifdef WEBRTC_SPEECH_DEVICES
#include "modules/audio_device/speech/speech_audio_device_factory.h"
#endif

class LambdaCreateSessionDescriptionObserver
    : public webrtc::CreateSessionDescriptionObserver {
public:
explicit LambdaCreateSessionDescriptionObserver(
    std::function<void(std::unique_ptr<webrtc::SessionDescriptionInterface> desc)>
        on_success)
    : on_success_(on_success) {}
void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
    // Takes ownership of answer, according to CreateSessionDescriptionObserver
    // convention.
    on_success_(absl::WrapUnique(desc));
}
void OnFailure(webrtc::RTCError error) override {
    RTC_DCHECK_NOTREACHED() << error.message();
}

private:
std::function<void(std::unique_ptr<webrtc::SessionDescriptionInterface> desc)>
    on_success_;
};

class LambdaSetLocalDescriptionObserver
    : public webrtc::SetLocalDescriptionObserverInterface {
public:
explicit LambdaSetLocalDescriptionObserver(
    std::function<void(webrtc::RTCError)> on_complete)
    : on_complete_(on_complete) {}
void OnSetLocalDescriptionComplete(webrtc::RTCError error) override {
    on_complete_(error);
}

private:
std::function<void(webrtc::RTCError)> on_complete_;
};

class LambdaSetRemoteDescriptionObserver
    : public webrtc::SetRemoteDescriptionObserverInterface {
 public:
  explicit LambdaSetRemoteDescriptionObserver(
      std::function<void(webrtc::RTCError)> on_complete)
      : on_complete_(on_complete) {}
  void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override {
    on_complete_(error);
  }

 private:
  std::function<void(webrtc::RTCError)> on_complete_;
};


class DirectApplication {
public:
    DirectApplication();
    virtual ~DirectApplication();

    // Initialize threads and basic WebRTC infrastructure
    bool Initialize();
    
    // Run the application event loop
    void Run();
    
    //rtc::VirtualSocketServer* vss() { return vss_.get(); }
    rtc::PhysicalSocketServer* pss() { return pss_.get(); }

protected:
    // Thread getters for derived classes
    rtc::Thread* signaling_thread() { return signaling_thread_.get(); }
    rtc::Thread* worker_thread() { return worker_thread_.get(); }
    rtc::Thread* network_thread() { return network_thread_.get(); }
    rtc::Thread* main_thread() { return main_thread_.get(); }

    void CleanupSocketServer();

    void QuitThreads() {
        should_quit_ = true;  // Add this member to DirectApplication class
        if (network_thread_) network_thread_->Quit();
        if (worker_thread_) worker_thread_->Quit();
        if (signaling_thread_) signaling_thread_->Quit();
        if (main_thread_) main_thread_->Quit();
    }

    // Common message handling
    virtual void HandleMessage(rtc::AsyncPacketSocket* socket,
                             const std::string& message,
                             const rtc::SocketAddress& remote_addr);
    
    virtual bool SendMessage(const std::string& message);
    
    // Message sequence tracking
    int ice_candidates_sent_ = 0;
    int ice_candidates_received_ = 0;
    int sdp_fragments_sent_ = 0;
    int sdp_fragments_received_ = 0;
    static constexpr int kMaxIceCandidates = 3;
    static constexpr int kMaxSdpFragments = 2;
    
    std::unique_ptr<rtc::AsyncTCPSocket> tcp_socket_;

    std::atomic<bool> should_quit_{false};
private:
    //std::unique_ptr<rtc::VirtualSocketServer> vss_;
    std::unique_ptr<rtc::Thread> main_thread_;

    // WebRTC threads
    std::unique_ptr<rtc::Thread> signaling_thread_;
    std::unique_ptr<rtc::Thread> worker_thread_;
    std::unique_ptr<rtc::Thread> network_thread_;
    
    // Ensure methods are called on correct thread
    webrtc::SequenceChecker sequence_checker_;
    std::unique_ptr<rtc::PhysicalSocketServer> pss_;  
};

class DirectPeer : public DirectApplication,
                  public webrtc::PeerConnectionObserver {
public:
    DirectPeer(
        const bool is_caller = true, 
        const bool enable_encryption = true,
        const bool enable_video = false,
        const bool enable_whisper = false
    );
    ~DirectPeer() override;

    void Start();

    // Override DirectApplication methods
    virtual void HandleMessage(rtc::AsyncPacketSocket* socket,
                             const std::string& message,
                             const rtc::SocketAddress& remote_addr) override;
    
    virtual bool SendMessage(const std::string& message) override;
    
    virtual void SetEnableEncryption(const bool enable_video) { enable_video_ = enable_video; }
    virtual void SetEnableVideo(const bool enable_video) { enable_video_ = enable_video; }
    virtual void SetEnableWhisper(const bool enable_whisper) { enable_whisper_ = enable_whisper; }
    virtual void SetWhisperModel(const std::string& whisper_model) { ::setenv("WHISPER_MODEL", whisper_model.c_str(), true); }
    virtual void SetLlamaModel(const std::string& llama_model) { ::setenv("LLAMA_MODEL", llama_model.c_str(), true); }

    // PeerConnectionObserver implementation
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override;
    void OnAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) override;
    void OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;
    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> channel) override;
    void OnRenegotiationNeeded() override;
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override;
    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
    void OnIceConnectionReceivingChange(bool receiving) override;

protected:
    void Shutdown();
    bool is_caller() const { return is_caller_; }
    webrtc::PeerConnectionInterface* peer_connection() const { return peer_connection_.get(); }

     // Session description methods
    void SetRemoteDescription(const std::string& sdp);
    void AddIceCandidate(const std::string& candidate_sdp);

private:
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory_;
    std::unique_ptr<rtc::BasicNetworkManager> network_manager_;    
    std::unique_ptr<rtc::BasicPacketSocketFactory> socket_factory_;
    rtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device_module_;
    std::vector<std::string> pending_ice_candidates_;

    rtc::scoped_refptr<LambdaCreateSessionDescriptionObserver> create_session_observer_;
    rtc::scoped_refptr<LambdaSetLocalDescriptionObserver> set_local_description_observer_;
    rtc::scoped_refptr<LambdaSetRemoteDescriptionObserver> set_remote_description_observer_;
    
    bool is_caller_ = false;
    bool enable_encryption_ = false;
    bool enable_video_ = false;
    bool enable_whisper_ = false;
};

class DirectCallee : public DirectPeer,
                    public sigslot::has_slots<> {
public:
    explicit DirectCallee(
        const int local_port,
        const bool enable_encryption = false,
        const bool enable_video = false,
        const bool enable_whisper = false
        );
    ~DirectCallee() override;

    // Start listening for incoming connections
    bool StartListening();

private:
    void OnNewConnection(rtc::AsyncListenSocket* socket, 
                        rtc::AsyncPacketSocket* new_socket);
    void OnMessage(rtc::AsyncPacketSocket* socket,
                  const unsigned char* data,
                  size_t len,
                  const rtc::SocketAddress& remote_addr);

    int local_port_;
    //std::unique_ptr<rtc::AsyncTCPSocket> tcp_socket_;
    std::unique_ptr<rtc::AsyncTcpListenSocket> listen_socket_;  // Changed to unique_ptr
};

class DirectCaller : public DirectPeer, 
                    public sigslot::has_slots<> {
public:
    explicit DirectCaller(
        const rtc::SocketAddress& remote_addr,
        const bool enable_encryption = false,
        const bool enable_video = false,
        const bool enable_whisper = false
        );
    ~DirectCaller() override;

    // Connect and send messages
    bool Connect();
    //bool SendMessage(const std::string& message);

private:
    // Called when data is received on the socket
    void OnMessage(rtc::AsyncPacketSocket* socket,
                  const unsigned char* data,
                  size_t len,
                  const rtc::SocketAddress& remote_addr);

    // Called when connection is established
    void OnConnect(rtc::AsyncPacketSocket* socket);

    rtc::SocketAddress remote_addr_;
    //std::unique_ptr<rtc::AsyncTCPSocket> tcp_socket_;
};

#endif  // WEBRTC_DIRECT_DIRECT_H_
