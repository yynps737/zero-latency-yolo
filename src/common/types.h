#pragma once

#include <cstdint>
#include <vector>
#include <chrono>

namespace zero_latency {

#define MAX_OBJECTS 32
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define PROTOCOL_VERSION 1

struct BoundingBox {
    float x;      // 中心 x 坐标 (归一化坐标0-1)
    float y;      // 中心 y 坐标 (归一化坐标0-1)
    float width;  // 宽度 (归一化坐标0-1)
    float height; // 高度 (归一化坐标0-1)
};

struct Detection {
    BoundingBox box;
    float confidence;
    int class_id;
    uint32_t track_id;
    uint64_t timestamp;
};

struct FrameData {
    uint32_t frame_id;
    uint64_t timestamp;
    uint16_t width;
    uint16_t height;
    std::vector<uint8_t> data;
    bool keyframe;
};

struct GameState {
    uint32_t frame_id;
    uint64_t timestamp;
    std::vector<Detection> detections;
};

struct ClientInfo {
    uint32_t client_id;
    uint32_t protocol_version;
    uint16_t screen_width;
    uint16_t screen_height;
    uint8_t game_id;  // 1 = CS 1.6, 2 = CSGO, etc.
};

struct ServerInfo {
    uint32_t server_id;
    uint32_t protocol_version;
    float model_version;
    uint8_t max_clients;
    uint16_t max_fps;
    uint8_t status;  // 0 = OK, 1 = Busy, 2 = Error
};

enum class PacketType : uint8_t {
    HEARTBEAT = 0,
    CLIENT_INFO = 1,
    SERVER_INFO = 2,
    FRAME_DATA = 3,
    DETECTION_RESULT = 4,
    ERROR = 5,
    COMMAND = 6,
    CONFIG_UPDATE = 7
};

enum class ErrorCode : uint8_t {
    NONE = 0,
    INVALID_PROTOCOL = 1,
    SERVER_FULL = 2,
    AUTHENTICATION_FAILED = 3,
    TIMEOUT = 4,
    INVALID_REQUEST = 5,
    SERVER_ERROR = 6
};

enum class CommandType : uint8_t {
    NONE = 0,
    START_STREAM = 1,
    STOP_STREAM = 2,
    PAUSE_STREAM = 3,
    RESUME_STREAM = 4,
    REQUEST_KEYFRAME = 5,
    SET_CONFIG = 6,
    GET_CONFIG = 7,
    PING = 8,
    DISCONNECT = 9
};

enum class GameType : uint8_t {
    UNKNOWN = 0,
    CS_1_6 = 1,
    CSGO = 2,
    VALORANT = 3,
    APEX = 4,
    PUBG = 5,
    FORTNITE = 6
};

enum class DetectionClass : uint8_t {
    UNKNOWN = 0,
    PLAYER_T = 1,    // CS:玩家(T方)
    PLAYER_CT = 2,   // CS:玩家(CT方)
    HEAD = 3,        // 头部
    BODY = 4,        // 身体
    WEAPON = 5,      // 武器
    GRENADE = 6,     // 手雷
    C4 = 7,          // C4炸弹
    HOSTAGE = 8      // 人质
};

struct Point2D {
    float x;
    float y;
};

struct Vector2D {
    float x;
    float y;
};

struct Vector3D {
    float x;
    float y;
    float z;
};

// 预测参数
struct PredictionParams {
    float max_prediction_time;     // 最大预测时间(毫秒)
    float position_uncertainty;    // 位置不确定性系数
    float velocity_uncertainty;    // 速度不确定性系数
    float acceleration_uncertainty; // 加速度不确定性系数
    float min_confidence_threshold; // 最低置信度阈值
};

// 压缩设置
struct CompressionSettings {
    uint8_t quality;               // JPEG质量(1-100)
    uint8_t keyframe_interval;     // 关键帧间隔(帧数)
    bool use_difference_encoding;  // 使用差分编码
    bool use_roi_encoding;         // 使用感兴趣区域编码
    uint8_t roi_padding;           // ROI填充像素
};

// 系统状态
struct SystemStatus {
    uint8_t cpu_usage;             // CPU使用率(百分比)
    uint32_t memory_usage;         // 内存使用(KB)
    uint16_t fps;                  // 当前FPS
    uint16_t ping;                 // 当前延迟(毫秒)
    uint8_t packet_loss;           // 丢包率(百分比) 
    uint32_t bandwidth_usage;      // 带宽使用(Bytes/s)
    uint64_t uptime;               // 运行时间(秒)
    uint32_t processed_frames;     // 已处理帧数
    uint8_t queue_utilization;     // 队列使用率(百分比)
};

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
using Duration = std::chrono::duration<double, std::milli>;

} // namespace zero_latency