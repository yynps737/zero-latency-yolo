#include "network.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <chrono>
#include <thread>
#include "../common/constants.h"

namespace zero_latency {

NetworkServer::NetworkServer(uint16_t port, YoloEngine* yolo_engine, GameAdapter* game_adapter)
    : port_(port),
      socket_fd_(-1),
      yolo_engine_(yolo_engine),
      game_adapter_(game_adapter),
      next_client_id_(1),
      timeout_running_(false) {
}

NetworkServer::~NetworkServer() {
    shutdown();
}

bool NetworkServer::initialize() {
    // 创建UDP套接字
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "创建套接字失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    // 设置为非阻塞模式
    int flags = fcntl(socket_fd_, F_GETFL, 0);
    if (flags < 0) {
        std::cerr << "获取套接字标志失败: " << strerror(errno) << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    if (fcntl(socket_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "设置非阻塞模式失败: " << strerror(errno) << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    // 设置套接字选项
    int option = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
        std::cerr << "设置SO_REUSEADDR失败: " << strerror(errno) << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    // 增加缓冲区大小
    int rcvbuf_size = 1024 * 1024; // 1MB接收缓冲区
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
        std::cerr << "设置SO_RCVBUF失败: " << strerror(errno) << std::endl;
        // 不是致命错误，继续
    }
    
    int sndbuf_size = 1024 * 1024; // 1MB发送缓冲区
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_SNDBUF, &sndbuf_size, sizeof(sndbuf_size)) < 0) {
        std::cerr << "设置SO_SNDBUF失败: " << strerror(errno) << std::endl;
        // 不是致命错误，继续
    }
    
    // 绑定地址
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port_);
    
    if (bind(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        if (errno == EADDRINUSE) {
            std::cerr << "警告: 端口 " << port_ << " 已被占用，尝试使用端口 " << (port_ + 1) << std::endl;
            close(socket_fd_);
            socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
            if (socket_fd_ < 0) {
                std::cerr << "重新创建套接字失败: " << strerror(errno) << std::endl;
                return false;
            }
            
            // 设置新的非阻塞模式
            fcntl(socket_fd_, F_SETFL, O_NONBLOCK);
            
            // 重新设置SO_REUSEADDR
            setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
            
            // 设置新端口
            port_++;
            server_addr.sin_port = htons(port_);
            
            // 重试绑定
            if (bind(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                std::cerr << "绑定备用端口失败: " << strerror(errno) << std::endl;
                close(socket_fd_);
                socket_fd_ = -1;
                return false;
            }
            
            std::cout << "成功绑定到备用端口: " << port_ << std::endl;
        } else {
            std::cerr << "绑定地址失败: " << strerror(errno) << std::endl;
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
    }
    
    // 设置推理结果回调
    yolo_engine_->setCallback(std::bind(&NetworkServer::onInferenceResult, this, 
                                        std::placeholders::_1, std::placeholders::_2));
    
    // 启动超时检查线程
    timeout_running_ = true;
    timeout_thread_ = std::thread([this]() {
        while (timeout_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            checkClientTimeouts();
        }
    });
    
    std::cout << "网络服务器初始化成功，监听端口: " << port_ << std::endl;
    return true;
}

void NetworkServer::run(const std::atomic<bool>& running) {
    if (socket_fd_ < 0) {
        std::cerr << "套接字未初始化，无法运行服务器" << std::endl;
        return;
    }
    
    // 使用本地缓冲区，避免命名空间冲突
    std::vector<uint8_t> buffer(PROTOCOL_MAX_PACKET_SIZE);
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    std::cout << "服务器开始运行..." << std::endl;
    
    while (running) {
        // 接收数据
        std::memset(&client_addr, 0, sizeof(client_addr));
        ssize_t received = recvfrom(socket_fd_, buffer.data(), buffer.size(), 0,
                                    (struct sockaddr*)&client_addr, &addr_len);
        
        if (received > 0) {
            // 处理接收到的数据
            buffer.resize(received);
            handlePacket(buffer, client_addr);
            buffer.resize(PROTOCOL_MAX_PACKET_SIZE);
        } else if (received < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "接收数据失败: " << strerror(errno) << std::endl;
                
                // 对于致命错误，可能需要重新初始化套接字
                if (errno == EBADF || errno == ECONNRESET || errno == EHOSTUNREACH) {
                    std::cerr << "检测到严重网络错误，尝试重新初始化套接字..." << std::endl;
                    close(socket_fd_);
                    if (!initialize()) {
                        std::cerr << "无法重新初始化网络，服务器将关闭" << std::endl;
                        return;
                    }
                }
            }
            // 对于EAGAIN/EWOULDBLOCK，这是正常的非阻塞行为，继续循环
        }
        
        // 短暂休眠以减少CPU使用
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    std::cout << "服务器停止运行" << std::endl;
}

void NetworkServer::shutdown() {
    // 停止超时检查线程
    if (timeout_running_) {
        timeout_running_ = false;
        if (timeout_thread_.joinable()) {
            timeout_thread_.join();
        }
    } else if (timeout_thread_.joinable()) {
        // 即使超时线程已经不在运行，仍需要join
        timeout_thread_.join();
    }
    
    // 关闭套接字
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
    
    // 清理客户端列表
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.clear();
}

void NetworkServer::handlePacket(const std::vector<uint8_t>& data, const struct sockaddr_in& client_addr) {
    // 创建数据包
    std::unique_ptr<Packet> packet = PacketFactory::createFromBuffer(data);
    if (!packet) {
        // 解析失败但不记录错误，因为可能是无效的数据包
        return;
    }
    
    // 根据数据包类型处理
    try {
        switch (packet->getType()) {
            case PacketType::HEARTBEAT: {
                auto heartbeat = dynamic_cast<HeartbeatPacket*>(packet.get());
                if (heartbeat) {
                    handleHeartbeat(*heartbeat, client_addr);
                }
                break;
            }
                
            case PacketType::CLIENT_INFO: {
                auto client_info = dynamic_cast<ClientInfoPacket*>(packet.get());
                if (client_info) {
                    handleClientInfo(*client_info, client_addr);
                }
                break;
            }
                
            case PacketType::FRAME_DATA: {
                auto frame_data = dynamic_cast<FrameDataPacket*>(packet.get());
                if (frame_data) {
                    uint32_t client_id = findClientByAddr(client_addr);
                    if (client_id > 0) {
                        handleFrameData(*frame_data, client_id);
                    }
                }
                break;
            }
                
            case PacketType::COMMAND: {
                // 命令包处理
                // 在实际实现中添加处理逻辑
                break;
            }
                
            default:
                std::cerr << "未处理的数据包类型: " << static_cast<int>(packet->getType()) << std::endl;
                break;
        }
    } catch (const std::exception& e) {
        std::cerr << "处理数据包时发生异常: " << e.what() << std::endl;
    }
}

size_t NetworkServer::getClientCount() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.size();
}

uint32_t NetworkServer::registerClient(const struct sockaddr_in& addr, const ClientInfo& info) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    // 检查是否已存在相同地址的客户端
    for (const auto& pair : clients_) {
        if (isSameAddr(pair.second.addr, addr)) {
            // 更新现有客户端信息
            clients_[pair.first].info = info;
            clients_[pair.first].last_active_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            clients_[pair.first].connected = true;
            
            std::cout << "更新客户端 #" << pair.first << " 信息，游戏ID: " 
                      << static_cast<int>(info.game_id) << std::endl;
            
            return pair.first;
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
    
    clients_[client_id] = connection;
    
    // 在游戏适配器中注册客户端
    game_adapter_->registerClient(client_id, info.game_id);
    
    std::cout << "新客户端 #" << client_id << " 连接，IP: " 
              << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port)
              << ", 游戏ID: " << static_cast<int>(info.game_id) << std::endl;
    
    return client_id;
}

void NetworkServer::removeClient(uint32_t client_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        std::cout << "客户端 #" << client_id << " 断开连接" << std::endl;
        
        // 在游戏适配器中注销客户端
        game_adapter_->unregisterClient(client_id);
        
        // 移除客户端
        clients_.erase(it);
    }
}

void NetworkServer::checkClientTimeouts() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    std::vector<uint32_t> timeout_clients;
    
    for (const auto& pair : clients_) {
        if (current_time - pair.second.last_active_time > constants::CONNECTION_TIMEOUT_MS) {
            timeout_clients.push_back(pair.first);
        }
    }
    
    // 移除超时客户端
    for (uint32_t client_id : timeout_clients) {
        std::cout << "客户端 #" << client_id << " 超时断开" << std::endl;
        
        // 在游戏适配器中注销客户端
        game_adapter_->unregisterClient(client_id);
        
        // 移除客户端
        clients_.erase(client_id);
    }
}

void NetworkServer::handleHeartbeat(const HeartbeatPacket& packet, const struct sockaddr_in& addr) {
    uint32_t client_id = findClientByAddr(addr);
    if (client_id == 0) {
        // 未知客户端发送心跳，忽略
        return;
    }
    
    // 更新客户端活动时间
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.find(client_id);
        if (it != clients_.end()) {
            it->second.last_active_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
        }
    }
    
    // 发送心跳响应
    HeartbeatPacket response;
    response.setPing(packet.getPing());
    response.setTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());
    
    sendPacket(response, addr);
}

void NetworkServer::handleClientInfo(const ClientInfoPacket& packet, const struct sockaddr_in& addr) {
    const ClientInfo& info = packet.getInfo();
    
    // 注册或更新客户端
    uint32_t client_id = registerClient(addr, info);
    
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
    
    sendPacket(response, addr);
}

void NetworkServer::handleFrameData(const FrameDataPacket& packet, uint32_t client_id) {
    const FrameData& frame_data = packet.getFrameData();
    
    // 更新客户端活动时间
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.find(client_id);
        if (it != clients_.end()) {
            it->second.last_active_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            it->second.last_frame_processed = frame_data.frame_id;
        }
    }
    
    // 验证帧数据
    if (frame_data.data.empty() || frame_data.width == 0 || frame_data.height == 0) {
        std::cerr << "收到无效的帧数据从客户端 #" << client_id << std::endl;
        return;
    }
    
    // 预期大小检查
    size_t expected_size = frame_data.width * frame_data.height * 3; // RGB格式
    if (frame_data.data.size() != expected_size) {
        std::cerr << "帧数据大小不匹配: 期望 " << expected_size 
                  << " 字节, 但收到 " << frame_data.data.size() << " 字节" << std::endl;
        return;
    }
    
    // 创建推理请求
    InferenceRequest request;
    request.client_id = client_id;
    request.frame_id = frame_data.frame_id;
    request.timestamp = frame_data.timestamp;
    request.width = frame_data.width;
    request.height = frame_data.height;
    request.data = frame_data.data;
    request.is_keyframe = frame_data.keyframe;
    
    // 提交推理请求
    if (!yolo_engine_->submitInference(request)) {
        std::cerr << "推理队列已满，丢弃帧 #" << frame_data.frame_id << std::endl;
    }
}

bool NetworkServer::sendPacket(const Packet& packet, const struct sockaddr_in& addr) {
    // 序列化数据包
    std::vector<uint8_t> data = packet.serialize();
    
    // 检查数据大小
    if (data.size() > PROTOCOL_MAX_PACKET_SIZE) {
        std::cerr << "数据包过大: " << data.size() << " 字节 (最大: " 
                 << PROTOCOL_MAX_PACKET_SIZE << " 字节)" << std::endl;
        return false;
    }
    
    // 发送数据
    ssize_t sent = sendto(socket_fd_, data.data(), data.size(), 0,
                          (struct sockaddr*)&addr, sizeof(addr));
    
    if (sent < 0) {
        std::cerr << "发送数据失败: " << strerror(errno) << std::endl;
        return false;
    }
    
    if (static_cast<size_t>(sent) != data.size()) {
        std::cerr << "发送数据不完整: 期望发送 " << data.size() 
                 << " 字节, 实际发送 " << sent << " 字节" << std::endl;
        return false;
    }
    
    return true;
}

bool NetworkServer::clientExists(uint32_t client_id) const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    return clients_.find(client_id) != clients_.end();
}

uint32_t NetworkServer::findClientByAddr(const struct sockaddr_in& addr) const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    for (const auto& pair : clients_) {
        if (isSameAddr(pair.second.addr, addr)) {
            return pair.first;
        }
    }
    
    return 0; // 未找到客户端
}

bool NetworkServer::isSameAddr(const struct sockaddr_in& addr1, const struct sockaddr_in& addr2) {
    return addr1.sin_addr.s_addr == addr2.sin_addr.s_addr && 
           addr1.sin_port == addr2.sin_port;
}

void NetworkServer::onInferenceResult(uint32_t client_id, const GameState& state) {
    // 检查客户端是否存在
    if (!clientExists(client_id)) {
        return;
    }
    
    // 获取客户端地址
    struct sockaddr_in client_addr;
    uint8_t game_id = 0;
    
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        auto it = clients_.find(client_id);
        if (it == clients_.end()) {
            return;
        }
        
        client_addr = it->second.addr;
        game_id = it->second.info.game_id;
    }
    
    // 处理游戏特定逻辑
    GameState processed_state = game_adapter_->processDetections(client_id, state, game_id);
    
    // 创建并发送检测结果包
    DetectionResultPacket packet;
    packet.setGameState(processed_state);
    packet.setTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count());
    
    sendPacket(packet, client_addr);
}

} // namespace zero_latency