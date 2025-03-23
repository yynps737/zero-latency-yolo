#include "reliable_udp.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>
#include "../common/logger.h"
#include "../common/event_bus.h"

namespace zero_latency {

// 构造函数
ReliableUdpServer::ReliableUdpServer(const ReliableUdpConfig& config)
    : config_(config),
      socket_fd_(-1),
      running_(false),
      next_client_id_(1),
      next_aggregation_id_(1),
      total_packets_sent_(0),
      total_packets_received_(0),
      total_bytes_sent_(0),
      total_bytes_received_(0),
      total_packets_retransmitted_(0),
      total_packets_dropped_(0) {
}

// 析构函数
ReliableUdpServer::~ReliableUdpServer() {
    stop();
}

// 初始化服务器
Result<void> ReliableUdpServer::initialize() {
    // 创建UDP套接字
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        std::string errorMsg = "Failed to create socket: " + std::string(strerror(errno));
        LOG_ERROR(errorMsg);
        return Result<void>::error(ErrorCode::SOCKET_ERROR, errorMsg);
    }
    
    // 设置为非阻塞模式
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (flags < 0) {
        std::string errorMsg = "Failed to get socket flags: " + std::string(strerror(errno));
        LOG_ERROR(errorMsg);
        close(socket_fd_);
        socket_fd_ = -1;
        return Result<void>::error(ErrorCode::SOCKET_ERROR, errorMsg);
    }
    
    if (fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::string errorMsg = "Failed to set non-blocking mode: " + std::string(strerror(errno));
        LOG_ERROR(errorMsg);
        close(socket_fd_);
        socket_fd_ = -1;
        return Result<void>::error(ErrorCode::SOCKET_ERROR, errorMsg);
    }
    
    // 设置套接字选项
    int option = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
        std::string errorMsg = "Failed to set SO_REUSEADDR: " + std::string(strerror(errno));
        LOG_ERROR(errorMsg);
        close(socket_fd_);
        socket_fd_ = -1;
        return Result<void>::error(ErrorCode::SOCKET_ERROR, errorMsg);
    }
    
    // 增加缓冲区大小
    int rcvbuf_size = config_.recv_buffer_size;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
        LOG_WARN("Failed to set SO_RCVBUF to " + std::to_string(rcvbuf_size) + 
                 ": " + std::string(strerror(errno)));
        // 非致命错误，继续
    }
    
    int sndbuf_size = config_.send_buffer_size;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDBUF, &sndbuf_size, sizeof(sndbuf_size)) < 0) {
        LOG_WARN("Failed to set SO_SNDBUF to " + std::to_string(sndbuf_size) + 
                 ": " + std::string(strerror(errno)));
        // 非致命错误，继续
    }
    
    // 绑定地址
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(config_.port);
    
    if (bind(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        if (errno == EADDRINUSE) {
            LOG_WARN("Port " + std::to_string(config_.port) + " is already in use, trying port " +
                     std::to_string(config_.port + 1));
            
            close(socket_fd_);
            socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
            if (socket_fd_ < 0) {
                std::string errorMsg = "Failed to recreate socket: " + std::string(strerror(errno));
                LOG_ERROR(errorMsg);
                return Result<void>::error(ErrorCode::SOCKET_ERROR, errorMsg);
            }
            
            // 设置新的非阻塞模式
            fcntl(socket_fd_, F_SETFL, O_NONBLOCK);
            
            // 重新设置SO_REUSEADDR
            setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
            
            // 设置新端口
            config_.port++;
            server_addr.sin_port = htons(config_.port);
            
            // 重试绑定
            if (bind(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                std::string errorMsg = "Failed to bind to backup port: " + std::string(strerror(errno));
                LOG_ERROR(errorMsg);
                close(socket_fd_);
                socket_fd_ = -1;
                return Result<void>::error(ErrorCode::SOCKET_ERROR, errorMsg);
            }
            
            LOG_INFO("Successfully bound to backup port: " + std::to_string(config_.port));
        } else {
            std::string errorMsg = "Failed to bind address: " + std::string(strerror(errno));
            LOG_ERROR(errorMsg);
            close(socket_fd_);
            socket_fd_ = -1;
            return Result<void>::error(ErrorCode::SOCKET_ERROR, errorMsg);
        }
    }
    
    LOG_INFO("ReliableUdpServer initialized on port " + std::to_string(config_.port));
    return Result<void>::ok();
}

// 启动服务器
Result<void> ReliableUdpServer::start() {
    if (socket_fd_ < 0) {
        return Result<void>::error(ErrorCode::NOT_INITIALIZED, "Socket not initialized");
    }
    
    if (running_) {
        return Result<void>::error(ErrorCode::INVALID_ARGUMENT, "Server already running");
    }
    
    running_ = true;
    
    // 启动接收线程
    receive_thread_ = std::thread(&ReliableUdpServer::receiveThreadFunc, this);
    
    // 启动管理线程
    management_thread_ = std::thread(&ReliableUdpServer::managementThreadFunc, this);
    
    LOG_INFO("ReliableUdpServer started");
    
    // 发布网络启动事件
    Event event(events::SYSTEM_STARTUP);
    event.setSource("ReliableUdpServer");
    publishEvent(event);
    
    return Result<void>::ok();
}

// 停止服务器
Result<void> ReliableUdpServer::stop() {
    if (!running_) {
        return Result<void>::ok();
    }
    
    running_ = false;
    
    // 等待线程退出
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    
    if (management_thread_.joinable()) {
        management_thread_.join();
    }
    
    // 关闭套接字
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    
    // 清理客户端列表
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.clear();
    }
    
    // 清理聚合包
    {
        std::lock_guard<std::mutex> lock(aggregation_mutex_);
        aggregated_packets_.clear();
    }
    
    LOG_INFO("ReliableUdpServer stopped");
    
    // 发布网络关闭事件
    Event event(events::SYSTEM_SHUTDOWN);
    event.setSource("ReliableUdpServer");
    publishEvent(event);
    
    return Result<void>::ok();
}

// 发送数据包
Result<void> ReliableUdpServer::sendPacket(const std::vector<uint8_t>& data, const struct sockaddr_in& addr, bool reliable) {
    if (socket_fd_ < 0) {
        return Result<void>::error(ErrorCode::NOT_INITIALIZED, "Socket not initialized");
    }
    
    if (!running_) {
        return Result<void>::error(ErrorCode::NOT_INITIALIZED, "Server not running");
    }
    
    if (data.empty()) {
        return Result<void>::error(ErrorCode::INVALID_ARGUMENT, "Empty data");
    }
    
    if (data.size() > PROTOCOL_MAX_PACKET_SIZE) {
        return Result<void>::error(ErrorCode::PACKET_TOO_LARGE, 
                                  "Packet too large: " + std::to_string(data.size()) + 
                                  " bytes (max: " + std::to_string(PROTOCOL_MAX_PACKET_SIZE) + " bytes)");
    }
    
    // 如果使用聚合，尝试添加到现有聚合包
    if (config_.use_packet_aggregation && data.size() < 1024) {
        std::lock_guard<std::mutex> lock(aggregation_mutex_);
        
        // 查找对应客户端的聚合包
        for (auto& [id, aggr] : aggregated_packets_) {
            if (isSameAddr(addr, sockaddr_in{})) {
                continue; // 跳过无效地址
            }
            
            // 检查大小限制
            if (aggr.total_size + data.size() <= config_.max_aggregation_size) {
                // 检查时间限制
                uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                
                if (current_time - aggr.creation_time <= config_.aggregation_time_ms) {
                    // 添加到聚合包
                    aggr.packets.push_back(data);
                    aggr.total_size += data.size();
                    
                    // 更新统计
                    total_packets_sent_++;
                    total_bytes_sent_ += data.size();
                    
                    return Result<void>::ok();
                }
            }
        }
        
        // 没有合适的聚合包，创建新的
        if (reliable) {
            AggregatedPacket aggr;
            aggr.aggregation_id = next_aggregation_id_++;
            aggr.creation_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            aggr.packets.push_back(data);
            aggr.total_size = data.size();
            
            aggregated_packets_[aggr.aggregation_id] = aggr;
            
            // 更新统计
            total_packets_sent_++;
            total_bytes_sent_ += data.size();
            
            return Result<void>::ok();
        }
    }
    
    // 直接发送
    ssize_t sent = sendto(socket_fd_, data.data(), data.size(), 0, 
                          (struct sockaddr*)&addr, sizeof(addr));
    
    if (sent < 0) {
        std::string errorMsg = "Failed to send data: " + std::string(strerror(errno));
        LOG_ERROR(errorMsg);
        return Result<void>::error(ErrorCode::NETWORK_ERROR, errorMsg);
    }
    
    if (static_cast<size_t>(sent) != data.size()) {
        std::string errorMsg = "Incomplete data sent: expected " + std::to_string(data.size()) + 
                              " bytes, sent " + std::to_string(sent) + " bytes";
        LOG_ERROR(errorMsg);
        return Result<void>::error(ErrorCode::NETWORK_ERROR, errorMsg);
    }
    
    // 如果是可靠发送，需要跟踪包
    if (reliable) {
        uint32_t client_id = 0;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            // 查找客户端
            for (const auto& [id, client] : clients_) {
                if (isSameAddr(client.addr, addr)) {
                    client_id = id;
                    break;
                }
            }
        }
        
        if (client_id > 0) {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            auto& client = clients_[client_id];
            
            // 添加到未确认列表
            PacketAckInfo ack_info;
            ack_info.sequence = client.next_send_sequence++;
            ack_info.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            ack_info.retries = 0;
            ack_info.data = data;
            
            client.unacked_packets[ack_info.sequence] = ack_info;
        }
    }
    
    // 更新统计
    total_packets_sent_++;
    total_bytes_sent_ += data.size();
    
    // 发布数据包发送事件
    EventBus::getInstance().publishSimpleEvent(events::PACKET_SENT);
    
    return Result<void>::ok();
}

// 设置数据包处理器
void ReliableUdpServer::setPacketHandler(PacketHandler handler) {
    packet_handler_ = std::move(handler);
}

// 获取客户端数量
size_t ReliableUdpServer::getClientCount() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

// 检查客户端是否存在
bool ReliableUdpServer::hasClient(uint32_t client_id) const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.find(client_id) != clients_.end();
}

// 根据地址查找客户端ID
std::optional<uint32_t> ReliableUdpServer::findClientByAddr(const struct sockaddr_in& addr) const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (const auto& [id, client] : clients_) {
        if (isSameAddr(client.addr, addr)) {
            return id;
        }
    }
    
    return std::nullopt;
}

// 注册新客户端
Result<uint32_t> ReliableUdpServer::registerClient(const struct sockaddr_in& addr, const ClientInfo& info) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    // 检查是否已达到最大客户端数
    if (clients_.size() >= config_.max_clients) {
        return Result<uint32_t>::error(ErrorCode::SERVER_FULL, "Server has reached maximum client limit");
    }
    
    // 检查是否已存在相同地址的客户端
    for (const auto& [id, client] : clients_) {
        if (isSameAddr(client.addr, addr)) {
            // 更新现有客户端信息
            clients_[id].info = info;
            clients_[id].last_active_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            clients_[id].connected = true;
            
            LOG_INFO("Updated client #" + std::to_string(id) + " info, game ID: " + 
                    std::to_string(info.game_id));
            
            return Result<uint32_t>::ok(id);
        }
    }
    
    // 创建新客户端
    uint32_t client_id = next_client_id_++;
    
    ClientConnection connection;
    connection.client_id = client_id;
    connection.addr = addr;
    connection.last_active_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    connection.last_frame_processed = 0;
    connection.info = info;
    connection.connected = true;
    connection.initConnection();
    
    clients_[client_id] = connection;
    
    LOG_INFO("New client #" + std::to_string(client_id) + " connected, IP: " + 
             inet_ntoa(addr.sin_addr) + ":" + std::to_string(ntohs(addr.sin_port)) + 
             ", game ID: " + std::to_string(info.game_id));
    
    // 发布客户端连接事件
    EventBus::getInstance().publishClientEvent(events::CLIENT_CONNECTED, client_id);
    
    return Result<uint32_t>::ok(client_id);
}

// 移除客户端
Result<void> ReliableUdpServer::removeClient(uint32_t client_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        LOG_INFO("Client #" + std::to_string(client_id) + " disconnected");
        
        // 发布客户端断开连接事件
        EventBus::getInstance().publishClientEvent(events::CLIENT_DISCONNECTED, client_id);
        
        // 移除客户端
        clients_.erase(it);
        
        return Result<void>::ok();
    }
    
    return Result<void>::error(ErrorCode::INVALID_ARGUMENT, "Client not found: " + std::to_string(client_id));
}

// 获取服务器状态信息
std::unordered_map<std::string, std::string> ReliableUdpServer::getStatus() const {
    std::unordered_map<std::string, std::string> status;
    
    status["running"] = running_ ? "true" : "false";
    status["port"] = std::to_string(config_.port);
    status["client_count"] = std::to_string(getClientCount());
    status["packets_sent"] = std::to_string(total_packets_sent_);
    status["packets_received"] = std::to_string(total_packets_received_);
    status["bytes_sent"] = std::to_string(total_bytes_sent_);
    status["bytes_received"] = std::to_string(total_bytes_received_);
    status["packets_retransmitted"] = std::to_string(total_packets_retransmitted_);
    status["packets_dropped"] = std::to_string(total_packets_dropped_);
    
    return status;
}

// 接收线程函数
void ReliableUdpServer::receiveThreadFunc() {
    LOG_INFO("Receive thread started");
    
    std::vector<uint8_t> buffer(PROTOCOL_MAX_PACKET_SIZE);
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    while (running_) {
        std::memset(&client_addr, 0, sizeof(client_addr));
        
        // 接收数据
        ssize_t received = recvfrom(socket_fd_, buffer.data(), buffer.size(), 0,
                                   (struct sockaddr*)&client_addr, &addr_len);
        
        if (received > 0) {
            // 调整缓冲区大小以匹配接收到的数据量
            buffer.resize(received);
            
            // 更新统计信息
            total_packets_received_++;
            total_bytes_received_ += received;
            
            // 处理接收到的数据包
            handleReceivedPacket(buffer, client_addr);
            
            // 重置缓冲区大小以供下次使用
            buffer.resize(PROTOCOL_MAX_PACKET_SIZE);
        } else if (received < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("Failed to receive data: " + std::string(strerror(errno)));
                
                // 对于致命错误，可能需要重新初始化套接字
                if (errno == EBADF || errno == ECONNRESET || errno == EHOSTUNREACH) {
                    LOG_ERROR("Critical network error detected, trying to reinitialize socket...");
                    close(socket_fd_);
                    
                    // 尝试重新初始化
                    auto result = initialize();
                    if (result.hasError()) {
                        LOG_ERROR("Failed to reinitialize network: " + result.error().message);
                        running_ = false;
                        break;
                    }
                }
            }
            // 对于EAGAIN/EWOULDBLOCK，这是正常的非阻塞行为，继续循环
        }
        
        // 短暂休眠以减少CPU使用
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    LOG_INFO("Receive thread stopped");
}

// 管理线程函数
void ReliableUdpServer::managementThreadFunc() {
    LOG_INFO("Management thread started");
    
    while (running_) {
        // 检查客户端超时
        checkClientTimeouts();
        
        // 处理重传
        handleRetransmissions();
        
        // 处理聚合数据包
        handleAggregatedPackets();
        
        // 每100毫秒执行一次
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_INFO("Management thread stopped");
}

// 处理接收到的数据包
void ReliableUdpServer::handleReceivedPacket(const std::vector<uint8_t>& data, const struct sockaddr_in& addr) {
    if (data.size() < sizeof(PacketHeader)) {
        // 包太小，无法解析头部
        return;
    }
    
    const PacketHeader* header = reinterpret_cast<const PacketHeader*>(data.data());
    
    // 验证魔数和版本
    if (header->magic != PROTOCOL_MAGIC_NUMBER || header->version != PROTOCOL_VERSION) {
        LOG_WARN("Invalid packet magic or version from " + 
                std::string(inet_ntoa(addr.sin_addr)) + ":" + std::to_string(ntohs(addr.sin_port)));
        return;
    }
    
    // 验证长度
    if (sizeof(PacketHeader) + header->length != data.size()) {
        LOG_WARN("Invalid packet length from " + 
                std::string(inet_ntoa(addr.sin_addr)) + ":" + std::to_string(ntohs(addr.sin_port)));
        return;
    }
    
    // 验证校验和
    uint16_t original_checksum = header->checksum;
    uint16_t calculated_checksum;
    
    // 临时创建一个没有校验和的拷贝
    std::vector<uint8_t> temp_buffer = data;
    std::memset(temp_buffer.data() + offsetof(PacketHeader, checksum), 0, sizeof(header->checksum));
    
    // 计算校验和
    calculated_checksum = calculateCRC16(
        temp_buffer.data() + sizeof(header->checksum),
        temp_buffer.size() - sizeof(header->checksum)
    );
    
    if (calculated_checksum != original_checksum) {
        LOG_WARN("Invalid packet checksum from " + 
                std::string(inet_ntoa(addr.sin_addr)) + ":" + std::to_string(ntohs(addr.sin_port)));
        return;
    }
    
    // 查找客户端ID
    uint32_t client_id = 0;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (const auto& [id, client] : clients_) {
            if (isSameAddr(client.addr, addr)) {
                client_id = id;
                
                // 更新客户端活动时间
                clients_[id].last_active_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                
                break;
            }
        }
    }
    
    // 检查是否需要发送确认包
    if (header->type != static_cast<uint8_t>(PacketType::ACK)) {
        sendAck(header->sequence, addr);
    }
    
    // 如果是ACK包，处理确认
    if (header->type == static_cast<uint8_t>(PacketType::ACK)) {
        if (client_id > 0) {
            handleAck(header->sequence, client_id);
        }
        return;
    }
    
    // 调用数据包处理器
    if (packet_handler_) {
        packet_handler_(data, addr);
    }
    
    // 发布数据包接收事件
    if (client_id > 0) {
        EventBus::getInstance().publishPacketEvent(events::PACKET_RECEIVED, client_id, header->sequence, header->type);
    }
}

// 处理确认包
void ReliableUdpServer::handleAck(uint32_t sequence, uint32_t client_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(client_id);
    if (it == clients_.end()) {
        return;
    }
    
    auto& client = it->second;
    
    // 查找对应的未确认包
    auto ack_it = client.unacked_packets.find(sequence);
    if (ack_it != client.unacked_packets.end()) {
        // 计算RTT
        uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        uint64_t rtt = current_time - ack_it->second.timestamp;
        
        // 更新客户端RTT
        client.updateRtt(rtt);
        
        // 增加拥塞窗口
        if (config_.congestion_control_enabled) {
            client.increaseCongestionWindow();
        }
        
        // 移除已确认的包
        client.unacked_packets.erase(ack_it);
    }
}

// 发送确认包
void ReliableUdpServer::sendAck(uint32_t sequence, const struct sockaddr_in& addr) {
    // 创建确认包头部
    PacketHeader header;
    header.magic = PROTOCOL_MAGIC_NUMBER;
    header.version = PROTOCOL_VERSION;
    header.type = static_cast<uint8_t>(PacketType::ACK);
    header.length = 0;  // 确认包没有额外数据
    header.sequence = sequence;
    header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    header.checksum = 0;  // 初始化为0，稍后计算
    
    // 计算校验和
    header.checksum = calculateCRC16(
        reinterpret_cast<const uint8_t*>(&header) + sizeof(header.checksum),
        sizeof(header) - sizeof(header.checksum)
    );
    
    // 发送确认包
    sendto(socket_fd_, &header, sizeof(header), 0, 
          (struct sockaddr*)&addr, sizeof(addr));
    
    // 更新统计
    total_packets_sent_++;
    total_bytes_sent_ += sizeof(header);
}

// 检查客户端超时
void ReliableUdpServer::checkClientTimeouts() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    std::vector<uint32_t> timeout_clients;
    
    for (const auto& [id, client] : clients_) {
        if (current_time - client.last_active_time > config_.timeout_ms) {
            timeout_clients.push_back(id);
        }
    }
    
    // 移除超时客户端
    for (uint32_t client_id : timeout_clients) {
        LOG_INFO("Client #" + std::to_string(client_id) + " timed out");
        
        // 发布客户端超时事件
        EventBus::getInstance().publishClientEvent(events::CLIENT_TIMEOUT, client_id);
        
        // 移除客户端
        clients_.erase(client_id);
    }
}

// 处理重传超时
void ReliableUdpServer::handleRetransmissions() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    for (auto& [client_id, client] : clients_) {
        std::vector<uint32_t> expired_packets;
        
        // 检查超时的包
        for (auto& [seq, packet] : client.unacked_packets) {
            // 计算经过的时间
            uint64_t elapsed = current_time - packet.timestamp;
            
            // 检查是否超过重传超时时间
            if (elapsed > client.retransmission_timeout_ms) {
                // 检查是否已达到最大重试次数
                if (packet.retries >= config_.max_retries) {
                    // 丢弃包
                    expired_packets.push_back(seq);
                    total_packets_dropped_++;
                    
                    // 触发拥塞控制
                    if (config_.congestion_control_enabled) {
                        client.handlePacketLoss(true);
                    }
                } else {
                    // 重传包
                    sendto(socket_fd_, packet.data.data(), packet.data.size(), 0,
                          (struct sockaddr*)&client.addr, sizeof(client.addr));
                    
                    // 更新重传信息
                    packet.retries++;
                    packet.timestamp = current_time;
                    
                    // 更新统计
                    total_packets_retransmitted_++;
                    total_packets_sent_++;
                    total_bytes_sent_ += packet.data.size();
                    
                    LOG_DEBUG("Retransmitting packet #" + std::to_string(seq) + 
                             " to client #" + std::to_string(client_id) + 
                             " (retry " + std::to_string(packet.retries) + 
                             " of " + std::to_string(config_.max_retries) + ")");
                    
                    // 触发拥塞控制
                    if (config_.congestion_control_enabled && packet.retries == 1) {
                        client.handlePacketLoss(true);
                    }
                }
            }
        }
        
        // 移除过期的包
        for (uint32_t seq : expired_packets) {
            client.unacked_packets.erase(seq);
        }
    }
}

// 处理聚合数据包
void ReliableUdpServer::handleAggregatedPackets() {
    std::lock_guard<std::mutex> lock(aggregation_mutex_);
    
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    std::vector<uint32_t> packets_to_send;
    
    // 查找需要发送的聚合包
    for (const auto& [id, aggr] : aggregated_packets_) {
        if (current_time - aggr.creation_time > config_.aggregation_time_ms) {
            packets_to_send.push_back(id);
        }
    }
    
    // 发送并移除聚合包
    for (uint32_t id : packets_to_send) {
        const auto& aggr = aggregated_packets_[id];
        
        // TODO: 实现真正的聚合包发送
        // 现在简单地按顺序发送每个包
        
        // 移除聚合包
        aggregated_packets_.erase(id);
    }
}

// 比较地址是否相同
bool ReliableUdpServer::isSameAddr(const struct sockaddr_in& addr1, const struct sockaddr_in& addr2) {
    return addr1.sin_addr.s_addr == addr2.sin_addr.s_addr && 
           addr1.sin_port == addr2.sin_port;
}

// 序列号比较 (考虑环绕)
bool ReliableUdpServer::isSequenceNewer(uint32_t a, uint32_t b) {
    return ((a > b) && (a - b <= 0x7FFFFFFF)) ||
           ((a < b) && (b - a > 0x7FFFFFFF));
}

} // namespace zero_latency