#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include <netinet/in.h>

#include "../common/types.h"
#include "../common/protocol.h"
#include "../common/result.h"
#include "../common/logger.h"
#include "../common/event_bus.h"
#include "../common/memory_pool.h"
#include "../inference/inference_engine.h"
#include "../game/game_adapter.h"
#include "reliable_udp.h"

namespace zero_latency {

// 网络服务器类
class NetworkServer {
public:
    // 构造函数
    NetworkServer(std::shared_ptr<ReliableUdpServer> network,
                 std::shared_ptr<IInferenceEngine> inference_engine,
                 std::shared_ptr<IGameAdapter> game_adapter);
    
    // 析构函数
    ~NetworkServer();
    
    // 处理接收到的数据包
    void handlePacket(const std::vector<uint8_t>& data, const struct sockaddr_in& client_addr);
    
    // 获取当前客户端数量
    size_t getClientCount() const;
    
private:
    // 处理心跳包
    Result<void> handleHeartbeat(const HeartbeatPacket& packet, const struct sockaddr_in& addr);
    
    // 处理客户端信息包
    Result<void> handleClientInfo(const ClientInfoPacket& packet, const struct sockaddr_in& addr);
    
    // 处理帧数据包
    Result<void> handleFrameData(const FrameDataPacket& packet, uint32_t clientId);
    
    // 发送数据包
    Result<void> sendPacket(const Packet& packet, const struct sockaddr_in& addr, bool reliable = true);
    
    // 推理结果回调
    void onInferenceResult(uint32_t clientId, const GameState& state);

private:
    // 组件
    std::shared_ptr<ReliableUdpServer> network_;
    std::shared_ptr<IInferenceEngine> inference_engine_;
    std::shared_ptr<IGameAdapter> game_adapter_;
    
    // 统计信息
    std::atomic<uint64_t> packets_received_;
    std::atomic<uint64_t> packets_sent_;
    std::atomic<uint64_t> bytes_received_;
    std::atomic<uint64_t> bytes_sent_;
    
    // 内存池
    std::unique_ptr<ThreadLocalBufferPool<uint8_t>> buffer_pool_;
};

} // namespace zero_latency