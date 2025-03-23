#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <array>
#include "types.h"

namespace zero_latency {

// 避免与constants命名空间冲突，改用PROTOCOL前缀
#define PROTOCOL_MAGIC_NUMBER 0x59544C5A // "ZLTY" in hex
#define PROTOCOL_VERSION 1
#define PROTOCOL_MAX_PACKET_SIZE 65536

// 数据包头部结构
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t magic;          // 魔数
    uint8_t version;         // 协议版本
    uint8_t type;            // 包类型(PacketType)
    uint16_t length;         // 包体长度
    uint32_t sequence;       // 序列号
    uint64_t timestamp;      // 时间戳 (毫秒)
    uint16_t checksum;       // 校验和(CRC16)
};
#pragma pack(pop)

// 计算CRC16校验和
inline uint16_t calculateCRC16(const uint8_t* data, size_t size) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < size; i++) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021; // CRC-16-CCITT polynomial
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// 数据包基类
class Packet {
public:
    Packet(PacketType type) : type_(type), sequence_(0), timestamp_(0) {}
    virtual ~Packet() = default;

    // 序列化数据包
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buffer;
        buffer.reserve(PROTOCOL_MAX_PACKET_SIZE); // 使用修改后的宏
        
        // 占位头部
        buffer.resize(sizeof(PacketHeader));
        
        // 序列化包体
        serializeBody(buffer);
        
        // 填充头部
        PacketHeader header;
        header.magic = PROTOCOL_MAGIC_NUMBER;
        header.version = PROTOCOL_VERSION;
        header.type = static_cast<uint8_t>(type_);
        header.length = static_cast<uint16_t>(buffer.size() - sizeof(PacketHeader));
        header.sequence = sequence_;
        header.timestamp = timestamp_;
        header.checksum = 0; // 暂时设为0，稍后计算
        
        // 复制头部到缓冲区
        std::memcpy(buffer.data(), &header, sizeof(PacketHeader));
        
        // 计算校验和
        header.checksum = calculateCRC16(buffer.data() + sizeof(header.checksum), 
                                         buffer.size() - sizeof(header.checksum));
        
        // 将校验和写回
        std::memcpy(buffer.data() + offsetof(PacketHeader, checksum), 
                   &header.checksum, sizeof(header.checksum));
        
        return buffer;
    }
    
    // 从缓冲区反序列化
    bool deserialize(const std::vector<uint8_t>& buffer) {
        if (buffer.size() < sizeof(PacketHeader)) {
            return false;
        }
        
        // 读取头部
        const PacketHeader* header = reinterpret_cast<const PacketHeader*>(buffer.data());
        
        // 验证魔数和版本
        if (header->magic != PROTOCOL_MAGIC_NUMBER || header->version != PROTOCOL_VERSION) {
            return false;
        }
        
        // 验证长度
        if (sizeof(PacketHeader) + header->length != buffer.size()) {
            return false;
        }
        
        // 验证类型
        if (static_cast<PacketType>(header->type) != type_) {
            return false;
        }
        
        // 验证校验和
        uint16_t original_checksum = header->checksum;
        uint16_t calculated_checksum;
        
        // 临时创建一个没有校验和的拷贝
        std::vector<uint8_t> temp_buffer = buffer;
        std::memset(temp_buffer.data() + offsetof(PacketHeader, checksum), 0, sizeof(header->checksum));
        
        // 计算校验和
        calculated_checksum = calculateCRC16(
            temp_buffer.data() + sizeof(header->checksum),
            temp_buffer.size() - sizeof(header->checksum)
        );
        
        if (calculated_checksum != original_checksum) {
            return false;
        }
        
        // 保存序列号和时间戳
        sequence_ = header->sequence;
        timestamp_ = header->timestamp;
        
        // 反序列化包体
        return deserializeBody(buffer.data() + sizeof(PacketHeader), header->length);
    }
    
    // 获取包类型
    PacketType getType() const { return type_; }
    
    // 获取/设置序列号
    uint32_t getSequence() const { return sequence_; }
    void setSequence(uint32_t sequence) { sequence_ = sequence; }
    
    // 获取/设置时间戳
    uint64_t getTimestamp() const { return timestamp_; }
    void setTimestamp(uint64_t timestamp) { timestamp_ = timestamp; }

protected:
    // 子类需要实现的序列化包体方法
    virtual void serializeBody(std::vector<uint8_t>& buffer) const = 0;
    
    // 子类需要实现的反序列化包体方法
    virtual bool deserializeBody(const uint8_t* data, uint16_t length) = 0;
    
private:
    PacketType type_;
    uint32_t sequence_;
    uint64_t timestamp_;
};

// 心跳包
class HeartbeatPacket : public Packet {
public:
    HeartbeatPacket() : Packet(PacketType::HEARTBEAT), ping_(0) {}
    
    uint32_t getPing() const { return ping_; }
    void setPing(uint32_t ping) { ping_ = ping; }
    
protected:
    void serializeBody(std::vector<uint8_t>& buffer) const override {
        size_t offset = buffer.size();
        buffer.resize(offset + sizeof(ping_));
        std::memcpy(buffer.data() + offset, &ping_, sizeof(ping_));
    }
    
    bool deserializeBody(const uint8_t* data, uint16_t length) override {
        if (length != sizeof(ping_)) {
            return false;
        }
        std::memcpy(&ping_, data, sizeof(ping_));
        return true;
    }
    
private:
    uint32_t ping_; // 单向延迟(毫秒)
};

// 客户端信息包
class ClientInfoPacket : public Packet {
public:
    ClientInfoPacket() : Packet(PacketType::CLIENT_INFO) {}
    
    const ClientInfo& getInfo() const { return info_; }
    void setInfo(const ClientInfo& info) { info_ = info; }
    
protected:
    void serializeBody(std::vector<uint8_t>& buffer) const override {
        size_t offset = buffer.size();
        buffer.resize(offset + sizeof(info_));
        std::memcpy(buffer.data() + offset, &info_, sizeof(info_));
    }
    
    bool deserializeBody(const uint8_t* data, uint16_t length) override {
        if (length != sizeof(info_)) {
            return false;
        }
        std::memcpy(&info_, data, sizeof(info_));
        return true;
    }
    
private:
    ClientInfo info_;
};

// 服务器信息包
class ServerInfoPacket : public Packet {
public:
    ServerInfoPacket() : Packet(PacketType::SERVER_INFO) {}
    
    const ServerInfo& getInfo() const { return info_; }
    void setInfo(const ServerInfo& info) { info_ = info; }
    
protected:
    void serializeBody(std::vector<uint8_t>& buffer) const override {
        size_t offset = buffer.size();
        buffer.resize(offset + sizeof(info_));
        std::memcpy(buffer.data() + offset, &info_, sizeof(info_));
    }
    
    bool deserializeBody(const uint8_t* data, uint16_t length) override {
        if (length != sizeof(info_)) {
            return false;
        }
        std::memcpy(&info_, data, sizeof(info_));
        return true;
    }
    
private:
    ServerInfo info_;
};

// 帧数据包
class FrameDataPacket : public Packet {
public:
    FrameDataPacket() : Packet(PacketType::FRAME_DATA) {}
    
    const FrameData& getFrameData() const { return frame_; }
    void setFrameData(const FrameData& frame) { frame_ = frame; }
    
protected:
    void serializeBody(std::vector<uint8_t>& buffer) const override {
        size_t header_size = 12; // frame_id(4) + timestamp(8) + keyframe(1) + width(2) + height(2) = 17 bytes
        size_t offset = buffer.size();
        
        // 扩展缓冲区
        buffer.resize(offset + header_size + frame_.data.size());
        
        // 写入帧ID
        std::memcpy(buffer.data() + offset, &frame_.frame_id, sizeof(frame_.frame_id));
        offset += sizeof(frame_.frame_id);
        
        // 写入时间戳
        std::memcpy(buffer.data() + offset, &frame_.timestamp, sizeof(frame_.timestamp));
        offset += sizeof(frame_.timestamp);
        
        // 写入宽度和高度
        std::memcpy(buffer.data() + offset, &frame_.width, sizeof(frame_.width));
        offset += sizeof(frame_.width);
        
        std::memcpy(buffer.data() + offset, &frame_.height, sizeof(frame_.height));
        offset += sizeof(frame_.height);
        
        // 写入关键帧标志
        uint8_t keyframe = frame_.keyframe ? 1 : 0;
        std::memcpy(buffer.data() + offset, &keyframe, sizeof(keyframe));
        offset += sizeof(keyframe);
        
        // 写入图像数据
        std::memcpy(buffer.data() + offset, frame_.data.data(), frame_.data.size());
    }
    
    bool deserializeBody(const uint8_t* data, uint16_t length) override {
        if (length < 12) { // 最小长度检查
            return false;
        }
        
        size_t offset = 0;
        
        // 读取帧ID
        std::memcpy(&frame_.frame_id, data + offset, sizeof(frame_.frame_id));
        offset += sizeof(frame_.frame_id);
        
        // 读取时间戳
        std::memcpy(&frame_.timestamp, data + offset, sizeof(frame_.timestamp));
        offset += sizeof(frame_.timestamp);
        
        // 读取宽度和高度
        std::memcpy(&frame_.width, data + offset, sizeof(frame_.width));
        offset += sizeof(frame_.width);
        
        std::memcpy(&frame_.height, data + offset, sizeof(frame_.height));
        offset += sizeof(frame_.height);
        
        // 读取关键帧标志
        uint8_t keyframe;
        std::memcpy(&keyframe, data + offset, sizeof(keyframe));
        frame_.keyframe = (keyframe == 1);
        offset += sizeof(keyframe);
        
        // 读取图像数据
        size_t data_size = length - offset;
        frame_.data.resize(data_size);
        std::memcpy(frame_.data.data(), data + offset, data_size);
        
        return true;
    }
    
private:
    FrameData frame_;
};

// 检测结果包
class DetectionResultPacket : public Packet {
public:
    DetectionResultPacket() : Packet(PacketType::DETECTION_RESULT) {}
    
    const GameState& getGameState() const { return state_; }
    void setGameState(const GameState& state) { state_ = state; }
    
protected:
    void serializeBody(std::vector<uint8_t>& buffer) const override {
        size_t offset = buffer.size();
        size_t header_size = sizeof(state_.frame_id) + sizeof(state_.timestamp) + sizeof(uint16_t); // 检测数量
        size_t detection_size = sizeof(Detection);
        
        // 扩展缓冲区
        buffer.resize(offset + header_size + state_.detections.size() * detection_size);
        
        // 写入帧ID
        std::memcpy(buffer.data() + offset, &state_.frame_id, sizeof(state_.frame_id));
        offset += sizeof(state_.frame_id);
        
        // 写入时间戳
        std::memcpy(buffer.data() + offset, &state_.timestamp, sizeof(state_.timestamp));
        offset += sizeof(state_.timestamp);
        
        // 写入检测数量
        uint16_t count = static_cast<uint16_t>(state_.detections.size());
        std::memcpy(buffer.data() + offset, &count, sizeof(count));
        offset += sizeof(count);
        
        // 写入每个检测结果
        for (const auto& detection : state_.detections) {
            std::memcpy(buffer.data() + offset, &detection, detection_size);
            offset += detection_size;
        }
    }
    
    bool deserializeBody(const uint8_t* data, uint16_t length) override {
        if (length < sizeof(state_.frame_id) + sizeof(state_.timestamp) + sizeof(uint16_t)) {
            return false;
        }
        
        size_t offset = 0;
        
        // 读取帧ID
        std::memcpy(&state_.frame_id, data + offset, sizeof(state_.frame_id));
        offset += sizeof(state_.frame_id);
        
        // 读取时间戳
        std::memcpy(&state_.timestamp, data + offset, sizeof(state_.timestamp));
        offset += sizeof(state_.timestamp);
        
        // 读取检测数量
        uint16_t count;
        std::memcpy(&count, data + offset, sizeof(count));
        offset += sizeof(count);
        
        // 检查长度是否合理
        size_t detection_size = sizeof(Detection);
        if (offset + count * detection_size > length) {
            return false;
        }
        
        // 读取每个检测结果
        state_.detections.resize(count);
        for (uint16_t i = 0; i < count; i++) {
            std::memcpy(&state_.detections[i], data + offset, detection_size);
            offset += detection_size;
        }
        
        return true;
    }
    
private:
    GameState state_;
};

// 数据包工厂
class PacketFactory {
public:
    // 从缓冲区创建包
    static std::unique_ptr<Packet> createFromBuffer(const std::vector<uint8_t>& buffer) {
        if (buffer.size() < sizeof(PacketHeader)) {
            return nullptr;
        }
        
        const PacketHeader* header = reinterpret_cast<const PacketHeader*>(buffer.data());
        PacketType type = static_cast<PacketType>(header->type);
        
        std::unique_ptr<Packet> packet;
        switch (type) {
            case PacketType::HEARTBEAT:
                packet = std::make_unique<HeartbeatPacket>();
                break;
            case PacketType::CLIENT_INFO:
                packet = std::make_unique<ClientInfoPacket>();
                break;
            case PacketType::SERVER_INFO:
                packet = std::make_unique<ServerInfoPacket>();
                break;
            case PacketType::FRAME_DATA:
                packet = std::make_unique<FrameDataPacket>();
                break;
            case PacketType::DETECTION_RESULT:
                packet = std::make_unique<DetectionResultPacket>();
                break;
            default:
                return nullptr;
        }
        
        if (!packet->deserialize(buffer)) {
            return nullptr;
        }
        
        return packet;
    }
};

} // namespace zero_latency