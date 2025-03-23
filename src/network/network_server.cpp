#include "network_server.h"
#include <chrono>
#include <iostream>
#include <future>

namespace zero_latency {

// 构造函数
NetworkServer::NetworkServer(std::shared_ptr<ReliableUdpServer> network,
                            std::shared_ptr<IInferenceEngine> inference_engine,
                            std::shared_ptr<IGameAdapter> game_adapter)
    : network_(network),
      inference_engine_(inference_engine),
      game_adapter_(game_adapter),
      packets_received_(0),
      packets_sent_(0),
      bytes_received_(0),
      bytes_sent_(0) {
    
    // 初始化内存池
    buffer_pool_ = std::make_unique<ThreadLocalBufferPool<uint8_t>>(PROTOCOL_MAX_PACKET_SIZE);
    
    // 设置推理引擎回调
    inference_engine_->setCallback(std::bind(&NetworkServer::onInferenceResult, this, 
                                          std::placeholders::_1, std::placeholders::_2));
    
    // 订阅系统事件
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

// 析构函数
NetworkServer::~NetworkServer() {
    LOG_INFO("NetworkServer shutting down");
}

// 处理接收到的数据包
void handlePacket(const std::vector<uint8_t>& data, const struct sockaddr_in& client_addr) {
    if (data.size() < sizeof(PacketHeader)) {
        LOG_WARN("Received invalid packet (too small)");
        return;
    }
    
    // 创建数据包
    std::unique_ptr<Packet> packet = PacketFactory::createFromBuffer(data);
    if (!packet) {
        LOG_WARN("Failed to parse packet");
        return;
    }
    
    // 更新统计信息
    packets_received_++;
    bytes_received_ += data.size();
    
    // 根据数据包类型处理
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
                // 命令包处理
                LOG_DEBUG("Received command packet");
                break;
            }
                
            case PacketType::ERROR: {
                // 错误包处理
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

// 获取当前客户端数量
size_t NetworkServer::getClientCount() const {
    return network_->getClientCount();
}

// 处理心跳包
Result<void> NetworkServer::handleHeartbeat(const HeartbeatPacket& packet, const struct sockaddr_in& addr) {
    auto client_id_opt = network_->findClientByAddr(addr);
    if (!client_id_opt) {
        LOG_DEBUG("Heartbeat from unknown client, ignoring");
        return Result<void>::ok();
    }
    
    uint32_t client_id = *client_id_opt;
    
    // 发送心跳响应
    HeartbeatPacket response;
    response.setPing(packet.getPing());
    response.setTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());
    
    // 发送数据包
    auto result = sendPacket(response, addr);
    if (result.hasError()) {
        return result;
    }
    
    return Result<void>::ok();
}

// 处理客户端信息包
Result<void> NetworkServer::handleClientInfo(const ClientInfoPacket& packet, const struct sockaddr_in& addr) {
    const ClientInfo& info = packet.getInfo();
    
    // 注册或更新客户端
    auto register_result = network_->registerClient(addr, info);
    if (register_result.hasError()) {
        return Result<void>::error(register_result.error());
    }
    
    uint32_t client_id = register_result.value();
    
    // 注册到游戏适配器
    auto adapter_result = game_adapter_->registerClient(client_id, info.game_id);
    if (adapter_result.hasError()) {
        LOG_WARN("Failed to register client with game adapter: " + adapter_result.error().toString());
        // 不是致命错误，继续
    }
    
    // 发送服务器信息响应
    ServerInfoPacket response;
    ServerInfo server_info;
    server_info.server_id = 1;
    server_info.protocol_version = PROTOCOL_VERSION;
    server_info.model_version = 1.0f;
    server_info.max_clients = constants::MAX_CLIENTS;
    server_info.max_fps = constants::TARGET_SERVER_FPS;
    server_info.status = 0; // OK
    
    response.setInfo(server_info);
    response.setTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());
    
    return sendPacket(response, addr);
}

// 处理帧数据包
Result<void> NetworkServer::handleFrameData(const FrameDataPacket& packet, uint32_t clientId) {
    const FrameData& frame_data = packet.getFrameData();
    
    // 验证帧数据
    if (frame_data.data.empty() || frame_data.width == 0 || frame_data.height == 0) {
        return Result<void>::error(ErrorCode::INVALID_INPUT, "Invalid frame data");
    }
    
    // 预期大小检查
    size_t expected_size = frame_data.width * frame_data.height * 3; // RGB格式
    if (frame_data.data.size() != expected_size) {
        return Result<void>::error(
            ErrorCode::INVALID_INPUT, 
            "Frame data size mismatch: expected " + std::to_string(expected_size) + 
            " bytes, but received " + std::to_string(frame_data.data.size()) + " bytes"
        );
    }
    
    // 创建推理请求
    InferenceRequest request;
    request.client_id = clientId;
    request.frame_id = frame_data.frame_id;
    request.timestamp = frame_data.timestamp;
    request.width = frame_data.width;
    request.height = frame_data.height;
    request.data = frame_data.data;
    request.is_keyframe = frame_data.keyframe;
    
    // 提交推理请求
    auto result = inference_engine_->submitInference(request);
    if (result.hasError()) {
        LOG_ERROR("Failed to submit inference request: " + result.error().toString());
        
        if (result.error().code == ErrorCode::INFERENCE_ERROR) {
            // 队列已满，可以尝试丢弃旧请求
            LOG_WARN("Inference queue full, dropping frame #" + std::to_string(frame_data.frame_id));
        }
        
        return result;
    }
    
    LOG_DEBUG("Submitted inference request for client #" + std::to_string(clientId) + 
             ", frame #" + std::to_string(frame_data.frame_id));
    
    return Result<void>::ok();
}

// 发送数据包
Result<void> NetworkServer::sendPacket(const Packet& packet, const struct sockaddr_in& addr, bool reliable) {
    // 序列化数据包
    auto& buffer = buffer_pool_->getBuffer();
    buffer.resize(PROTOCOL_MAX_PACKET_SIZE);
    
    // 序列化到可重用缓冲区
    std::vector<uint8_t> data = packet.serialize();
    
    // 发送数据包
    auto result = network_->sendPacket(data, addr, reliable);
    if (result.hasError()) {
        return result;
    }
    
    // 更新统计信息
    packets_sent_++;
    bytes_sent_ += data.size();
    
    return Result<void>::ok();
}

// 推理结果回调
void NetworkServer::onInferenceResult(uint32_t clientId, const GameState& state) {
    // 检查客户端是否存在
    if (!network_->hasClient(clientId)) {
        LOG_WARN("Inference result for unknown client #" + std::to_string(clientId));
        return;
    }
    
    // 获取客户端地址
    auto client_id_opt = network_->findClientByAddr(client_addr);
    if (!client_id_opt) {
        LOG_ERROR("Failed to find client address for client #" + std::to_string(clientId));
        return;
    }
    
    struct sockaddr_in client_addr = client_addr_opt.value();
    
    // 处理游戏特定逻辑
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
    
    // 创建并发送检测结果包
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