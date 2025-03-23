#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <algorithm>
#include <any>
#include "logger.h"

namespace zero_latency {

// 事件类型定义
using EventType = std::string;

// 预定义的事件类型常量
namespace events {
    // 系统事件
    constexpr const char* SYSTEM_STARTUP = "SYSTEM_STARTUP";
    constexpr const char* SYSTEM_SHUTDOWN = "SYSTEM_SHUTDOWN";
    
    // 客户端事件
    constexpr const char* CLIENT_CONNECTED = "CLIENT_CONNECTED";
    constexpr const char* CLIENT_DISCONNECTED = "CLIENT_DISCONNECTED";
    constexpr const char* CLIENT_TIMEOUT = "CLIENT_TIMEOUT";
    
    // 网络事件
    constexpr const char* PACKET_RECEIVED = "PACKET_RECEIVED";
    constexpr const char* PACKET_SENT = "PACKET_SENT";
    constexpr const char* NETWORK_ERROR = "NETWORK_ERROR";
    
    // 推理事件
    constexpr const char* INFERENCE_REQUESTED = "INFERENCE_REQUESTED";
    constexpr const char* INFERENCE_COMPLETED = "INFERENCE_COMPLETED";
    constexpr const char* INFERENCE_ERROR = "INFERENCE_ERROR";
    
    // 配置事件
    constexpr const char* CONFIG_LOADED = "CONFIG_LOADED";
    constexpr const char* CONFIG_SAVED = "CONFIG_SAVED";
    constexpr const char* CONFIG_ERROR = "CONFIG_ERROR";
    
    // 游戏事件
    constexpr const char* DETECTION_PROCESSED = "DETECTION_PROCESSED";
    constexpr const char* TARGET_SELECTED = "TARGET_SELECTED";
}

// 事件基类
class Event {
public:
    explicit Event(EventType type) : type_(std::move(type)), timestamp_(std::chrono::system_clock::now()) {}
    virtual ~Event() = default;
    
    // 获取事件类型
    const EventType& getType() const { return type_; }
    
    // 获取事件时间戳
    const std::chrono::system_clock::time_point& getTimestamp() const { return timestamp_; }
    
    // 获取事件时间戳字符串
    std::string getTimestampString() const {
        auto time = std::chrono::system_clock::to_time_t(timestamp_);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp_.time_since_epoch()) % 1000;
        
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }
    
    // 设置事件源
    void setSource(const std::string& source) { source_ = source; }
    
    // 获取事件源
    const std::string& getSource() const { return source_; }
    
    // 添加自定义数据
    template<typename T>
    void setData(const std::string& key, const T& value) {
        data_[key] = value;
    }
    
    // 获取自定义数据
    template<typename T>
    T getData(const std::string& key) const {
        auto it = data_.find(key);
        if (it != data_.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast&) {
                LOG_ERROR("Failed to cast event data: " + key);
            }
        }
        return T();
    }
    
    // 检查数据是否存在
    bool hasData(const std::string& key) const {
        return data_.find(key) != data_.end();
    }

private:
    EventType type_;
    std::chrono::system_clock::time_point timestamp_;
    std::string source_;
    std::unordered_map<std::string, std::any> data_;
};

// 客户端连接事件
class ClientEvent : public Event {
public:
    ClientEvent(const std::string& type, uint32_t clientId)
        : Event(type), client_id_(clientId) {
    }
    
    uint32_t getClientId() const { return client_id_; }

private:
    uint32_t client_id_;
};

// 网络数据包事件
class PacketEvent : public Event {
public:
    PacketEvent(const std::string& type, uint32_t clientId, uint32_t packetId, uint8_t packetType)
        : Event(type), client_id_(clientId), packet_id_(packetId), packet_type_(packetType) {
    }
    
    uint32_t getClientId() const { return client_id_; }
    uint32_t getPacketId() const { return packet_id_; }
    uint8_t getPacketType() const { return packet_type_; }

private:
    uint32_t client_id_;
    uint32_t packet_id_;
    uint8_t packet_type_;
};

// 推理事件
class InferenceEvent : public Event {
public:
    InferenceEvent(const std::string& type, uint32_t clientId, uint32_t frameId)
        : Event(type), client_id_(clientId), frame_id_(frameId) {
    }
    
    uint32_t getClientId() const { return client_id_; }
    uint32_t getFrameId() const { return frame_id_; }

private:
    uint32_t client_id_;
    uint32_t frame_id_;
};

// 事件处理器类型
using EventHandler = std::function<void(const Event&)>;

// 事件总线类 (单例)
class EventBus {
public:
    static EventBus& getInstance() {
        static EventBus instance;
        return instance;
    }
    
    // 注册事件处理器
    void subscribe(const EventType& type, EventHandler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_[type].push_back(std::move(handler));
    }
    
    // 取消注册
    void unsubscribe(const EventType& type, const EventHandler& handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(type);
        if (it != handlers_.end()) {
            // 注意：这个比较不能直接用，这里只是示意
            // 实际应用中应该为handler添加标识符
            // auto& handlers = it->second;
            // handlers.erase(std::remove(handlers.begin(), handlers.end(), handler), handlers.end());
        }
    }
    
    // 发布事件
    void publish(const Event& event) {
        decltype(handlers_[event.getType()]) handlers_copy;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = handlers_.find(event.getType());
            if (it != handlers_.end()) {
                handlers_copy = it->second;
            }
        }
        
        for (const auto& handler : handlers_copy) {
            try {
                handler(event);
            } catch (const std::exception& e) {
                LOG_ERROR("Exception in event handler: " + std::string(e.what()));
            }
        }
    }
    
    // 创建并发布事件
    template<typename T, typename... Args>
    void createAndPublish(Args&&... args) {
        T event(std::forward<Args>(args)...);
        publish(event);
    }
    
    // 发布简单事件
    void publishSimple(const EventType& type) {
        Event event(type);
        publish(event);
    }
    
    // 发布客户端事件
    void publishClientEvent(const EventType& type, uint32_t clientId) {
        ClientEvent event(type, clientId);
        publish(event);
    }
    
    // 发布网络包事件
    void publishPacketEvent(const EventType& type, uint32_t clientId, uint32_t packetId, uint8_t packetType) {
        PacketEvent event(type, clientId, packetId, packetType);
        publish(event);
    }
    
    // 发布推理事件
    void publishInferenceEvent(const EventType& type, uint32_t clientId, uint32_t frameId) {
        InferenceEvent event(type, clientId, frameId);
        publish(event);
    }

private:
    EventBus() = default;
    ~EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    
    std::mutex mutex_;
    std::unordered_map<EventType, std::vector<EventHandler>> handlers_;
};

// 便捷函数
inline void subscribeEvent(const EventType& type, EventHandler handler) {
    EventBus::getInstance().subscribe(type, std::move(handler));
}

inline void publishEvent(const Event& event) {
    EventBus::getInstance().publish(event);
}

inline void publishSimpleEvent(const EventType& type) {
    EventBus::getInstance().publishSimple(type);
}

} // namespace zero_latency