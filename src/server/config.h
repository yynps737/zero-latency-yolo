#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include "../common/constants.h"
#include "../common/types.h"
#include "../common/result.h"
#include "../common/logger.h"

// 使用第三方JSON库
#include <nlohmann/json.hpp>

namespace zero_latency {

// 使用nlohmann/json库
using json = nlohmann::json;

// 网络配置
struct NetworkConfig {
    uint16_t port;                    // 服务端口
    uint16_t web_port;                // Web服务端口
    uint32_t recv_buffer_size;        // 接收缓冲区大小
    uint32_t send_buffer_size;        // 发送缓冲区大小
    uint32_t timeout_ms;              // 超时时间(毫秒)
    uint32_t heartbeat_interval_ms;   // 心跳间隔(毫秒)
    uint8_t max_retries;              // 最大重试次数
    bool use_reliable_udp;            // 是否使用可靠UDP
    
    // 默认构造函数
    NetworkConfig()
        : port(constants::DEFAULT_SERVER_PORT),
          web_port(constants::DEFAULT_WEB_PORT),
          recv_buffer_size(1048576),
          send_buffer_size(1048576),
          timeout_ms(5000),
          heartbeat_interval_ms(1000),
          max_retries(3),
          use_reliable_udp(true) {
    }
    
    // JSON序列化/反序列化
    void to_json(json& j) const {
        j = json{
            {"port", port},
            {"web_port", web_port},
            {"recv_buffer_size", recv_buffer_size},
            {"send_buffer_size", send_buffer_size},
            {"timeout_ms", timeout_ms},
            {"heartbeat_interval_ms", heartbeat_interval_ms},
            {"max_retries", max_retries},
            {"use_reliable_udp", use_reliable_udp}
        };
    }
    
    void from_json(const json& j) {
        if (j.contains("port")) j.at("port").get_to(port);
        if (j.contains("web_port")) j.at("web_port").get_to(web_port);
        if (j.contains("recv_buffer_size")) j.at("recv_buffer_size").get_to(recv_buffer_size);
        if (j.contains("send_buffer_size")) j.at("send_buffer_size").get_to(send_buffer_size);
        if (j.contains("timeout_ms")) j.at("timeout_ms").get_to(timeout_ms);
        if (j.contains("heartbeat_interval_ms")) j.at("heartbeat_interval_ms").get_to(heartbeat_interval_ms);
        if (j.contains("max_retries")) j.at("max_retries").get_to(max_retries);
        if (j.contains("use_reliable_udp")) j.at("use_reliable_udp").get_to(use_reliable_udp);
    }
};

// 日志配置
struct LoggingConfig {
    bool enable_logging;              // 是否启用日志
    std::string log_level;            // 日志级别
    std::string log_file;             // 日志文件路径
    uint32_t max_log_size_mb;         // 最大日志大小(MB)
    uint32_t max_log_files;           // 最大日志文件数
    
    // 默认构造函数
    LoggingConfig()
        : enable_logging(true),
          log_level("info"),
          log_file("logs/server.log"),
          max_log_size_mb(10),
          max_log_files(5) {
    }
    
    // JSON序列化/反序列化
    void to_json(json& j) const {
        j = json{
            {"enable_logging", enable_logging},
            {"log_level", log_level},
            {"log_file", log_file},
            {"max_log_size_mb", max_log_size_mb},
            {"max_log_files", max_log_files}
        };
    }
    
    void from_json(const json& j) {
        if (j.contains("enable_logging")) j.at("enable_logging").get_to(enable_logging);
        if (j.contains("log_level")) j.at("log_level").get_to(log_level);
        if (j.contains("log_file")) j.at("log_file").get_to(log_file);
        if (j.contains("max_log_size_mb")) j.at("max_log_size_mb").get_to(max_log_size_mb);
        if (j.contains("max_log_files")) j.at("max_log_files").get_to(max_log_files);
    }
};

// 检测配置
struct DetectionConfig {
    uint16_t model_width;             // 模型输入宽度
    uint16_t model_height;            // 模型输入高度
    bool enable_tracking;             // 是否启用跟踪
    uint32_t max_tracking_age_ms;     // 最大跟踪生命周期
    std::unordered_map<std::string, float> class_weights; // 类别权重
    
    // 默认构造函数
    DetectionConfig()
        : model_width(constants::DEFAULT_MODEL_WIDTH),
          model_height(constants::DEFAULT_MODEL_HEIGHT),
          enable_tracking(true),
          max_tracking_age_ms(500) {
        
        // 默认类别权重
        class_weights["player_t"] = 1.0f;
        class_weights["player_ct"] = 1.0f;
        class_weights["head"] = 1.2f;
        class_weights["weapon"] = 0.8f;
    }
    
    // JSON序列化/反序列化
    void to_json(json& j) const {
        j = json{
            {"model_width", model_width},
            {"model_height", model_height},
            {"enable_tracking", enable_tracking},
            {"max_tracking_age_ms", max_tracking_age_ms},
            {"class_weights", class_weights}
        };
    }
    
    void from_json(const json& j) {
        if (j.contains("model_width")) j.at("model_width").get_to(model_width);
        if (j.contains("model_height")) j.at("model_height").get_to(model_height);
        if (j.contains("enable_tracking")) j.at("enable_tracking").get_to(enable_tracking);
        if (j.contains("max_tracking_age_ms")) j.at("max_tracking_age_ms").get_to(max_tracking_age_ms);
        if (j.contains("class_weights")) j.at("class_weights").get_to(class_weights);
    }
};

// 游戏适配器配置
struct GameAdaptersConfig {
    struct WeaponConfig {
        float recoil_factor;
        float priority;
        
        // 默认构造函数
        WeaponConfig() : recoil_factor(1.0f), priority(1.0f) {}
        
        // JSON序列化/反序列化
        void to_json(json& j) const {
            j = json{
                {"recoil_factor", recoil_factor},
                {"priority", priority}
            };
        }
        
        void from_json(const json& j) {
            if (j.contains("recoil_factor")) j.at("recoil_factor").get_to(recoil_factor);
            if (j.contains("priority")) j.at("priority").get_to(priority);
        }
    };
    
    struct GameConfig {
        bool enabled;
        float aim_target_offset_y;
        float head_size_factor;
        std::unordered_map<std::string, WeaponConfig> weapons;
        
        // 默认构造函数
        GameConfig()
            : enabled(false),
              aim_target_offset_y(-0.15f),
              head_size_factor(0.7f) {
        }
        
        // JSON序列化/反序列化
        void to_json(json& j) const {
            j = json{
                {"enabled", enabled},
                {"aim_target_offset_y", aim_target_offset_y},
                {"head_size_factor", head_size_factor},
                {"weapons", json::object()}
            };
            
            for (const auto& [name, weapon] : weapons) {
                json weapon_json;
                weapon.to_json(weapon_json);
                j["weapons"][name] = weapon_json;
            }
        }
        
        void from_json(const json& j) {
            if (j.contains("enabled")) j.at("enabled").get_to(enabled);
            if (j.contains("aim_target_offset_y")) j.at("aim_target_offset_y").get_to(aim_target_offset_y);
            if (j.contains("head_size_factor")) j.at("head_size_factor").get_to(head_size_factor);
            
            if (j.contains("weapons") && j["weapons"].is_object()) {
                for (auto& [name, weapon_json] : j["weapons"].items()) {
                    WeaponConfig weapon;
                    weapon.from_json(weapon_json);
                    weapons[name] = weapon;
                }
            }
        }
    };
    
    std::unordered_map<std::string, GameConfig> games;
    
    // 默认构造函数
    GameAdaptersConfig() {
        // 默认配置CS 1.6
        GameConfig cs16;
        cs16.enabled = true;
        
        // 默认武器配置
        WeaponConfig ak47;
        ak47.recoil_factor = constants::cs16::WeaponRecoil::AK47;
        ak47.priority = 1.0f;
        cs16.weapons["ak47"] = ak47;
        
        WeaponConfig m4a1;
        m4a1.recoil_factor = constants::cs16::WeaponRecoil::M4A1;
        m4a1.priority = 1.0f;
        cs16.weapons["m4a1"] = m4a1;
        
        WeaponConfig awp;
        awp.recoil_factor = constants::cs16::WeaponRecoil::AWP;
        awp.priority = 1.5f;
        cs16.weapons["awp"] = awp;
        
        WeaponConfig deagle;
        deagle.recoil_factor = constants::cs16::WeaponRecoil::DEAGLE;
        deagle.priority = 1.2f;
        cs16.weapons["deagle"] = deagle;
        
        games["cs16"] = cs16;
    }
    
    // JSON序列化/反序列化
    void to_json(json& j) const {
        j = json::object();
        for (const auto& [name, game] : games) {
            json game_json;
            game.to_json(game_json);
            j[name] = game_json;
        }
    }
    
    void from_json(const json& j) {
        if (j.is_object()) {
            for (auto& [name, game_json] : j.items()) {
                GameConfig game;
                game.from_json(game_json);
                games[name] = game;
            }
        }
    }
};

// 分析配置
struct AnalyticsConfig {
    bool enable_analytics;            // 是否启用分析
    uint32_t stats_interval_sec;      // 统计间隔(秒)
    bool save_stats_to_file;          // 是否保存统计到文件
    std::string stats_file;           // 统计文件路径
    
    // 默认构造函数
    AnalyticsConfig()
        : enable_analytics(true),
          stats_interval_sec(60),
          save_stats_to_file(true),
          stats_file("logs/stats.json") {
    }
    
    // JSON序列化/反序列化
    void to_json(json& j) const {
        j = json{
            {"enable_analytics", enable_analytics},
            {"stats_interval_sec", stats_interval_sec},
            {"save_stats_to_file", save_stats_to_file},
            {"stats_file", stats_file}
        };
    }
    
    void from_json(const json& j) {
        if (j.contains("enable_analytics")) j.at("enable_analytics").get_to(enable_analytics);
        if (j.contains("stats_interval_sec")) j.at("stats_interval_sec").get_to(stats_interval_sec);
        if (j.contains("save_stats_to_file")) j.at("save_stats_to_file").get_to(save_stats_to_file);
        if (j.contains("stats_file")) j.at("stats_file").get_to(stats_file);
    }
};

// 服务器配置
struct ServerConfig {
    std::string model_path;           // 模型路径
    std::string inference_engine;     // 推理引擎类型
    uint8_t max_clients;              // 最大客户端数
    uint32_t target_fps;              // 目标FPS
    float confidence_threshold;       // 置信度阈值
    float nms_threshold;              // NMS阈值
    size_t max_queue_size;            // 最大队列大小
    bool use_cpu_affinity;            // 是否使用CPU亲和性
    int cpu_core_id;                  // CPU核心ID
    bool use_high_priority;           // 是否使用高优先级
    uint8_t worker_threads;           // 工作线程数
    
    // 子配置
    NetworkConfig network;            // 网络配置
    LoggingConfig logging;            // 日志配置
    DetectionConfig detection;        // 检测配置
    GameAdaptersConfig game_adapters; // 游戏适配器配置
    AnalyticsConfig analytics;        // 分析配置
    
    // 默认构造函数
    ServerConfig()
        : model_path(constants::paths::DEFAULT_MODEL_PATH),
          inference_engine("onnx"),
          max_clients(constants::MAX_CLIENTS),
          target_fps(constants::TARGET_SERVER_FPS),
          confidence_threshold(constants::DEFAULT_CONF_THRESHOLD),
          nms_threshold(constants::DEFAULT_NMS_THRESHOLD),
          max_queue_size(constants::INFERENCE_QUEUE_SIZE),
          use_cpu_affinity(true),
          cpu_core_id(0),
          use_high_priority(true),
          worker_threads(std::thread::hardware_concurrency()) {
        
        // 限制工作线程数
        if (worker_threads > 16) {
            worker_threads = 16;
        } else if (worker_threads < 1) {
            worker_threads = 1;
        }
    }
    
    // JSON序列化/反序列化
    void to_json(json& j) const {
        j = json{
            {"model_path", model_path},
            {"inference_engine", inference_engine},
            {"port", network.port},
            {"web_port", network.web_port},
            {"max_clients", max_clients},
            {"target_fps", target_fps},
            {"confidence_threshold", confidence_threshold},
            {"nms_threshold", nms_threshold},
            {"max_queue_size", max_queue_size},
            {"use_cpu_affinity", use_cpu_affinity},
            {"cpu_core_id", cpu_core_id},
            {"use_high_priority", use_high_priority},
            {"worker_threads", worker_threads}
        };
        
        // 添加子配置
        json network_json;
        network.to_json(network_json);
        j["network"] = network_json;
        
        json logging_json;
        logging.to_json(logging_json);
        j["logging"] = logging_json;
        
        json detection_json;
        detection.to_json(detection_json);
        j["detection"] = detection_json;
        
        json game_adapters_json;
        game_adapters.to_json(game_adapters_json);
        j["game_adapters"] = game_adapters_json;
        
        json analytics_json;
        analytics.to_json(analytics_json);
        j["analytics"] = analytics_json;
    }
    
    void from_json(const json& j) {
        if (j.contains("model_path")) j.at("model_path").get_to(model_path);
        if (j.contains("inference_engine")) j.at("inference_engine").get_to(inference_engine);
        if (j.contains("port")) j.at("port").get_to(network.port);
        if (j.contains("web_port")) j.at("web_port").get_to(network.web_port);
        if (j.contains("max_clients")) j.at("max_clients").get_to(max_clients);
        if (j.contains("target_fps")) j.at("target_fps").get_to(target_fps);
        if (j.contains("confidence_threshold")) j.at("confidence_threshold").get_to(confidence_threshold);
        if (j.contains("nms_threshold")) j.at("nms_threshold").get_to(nms_threshold);
        if (j.contains("max_queue_size")) j.at("max_queue_size").get_to(max_queue_size);
        if (j.contains("use_cpu_affinity")) j.at("use_cpu_affinity").get_to(use_cpu_affinity);
        if (j.contains("cpu_core_id")) j.at("cpu_core_id").get_to(cpu_core_id);
        if (j.contains("use_high_priority")) j.at("use_high_priority").get_to(use_high_priority);
        if (j.contains("worker_threads")) j.at("worker_threads").get_to(worker_threads);
        
        // 子配置
        if (j.contains("network")) network.from_json(j.at("network"));
        if (j.contains("logging")) logging.from_json(j.at("logging"));
        if (j.contains("detection")) detection.from_json(j.at("detection"));
        if (j.contains("game_adapters")) game_adapters.from_json(j.at("game_adapters"));
        if (j.contains("analytics")) analytics.from_json(j.at("analytics"));
    }
};

// 客户端配置
struct ClientConfig {
    std::string server_ip;            // 服务器IP
    uint16_t server_port;             // 服务器端口
    uint8_t game_id;                  // 游戏ID
    uint32_t target_fps;              // 目标FPS
    uint16_t screen_width;            // 屏幕宽度
    uint16_t screen_height;           // 屏幕高度
    bool auto_connect;                // 是否自动连接
    bool auto_start;                  // 是否自动启动
    bool enable_aim_assist;           // 是否启用瞄准辅助
    bool enable_esp;                  // 是否启用ESP
    bool enable_recoil_control;       // 是否启用后座力控制
    bool use_high_priority;           // 是否使用高优先级
    
    CompressionSettings compression;  // 压缩设置
    PredictionParams prediction;      // 预测参数
    
    // 默认构造函数
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
    
    // JSON序列化/反序列化
    void to_json(json& j) const {
        j = json{
            {"server_ip", server_ip},
            {"server_port", server_port},
            {"game_id", game_id},
            {"target_fps", target_fps},
            {"screen_width", screen_width},
            {"screen_height", screen_height},
            {"auto_connect", auto_connect},
            {"auto_start", auto_start},
            {"enable_aim_assist", enable_aim_assist},
            {"enable_esp", enable_esp},
            {"enable_recoil_control", enable_recoil_control},
            {"use_high_priority", use_high_priority}
        };
        
        // 添加压缩设置
        j["compression"] = {
            {"quality", compression.quality},
            {"keyframe_interval", compression.keyframe_interval},
            {"use_difference_encoding", compression.use_difference_encoding},
            {"use_roi_encoding", compression.use_roi_encoding},
            {"roi_padding", compression.roi_padding}
        };
        
        // 添加预测参数
        j["prediction"] = {
            {"max_prediction_time", prediction.max_prediction_time},
            {"position_uncertainty", prediction.position_uncertainty},
            {"velocity_uncertainty", prediction.velocity_uncertainty},
            {"acceleration_uncertainty", prediction.acceleration_uncertainty},
            {"min_confidence_threshold", prediction.min_confidence_threshold}
        };
    }
    
    void from_json(const json& j) {
        if (j.contains("server_ip")) j.at("server_ip").get_to(server_ip);
        if (j.contains("server_port")) j.at("server_port").get_to(server_port);
        if (j.contains("game_id")) j.at("game_id").get_to(game_id);
        if (j.contains("target_fps")) j.at("target_fps").get_to(target_fps);
        if (j.contains("screen_width")) j.at("screen_width").get_to(screen_width);
        if (j.contains("screen_height")) j.at("screen_height").get_to(screen_height);
        if (j.contains("auto_connect")) j.at("auto_connect").get_to(auto_connect);
        if (j.contains("auto_start")) j.at("auto_start").get_to(auto_start);
        if (j.contains("enable_aim_assist")) j.at("enable_aim_assist").get_to(enable_aim_assist);
        if (j.contains("enable_esp")) j.at("enable_esp").get_to(enable_esp);
        if (j.contains("enable_recoil_control")) j.at("enable_recoil_control").get_to(enable_recoil_control);
        if (j.contains("use_high_priority")) j.at("use_high_priority").get_to(use_high_priority);
        
        // 压缩设置
        if (j.contains("compression")) {
            const json& comp = j.at("compression");
            if (comp.contains("quality")) comp.at("quality").get_to(compression.quality);
            if (comp.contains("keyframe_interval")) comp.at("keyframe_interval").get_to(compression.keyframe_interval);
            if (comp.contains("use_difference_encoding")) comp.at("use_difference_encoding").get_to(compression.use_difference_encoding);
            if (comp.contains("use_roi_encoding")) comp.at("use_roi_encoding").get_to(compression.use_roi_encoding);
            if (comp.contains("roi_padding")) comp.at("roi_padding").get_to(compression.roi_padding);
        }
        
        // 预测参数
        if (j.contains("prediction")) {
            const json& pred = j.at("prediction");
            if (pred.contains("max_prediction_time")) pred.at("max_prediction_time").get_to(prediction.max_prediction_time);
            if (pred.contains("position_uncertainty")) pred.at("position_uncertainty").get_to(prediction.position_uncertainty);
            if (pred.contains("velocity_uncertainty")) pred.at("velocity_uncertainty").get_to(prediction.velocity_uncertainty);
            if (pred.contains("acceleration_uncertainty")) pred.at("acceleration_uncertainty").get_to(prediction.acceleration_uncertainty);
            if (pred.contains("min_confidence_threshold")) pred.at("min_confidence_threshold").get_to(prediction.min_confidence_threshold);
        }
    }
};

// 配置管理类
class ConfigManager {
public:
    // 单例访问
    static ConfigManager& getInstance() {
        static ConfigManager instance;
        return instance;
    }
    
    // 加载服务器配置
    Result<ServerConfig> loadServerConfig(const std::string& path = "configs/server.json") {
        try {
            namespace fs = std::filesystem;
            
            // 检查文件是否存在
            if (!fs::exists(path)) {
                LOG_WARN("Config file not found: " + path + ", creating default config");
                
                // 创建默认配置
                createDefaultServerConfig(path);
                
                // 返回默认配置
                return Result<ServerConfig>::ok(ServerConfig());
            }
            
            // 打开并读取文件
            std::ifstream file(path);
            if (!file.is_open()) {
                LOG_ERROR("Failed to open config file: " + path);
                return Result<ServerConfig>::error(ErrorCode::FILE_ACCESS_DENIED, "Failed to open config file");
            }
            
            // 解析JSON
            json j;
            file >> j;
            
            // 转换为配置对象
            ServerConfig config;
            config.from_json(j);
            
            LOG_INFO("Server config loaded successfully from " + path);
            
            return Result<ServerConfig>::ok(config);
        } catch (const json::exception& e) {
            LOG_ERROR("JSON parsing error: " + std::string(e.what()));
            return Result<ServerConfig>::error(ErrorCode::CONFIG_PARSE_ERROR, "JSON parsing error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to load server config: " + std::string(e.what()));
            return Result<ServerConfig>::error(ErrorCode::CONFIG_ERROR, "Failed to load server config: " + std::string(e.what()));
        }
    }
    
    // 保存服务器配置
    Result<void> saveServerConfig(const ServerConfig& config, const std::string& path = "configs/server.json") {
        try {
            namespace fs = std::filesystem;
            
            // 确保目录存在
            fs::path config_path(path);
            fs::path dir_path = config_path.parent_path();
            
            if (!dir_path.empty() && !fs::exists(dir_path)) {
                fs::create_directories(dir_path);
            }
            
            // 转换为JSON
            json j;
            config.to_json(j);
            
            // 写入文件
            std::ofstream file(path);
            if (!file.is_open()) {
                LOG_ERROR("Failed to open config file for writing: " + path);
                return Result<void>::error(ErrorCode::FILE_ACCESS_DENIED, "Failed to open config file for writing");
            }
            
            file << std::setw(4) << j << std::endl;
            
            LOG_INFO("Server config saved successfully to " + path);
            
            return Result<void>::ok();
        } catch (const json::exception& e) {
            LOG_ERROR("JSON serialization error: " + std::string(e.what()));
            return Result<void>::error(ErrorCode::CONFIG_ERROR, "JSON serialization error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to save server config: " + std::string(e.what()));
            return Result<void>::error(ErrorCode::CONFIG_ERROR, "Failed to save server config: " + std::string(e.what()));
        }
    }
    
    // 创建默认服务器配置
    Result<void> createDefaultServerConfig(const std::string& path = "configs/server.json") {
        return saveServerConfig(ServerConfig(), path);
    }
    
    // 加载客户端配置
    Result<ClientConfig> loadClientConfig(const std::string& path = "configs/client.json") {
        try {
            namespace fs = std::filesystem;
            
            // 检查文件是否存在
            if (!fs::exists(path)) {
                LOG_WARN("Config file not found: " + path + ", creating default config");
                
                // 创建默认配置
                createDefaultClientConfig(path);
                
                // 返回默认配置
                return Result<ClientConfig>::ok(ClientConfig());
            }
            
            // 打开并读取文件
            std::ifstream file(path);
            if (!file.is_open()) {
                LOG_ERROR("Failed to open config file: " + path);
                return Result<ClientConfig>::error(ErrorCode::FILE_ACCESS_DENIED, "Failed to open config file");
            }
            
            // 解析JSON
            json j;
            file >> j;
            
            // 转换为配置对象
            ClientConfig config;
            config.from_json(j);
            
            LOG_INFO("Client config loaded successfully from " + path);
            
            return Result<ClientConfig>::ok(config);
        } catch (const json::exception& e) {
            LOG_ERROR("JSON parsing error: " + std::string(e.what()));
            return Result<ClientConfig>::error(ErrorCode::CONFIG_PARSE_ERROR, "JSON parsing error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to load client config: " + std::string(e.what()));
            return Result<ClientConfig>::error(ErrorCode::CONFIG_ERROR, "Failed to load client config: " + std::string(e.what()));
        }
    }
    
    // 保存客户端配置
    Result<void> saveClientConfig(const ClientConfig& config, const std::string& path = "configs/client.json") {
        try {
            namespace fs = std::filesystem;
            
            // 确保目录存在
            fs::path config_path(path);
            fs::path dir_path = config_path.parent_path();
            
            if (!dir_path.empty() && !fs::exists(dir_path)) {
                fs::create_directories(dir_path);
            }
            
            // 转换为JSON
            json j;
            config.to_json(j);
            
            // 写入文件
            std::ofstream file(path);
            if (!file.is_open()) {
                LOG_ERROR("Failed to open config file for writing: " + path);
                return Result<void>::error(ErrorCode::FILE_ACCESS_DENIED, "Failed to open config file for writing");
            }
            
            file << std::setw(4) << j << std::endl;
            
            LOG_INFO("Client config saved successfully to " + path);
            
            return Result<void>::ok();
        } catch (const json::exception& e) {
            LOG_ERROR("JSON serialization error: " + std::string(e.what()));
            return Result<void>::error(ErrorCode::CONFIG_ERROR, "JSON serialization error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to save client config: " + std::string(e.what()));
            return Result<void>::error(ErrorCode::CONFIG_ERROR, "Failed to save client config: " + std::string(e.what()));
        }
    }
    
    // 创建默认客户端配置
    Result<void> createDefaultClientConfig(const std::string& path = "configs/client.json") {
        return saveClientConfig(ClientConfig(), path);
    }
    
    // 导出配置到JSON字符串
    template<typename ConfigType>
    Result<std::string> exportConfigToJson(const ConfigType& config) {
        try {
            json j;
            config.to_json(j);
            return Result<std::string>::ok(j.dump(4));
        } catch (const json::exception& e) {
            LOG_ERROR("JSON serialization error: " + std::string(e.what()));
            return Result<std::string>::error(ErrorCode::CONFIG_ERROR, "JSON serialization error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to export config to JSON: " + std::string(e.what()));
            return Result<std::string>::error(ErrorCode::CONFIG_ERROR, "Failed to export config to JSON: " + std::string(e.what()));
        }
    }
    
    // 从JSON字符串导入配置
    template<typename ConfigType>
    Result<ConfigType> importConfigFromJson(const std::string& json_str) {
        try {
            json j = json::parse(json_str);
            
            ConfigType config;
            config.from_json(j);
            
            return Result<ConfigType>::ok(config);
        } catch (const json::exception& e) {
            LOG_ERROR("JSON parsing error: " + std::string(e.what()));
            return Result<ConfigType>::error(ErrorCode::CONFIG_PARSE_ERROR, "JSON parsing error: " + std::string(e.what()));
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to import config from JSON: " + std::string(e.what()));
            return Result<ConfigType>::error(ErrorCode::CONFIG_ERROR, "Failed to import config from JSON: " + std::string(e.what()));
        }
    }

private:
    // 私有构造函数
    ConfigManager() = default;
    
    // 禁用拷贝和赋值
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
};

} // namespace zero_latency