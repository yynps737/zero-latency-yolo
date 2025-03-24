#pragma once
#include <cstdint>

namespace zero_latency {
namespace constants {

constexpr uint16_t DEFAULT_SERVER_PORT = 7788;
constexpr uint16_t DEFAULT_WEB_PORT = 3000;
constexpr size_t MAX_PACKET_SIZE = 65536;
constexpr size_t MAX_FRAME_SIZE = 1920 * 1080 * 3;
constexpr uint32_t CONNECTION_TIMEOUT_MS = 5000;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;
constexpr uint8_t MAX_RETRY_COUNT = 3;
constexpr uint8_t MAX_CLIENTS = 10;

constexpr uint32_t INFERENCE_QUEUE_SIZE = 8;
constexpr uint32_t TARGET_SERVER_FPS = 60;
constexpr uint32_t MAX_DETECTION_COUNT = 32;
constexpr uint32_t MIN_DETECTION_INTERVAL_MS = 16;

constexpr uint32_t CAPTURE_BUFFER_SIZE = 4;
constexpr uint32_t TARGET_CLIENT_FPS = 60;
constexpr uint32_t MAX_PREDICTION_MS = 200;
constexpr uint32_t RENDER_AHEAD_FRAMES = 1;

constexpr uint16_t DEFAULT_MODEL_WIDTH = 416;
constexpr uint16_t DEFAULT_MODEL_HEIGHT = 416;
constexpr float DEFAULT_CONF_THRESHOLD = 0.5f;
constexpr float DEFAULT_NMS_THRESHOLD = 0.45f;

namespace cs16 {
    constexpr float HEAD_OFFSET_Y = -0.15f;
    constexpr float BODY_CENTER_Y = 0.4f;
    constexpr float DEFAULT_RECOIL_FACTOR = 0.7f;
    constexpr uint8_t CLASS_COUNT = 4;
    
    constexpr uint8_t CLASS_T = 0;
    constexpr uint8_t CLASS_CT = 1;
    constexpr uint8_t CLASS_HEAD = 2;
    constexpr uint8_t CLASS_WEAPON = 3;
    
    struct WeaponRecoil {
        constexpr static float AK47 = 2.5f;
        constexpr static float M4A1 = 2.0f;
        constexpr static float AWP = 0.0f;
        constexpr static float DEAGLE = 3.0f;
    };
}

namespace paths {
    constexpr const char* DEFAULT_MODEL_PATH = "models/yolo_nano_cs16.onnx";
    constexpr const char* SERVER_CONFIG_PATH = "configs/server.json";
    constexpr const char* CLIENT_CONFIG_PATH = "configs/client.json";
    constexpr const char* LOG_PATH = "logs/";
}

namespace ui {
    constexpr uint8_t ESP_LINE_THICKNESS = 2;
    constexpr uint8_t ESP_BOX_OPACITY = 160;
    constexpr uint8_t TEXT_SIZE = 14;
    constexpr uint8_t TEXT_THICKNESS = 1;
    
    namespace colors {
        constexpr uint32_t T_COLOR = 0xFF0000FF;
        constexpr uint32_t CT_COLOR = 0x0000FFFF;
        constexpr uint32_t HEAD_COLOR = 0x00FF00FF;
        constexpr uint32_t TEXT_COLOR = 0xFFFFFFFF;
        constexpr uint32_t DISTANCE_COLOR = 0xFFFF00FF;
    }
}

namespace system {
    constexpr const char* USER_AGENT = "ZeroLatencyClient/1.0";
    constexpr const char* SERVER_NAME = "ZeroLatencyServer/1.0";
}

namespace dual_engine {
    constexpr float LOCAL_CONFIDENCE_DECAY = 0.05f;
    constexpr float LOCAL_PREDICTION_WEIGHT = 0.7f;
    constexpr float SERVER_CORRECTION_WEIGHT = 0.3f;
    constexpr uint32_t MAX_PREDICTION_FRAMES = 12;
    constexpr float MIN_SERVER_CONFIDENCE = 0.4f;
    constexpr float TRANSITION_SPEED = 0.2f;
}

}
}