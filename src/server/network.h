#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <netinet/in.h>

#include "yolo_engine.h"
#include "game_adapter.h"
#include "../common/types.h"
#include "../common/protocol.h"

namespace zero_latency {

// 客户端连接信息
struct ClientConnection {
    uint32_t client_id;
    struct sockaddr_in addr;
    uint64_t last_active_time;
    uint32_t last_frame_processed;
    ClientInfo info;
    bool connected;
};

// 网络服务器类
class NetworkServer {
public:
    NetworkServer(uint16_t port, YoloEngine* yolo_engine, GameAdapter* game_adapter);
    ~NetworkServer();
    
    // 初始化服务器
    bool initialize();
    
    // 运行服务器
    void run(const std::atomic<bool>& running);
    
    // 关闭服务器
    void shutdown();
    
    // 处理接收到的数据包
    void handlePacket(const std::vector<uint8_t>& data, const struct sockaddr_in& client_addr);
    
    // 获取当前客户端数量
    size_t getClientCount() const;
    
private:
    // 注册新客户端
    uint32_t registerClient(const struct sockaddr_in& addr, const ClientInfo& info);
    
    // 移除客户端
    void removeClient(uint32_t client_id);
    
    // 检查客户端超时
    void checkClientTimeouts();
    
    // 处理心跳包
    void handleHeartbeat(const HeartbeatPacket& packet, const struct sockaddr_in& addr);
    
    // 处理客户端信息包
    void handleClientInfo(const ClientInfoPacket& packet, const struct sockaddr_in& addr);
    
    // 处理帧数据包
    void handleFrameData(const FrameDataPacket& packet, uint32_t client_id);
    
    // 发送数据包到客户端
    bool sendPacket(const Packet& packet, const struct sockaddr_in& addr);
    
    // 检查客户端是否存在
    bool clientExists(uint32_t client_id) const;
    
    // 根据地址查找客户端ID
    uint32_t findClientByAddr(const struct sockaddr_in& addr) const;
    
    // 检查地址是否相同
    static bool isSameAddr(const struct sockaddr_in& addr1, const struct sockaddr_in& addr2);
    
    // 推理结果回调
    void onInferenceResult(uint32_t client_id, const GameState& state);
    
private:
    uint16_t port_;
    int socket_fd_;
    
    YoloEngine* yolo_engine_;
    GameAdapter* game_adapter_;
    
    // 标记为 mutable 以允许在 const 成员函数中修改
    mutable std::mutex clients_mutex_;
    std::unordered_map<uint32_t, ClientConnection> clients_;
    uint32_t next_client_id_;
    
    std::thread timeout_thread_;
    std::atomic<bool> timeout_running_;
};

} // namespace zero_latency