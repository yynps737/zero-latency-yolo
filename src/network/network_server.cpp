#include "network_server.h"
#include <chrono>
#include <iostream>
#include <future>

namespace zero_latency {

NetworkServer::NetworkServer(std::shared_ptr<ReliableUdpServer> network,
                            std::shared_ptr<IInferenceEngine> inference_engine,
                            std::shared_ptr<GameAdapterBase> game_adapter)
    : network_(network),
      inference_engine_(inference_engine),
      game_adapter_(game_adapter),
      packets_received_(0),
      packets_sent_(0),
      bytes_received_(0),
      bytes_sent_(0) {
    
    buffer_pool_ = std::make_unique<ThreadLocalBufferPool<uint8_t>>(PROTOCOL_MAX_PACKET_SIZE);
    
    inference_engine_->setCallback(std::bind(&NetworkServer::onInferenceResult, this, 
                                          std::placeholders::_1, std::placeholders::_2));
    
    subscribeEvent(events::CLIENT_CONNECTED, [this](const Event& event) {
        if (event.hasData<uint32_t>("client_id")) {
            uint32_t clientId = event.getData<uint32_t>("client_id");
            LOG_INFO("NetworkServer: Client #" + std::to_string(clientId) + " connected");
        }
    });
    
    subscribeEvent(events::CLIENT_DISCONNECTED, [this](const Event& event) {
        if (event.hasData<uint32_t>("client_id")) {
            uint32_t clientId = event.getData<uint32_t>("client_id");
            LOG_INFO("NetworkServer: Client #" + std::to_string(clientId) + " disconnected");
        }
    });
    
    LOG_INFO("NetworkServer initialized");
}

NetworkServer::~NetworkServer() {
    LOG_INFO("NetworkServer shutting down");
}

void NetworkServer::handlePacket(const std::vector<uint8_t>& data, const struct sockaddr_in& client_addr) {
    if (data.size() < sizeof(PacketHeader)) {
        LOG_WARN("Received invalid packet (too small)");
        return;
    }
    
    auto packet_result = PacketFactory::createFromBuffer(data);
    if (packet_result.hasError()) {
        LOG_WARN("Failed to parse packet: " + packet_result.error().message);
        return;
    }
    
    auto packet = std::move(packet_result.value());
    
    packets_received_++;
    bytes_received_ += data.size();
    
    PacketType type = packet->getType();
    try {
        Result<void> result;
        
        switch (type) {
            case PacketType::HEARTBEAT: {
                auto heartbeat = dynamic_cast<HeartbeatPacket*>(packet.get());
                if (heartbeat) {
                    result = handleHeartbeat(*heartbeat, client_addr);
                } else {
                    LOG_ERROR("Invalid heartbeat packet");
                }
                break;
            }
                
            case PacketType::CLIENT_INFO: {
                auto client_info = dynamic_cast<ClientInfoPacket*>(packet.get());
                if (client_info) {
                    result = handleClientInfo(*client_info, client_addr);
                } else {
                    LOG_ERROR("Invalid client info packet");
                }
                break;
            }
                
            case PacketType::FRAME_DATA: {
                auto frame_data = dynamic_cast<FrameDataPacket*>(packet.get());
                if (frame_data) {
                    auto client_id_opt = network_->findClientByAddr(client_addr);
                    if (client_id_opt) {
                        result = handleFrameData(*frame_data, *client_id_opt);
                    } else {
                        LOG_WARN("Received frame data from unknown client");
                    }
                } else {
                    LOG_ERROR("Invalid frame data packet");
                }
                break;
            }
                
            case PacketType::COMMAND: {
                LOG_DEBUG("Received command packet");
                break;
            }
                
            case PacketType::ERROR: {
                LOG_DEBUG("Received error packet");
                break;
            }
                
            default:
                LOG_WARN("Unhandled packet type: " + std::to_string(static_cast<int>(type)));
                break;
        }
        
        if (result.hasError()) {
            LOG_ERROR("Error handling packet: " + result.error().toString());
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception while handling packet: " + std::string(e.what()));
    }
}

size_t NetworkServer::getClientCount() const {
    return network_->getClientCount();
}

Result<void> NetworkServer::handleHeartbeat(const HeartbeatPacket& packet, const struct sockaddr_in& addr) {
    auto client_id_opt = network_->findClientByAddr(addr);
    if (!client_id_opt) {
        LOG_DEBUG("Heartbeat from unknown client, ignoring");
        return Result<void>::ok();
    }
    
    uint32_t client_id = *client_id_opt;
    
    HeartbeatPacket response;
    response.setPing(packet.getPing());
    response.setTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());
    
    auto result = sendPacket(response, addr);
    if (result.hasError()) {
        return result;
    }
    
    return Result<void>::ok();
}

Result<void> NetworkServer::handleClientInfo(const ClientInfoPacket& packet, const struct sockaddr_in& addr) {
    const ClientInfo& info = packet.getInfo();
    
    auto register_result = network_->registerClient(addr, info);
    if (register_result.hasError()) {
        return Result<void>::error(register_result.error());
    }
    
    uint32_t client_id = register_result.value();
    
    auto adapter_result = game_adapter_->registerClient(client_id, info.game_id);
    if (adapter_result.hasError()) {
        LOG_WARN("Failed to register client with game adapter: " + adapter_result.error().toString());
    }
    
    ServerInfoPacket response;
    ServerInfo server_info;
    server_info.server_id = 1;
    server_info.protocol_version = PROTOCOL_VERSION;
    server_info.model_version = 1.0f;
    server_info.max_clients = constants::MAX_CLIENTS;
    server_info.max_fps = constants::TARGET_SERVER_FPS;
    server_info.status = 0;
    
    response.setInfo(server_info);
    response.setTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());
    
    return sendPacket(response, addr);
}

Result<void> NetworkServer::handleFrameData(const FrameDataPacket& packet, uint32_t clientId) {
    const FrameData& frame_data = packet.getFrameData();
    
    if (frame_data.data.empty() || frame_data.width == 0 || frame_data.height == 0) {
        return Result<void>::error(ErrorCode::INVALID_INPUT, "Invalid frame data");
    }
    
    size_t expected_size = frame_data.width * frame_data.height * 3;
    if (frame_data.data.size() != expected_size) {
        return Result<void>::error(
            ErrorCode::INVALID_INPUT, 
            "Frame data size mismatch: expected " + std::to_string(expected_size) + 
            " bytes, but received " + std::to_string(frame_data.data.size()) + " bytes"
        );
    }
    
    InferenceRequest request;
    request.client_id = clientId;
    request.frame_id = frame_data.frame_id;
    request.timestamp = frame_data.timestamp;
    request.width = frame_data.width;
    request.height = frame_data.height;
    request.data = frame_data.data;
    request.is_keyframe = frame_data.keyframe;
    
    auto result = inference_engine_->submitInference(request);
    if (result.hasError()) {
        LOG_ERROR("Failed to submit inference request: " + result.error().toString());
        
        if (result.error().code == ErrorCode::INFERENCE_ERROR) {
            LOG_WARN("Inference queue full, dropping frame #" + std::to_string(frame_data.frame_id));
        }
        
        return result;
    }
    
    LOG_DEBUG("Submitted inference request for client #" + std::to_string(clientId) + 
             ", frame #" + std::to_string(frame_data.frame_id));
    
    return Result<void>::ok();
}

Result<void> NetworkServer::sendPacket(const Packet& packet, const struct sockaddr_in& addr, bool reliable) {
    auto& buffer = buffer_pool_->getBuffer();
    buffer.resize(PROTOCOL_MAX_PACKET_SIZE);
    
    std::vector<uint8_t> data = packet.serialize();
    
    auto result = network_->sendPacket(data, addr, reliable);
    if (result.hasError()) {
        return result;
    }
    
    packets_sent_++;
    bytes_sent_ += data.size();
    
    return Result<void>::ok();
}

void NetworkServer::onInferenceResult(uint32_t clientId, const GameState& state) {
    if (!network_->hasClient(clientId)) {
        LOG_WARN("Inference result for unknown client #" + std::to_string(clientId));
        return;
    }
    
    auto client_info = network_->getClientInfo(clientId);
    if (!client_info.has_value()) {
        LOG_ERROR("Failed to get client info for client #" + std::to_string(clientId));
        return;
    }
    
    struct sockaddr_in client_addr = client_info.value().addr;
    
    auto client_state = game_adapter_->getClientState(clientId);
    if (!client_state) {
        LOG_WARN("Client state not found for client #" + std::to_string(clientId));
        return;
    }
    
    auto processed_result = game_adapter_->processDetections(
        clientId, state, client_state->getGameId());
    
    if (processed_result.hasError()) {
        LOG_ERROR("Failed to process detections: " + processed_result.error().toString());
        return;
    }
    
    GameState processed_state = processed_result.value();
    
    DetectionResultPacket packet;
    packet.setGameState(processed_state);
    packet.setTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());
    
    auto send_result = sendPacket(packet, client_addr);
    if (send_result.hasError()) {
        LOG_ERROR("Failed to send detection result: " + send_result.error().toString());
    }
}

} // namespace zero_latency