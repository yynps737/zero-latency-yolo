#include "network.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include "../common/constants.h"

#pragma comment(lib, "ws2_32.lib")

namespace zero_latency {

NetworkClient::NetworkClient(const std::string& server_ip, uint16_t server_port)
    : server_ip_(server_ip),
      server_port_(server_port),
      socket_(INVALID_SOCKET),
      running_(false),
      connected_(false),
      sequence_number_(0),
      frame_id_counter_(0) {
    
    // 初始化服务器地址
    memset(&server_addr_, 0, sizeof(server_addr_));
    server_addr_.sin_family = AF_INET;
    server_addr_.sin_port = htons(server_port_);
    
    // 初始化系统状态
    memset(&status_, 0, sizeof(status_));
}

NetworkClient::~NetworkClient() {
    disconnect();
    cleanupWinsock();
}

bool NetworkClient::initialize() {
    // 初始化Winsock
    if (!initializeWinsock()) {
        std::cerr << "初始化Winsock失败" << std::endl;
        return false;
    }
    
    // 解析服务器IP地址
    if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr_.sin_addr) != 1) {
        std::cerr << "解析服务器IP地址失败: " << server_ip_ << std::endl;
        cleanupWinsock();
        return false;
    }
    
    // 创建套接字
    if (!createSocket()) {
        std::cerr << "创建套接字失败" << std::endl;
        cleanupWinsock();
        return false;
    }
    
    return true;
}

bool NetworkClient::connect() {
    if (connected_) {
        return true;
    }
    
    // 确保套接字有效
    if (socket_ == INVALID_SOCKET) {
        if (!createSocket()) {
            std::cerr << "创建套接字失败" << std::endl;
            return false;
        }
    }
    
    // 设置连接超时
    DWORD timeout = 3000; // 3秒
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        std::cerr << "设置接收超时失败: " << WSAGetLastError() << std::endl;
        // 非致命错误，继续
    }
    
    // 发送客户端信息包
    ClientInfoPacket info_packet;
    info_packet.setInfo(client_info_);
    info_packet.setTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());
    
    if (!sendPacket(info_packet)) {
        std::cerr << "发送客户端信息失败" << std::endl;
        return false;
    }
    
    // 等待服务器响应
    char buffer[1024];
    int addr_len = sizeof(server_addr_);
    int received = recvfrom(socket_, buffer, sizeof(buffer), 0, 
                           (struct sockaddr*)&server_addr_, &addr_len);
    
    if (received <= 0) {
        std::cerr << "接收服务器响应失败: " << WSAGetLastError() << std::endl;
        return false;
    }
    
    // 重置超时为正常值
    timeout = 0; // 不超时
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        std::cerr << "重置接收超时失败: " << WSAGetLastError() << std::endl;
        // 非致命错误，继续
    }
    
    // 处理服务器响应
    std::vector<uint8_t> response_data(buffer, buffer + received);
    std::unique_ptr<Packet> packet = PacketFactory::createFromBuffer(response_data);
    
    if (!packet || packet->getType() != PacketType::SERVER_INFO) {
        std::cerr << "无效的服务器响应" << std::endl;
        return false;
    }
    
    // 处理服务器信息
    ServerInfoPacket* server_info = dynamic_cast<ServerInfoPacket*>(packet.get());
    if (server_info) {
        handleServerInfo(*server_info);
    }
    
    // 启动接收线程
    running_ = true;
    connected_ = true;
    receive_thread_ = std::thread(&NetworkClient::receiveThread, this);
    heartbeat_thread_ = std::thread(&NetworkClient::heartbeatThread, this);
    
    std::cout << "已连接到服务器: " << server_ip_ << ":" << server_port_ << std::endl;
    return true;
}

void NetworkClient::disconnect() {
    if (!connected_) {
        return;
    }
    
    // 发送断开连接命令
    sendCommand(CommandType::DISCONNECT);
    
    // 停止线程
    running_ = false;
    
    // 等待线程结束
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    
    // 关闭套接字
    closeSocket();
    
    connected_ = false;
    std::cout << "已断开与服务器的连接" << std::endl;
}

bool NetworkClient::sendFrame(const FrameData& frame) {
    if (!connected_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(frame_mutex_);
    
    // 创建帧数据包
    FrameDataPacket packet;
    packet.setFrameData(frame);
    packet.setSequence(sequence_number_++);
    packet.setTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());
    
    // 发送数据包
    return sendPacket(packet);
}

bool NetworkClient::sendCommand(CommandType command_type) {
    if (!connected_ && command_type != CommandType::DISCONNECT) {
        return false;
    }
    
    // TODO: 实现命令包
    return true;
}

void NetworkClient::setClientInfo(const ClientInfo& info) {
    client_info_ = info;
}

void NetworkClient::setResultCallback(ResultCallback callback) {
    result_callback_ = std::move(callback);
}

SystemStatus NetworkClient::getStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return status_;
}

bool NetworkClient::isConnected() const {
    return connected_;
}

void NetworkClient::receiveThread() {
    std::vector<uint8_t> buffer(constants::MAX_PACKET_SIZE);
    
    while (running_) {
        // 检查套接字是否有效
        if (socket_ == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // 接收数据
        struct sockaddr_in from_addr;
        int addr_len = sizeof(from_addr);
        int received = recvfrom(socket_, reinterpret_cast<char*>(buffer.data()), 
                               static_cast<int>(buffer.size()), 0, 
                               (struct sockaddr*)&from_addr, &addr_len);
        
        if (received > 0) {
            // 验证数据来源
            if (from_addr.sin_addr.s_addr == server_addr_.sin_addr.s_addr && 
                from_addr.sin_port == server_addr_.sin_port) {
                
                // 处理数据包
                buffer.resize(received);
                handlePacket(buffer);
                buffer.resize(constants::MAX_PACKET_SIZE);
            }
        } else if (received == 0) {
            // 连接关闭
            std::cerr << "连接已关闭" << std::endl;
            connected_ = false;
            break;
        } else {
            // 接收错误
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK && error != WSAETIMEDOUT) {
                std::cerr << "接收数据失败: " << error << std::endl;
                connected_ = false;
                break;
            }
        }
        
        // 短暂休眠以减少CPU使用
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void NetworkClient::heartbeatThread() {
    while (running_ && connected_) {
        // 发送心跳包
        HeartbeatPacket heartbeat;
        heartbeat.setPing(0); // 客户端不需要计算服务器的ping值
        heartbeat.setTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count());
        
        sendPacket(heartbeat);
        
        // 每秒发送一次心跳
        std::this_thread::sleep_for(std::chrono::milliseconds(constants::HEARTBEAT_INTERVAL_MS));
    }
}

void NetworkClient::handlePacket(const std::vector<uint8_t>& data) {
    std::unique_ptr<Packet> packet = PacketFactory::createFromBuffer(data);
    if (!packet) {
        std::cerr << "无法解析数据包" << std::endl;
        return;
    }
    
    // 根据数据包类型处理
    switch (packet->getType()) {
        case PacketType::HEARTBEAT: {
            auto heartbeat = dynamic_cast<HeartbeatPacket*>(packet.get());
            if (heartbeat) {
                // 计算往返延迟
                uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                
                updatePing(heartbeat->getTimestamp(), current_time);
            }
            break;
        }
            
        case PacketType::SERVER_INFO: {
            auto server_info = dynamic_cast<ServerInfoPacket*>(packet.get());
            if (server_info) {
                handleServerInfo(*server_info);
            }
            break;
        }
            
        case PacketType::DETECTION_RESULT: {
            auto detection_result = dynamic_cast<DetectionResultPacket*>(packet.get());
            if (detection_result) {
                handleDetectionResult(*detection_result);
            }
            break;
        }
            
        case PacketType::ERROR: {
            // TODO: 处理错误包
            std::cerr << "收到错误包" << std::endl;
            break;
        }
            
        default:
            std::cerr << "未处理的数据包类型: " << static_cast<int>(packet->getType()) << std::endl;
            break;
    }
}

void NetworkClient::handleServerInfo(const ServerInfoPacket& packet) {
    const ServerInfo& info = packet.getInfo();
    
    std::lock_guard<std::mutex> lock(status_mutex_);
    
    // 更新状态信息
    status_.fps = info.max_fps;
    
    // 计算延迟
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    updatePing(packet.getTimestamp(), current_time);
}

void NetworkClient::handleDetectionResult(const DetectionResultPacket& packet) {
    const GameState& state = packet.getGameState();
    
    // 调用回调函数
    if (result_callback_) {
        result_callback_(state);
    }
}

bool NetworkClient::initializeWinsock() {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "WSAStartup失败: " << WSAGetLastError() << std::endl;
        return false;
    }
    
    return true;
}

void NetworkClient::cleanupWinsock() {
    WSACleanup();
}

bool NetworkClient::createSocket() {
    // 创建UDP套接字
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        std::cerr << "创建套接字失败: " << WSAGetLastError() << std::endl;
        return false;
    }
    
    // 设置非阻塞模式
    u_long mode = 1;
    if (ioctlsocket(socket_, FIONBIO, &mode) != 0) {
        std::cerr << "设置非阻塞模式失败: " << WSAGetLastError() << std::endl;
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    // 设置缓冲区大小
    int buffer_size = 1024 * 1024; // 1MB
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, (const char*)&buffer_size, sizeof(buffer_size)) != 0) {
        std::cerr << "设置接收缓冲区大小失败: " << WSAGetLastError() << std::endl;
        // 非致命错误，继续
    }
    
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDBUF, (const char*)&buffer_size, sizeof(buffer_size)) != 0) {
        std::cerr << "设置发送缓冲区大小失败: " << WSAGetLastError() << std::endl;
        // 非致命错误，继续
    }
    
    return true;
}

void NetworkClient::closeSocket() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

bool NetworkClient::sendPacket(const Packet& packet) {
    if (socket_ == INVALID_SOCKET) {
        return false;
    }
    
    // 序列化数据包
    std::vector<uint8_t> data = packet.serialize();
    
    // 发送数据
    int sent = sendto(socket_, reinterpret_cast<const char*>(data.data()), 
                     static_cast<int>(data.size()), 0, 
                     (struct sockaddr*)&server_addr_, sizeof(server_addr_));
    
    if (sent != data.size()) {
        std::cerr << "发送数据失败: " << WSAGetLastError() << std::endl;
        return false;
    }
    
    return true;
}

void NetworkClient::updatePing(uint64_t send_time, uint64_t receive_time) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    
    // 计算往返延迟
    uint16_t rtt = static_cast<uint16_t>(receive_time - send_time);
    
    // 平滑更新ping值
    if (status_.ping == 0) {
        status_.ping = rtt;
    } else {
        status_.ping = (status_.ping * 7 + rtt) / 8; // 指数平滑
    }
}

} // namespace zero_latency