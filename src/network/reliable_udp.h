#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <optional>
#include <netinet/in.h>
#include "../common/result.h"
#include "../common/logger.h"
#include "../common/protocol.h"
#include "../common/event_bus.h"

namespace zero_latency {

// 可靠UDP配置
struct ReliableUdpConfig {
    uint16_t port;                        // 监听端口
    uint32_t send_buffer_size;            // 发送缓冲区大小
    uint32_t recv_buffer_size;            // 接收缓冲区大小
    uint32_t timeout_ms;                  // 连接超时时间 (毫秒)
    uint32_t heartbeat_interval_ms;       // 心跳包间隔 (毫秒)
    uint8_t max_retries;                  // 最大重试次数
    uint32_t retry_interval_ms;           // 重试间隔 (毫秒)
    uint32_t ack_timeout_ms;              // 确认超时时间 (毫秒)
    uint32_t max_packets_in_flight;       // 最大同时在途包数量
    uint32_t max_packet_age_ms;           // 最大包寿命 (毫秒)
    bool use_packet_aggregation;          // 是否使用数据包聚合
    uint32_t aggregation_time_ms;         // 聚合时间窗口 (毫秒)
    uint32_t max_aggregation_size;        // 最大聚合大小
    bool congestion_control_enabled;      // 是否启用拥塞控制
    
    // 默认配置
    ReliableUdpConfig()
        : port(7788),
          send_buffer_size(1048576),      // 1MB
          recv_buffer_size(1048576),      // 1MB
          timeout_ms(5000),               // 5秒
          heartbeat_interval_ms(1000),    // 1秒
          max_retries(5),                 // 5次重试
          retry_interval_ms(200),         // 200毫秒
          ack_timeout_ms(500),            // 500毫秒
          max_packets_in_flight(32),      // 最多32个包在途
          max_packet_age_ms(10000),       // 10秒
          use_packet_aggregation(true),   // 启用聚合
          aggregation_time_ms(10),        // 10毫秒聚合窗口
          max_aggregation_size(8192),     // 最大8KB聚合
          congestion_control_enabled(true) // 启用拥塞控制
    {}
};

// 数据包确认信息
struct PacketAckInfo {
    uint32_t sequence;         // 数据包序列号
    uint64_t timestamp;        // 发送时间戳
    uint8_t retries;           // 重试次数
    std::vector<uint8_t> data; // 数据包内容
};

// 客户端连接状态
struct ClientConnection {
    uint32_t client_id;                                // 客户端ID
    struct sockaddr_in addr;                           // 客户端地址
    uint64_t last_active_time;                         // 最后活动时间
    uint32_t last_frame_processed;                     // 最后处理的帧ID
    ClientInfo info;                                   // 客户端信息
    bool connected;                                    // 是否已连接
    
    // 可靠传输相关
    uint32_t next_send_sequence;                       // 下一个发送序列号
    uint32_t next_expected_sequence;                   // 下一个期望接收的序列号
    std::unordered_map<uint32_t, PacketAckInfo> unacked_packets; // 未确认的数据包
    std::queue<uint32_t> out_of_order_packets;         // 乱序包队列
    
    // 拥塞控制
    uint32_t congestion_window;                        // 拥塞窗口大小
    uint32_t slow_start_threshold;                     // 慢启动阈值
    uint64_t last_rtt_ms;                              // 最后一次RTT (毫秒)
    uint64_t smoothed_rtt_ms;                          // 平滑RTT (毫秒)
    uint64_t rtt_variation_ms;                         // RTT变化量 (毫秒)
    uint64_t retransmission_timeout_ms;                // 重传超时 (毫秒)
    
    // 初始化新连接
    void initConnection() {
        next_send_sequence = 1;
        next_expected_sequence = 1;
        unacked_packets.clear();
        
        // 拥塞控制初始化
        congestion_window = 1;
        slow_start_threshold = 64;
        last_rtt_ms = 0;
        smoothed_rtt_ms = 500; // 初始500ms
        rtt_variation_ms = 250; // 初始250ms
        retransmission_timeout_ms = 1000; // 初始1秒
    }
    
    // 更新RTT
    void updateRtt(uint64_t measured_rtt_ms) {
        last_rtt_ms = measured_rtt_ms;
        
        // 使用RFC6298算法更新平滑RTT和RTT变化量
        const double alpha = 0.125;
        const double beta = 0.25;
        
        if (smoothed_rtt_ms == 0) {
            smoothed_rtt_ms = measured_rtt_ms;
            rtt_variation_ms = measured_rtt_ms / 2;
        } else {
            int64_t rtt_diff = static_cast<int64_t>(measured_rtt_ms) - static_cast<int64_t>(smoothed_rtt_ms);
            rtt_variation_ms = (1 - beta) * rtt_variation_ms + beta * std::abs(rtt_diff);
            smoothed_rtt_ms = (1 - alpha) * smoothed_rtt_ms + alpha * measured_rtt_ms;
        }
        
        // 更新重传超时
        retransmission_timeout_ms = smoothed_rtt_ms + 4 * rtt_variation_ms;
        
        // 确保超时时间在合理范围内
        retransmission_timeout_ms = std::max(retransmission_timeout_ms, static_cast<uint64_t>(200));
        retransmission_timeout_ms = std::min(retransmission_timeout_ms, static_cast<uint64_t>(10000));
    }
    
    // 增加拥塞窗口 (ACK接收时调用)
    void increaseCongestionWindow() {
        if (congestion_window < slow_start_threshold) {
            // 慢启动阶段：每个ACK使窗口增加1个MSS
            congestion_window++;
        } else {
            // 拥塞避免阶段：每个窗口大小的ACK使窗口增加1个MSS
            congestion_window += 1.0 / congestion_window;
        }
    }
    
    // 处理包丢失 (超时或3个重复ACK时调用)
    void handlePacketLoss(bool isTimeout) {
        if (isTimeout) {
            // 超时：慢启动阈值设为当前窗口的一半，窗口重置为1
            slow_start_threshold = std::max(congestion_window / 2, static_cast<uint32_t>(2));
            congestion_window = 1;
        } else {
            // 快速重传：慢启动阈值和窗口大小都设为当前窗口的一半
            slow_start_threshold = std::max(congestion_window / 2, static_cast<uint32_t>(2));
            congestion_window = slow_start_threshold + 3; // 快速恢复
        }
    }
};

// 聚合数据包结构
struct AggregatedPacket {
    uint32_t aggregation_id;               // 聚合包ID
    uint64_t creation_time;                // 创建时间
    std::vector<std::vector<uint8_t>> packets; // 包含的数据包
    uint32_t total_size;                   // 总大小
};

// 回调函数类型
using PacketHandler = std::function<void(const std::vector<uint8_t>&, const struct sockaddr_in&)>;

// 可靠UDP服务类
class ReliableUdpServer {
public:
    // 构造函数
    explicit ReliableUdpServer(const ReliableUdpConfig& config);
    
    // 析构函数
    ~ReliableUdpServer();
    
    // 禁用拷贝和赋值
    ReliableUdpServer(const ReliableUdpServer&) = delete;
    ReliableUdpServer& operator=(const ReliableUdpServer&) = delete;
    
    // 初始化服务器
    Result<void> initialize();
    
    // 启动服务器
    Result<void> start();
    
    // 停止服务器
    Result<void> stop();
    
    // 发送数据包
    Result<void> sendPacket(const std::vector<uint8_t>& data, const struct sockaddr_in& addr, bool reliable = true);
    
    // 设置数据包处理器
    void setPacketHandler(PacketHandler handler);
    
    // 获取客户端数量
    size_t getClientCount() const;
    
    // 检查客户端是否存在
    bool hasClient(uint32_t client_id) const;
    
    // 根据地址查找客户端ID
    std::optional<uint32_t> findClientByAddr(const struct sockaddr_in& addr) const;
    
    // 注册新客户端
    Result<uint32_t> registerClient(const struct sockaddr_in& addr, const ClientInfo& info);
    
    // 移除客户端
    Result<void> removeClient(uint32_t client_id);
    
    // 获取服务器状态信息
    std::unordered_map<std::string, std::string> getStatus() const;

private:
    // 接收线程函数
    void receiveThreadFunc();
    
    // 管理线程函数
    void managementThreadFunc();
    
    // 处理接收到的数据包
    void handleReceivedPacket(const std::vector<uint8_t>& data, const struct sockaddr_in& addr);
    
    // 处理确认包
    void handleAck(uint32_t sequence, uint32_t client_id);
    
    // 发送确认包
    void sendAck(uint32_t sequence, const struct sockaddr_in& addr);
    
    // 检查客户端超时
    void checkClientTimeouts();
    
    // 处理重传超时
    void handleRetransmissions();
    
    // 处理聚合数据包
    void handleAggregatedPackets();
    
    // 比较地址是否相同
    static bool isSameAddr(const struct sockaddr_in& addr1, const struct sockaddr_in& addr2);
    
    // 序列号比较 (考虑环绕)
    static bool isSequenceNewer(uint32_t a, uint32_t b);

private:
    // 配置
    ReliableUdpConfig config_;
    
    // 套接字
    int socket_fd_;
    
    // 线程控制
    std::atomic<bool> running_;
    std::thread receive_thread_;
    std::thread management_thread_;
    
    // 同步原语
    mutable std::mutex clients_mutex_;
    mutable std::mutex aggregation_mutex_;
    
    // 客户端连接
    std::unordered_map<uint32_t, ClientConnection> clients_;
    uint32_t next_client_id_;
    
    // 回调函数
    PacketHandler packet_handler_;
    
    // 聚合包
    std::unordered_map<uint32_t, AggregatedPacket> aggregated_packets_;
    uint32_t next_aggregation_id_;
    
    // 统计数据
    std::atomic<uint64_t> total_packets_sent_;
    std::atomic<uint64_t> total_packets_received_;
    std::atomic<uint64_t> total_bytes_sent_;
    std::atomic<uint64_t> total_bytes_received_;
    std::atomic<uint64_t> total_packets_retransmitted_;
    std::atomic<uint64_t> total_packets_dropped_;
};

} // namespace zero_latency