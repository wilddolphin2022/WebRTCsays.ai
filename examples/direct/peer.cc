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

DirectPeer::DirectPeer(
    const bool is_caller, 
    const bool enable_encryption,
    const bool enable_video,
    const bool enable_whisper
) : DirectApplication(), 
    peer_connection_(nullptr), 
    network_manager_(std::make_unique<rtc::BasicNetworkManager>(pss())),
    socket_factory_(std::make_unique<rtc::BasicPacketSocketFactory>(pss())),
    is_caller_(is_caller),
    enable_encryption_(enable_encryption),
    enable_video_(enable_video),
    enable_whisper_(enable_whisper)
{
}

DirectPeer::~DirectPeer() {
}

void DirectPeer::Shutdown() {
    // Clear observers first
    create_session_observer_ = nullptr;
    set_local_description_observer_ = nullptr;
    
    // Clear peer connection
    if (peer_connection_) {
        peer_connection_->Close();
    }
    peer_connection_ = nullptr;
    
    // Clear factory after peer connection
    peer_connection_factory_ = nullptr;
    
    // Clear remaining members
    audio_device_module_ = nullptr;
    network_manager_.reset();
    socket_factory_.reset();
}

void DirectPeer::Start() {

  signaling_thread()->PostTask([this]() {

    webrtc::PeerConnectionFactoryDependencies deps;
    deps.network_thread = network_thread();
    deps.worker_thread = worker_thread();
    deps.signaling_thread = signaling_thread();

#ifdef WEBRTC_SPEECH_DEVICES    
    if(enable_whisper_) {
        RTC_LOG(LS_INFO) << "whisper is enabled!";

        deps.task_queue_factory.reset(webrtc::CreateDefaultTaskQueueFactory().release());
        audio_device_module_ = deps.worker_thread->BlockingCall([&]() -> rtc::scoped_refptr<webrtc::AudioDeviceModule> {
            auto adm = webrtc::AudioDeviceModule::Create(
                webrtc::AudioDeviceModule::kSpeechAudio, 
                deps.task_queue_factory.get()
                );
            if (adm) {
                RTC_LOG(LS_INFO) << "Audio device module created successfully";                
            }
            return adm;
        });

        if (!audio_device_module_) {
            RTC_LOG(LS_ERROR) << "Failed to create audio device module";
            return;
        }
    }
#endif

    peer_connection_factory_ = webrtc::CreatePeerConnectionFactory(
        deps.network_thread,
        deps.worker_thread,
        deps.signaling_thread,
        audio_device_module_, 
        webrtc::CreateBuiltinAudioEncoderFactory(),
        webrtc::CreateBuiltinAudioDecoderFactory(),
        enable_video_ ? std::make_unique<webrtc::VideoEncoderFactoryTemplate<
            webrtc::LibvpxVp8EncoderTemplateAdapter,
            webrtc::LibvpxVp9EncoderTemplateAdapter,
            webrtc::OpenH264EncoderTemplateAdapter,
            webrtc::LibaomAv1EncoderTemplateAdapter>>() : nullptr,
        enable_video_ ? std::make_unique<webrtc::VideoDecoderFactoryTemplate<
            webrtc::LibvpxVp8DecoderTemplateAdapter,
            webrtc::LibvpxVp9DecoderTemplateAdapter,
            webrtc::OpenH264DecoderTemplateAdapter,
            webrtc::Dav1dDecoderTemplateAdapter>>() : nullptr,
        nullptr, nullptr);


    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    if(enable_encryption_) {
        RTC_LOG(LS_INFO) << "Encryption is enabled!";
        auto certificate = LoadCertificateFromEnv();
        config.certificates.push_back(certificate);
    } else {
        // WARNING! FOLLOWING CODE IS FOR DEBUG ONLY!
        webrtc::PeerConnectionFactory::Options options = {};
        options.disable_encryption = true;
        peer_connection_factory_->SetOptions(options);
        // END OF WARNING
    }

    config.type = webrtc::PeerConnectionInterface::IceTransportsType::kAll;
    config.rtcp_mux_policy = webrtc::PeerConnectionInterface::kRtcpMuxPolicyRequire;
    config.enable_ice_renomination = false;
    config.ice_candidate_pool_size = 0;
    config.continual_gathering_policy = 
        webrtc::PeerConnectionInterface::ContinualGatheringPolicy::GATHER_ONCE;
    config.ice_connection_receiving_timeout = 1000;
    config.ice_backup_candidate_pair_ping_interval = 2000;

    cricket::ServerAddresses stun_servers;
    std::vector<cricket::RelayServerConfig> turn_servers;

    webrtc::PeerConnectionInterface::IceServer stun_server;
    stun_server.uri = "stun:stun.l.google.com:19302";
    stun_server.uri = "stun:192.168.100.4:3478";
    config.servers.push_back(stun_server);

    for (const auto& server : config.servers) {
        if (server.uri.find("stun:") == 0) {
            std::string host_port = server.uri.substr(5);
            size_t colon_pos = host_port.find(':');
            if (colon_pos != std::string::npos) {
                std::string host = host_port.substr(0, colon_pos);
                int port = std::stoi(host_port.substr(colon_pos + 1));
                stun_servers.insert(rtc::SocketAddress(host, port));
            }
        } else if (server.uri.find("turn:") == 0) {
            std::string host_port = server.uri.substr(5);
            size_t colon_pos = host_port.find(':');
            if (colon_pos != std::string::npos) {
                cricket::RelayServerConfig turn_config;
                turn_config.credentials = cricket::RelayCredentials(server.username, server.password);
                turn_config.ports.push_back(cricket::ProtocolAddress(
                    rtc::SocketAddress(
                        host_port.substr(0, colon_pos),
                        std::stoi(host_port.substr(colon_pos + 1))),
                    cricket::PROTO_UDP));
                turn_servers.push_back(turn_config);
            }
        }
    }

    RTC_LOG(LS_INFO) << "Configured STUN/TURN servers:";
    for (const auto& addr : stun_servers) {
        RTC_LOG(LS_INFO) << "  STUN Server: " << addr.ToString();
    }
    for (const auto& turn : turn_servers) {
        for (const auto& addr : turn.ports) {
            RTC_LOG(LS_INFO) << "  TURN Server: " << addr.address.ToString()
                             << " (Protocol: " << addr.proto << ")";
        }
    }

    auto port_allocator = std::make_unique<cricket::BasicPortAllocator>(
        network_manager_.get(), socket_factory_.get());
    RTC_DCHECK(port_allocator.get());    

    port_allocator->SetConfiguration(
        stun_servers,
        turn_servers,
        0,  // Keep this as 0
        webrtc::PeerConnectionInterface::ContinualGatheringPolicy::GATHER_ONCE,
        nullptr,
        std::nullopt
    );

    // Allow flexible port allocation for UDP
    uint32_t flags = cricket::PORTALLOCATOR_ENABLE_SHARED_SOCKET;
    port_allocator->set_flags(flags);
    port_allocator->set_step_delay(cricket::kMinimumStepDelay);  // Speed up gathering
    port_allocator->set_candidate_filter(cricket::CF_ALL);  // Allow all candidate types

    webrtc::PeerConnectionDependencies pc_dependencies(this);
    pc_dependencies.allocator = std::move(port_allocator);

    auto pcf_result = peer_connection_factory_->CreatePeerConnectionOrError(
        config, std::move(pc_dependencies));
    RTC_DCHECK(pcf_result.ok());    
    peer_connection_ = pcf_result.MoveValue();
    RTC_LOG(LS_INFO) << "PeerConnection created successfully.";

    if (is_caller_) {
        cricket::AudioOptions audio_options;
        // audio_options.echo_cancellation = true;
        // audio_options.noise_suppression = true;
        // audio_options.auto_gain_control = true;

        auto audio_source = peer_connection_factory_->CreateAudioSource(audio_options);
        RTC_DCHECK(audio_source.get());
        auto audio_track = peer_connection_factory_->CreateAudioTrack("a", audio_source.get());
        RTC_DCHECK(audio_track.get());

        webrtc::RtpTransceiverInit init;
        init.direction = webrtc::RtpTransceiverDirection::kSendRecv;
        auto at_result = peer_connection_->AddTransceiver(audio_track, init);
        RTC_DCHECK(at_result.ok());
        auto transceiver = at_result.value();

        // Force the direction immediately after creation
        auto direction_result = transceiver->SetDirectionWithError(webrtc::RtpTransceiverDirection::kSendRecv);
        RTC_LOG(LS_INFO) << "Initial transceiver direction set: " << 
            (direction_result.ok() ? "success" : "failed");
    
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions offer_options;

        // Store observer in a member variable to keep it alive
        create_session_observer_ = rtc::make_ref_counted<LambdaCreateSessionDescriptionObserver>(
            [this](std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
                std::string sdp;
                desc->ToString(&sdp);
                
                // Store observer in a member variable to keep it alive
                set_local_description_observer_ = rtc::make_ref_counted<LambdaSetLocalDescriptionObserver>(
                    [this, sdp](webrtc::RTCError error) {
                        if (!error.ok()) {
                            RTC_LOG(LS_ERROR) << "Failed to set local description: " 
                                            << error.message();
                            signaling_thread()->PostTask([this]() {
                                SendMessage("BYE");
                            });
                            return;
                        }
                        RTC_LOG(LS_INFO) << "Local description set successfully";
                        SendMessage("OFFER:" + sdp);
                    });

                peer_connection_->SetLocalDescription(std::move(desc), set_local_description_observer_);
            });

        peer_connection_->CreateOffer(create_session_observer_.get(), offer_options);
 
     } else {
        RTC_LOG(LS_INFO) << "Waiting for offer...";
        SendMessage("WAITING");
    }
 
  });

}

void DirectPeer::HandleMessage(rtc::AsyncPacketSocket* socket,
                             const std::string& message,
                             const rtc::SocketAddress& remote_addr) {

   if (message.find("INIT") == 0) {
        if (!is_caller()) {
          Start();
        } else {
          RTC_LOG(LS_ERROR) << "Peer is not a callee, cannot init";
        }

   } else if (message == "WAITING") {
        if (is_caller()) {
          Start();
        } else {
          RTC_LOG(LS_ERROR) << "Peer is not a caller, cannot wait";
        }
   } else if (!is_caller() && message.find("OFFER:") == 0) {
      std::string sdp = message.substr(6);  // Use exact length of "OFFER:"
      if(!sdp.empty()) {
        SetRemoteDescription(sdp);
      } else {
        RTC_LOG(LS_ERROR) << "Invalid SDP offer received";
      }
   } else if (is_caller() && message.find("ANSWER:") == 0) {
      std::string sdp = message.substr(7);

      // Got an ANSWER from the callee
      if(sdp.size())
        SetRemoteDescription(sdp);
      else
        RTC_LOG(LS_ERROR) << "Invalid SDP answer received";

   } else if (message.find("ICE:") == 0) {
     std::string candidate = message.substr(4);
      if (!candidate.empty()) {
          RTC_LOG(LS_INFO) << "Received ICE candidate: " << candidate;
          AddIceCandidate(candidate);
      } else {
          RTC_LOG(LS_ERROR) << "Invalid ICE candidate received";
      }
   } else {
       DirectApplication::HandleMessage(socket, message, remote_addr);
   }
}

bool DirectPeer::SendMessage(const std::string& message) {
    
    return DirectApplication::SendMessage(message);
}

// PeerConnectionObserver implementation
void DirectPeer::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) {
    // Implementation will go here
}

void DirectPeer::OnAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
                           const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) {
    // Implementation will go here
}

void DirectPeer::OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
    // Implementation will go here
}

void DirectPeer::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> channel) {
    // Implementation will go here
}

void DirectPeer::OnRenegotiationNeeded() {
    // Implementation will go here
}

void DirectPeer::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
    // Implementation will go here
}

void DirectPeer::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) {
  switch (new_state) {
    case webrtc::PeerConnectionInterface::kIceGatheringNew:
      RTC_LOG(LS_INFO) << "ICE gathering state: New - Starting to gather candidates";
      break;
    case webrtc::PeerConnectionInterface::kIceGatheringGathering:
      RTC_LOG(LS_INFO) << "ICE gathering state: Gathering - Collecting candidates";
      break;
    case webrtc::PeerConnectionInterface::kIceGatheringComplete:
      RTC_LOG(LS_INFO) << "ICE gathering state: Complete - All candidates collected";
      break;
  }
}

void DirectPeer::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
    std::string sdp;
    if (!candidate->ToString(&sdp)) {
        RTC_LOG(LS_ERROR) << "Failed to serialize candidate";
        return;
    }

    RTC_LOG(LS_INFO) << "New ICE candidate: " << sdp 
                     << " mid: " << candidate->sdp_mid()
                     << " mlineindex: " << candidate->sdp_mline_index();
    
    // Only send ICE candidates after local description is set
    if (!peer_connection_->local_description()) {
        RTC_LOG(LS_INFO) << "Queuing ICE candidate until local description is set";
        pending_ice_candidates_.push_back(sdp);
        return;
    }
    
    SendMessage("ICE:" + sdp);
}

void DirectPeer::OnIceConnectionReceivingChange(bool receiving) {
    // Implementation will go here
}

void DirectPeer::SetRemoteDescription(const std::string& sdp) {
    if (!peer_connection()) {
        RTC_LOG(LS_ERROR) << "PeerConnection not initialized...";
        return;
    }
  
    signaling_thread()->PostTask([this, sdp]() {
        RTC_LOG(LS_INFO) << "Processing remote description as " 
                        << (is_caller_ ? "ANSWER" : "OFFER");
        
        webrtc::SdpParseError error;
        webrtc::SdpType sdp_type = is_caller_ ? webrtc::SdpType::kAnswer 
                                             : webrtc::SdpType::kOffer;
        
        std::unique_ptr<webrtc::SessionDescriptionInterface> session_description =
            webrtc::CreateSessionDescription(sdp_type, sdp, &error);
            
        if (!session_description) {
            RTC_LOG(LS_ERROR) << "Failed to parse remote SDP: " << error.description;
            return;
        }

        auto observer = rtc::make_ref_counted<LambdaSetRemoteDescriptionObserver>(
            [this](webrtc::RTCError error) {
                if (!error.ok()) {
                    RTC_LOG(LS_ERROR) << "Failed to set remote description: " 
                                    << error.message();
                    return;
                }
                RTC_LOG(LS_INFO) << "Remote description set successfully";
                auto transceivers = peer_connection()->GetTransceivers();
                RTC_DCHECK(transceivers.size() > 0);
                auto transceiver = transceivers[0];

                // Force send/recv mode
                auto result = transceiver->SetDirectionWithError(webrtc::RtpTransceiverDirection::kSendRecv);
                if (!result.ok()) {
                    RTC_LOG(LS_ERROR) << "Failed to set transceiver direction: " << result.message();
                }
                
                webrtc::RtpTransceiverDirection direction = transceiver->direction();
                RTC_LOG(LS_INFO) << "Transceiver direction is " << 
                    (direction == webrtc::RtpTransceiverDirection::kSendRecv ? "send/rcv" : 
                     direction == webrtc::RtpTransceiverDirection::kRecvOnly ? "recv-only" : "other");               

                if (!is_caller() && 
                    peer_connection()->signaling_state() == 
                        webrtc::PeerConnectionInterface::kHaveRemoteOffer) {
                    RTC_LOG(LS_INFO) << "Creating answer as callee...";
                                                            
                    create_session_observer_ = rtc::make_ref_counted<LambdaCreateSessionDescriptionObserver>(
                        [this](std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
                            std::string sdp;
                            desc->ToString(&sdp);
                            
                            set_local_description_observer_ = rtc::make_ref_counted<LambdaSetLocalDescriptionObserver>(
                                [this, sdp](webrtc::RTCError error) {
                                    if (!error.ok()) {
                                        RTC_LOG(LS_ERROR) << "Failed to set local description: " 
                                                        << error.message();
                                        signaling_thread()->PostTask([this]() {
                                            SendMessage("BYE");
                                        });
                                        return;
                                    }
                                    RTC_LOG(LS_INFO) << "Local description set successfully";
                                    SendMessage("ANSWER:" + sdp);
                            });

                            peer_connection_->SetLocalDescription(std::move(desc), set_local_description_observer_);
                        });
                        
                    peer_connection_->CreateAnswer(create_session_observer_.get(), webrtc::PeerConnectionInterface::RTCOfferAnswerOptions{});
                }
            });

        peer_connection_->SetRemoteDescription(std::move(session_description), observer);
    });
}

void DirectPeer::AddIceCandidate(const std::string& candidate_sdp) {
    signaling_thread()->PostTask([this, candidate_sdp]() {
        // Simply queue if descriptions aren't ready
        if (!peer_connection_->remote_description() || !peer_connection_->local_description()) {
            RTC_LOG(LS_INFO) << "Queuing ICE candidate - descriptions not ready";
            pending_ice_candidates_.push_back(candidate_sdp);
            return;
        }

        webrtc::SdpParseError error;
        std::unique_ptr<webrtc::IceCandidateInterface> candidate(
            webrtc::CreateIceCandidate("0", 0, candidate_sdp, &error));
        if (!candidate) {
            RTC_LOG(LS_ERROR) << "Failed to parse ICE candidate: " << error.description;
            return;
        }

        RTC_LOG(LS_INFO) << "Adding ICE candidate";
        peer_connection_->AddIceCandidate(candidate.get());
    });
}

