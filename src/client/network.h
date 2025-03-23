#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>

#include "../common/types.h"
#include "../common/protocol.h"

namespace zero_latency {

// 结果回调函数类型
using ResultCallback = std::function<void(const GameState& state)>;

// 网络客户端类
class NetworkClient {
public:
    NetworkClient(const std::string& server_ip, uint16_t server_port);
    ~NetworkClient();
    
    // 初始化网络客户端
    bool initialize();
    
    // 连接到服务器
    bool connect();
    
    // 断开连接
    void disconnect();
    
    // 发送帧数据
    bool sendFrame(const FrameData& frame);
    
    // 发送命令
    bool sendCommand(CommandType command_type);
    
    // 设置客户端信息
    void setClientInfo(const ClientInfo& info);
    
    // 设置结果回调
    void setResultCallback(ResultCallback callback);
    
    // 获取系统状态
    SystemStatus getStatus() const;
    
    // 是否已连接
    bool isConnected() const;
    
private:
    // 网络接收线程
    void receiveThread();
    
    // 心跳线程
    void heartbeatThread();
    
    // 处理接收到的数据包
    void handlePacket(const std::vector<uint8_t>& data);
    
    // 处理服务器信息包
    void handleServerInfo(const ServerInfoPacket& packet);
    
    // 处理检测结果包
    void handleDetectionResult(const DetectionResultPacket& packet);
    
    // 初始化Winsock
    bool initializeWinsock();
    
    // 清理Winsock
    void cleanupWinsock();
    
    // 创建套接字
    bool createSocket();
    
    // 关闭套接字
    void closeSocket();
    
    // 发送数据包
    bool sendPacket(const Packet& packet);
    
    // 计算网络延迟
    void updatePing(uint64_t send_time, uint64_t receive_time);
    
private:
    // 服务器信息
    std::string server_ip_;
    uint16_t server_port_;
    struct sockaddr_in server_addr_;
    
    // 客户端信息
    ClientInfo client_info_;
    
    // 套接字
    SOCKET socket_;
    
    // 线程控制
    std::atomic<bool> running_;
    std::thread receive_thread_;
    std::thread heartbeat_thread_;
    
    // 连接状态
    std::atomic<bool> connected_;
    uint32_t sequence_number_;
    
    // 回调函数
    ResultCallback result_callback_;
    
    // 系统状态
    mutable std::mutex status_mutex_;
    SystemStatus status_;
    
    // 互斥锁
    mutable std::mutex frame_mutex_;
    
    // 帧ID计数器
    uint32_t frame_id_counter_;
};

} // namespace zero_latency