#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <array>
#include <variant>
#include <optional>
#include <stdexcept>
#include <chrono>
#include <functional>
#include "types.h"
#include "result.h"
#include "logger.h"
#include "memory_pool.h"

namespace zero_latency {

// 协议常量定义
constexpr uint32_t PROTOCOL_MAGIC_NUMBER = 0x59544C5A; // "ZLTY" in hex
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint32_t PROTOCOL_MAX_PACKET_SIZE = 65536;
constexpr uint32_t PROTOCOL_MIN_PACKET_SIZE = sizeof(uint32_t) * 4; // 至少16字节
constexpr uint16_t PROTOCOL_HEADER_SIZE = 16;

// 序列号生成器
class SequenceGenerator {
public:
    static uint32_t next() {
        static std::atomic<uint32_t> sequence{1};
        return sequence.fetch_add(1, std::memory_order_relaxed);
    }
};

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

    // 构造函数
    PacketHeader()
        : magic(PROTOCOL_MAGIC_NUMBER),
          version(PROTOCOL_VERSION),
          type(0),
          length(0),
          sequence(0),
          timestamp(0),
          checksum(0) {}

    // 带参数的构造函数
    PacketHeader(PacketType type, uint32_t seq = 0)
        : magic(PROTOCOL_MAGIC_NUMBER),
          version(PROTOCOL_VERSION),
          type(static_cast<uint8_t>(type)),
          length(0),
          sequence(seq),
          timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()).count()),
          checksum(0) {}

    // 验证头部有效性
    bool isValid() const {
        return magic == PROTOCOL_MAGIC_NUMBER && version == PROTOCOL_VERSION;
    }
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

// 数据包基类 (接口)
class IPacket {
public:
    virtual ~IPacket() = default;
    
    // 获取包类型
    virtual PacketType getType() const = 0;
    
    // 获取序列号
    virtual uint32_t getSequence() const = 0;
    
    // 设置序列号
    virtual void setSequence(uint32_t sequence) = 0;
    
    // 获取时间戳
    virtual uint64_t getTimestamp() const = 0;
    
    // 设置时间戳
    virtual void setTimestamp(uint64_t timestamp) = 0;
    
    // 序列化包
    virtual std::vector<uint8_t> serialize() const = 0;
    
    // 从缓冲区反序列化
    virtual Result<void> deserialize(const std::vector<uint8_t>& buffer) = 0;
    
    // 从缓冲区反序列化
    virtual Result<void> deserialize(const uint8_t* data, size_t size) = 0;
    
    // 验证包
    virtual Result<void> validate() const = 0;
};

// 数据包基类 (实现)
class Packet : public IPacket {
public:
    Packet(PacketType type, uint32_t sequence = 0)
        : type_(type),
          sequence_(sequence == 0 ? SequenceGenerator::next() : sequence),
          timestamp_(std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()).count()) {}
    
    virtual ~Packet() = default;
    
    // 获取包类型
    PacketType getType() const override { return type_; }
    
    // 获取序列号
    uint32_t getSequence() const override { return sequence_; }
    
    // 设置序列号
    void setSequence(uint32_t sequence) override { sequence_ = sequence; }
    
    // 获取时间戳
    uint64_t getTimestamp() const override { return timestamp_; }
    
    // 设置时间戳
    void setTimestamp(uint64_t timestamp) override { timestamp_ = timestamp; }
    
    // 序列化数据包
    std::vector<uint8_t> serialize() const override {
        // 使用线程局部缓冲池
        auto& buffer_pool = getThreadLocalBufferPool<uint8_t>();
        auto& buffer = buffer_pool.getBuffer();
        buffer.reset();
        
        // 预分配足够空间
        buffer.reserve(PROTOCOL_MAX_PACKET_SIZE);
        
        // 初始化头部
        PacketHeader header;
        header.magic = PROTOCOL_MAGIC_NUMBER;
        header.version = PROTOCOL_VERSION;
        header.type = static_cast<uint8_t>(type_);
        header.sequence = sequence_;
        header.timestamp = timestamp_;
        header.checksum = 0; // 暂时设为0，后面计算
        
        // 添加头部到缓冲区
        const uint8_t* header_ptr = reinterpret_cast<const uint8_t*>(&header);
        buffer.resize(sizeof(PacketHeader));
        std::memcpy(buffer.data(), header_ptr, sizeof(PacketHeader));
        
        // 序列化包体
        serializeBody(buffer);
        
        // 更新包长度
        header.length = static_cast<uint16_t>(buffer.size() - sizeof(PacketHeader));
        std::memcpy(buffer.data() + offsetof(PacketHeader, length), &header.length, sizeof(header.length));
        
        // 计算校验和
        header.checksum = calculateCRC16(
            buffer.data() + sizeof(header.checksum),
            buffer.size() - sizeof(header.checksum)
        );
        
        // 将校验和写回
        std::memcpy(
            buffer.data() + offsetof(PacketHeader, checksum),
            &header.checksum,
            sizeof(header.checksum)
        );
        
        return buffer.getBuffer();
    }
    
    // 从缓冲区反序列化
    Result<void> deserialize(const std::vector<uint8_t>& buffer) override {
        return deserialize(buffer.data(), buffer.size());
    }
    
    // 从缓冲区反序列化
    Result<void> deserialize(const uint8_t* data, size_t size) override {
        if (size < sizeof(PacketHeader)) {
            return Result<void>::error(ErrorCode::INVALID_PACKET, "Packet too small");
        }
        
        // 读取头部
        const PacketHeader* header = reinterpret_cast<const PacketHeader*>(data);
        
        // 验证魔数和版本
        if (header->magic != PROTOCOL_MAGIC_NUMBER || header->version != PROTOCOL_VERSION) {
            return Result<void>::error(ErrorCode::PROTOCOL_ERROR, "Invalid packet magic or version");
        }
        
        // 验证长度
        if (sizeof(PacketHeader) + header->length != size) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid packet length: expected " + std::to_string(sizeof(PacketHeader) + header->length) +
                ", got " + std::to_string(size)
            );
        }
        
        // 验证类型
        if (static_cast<PacketType>(header->type) != type_) {
            return Result<void>::error(
                ErrorCode::PROTOCOL_ERROR,
                "Invalid packet type: expected " + std::to_string(static_cast<int>(type_)) +
                ", got " + std::to_string(header->type)
            );
        }
        
        // 验证校验和
        uint16_t original_checksum = header->checksum;
        
        // 临时创建一个没有校验和的拷贝
        std::vector<uint8_t> temp_buffer(data, data + size);
        *reinterpret_cast<uint16_t*>(temp_buffer.data() + offsetof(PacketHeader, checksum)) = 0;
        
        // 计算校验和
        uint16_t calculated_checksum = calculateCRC16(
            temp_buffer.data() + sizeof(header->checksum),
            temp_buffer.size() - sizeof(header->checksum)
        );
        
        if (calculated_checksum != original_checksum) {
            return Result<void>::error(
                ErrorCode::PROTOCOL_ERROR,
                "Invalid packet checksum: expected " + std::to_string(original_checksum) +
                ", calculated " + std::to_string(calculated_checksum)
            );
        }
        
        // 保存序列号和时间戳
        sequence_ = header->sequence;
        timestamp_ = header->timestamp;
        
        // 反序列化包体
        return deserializeBody(data + sizeof(PacketHeader), header->length);
    }
    
    // 验证包
    Result<void> validate() const override {
        // 基类中的验证逻辑较为简单
        if (sequence_ == 0) {
            return Result<void>::error(ErrorCode::INVALID_PACKET, "Invalid sequence number");
        }
        
        if (timestamp_ == 0) {
            return Result<void>::error(ErrorCode::INVALID_PACKET, "Invalid timestamp");
        }
        
        return Result<void>::ok();
    }

protected:
    // 子类需要实现的序列化包体方法
    virtual void serializeBody(std::vector<uint8_t>& buffer) const = 0;
    
    // 子类需要实现的反序列化包体方法
    virtual Result<void> deserializeBody(const uint8_t* data, uint16_t length) = 0;
    
private:
    PacketType type_;
    uint32_t sequence_;
    uint64_t timestamp_;
};

// 心跳包
class HeartbeatPacket : public Packet {
public:
    HeartbeatPacket() : Packet(PacketType::HEARTBEAT), ping_(0) {}
    
    explicit HeartbeatPacket(uint32_t ping)
        : Packet(PacketType::HEARTBEAT), ping_(ping) {}
    
    uint32_t getPing() const { return ping_; }
    void setPing(uint32_t ping) { ping_ = ping; }
    
protected:
    void serializeBody(std::vector<uint8_t>& buffer) const override {
        size_t offset = buffer.size();
        buffer.resize(offset + sizeof(ping_));
        std::memcpy(buffer.data() + offset, &ping_, sizeof(ping_));
    }
    
    Result<void> deserializeBody(const uint8_t* data, uint16_t length) override {
        if (length != sizeof(ping_)) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid heartbeat packet body length: expected " + std::to_string(sizeof(ping_)) +
                ", got " + std::to_string(length)
            );
        }
        
        std::memcpy(&ping_, data, sizeof(ping_));
        return Result<void>::ok();
    }
    
private:
    uint32_t ping_; // 单向延迟(毫秒)
};

// 客户端信息包
class ClientInfoPacket : public Packet {
public:
    ClientInfoPacket() : Packet(PacketType::CLIENT_INFO) {}
    
    explicit ClientInfoPacket(const ClientInfo& info)
        : Packet(PacketType::CLIENT_INFO), info_(info) {}
    
    const ClientInfo& getInfo() const { return info_; }
    void setInfo(const ClientInfo& info) { info_ = info; }
    
protected:
    void serializeBody(std::vector<uint8_t>& buffer) const override {
        size_t offset = buffer.size();
        buffer.resize(offset + sizeof(info_));
        std::memcpy(buffer.data() + offset, &info_, sizeof(info_));
    }
    
    Result<void> deserializeBody(const uint8_t* data, uint16_t length) override {
        if (length != sizeof(info_)) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid client info packet body length: expected " + std::to_string(sizeof(info_)) +
                ", got " + std::to_string(length)
            );
        }
        
        std::memcpy(&info_, data, sizeof(info_));
        return Result<void>::ok();
    }
    
private:
    ClientInfo info_;
};

// 服务器信息包
class ServerInfoPacket : public Packet {
public:
    ServerInfoPacket() : Packet(PacketType::SERVER_INFO) {}
    
    explicit ServerInfoPacket(const ServerInfo& info)
        : Packet(PacketType::SERVER_INFO), info_(info) {}
    
    const ServerInfo& getInfo() const { return info_; }
    void setInfo(const ServerInfo& info) { info_ = info; }
    
protected:
    void serializeBody(std::vector<uint8_t>& buffer) const override {
        size_t offset = buffer.size();
        buffer.resize(offset + sizeof(info_));
        std::memcpy(buffer.data() + offset, &info_, sizeof(info_));
    }
    
    Result<void> deserializeBody(const uint8_t* data, uint16_t length) override {
        if (length != sizeof(info_)) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid server info packet body length: expected " + std::to_string(sizeof(info_)) +
                ", got " + std::to_string(length)
            );
        }
        
        std::memcpy(&info_, data, sizeof(info_));
        return Result<void>::ok();
    }
    
private:
    ServerInfo info_;
};

// 帧数据包
class FrameDataPacket : public Packet {
public:
    FrameDataPacket() : Packet(PacketType::FRAME_DATA) {}
    
    explicit FrameDataPacket(const FrameData& frame)
        : Packet(PacketType::FRAME_DATA), frame_(frame) {}
    
    const FrameData& getFrameData() const { return frame_; }
    void setFrameData(const FrameData& frame) { frame_ = frame; }
    
protected:
    void serializeBody(std::vector<uint8_t>& buffer) const override {
        size_t header_size = sizeof(frame_.frame_id) + sizeof(frame_.timestamp) + 
                            sizeof(frame_.width) + sizeof(frame_.height) + sizeof(uint8_t);
        size_t offset = buffer.size();
        
        // 扩展缓冲区
        buffer.resize(offset + header_size + frame_.data.size());
        
        // 写入帧ID
        std::memcpy(buffer.data() + offset, &frame_.frame_id, sizeof(frame_.frame_id));
        offset += sizeof(frame_.frame_id);
        
        // 写入时间戳
        std::memcpy(buffer.data() + offset, &frame_.timestamp, sizeof(frame_.timestamp));
        offset += sizeof(frame_.timestamp);
        
        // 写入宽度
        std::memcpy(buffer.data() + offset, &frame_.width, sizeof(frame_.width));
        offset += sizeof(frame_.width);
        
        // 写入高度
        std::memcpy(buffer.data() + offset, &frame_.height, sizeof(frame_.height));
        offset += sizeof(frame_.height);
        
        // 写入关键帧标志
        uint8_t keyframe = frame_.keyframe ? 1 : 0;
        std::memcpy(buffer.data() + offset, &keyframe, sizeof(keyframe));
        offset += sizeof(keyframe);
        
        // 写入图像数据
        if (!frame_.data.empty()) {
            std::memcpy(buffer.data() + offset, frame_.data.data(), frame_.data.size());
        }
    }
    
    Result<void> deserializeBody(const uint8_t* data, uint16_t length) override {
        const size_t header_size = sizeof(frame_.frame_id) + sizeof(frame_.timestamp) + 
                                  sizeof(frame_.width) + sizeof(frame_.height) + sizeof(uint8_t);
        
        if (length < header_size) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid frame data packet body length: expected at least " + std::to_string(header_size) +
                ", got " + std::to_string(length)
            );
        }
        
        size_t offset = 0;
        
        // 读取帧ID
        std::memcpy(&frame_.frame_id, data + offset, sizeof(frame_.frame_id));
        offset += sizeof(frame_.frame_id);
        
        // 读取时间戳
        std::memcpy(&frame_.timestamp, data + offset, sizeof(frame_.timestamp));
        offset += sizeof(frame_.timestamp);
        
        // 读取宽度
        std::memcpy(&frame_.width, data + offset, sizeof(frame_.width));
        offset += sizeof(frame_.width);
        
        // 读取高度
        std::memcpy(&frame_.height, data + offset, sizeof(frame_.height));
        offset += sizeof(frame_.height);
        
        // 读取关键帧标志
        uint8_t keyframe;
        std::memcpy(&keyframe, data + offset, sizeof(keyframe));
        frame_.keyframe = (keyframe == 1);
        offset += sizeof(keyframe);
        
        // 验证宽度和高度
        if (frame_.width == 0 || frame_.height == 0) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid frame dimensions: " + std::to_string(frame_.width) + "x" + std::to_string(frame_.height)
            );
        }
        
        // 计算预期数据大小
        size_t expected_data_size = frame_.width * frame_.height * 3; // RGB格式，每像素3字节
        size_t actual_data_size = length - header_size;
        
        // 非严格检查，允许压缩数据
        if (actual_data_size > 0) {
            // 读取图像数据
            frame_.data.resize(actual_data_size);
            std::memcpy(frame_.data.data(), data + offset, actual_data_size);
        } else {
            frame_.data.clear();
        }
        
        return Result<void>::ok();
    }
    
    // 重写验证方法，添加额外的帧数据包特定验证
    Result<void> validate() const override {
        // 先调用基类验证
        auto result = Packet::validate();
        if (result.hasError()) {
            return result;
        }
        
        // 帧数据包特定验证
        if (frame_.frame_id == 0) {
            return Result<void>::error(ErrorCode::INVALID_PACKET, "Invalid frame ID");
        }
        
        if (frame_.width == 0 || frame_.height == 0) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid frame dimensions: " + std::to_string(frame_.width) + "x" + std::to_string(frame_.height)
            );
        }
        
        return Result<void>::ok();
    }
    
private:
    FrameData frame_;
};

// 检测结果包
class DetectionResultPacket : public Packet {
public:
    DetectionResultPacket() : Packet(PacketType::DETECTION_RESULT) {}
    
    explicit DetectionResultPacket(const GameState& state)
        : Packet(PacketType::DETECTION_RESULT), state_(state) {}
    
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
    
    Result<void> deserializeBody(const uint8_t* data, uint16_t length) override {
        const size_t min_size = sizeof(state_.frame_id) + sizeof(state_.timestamp) + sizeof(uint16_t);
        
        if (length < min_size) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid detection result packet body length: expected at least " + std::to_string(min_size) +
                ", got " + std::to_string(length)
            );
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
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid detection count: expected space for " + std::to_string(count) + 
                " detections, but only have " + std::to_string((length - offset) / detection_size)
            );
        }
        
        // 读取每个检测结果
        state_.detections.resize(count);
        for (uint16_t i = 0; i < count; i++) {
            std::memcpy(&state_.detections[i], data + offset, detection_size);
            offset += detection_size;
        }
        
        return Result<void>::ok();
    }
    
private:
    GameState state_;
};

// 命令包
class CommandPacket : public Packet {
public:
    CommandPacket() : Packet(PacketType::COMMAND), command_type_(CommandType::NONE) {}
    
    explicit CommandPacket(CommandType command_type)
        : Packet(PacketType::COMMAND), command_type_(command_type) {}
    
    CommandType getCommandType() const { return command_type_; }
    void setCommandType(CommandType command_type) { command_type_ = command_type; }
    
    const std::vector<uint8_t>& getCommandData() const { return command_data_; }
    void setCommandData(const std::vector<uint8_t>& data) { command_data_ = data; }
    
protected:
    void serializeBody(std::vector<uint8_t>& buffer) const override {
        size_t offset = buffer.size();
        size_t header_size = sizeof(command_type_) + sizeof(uint16_t); // 命令数据长度
        
        // 扩展缓冲区
        buffer.resize(offset + header_size + command_data_.size());
        
        // 写入命令类型
        std::memcpy(buffer.data() + offset, &command_type_, sizeof(command_type_));
        offset += sizeof(command_type_);
        
        // 写入命令数据长度
        uint16_t data_length = static_cast<uint16_t>(command_data_.size());
        std::memcpy(buffer.data() + offset, &data_length, sizeof(data_length));
        offset += sizeof(data_length);
        
        // 写入命令数据
        if (!command_data_.empty()) {
            std::memcpy(buffer.data() + offset, command_data_.data(), command_data_.size());
        }
    }
    
    Result<void> deserializeBody(const uint8_t* data, uint16_t length) override {
        const size_t min_size = sizeof(command_type_) + sizeof(uint16_t);
        
        if (length < min_size) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid command packet body length: expected at least " + std::to_string(min_size) +
                ", got " + std::to_string(length)
            );
        }
        
        size_t offset = 0;
        
        // 读取命令类型
        std::memcpy(&command_type_, data + offset, sizeof(command_type_));
        offset += sizeof(command_type_);
        
        // 读取命令数据长度
        uint16_t data_length;
        std::memcpy(&data_length, data + offset, sizeof(data_length));
        offset += sizeof(data_length);
        
        // 检查长度是否合理
        if (offset + data_length > length) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid command data length: expected " + std::to_string(data_length) +
                " bytes, but only have " + std::to_string(length - offset)
            );
        }
        
        // 读取命令数据
        command_data_.resize(data_length);
        if (data_length > 0) {
            std::memcpy(command_data_.data(), data + offset, data_length);
        }
        
        return Result<void>::ok();
    }
    
private:
    CommandType command_type_;
    std::vector<uint8_t> command_data_;
};

// 错误包
class ErrorPacket : public Packet {
public:
    ErrorPacket() : Packet(PacketType::ERROR), error_code_(ErrorCode::NONE) {}
    
    explicit ErrorPacket(ErrorCode error_code, const std::string& error_message = "")
        : Packet(PacketType::ERROR), error_code_(error_code), error_message_(error_message) {}
    
    ErrorCode getErrorCode() const { return error_code_; }
    void setErrorCode(ErrorCode error_code) { error_code_ = error_code; }
    
    const std::string& getErrorMessage() const { return error_message_; }
    void setErrorMessage(const std::string& message) { error_message_ = message; }
    
protected:
    void serializeBody(std::vector<uint8_t>& buffer) const override {
        size_t offset = buffer.size();
        size_t header_size = sizeof(error_code_) + sizeof(uint16_t); // 错误消息长度
        
        // 扩展缓冲区
        buffer.resize(offset + header_size + error_message_.size());
        
        // 写入错误代码
        std::memcpy(buffer.data() + offset, &error_code_, sizeof(error_code_));
        offset += sizeof(error_code_);
        
        // 写入错误消息长度
        uint16_t message_length = static_cast<uint16_t>(error_message_.size());
        std::memcpy(buffer.data() + offset, &message_length, sizeof(message_length));
        offset += sizeof(message_length);
        
        // 写入错误消息
        if (!error_message_.empty()) {
            std::memcpy(buffer.data() + offset, error_message_.data(), error_message_.size());
        }
    }
    
    Result<void> deserializeBody(const uint8_t* data, uint16_t length) override {
        const size_t min_size = sizeof(error_code_) + sizeof(uint16_t);
        
        if (length < min_size) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid error packet body length: expected at least " + std::to_string(min_size) +
                ", got " + std::to_string(length)
            );
        }
        
        size_t offset = 0;
        
        // 读取错误代码
        std::memcpy(&error_code_, data + offset, sizeof(error_code_));
        offset += sizeof(error_code_);
        
        // 读取错误消息长度
        uint16_t message_length;
        std::memcpy(&message_length, data + offset, sizeof(message_length));
        offset += sizeof(message_length);
        
        // 检查长度是否合理
        if (offset + message_length > length) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid error message length: expected " + std::to_string(message_length) +
                " bytes, but only have " + std::to_string(length - offset)
            );
        }
        
        // 读取错误消息
        error_message_.resize(message_length);
        if (message_length > 0) {
            std::memcpy(error_message_.data(), data + offset, message_length);
        }
        
        return Result<void>::ok();
    }
    
private:
    ErrorCode error_code_;
    std::string error_message_;
};

// 确认包
class AckPacket : public Packet {
public:
    AckPacket() : Packet(PacketType::ACK), acked_sequence_(0) {}
    
    explicit AckPacket(uint32_t acked_sequence)
        : Packet(PacketType::ACK), acked_sequence_(acked_sequence) {}
    
    uint32_t getAckedSequence() const { return acked_sequence_; }
    void setAckedSequence(uint32_t sequence) { acked_sequence_ = sequence; }
    
protected:
    void serializeBody(std::vector<uint8_t>& buffer) const override {
        size_t offset = buffer.size();
        buffer.resize(offset + sizeof(acked_sequence_));
        std::memcpy(buffer.data() + offset, &acked_sequence_, sizeof(acked_sequence_));
    }
    
    Result<void> deserializeBody(const uint8_t* data, uint16_t length) override {
        if (length != sizeof(acked_sequence_)) {
            return Result<void>::error(
                ErrorCode::INVALID_PACKET,
                "Invalid ACK packet body length: expected " + std::to_string(sizeof(acked_sequence_)) +
                ", got " + std::to_string(length)
            );
        }
        
        std::memcpy(&acked_sequence_, data, sizeof(acked_sequence_));
        return Result<void>::ok();
    }
    
private:
    uint32_t acked_sequence_; // 被确认的包序列号
};

// 数据包工厂
class PacketFactory {
public:
    // 从缓冲区创建包
    static Result<std::unique_ptr<IPacket>> createFromBuffer(const std::vector<uint8_t>& buffer) {
        return createFromBuffer(buffer.data(), buffer.size());
    }
    
    // 从缓冲区创建包
    static Result<std::unique_ptr<IPacket>> createFromBuffer(const uint8_t* data, size_t size) {
        if (size < sizeof(PacketHeader)) {
            return Result<std::unique_ptr<IPacket>>::error(
                ErrorCode::INVALID_PACKET,
                "Buffer too small for packet header"
            );
        }
        
        const PacketHeader* header = reinterpret_cast<const PacketHeader*>(data);
        
        // 验证魔数和版本
        if (header->magic != PROTOCOL_MAGIC_NUMBER || header->version != PROTOCOL_VERSION) {
            return Result<std::unique_ptr<IPacket>>::error(
                ErrorCode::PROTOCOL_ERROR,
                "Invalid packet magic or version"
            );
        }
        
        PacketType type = static_cast<PacketType>(header->type);
        std::unique_ptr<IPacket> packet;
        
        // 根据类型创建相应的包
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
                
            case PacketType::COMMAND:
                packet = std::make_unique<CommandPacket>();
                break;
                
            case PacketType::ERROR:
                packet = std::make_unique<ErrorPacket>();
                break;
                
            case PacketType::ACK:
                packet = std::make_unique<AckPacket>();
                break;
                
            default:
                return Result<std::unique_ptr<IPacket>>::error(
                    ErrorCode::PROTOCOL_ERROR,
                    "Unknown packet type: " + std::to_string(header->type)
                );
        }
        
        // 反序列化包数据
        auto result = packet->deserialize(data, size);
        if (result.hasError()) {
            return Result<std::unique_ptr<IPacket>>::error(result.error());
        }
        
        return Result<std::unique_ptr<IPacket>>::ok(std::move(packet));
    }
    
    // 创建心跳包
    static std::unique_ptr<HeartbeatPacket> createHeartbeat(uint32_t ping = 0) {
        return std::make_unique<HeartbeatPacket>(ping);
    }
    
    // 创建客户端信息包
    static std::unique_ptr<ClientInfoPacket> createClientInfo(const ClientInfo& info) {
        return std::make_unique<ClientInfoPacket>(info);
    }
    
    // 创建服务器信息包
    static std::unique_ptr<ServerInfoPacket> createServerInfo(const ServerInfo& info) {
        return std::make_unique<ServerInfoPacket>(info);
    }
    
    // 创建帧数据包
    static std::unique_ptr<FrameDataPacket> createFrameData(const FrameData& frame) {
        return std::make_unique<FrameDataPacket>(frame);
    }
    
    // 创建检测结果包
    static std::unique_ptr<DetectionResultPacket> createDetectionResult(const GameState& state) {
        return std::make_unique<DetectionResultPacket>(state);
    }
    
    // 创建命令包
    static std::unique_ptr<CommandPacket> createCommand(CommandType command_type) {
        return std::make_unique<CommandPacket>(command_type);
    }
    
    // 创建错误包
    static std::unique_ptr<ErrorPacket> createError(ErrorCode error_code, const std::string& message = "") {
        return std::make_unique<ErrorPacket>(error_code, message);
    }
    
    // 创建确认包
    static std::unique_ptr<AckPacket> createAck(uint32_t acked_sequence) {
        return std::make_unique<AckPacket>(acked_sequence);
    }
};

} // namespace zero_latency