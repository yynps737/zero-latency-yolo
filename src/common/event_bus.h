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

using EventType = std::string;

namespace events {
    constexpr const char* SYSTEM_STARTUP = "SYSTEM_STARTUP";
    constexpr const char* SYSTEM_SHUTDOWN = "SYSTEM_SHUTDOWN";
    constexpr const char* CLIENT_CONNECTED = "CLIENT_CONNECTED";
    constexpr const char* CLIENT_DISCONNECTED = "CLIENT_DISCONNECTED";
    constexpr const char* CLIENT_TIMEOUT = "CLIENT_TIMEOUT";
    constexpr const char* PACKET_RECEIVED = "PACKET_RECEIVED";
    constexpr const char* PACKET_SENT = "PACKET_SENT";
    constexpr const char* NETWORK_ERROR = "NETWORK_ERROR";
    constexpr const char* INFERENCE_REQUESTED = "INFERENCE_REQUESTED";
    constexpr const char* INFERENCE_COMPLETED = "INFERENCE_COMPLETED";
    constexpr const char* INFERENCE_ERROR = "INFERENCE_ERROR";
    constexpr const char* CONFIG_LOADED = "CONFIG_LOADED";
    constexpr const char* CONFIG_SAVED = "CONFIG_SAVED";
    constexpr const char* CONFIG_ERROR = "CONFIG_ERROR";
    constexpr const char* DETECTION_PROCESSED = "DETECTION_PROCESSED";
    constexpr const char* TARGET_SELECTED = "TARGET_SELECTED";
}

class Event {
public:
    explicit Event(EventType type) : type_(std::move(type)), timestamp_(std::chrono::system_clock::now()) {}
    virtual ~Event() = default;
    
    const EventType& getType() const { return type_; }
    const std::chrono::system_clock::time_point& getTimestamp() const { return timestamp_; }
    
    std::string getTimestampString() const {
        auto time = std::chrono::system_clock::to_time_t(timestamp_);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timestamp_.time_since_epoch()) % 1000;
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << '.' 
            << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }
    
    void setSource(const std::string& source) { source_ = source; }
    const std::string& getSource() const { return source_; }
    
    template<typename T>
    void setData(const std::string& key, const T& value) { data_[key] = value; }
    
    template<typename T>
    T getData(const std::string& key) const {
        auto it = data_.find(key);
        if (it != data_.end()) {
            try { return std::any_cast<T>(it->second); }
            catch (const std::bad_any_cast&) { LOG_ERROR("Failed to cast event data: " + key); }
        }
        return T();
    }
    
    bool hasData(const std::string& key) const { return data_.find(key) != data_.end(); }

private:
    EventType type_;
    std::chrono::system_clock::time_point timestamp_;
    std::string source_;
    std::unordered_map<std::string, std::any> data_;
};

class ClientEvent : public Event {
public:
    ClientEvent(const std::string& type, uint32_t clientId) : Event(type), client_id_(clientId) {}
    uint32_t getClientId() const { return client_id_; }
private:
    uint32_t client_id_;
};

class PacketEvent : public Event {
public:
    PacketEvent(const std::string& type, uint32_t clientId, uint32_t packetId, uint8_t packetType)
        : Event(type), client_id_(clientId), packet_id_(packetId), packet_type_(packetType) {}
    
    uint32_t getClientId() const { return client_id_; }
    uint32_t getPacketId() const { return packet_id_; }
    uint8_t getPacketType() const { return packet_type_; }
private:
    uint32_t client_id_, packet_id_;
    uint8_t packet_type_;
};

class InferenceEvent : public Event {
public:
    InferenceEvent(const std::string& type, uint32_t clientId, uint32_t frameId)
        : Event(type), client_id_(clientId), frame_id_(frameId) {}
    
    uint32_t getClientId() const { return client_id_; }
    uint32_t getFrameId() const { return frame_id_; }
private:
    uint32_t client_id_, frame_id_;
};

using EventHandler = std::function<void(const Event&)>;

class EventBus {
public:
    static EventBus& getInstance() {
        static EventBus instance;
        return instance;
    }
    
    void subscribe(const EventType& type, EventHandler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_[type].push_back(std::move(handler));
    }
    
    void unsubscribe(const EventType& type, const EventHandler& handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(type);
        if (it != handlers_.end()) {}
    }
    
    void publish(const Event& event) {
        decltype(handlers_[event.getType()]) handlers_copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = handlers_.find(event.getType());
            if (it != handlers_.end()) handlers_copy = it->second;
        }
        
        for (const auto& handler : handlers_copy) {
            try { handler(event); }
            catch (const std::exception& e) { LOG_ERROR("Exception in event handler: " + std::string(e.what())); }
        }
    }
    
    template<typename T, typename... Args>
    void createAndPublish(Args&&... args) {
        T event(std::forward<Args>(args)...);
        publish(event);
    }
    
    void publishSimple(const EventType& type) {
        Event event(type);
        publish(event);
    }
    
    void publishClientEvent(const EventType& type, uint32_t clientId) {
        ClientEvent event(type, clientId);
        publish(event);
    }
    
    void publishPacketEvent(const EventType& type, uint32_t clientId, uint32_t packetId, uint8_t packetType) {
        PacketEvent event(type, clientId, packetId, packetType);
        publish(event);
    }
    
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

inline void subscribeEvent(const EventType& type, EventHandler handler) {
    EventBus::getInstance().subscribe(type, std::move(handler));
}

inline void publishEvent(const Event& event) {
    EventBus::getInstance().publish(event);
}

inline void publishSimpleEvent(const EventType& type) {
    EventBus::getInstance().publishSimple(type);
}

}