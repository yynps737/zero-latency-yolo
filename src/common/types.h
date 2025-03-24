#pragma once
#include <cstdint>
#include <vector>
#include <chrono>
#include <functional>

namespace zero_latency {

#define MAX_OBJECTS 32
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define PROTOCOL_VERSION 1

enum class GameType : uint8_t;

struct BoundingBox {
    float x, y, width, height;
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
    uint16_t width, height;
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
    uint16_t screen_width, screen_height;
    uint8_t game_id;
};

struct ServerInfo {
    uint32_t server_id;
    uint32_t protocol_version;
    float model_version;
    uint8_t max_clients;
    uint16_t max_fps;
    uint8_t status;
};

struct CompressionSettings {
    uint8_t quality;
    uint8_t keyframe_interval;
    bool use_difference_encoding, use_roi_encoding;
    uint8_t roi_padding;
};

struct PredictionParams {
    float max_prediction_time;
    float position_uncertainty;
    float velocity_uncertainty;
    float acceleration_uncertainty;
    float min_confidence_threshold;
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
    FORTNITE = 6,
    CS2 = 7,
    L4D2 = 8
};

enum class DetectionClass : uint8_t {
    UNKNOWN = 0,
    PLAYER_T = 1,
    PLAYER_CT = 2,
    HEAD = 3,
    BODY = 4,
    WEAPON = 5,
    GRENADE = 6,
    C4 = 7,
    HOSTAGE = 8,
    ZOMBIE = 9,
    SPECIAL = 10,
    SURVIVOR = 11,
    TANK = 12,
    WITCH = 13
};

struct Point2D { float x, y; };
struct Vector2D { float x, y; };
struct Vector3D { float x, y, z; };

struct SystemStatus {
    uint8_t cpu_usage;
    uint32_t memory_usage;
    uint16_t fps, ping;
    uint8_t packet_loss;
    uint32_t bandwidth_usage;
    uint64_t uptime;
    uint32_t processed_frames;
    uint8_t queue_utilization;
};

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
using Duration = std::chrono::duration<double, std::milli>;

}