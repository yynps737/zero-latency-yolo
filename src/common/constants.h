#pragma once

#include <cstdint>

namespace zero_latency {
namespace constants {

// 网络常量
constexpr uint16_t DEFAULT_SERVER_PORT = 7788;
constexpr uint16_t DEFAULT_WEB_PORT = 3000;
constexpr size_t MAX_PACKET_SIZE = 65536;
constexpr size_t MAX_FRAME_SIZE = 1920 * 1080 * 3; // 最大支持1080p RGB数据
constexpr uint32_t CONNECTION_TIMEOUT_MS = 5000;   // 5秒连接超时
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 1000;   // 1秒心跳间隔
constexpr uint8_t MAX_RETRY_COUNT = 3;             // 最大重试次数
constexpr uint8_t MAX_CLIENTS = 10;                // 最大客户端连接数

// 服务器性能常量
constexpr uint32_t INFERENCE_QUEUE_SIZE = 8;       // 推理队列大小
constexpr uint32_t TARGET_SERVER_FPS = 60;         // 目标服务器FPS
constexpr uint32_t MAX_DETECTION_COUNT = 32;       // 最大检测对象数
constexpr uint32_t MIN_DETECTION_INTERVAL_MS = 16; // 最小检测间隔(约60FPS)

// 客户端性能常量
constexpr uint32_t CAPTURE_BUFFER_SIZE = 4;        // 捕获缓冲区大小
constexpr uint32_t TARGET_CLIENT_FPS = 60;         // 目标客户端FPS
constexpr uint32_t MAX_PREDICTION_MS = 200;        // 最大预测毫秒数
constexpr uint32_t RENDER_AHEAD_FRAMES = 1;        // 提前渲染帧数

// 图像处理常量
constexpr uint16_t DEFAULT_MODEL_WIDTH = 416;      // 默认模型输入宽度
constexpr uint16_t DEFAULT_MODEL_HEIGHT = 416;     // 默认模型输入高度
constexpr float DEFAULT_CONF_THRESHOLD = 0.5f;     // 默认置信度阈值
constexpr float DEFAULT_NMS_THRESHOLD = 0.45f;     // 默认NMS阈值

// CS 1.6特定常量
namespace cs16 {
    constexpr float HEAD_OFFSET_Y = -0.15f;        // 头部Y轴偏移(相对于边界框高度)
    constexpr float BODY_CENTER_Y = 0.4f;          // 身体中心Y轴位置(相对于边界框高度)
    constexpr float DEFAULT_RECOIL_FACTOR = 0.7f;  // 默认后座力系数
    constexpr uint8_t CLASS_COUNT = 4;             // 类别数量
    
    // 类别映射
    constexpr uint8_t CLASS_T = 0;                 // T方玩家
    constexpr uint8_t CLASS_CT = 1;                // CT方玩家
    constexpr uint8_t CLASS_HEAD = 2;              // 头部
    constexpr uint8_t CLASS_WEAPON = 3;            // 武器
    
    // 武器特定后座力模式
    struct WeaponRecoil {
        constexpr static float AK47 = 2.5f;        // AK47后座力
        constexpr static float M4A1 = 2.0f;        // M4A1后座力
        constexpr static float AWP = 0.0f;         // AWP后座力(无需补偿)
        constexpr static float DEAGLE = 3.0f;      // 沙漠之鹰后座力
    };
}

// 文件路径常量
namespace paths {
    constexpr const char* DEFAULT_MODEL_PATH = "models/yolo_nano_cs16.onnx";
    constexpr const char* SERVER_CONFIG_PATH = "configs/server.json";
    constexpr const char* CLIENT_CONFIG_PATH = "configs/client.json";
    constexpr const char* LOG_PATH = "logs/";
}

// UI常量
namespace ui {
    constexpr uint8_t ESP_LINE_THICKNESS = 2;      // ESP线条粗细
    constexpr uint8_t ESP_BOX_OPACITY = 160;       // ESP框透明度(0-255)
    constexpr uint8_t TEXT_SIZE = 14;              // 文本大小
    constexpr uint8_t TEXT_THICKNESS = 1;          // 文本粗细
    
    // 颜色(RGBA格式)
    namespace colors {
        constexpr uint32_t T_COLOR = 0xFF0000FF;   // T方颜色(红色)
        constexpr uint32_t CT_COLOR = 0x0000FFFF;  // CT方颜色(蓝色)
        constexpr uint32_t HEAD_COLOR = 0x00FF00FF;// 头部颜色(绿色)
        constexpr uint32_t TEXT_COLOR = 0xFFFFFFFF;// 文本颜色(白色)
        constexpr uint32_t DISTANCE_COLOR = 0xFFFF00FF; // 距离颜色(黄色)
    }
}

// 系统常量
namespace system {
    constexpr const char* USER_AGENT = "ZeroLatencyClient/1.0";
    constexpr const char* SERVER_NAME = "ZeroLatencyServer/1.0";
}

// 双引擎系统常量
namespace dual_engine {
    constexpr float LOCAL_CONFIDENCE_DECAY = 0.05f;// 本地预测置信度衰减率(每帧)
    constexpr float LOCAL_PREDICTION_WEIGHT = 0.7f;// 本地预测权重(初始)
    constexpr float SERVER_CORRECTION_WEIGHT = 0.3f;// 服务器校正权重(初始)
    constexpr uint32_t MAX_PREDICTION_FRAMES = 12; // 最大预测帧数
    constexpr float MIN_SERVER_CONFIDENCE = 0.4f;  // 最低服务器置信度
    constexpr float TRANSITION_SPEED = 0.2f;       // 从本地到服务器结果过渡速度
}

} // namespace constants
} // namespace zero_latency