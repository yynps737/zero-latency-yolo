#pragma once

// 标准库头文件
#include <string>
#include <vector>
#include <unordered_map>

// 项目头文件 - 确保正确的包含顺序
#include "../common/constants.h"
#include "../common/types.h"

namespace zero_latency {

// 服务器配置
struct ServerConfig {
    std::string model_path;
    uint16_t port;
    uint16_t web_port;
    uint8_t max_clients;
    uint32_t target_fps;
    float confidence_threshold;
    float nms_threshold;
    size_t max_queue_size;
    bool use_cpu_affinity;
    int cpu_core_id;
    bool use_high_priority;
    
    // 默认构造函数使用默认值
    ServerConfig()
        : model_path(constants::paths::DEFAULT_MODEL_PATH),
          port(constants::DEFAULT_SERVER_PORT),
          web_port(constants::DEFAULT_WEB_PORT),
          max_clients(constants::MAX_CLIENTS),
          target_fps(constants::TARGET_SERVER_FPS),
          confidence_threshold(constants::DEFAULT_CONF_THRESHOLD),
          nms_threshold(constants::DEFAULT_NMS_THRESHOLD),
          max_queue_size(constants::INFERENCE_QUEUE_SIZE),
          use_cpu_affinity(true),
          cpu_core_id(0),
          use_high_priority(true) {
    }
};

// 客户端配置
struct ClientConfig {
    std::string server_ip;
    uint16_t server_port;
    uint8_t game_id;
    uint32_t target_fps;
    uint16_t screen_width;
    uint16_t screen_height;
    
    CompressionSettings compression;
    PredictionParams prediction;
    
    bool auto_connect;
    bool auto_start;
    
    bool enable_aim_assist;
    bool enable_esp;
    bool enable_recoil_control;
    
    bool use_high_priority;
    
    // 默认构造函数使用默认值
    ClientConfig()
        : server_ip("127.0.0.1"),
          server_port(constants::DEFAULT_SERVER_PORT),
          game_id(static_cast<uint8_t>(GameType::CS_1_6)),
          target_fps(constants::TARGET_CLIENT_FPS),
          screen_width(SCREEN_WIDTH),
          screen_height(SCREEN_HEIGHT),
          auto_connect(true),
          auto_start(false),
          enable_aim_assist(true),
          enable_esp(true),
          enable_recoil_control(true),
          use_high_priority(true) {
        
        // 设置默认压缩参数
        compression.quality = 75;
        compression.keyframe_interval = 30;
        compression.use_difference_encoding = true;
        compression.use_roi_encoding = true;
        compression.roi_padding = 20;
        
        // 设置默认预测参数
        prediction.max_prediction_time = 200.0f;
        prediction.position_uncertainty = 0.1f;
        prediction.velocity_uncertainty = 0.2f;
        prediction.acceleration_uncertainty = 0.3f;
        prediction.min_confidence_threshold = 0.5f;
    }
};

// 配置管理类
class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();
    
    // 加载服务器配置
    bool loadServerConfig(const std::string& path, ServerConfig& config);
    
    // 保存服务器配置
    bool saveServerConfig(const std::string& path, const ServerConfig& config);
    
    // 加载客户端配置
    bool loadClientConfig(const std::string& path, ClientConfig& config);
    
    // 保存客户端配置
    bool saveClientConfig(const std::string& path, const ClientConfig& config);
    
    // 导出配置为JSON
    std::string exportConfigToJson(const ClientConfig& config);
    
    // 从JSON导入配置
    bool importConfigFromJson(const std::string& json_str, ClientConfig& config);
    
private:
    // 创建默认配置
    void createDefaultServerConfig(const std::string& path);
    void createDefaultClientConfig(const std::string& path);
};

} // namespace zero_latency