#include "config.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <windows.h>
#include "../common/constants.h"

namespace fs = std::filesystem;

namespace zero_latency {

ConfigManager::ConfigManager() {
}

ConfigManager::~ConfigManager() {
}

bool ConfigManager::loadClientConfig(const std::string& path, ClientConfig& config) {
    // 检查文件是否存在
    if (!fs::exists(path)) {
        std::cerr << "配置文件不存在: " << path << std::endl;
        
        // 创建默认配置
        createDefaultConfig(path);
        
        // 返回默认配置
        config = ClientConfig();
        return false;
    }
    
    // 打开并读取文件
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "无法打开配置文件: " << path << std::endl;
        return false;
    }
    
    try {
        // 简单的行解析
        std::string line;
        while (std::getline(file, line)) {
            // 跳过注释和空行
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            // 查找分隔符
            size_t pos = line.find('=');
            if (pos == std::string::npos) {
                continue;
            }
            
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            
            // 去除首尾空格
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // 解析各个配置选项
            if (key == "server_ip") {
                config.server_ip = value;
            } else if (key == "server_port") {
                config.server_port = static_cast<uint16_t>(std::stoi(value));
            } else if (key == "game_id") {
                config.game_id = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "target_fps") {
                config.target_fps = static_cast<uint32_t>(std::stoi(value));
            } else if (key == "screen_width") {
                config.screen_width = static_cast<uint16_t>(std::stoi(value));
            } else if (key == "screen_height") {
                config.screen_height = static_cast<uint16_t>(std::stoi(value));
            } else if (key == "auto_connect") {
                config.auto_connect = (value == "true" || value == "1");
            } else if (key == "auto_start") {
                config.auto_start = (value == "true" || value == "1");
            } else if (key == "enable_aim_assist") {
                config.enable_aim_assist = (value == "true" || value == "1");
            } else if (key == "enable_esp") {
                config.enable_esp = (value == "true" || value == "1");
            } else if (key == "enable_recoil_control") {
                config.enable_recoil_control = (value == "true" || value == "1");
            } else if (key == "use_high_priority") {
                config.use_high_priority = (value == "true" || value == "1");
            } else if (key == "compression_quality") {
                config.compression.quality = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "keyframe_interval") {
                config.compression.keyframe_interval = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "use_difference_encoding") {
                config.compression.use_difference_encoding = (value == "true" || value == "1");
            } else if (key == "use_roi_encoding") {
                config.compression.use_roi_encoding = (value == "true" || value == "1");
            } else if (key == "roi_padding") {
                config.compression.roi_padding = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "max_prediction_time") {
                config.prediction.max_prediction_time = std::stof(value);
            } else if (key == "position_uncertainty") {
                config.prediction.position_uncertainty = std::stof(value);
            } else if (key == "velocity_uncertainty") {
                config.prediction.velocity_uncertainty = std::stof(value);
            } else if (key == "acceleration_uncertainty") {
                config.prediction.acceleration_uncertainty = std::stof(value);
            } else if (key == "min_confidence_threshold") {
                config.prediction.min_confidence_threshold = std::stof(value);
            }
        }
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "解析配置文件失败: " << e.what() << std::endl;
        file.close();
        return false;
    }
}

bool ConfigManager::saveClientConfig(const std::string& path, const ClientConfig& config) {
    // 确保目录存在
    fs::path config_path(path);
    fs::path dir_path = config_path.parent_path();
    
    if (!dir_path.empty() && !fs::exists(dir_path)) {
        try {
            fs::create_directories(dir_path);
        } catch (const std::exception& e) {
            std::cerr << "创建配置目录失败: " << e.what() << std::endl;
            return false;
        }
    }
    
    // 打开文件进行写入
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "无法打开配置文件进行写入: " << path << std::endl;
        return false;
    }
    
    try {
        // 写入配置项
        file << "# 零延迟YOLO FPS云辅助系统客户端配置文件\n\n";
        
        file << "# 服务器设置\n";
        file << "server_ip=" << config.server_ip << "\n";
        file << "server_port=" << config.server_port << "\n\n";
        
        file << "# 游戏设置\n";
        file << "game_id=" << static_cast<int>(config.game_id) << "\n";
        file << "screen_width=" << config.screen_width << "\n";
        file << "screen_height=" << config.screen_height << "\n\n";
        
        file << "# 性能设置\n";
        file << "target_fps=" << config.target_fps << "\n";
        file << "use_high_priority=" << (config.use_high_priority ? "true" : "false") << "\n\n";
        
        file << "# 功能设置\n";
        file << "enable_aim_assist=" << (config.enable_aim_assist ? "true" : "false") << "\n";
        file << "enable_esp=" << (config.enable_esp ? "true" : "false") << "\n";
        file << "enable_recoil_control=" << (config.enable_recoil_control ? "true" : "false") << "\n\n";
        
        file << "# 启动设置\n";
        file << "auto_connect=" << (config.auto_connect ? "true" : "false") << "\n";
        file << "auto_start=" << (config.auto_start ? "true" : "false") << "\n\n";
        
        file << "# 压缩设置\n";
        file << "compression_quality=" << static_cast<int>(config.compression.quality) << "\n";
        file << "keyframe_interval=" << static_cast<int>(config.compression.keyframe_interval) << "\n";
        file << "use_difference_encoding=" << (config.compression.use_difference_encoding ? "true" : "false") << "\n";
        file << "use_roi_encoding=" << (config.compression.use_roi_encoding ? "true" : "false") << "\n";
        file << "roi_padding=" << static_cast<int>(config.compression.roi_padding) << "\n\n";
        
        file << "# 预测设置\n";
        file << "max_prediction_time=" << config.prediction.max_prediction_time << "\n";
        file << "position_uncertainty=" << config.prediction.position_uncertainty << "\n";
        file << "velocity_uncertainty=" << config.prediction.velocity_uncertainty << "\n";
        file << "acceleration_uncertainty=" << config.prediction.acceleration_uncertainty << "\n";
        file << "min_confidence_threshold=" << config.prediction.min_confidence_threshold << "\n";
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "写入配置文件失败: " << e.what() << std::endl;
        file.close();
        return false;
    }
}

void ConfigManager::createDefaultConfig(const std::string& path) {
    // 创建默认配置
    ClientConfig config;
    
    // 保存默认配置
    saveClientConfig(path, config);
}

std::string ConfigManager::exportConfigToJson(const ClientConfig& config) {
    std::stringstream ss;
    
    ss << "{\n";
    ss << "  \"server_ip\": \"" << config.server_ip << "\",\n";
    ss << "  \"server_port\": " << config.server_port << ",\n";
    ss << "  \"game_id\": " << static_cast<int>(config.game_id) << ",\n";
    ss << "  \"target_fps\": " << config.target_fps << ",\n";
    ss << "  \"screen_width\": " << config.screen_width << ",\n";
    ss << "  \"screen_height\": " << config.screen_height << ",\n";
    ss << "  \"auto_connect\": " << (config.auto_connect ? "true" : "false") << ",\n";
    ss << "  \"auto_start\": " << (config.auto_start ? "true" : "false") << ",\n";
    ss << "  \"enable_aim_assist\": " << (config.enable_aim_assist ? "true" : "false") << ",\n";
    ss << "  \"enable_esp\": " << (config.enable_esp ? "true" : "false") << ",\n";
    ss << "  \"enable_recoil_control\": " << (config.enable_recoil_control ? "true" : "false") << ",\n";
    ss << "  \"use_high_priority\": " << (config.use_high_priority ? "true" : "false") << ",\n";
    
    ss << "  \"compression\": {\n";
    ss << "    \"quality\": " << static_cast<int>(config.compression.quality) << ",\n";
    ss << "    \"keyframe_interval\": " << static_cast<int>(config.compression.keyframe_interval) << ",\n";
    ss << "    \"use_difference_encoding\": " << (config.compression.use_difference_encoding ? "true" : "false") << ",\n";
    ss << "    \"use_roi_encoding\": " << (config.compression.use_roi_encoding ? "true" : "false") << ",\n";
    ss << "    \"roi_padding\": " << static_cast<int>(config.compression.roi_padding) << "\n";
    ss << "  },\n";
    
    ss << "  \"prediction\": {\n";
    ss << "    \"max_prediction_time\": " << config.prediction.max_prediction_time << ",\n";
    ss << "    \"position_uncertainty\": " << config.prediction.position_uncertainty << ",\n";
    ss << "    \"velocity_uncertainty\": " << config.prediction.velocity_uncertainty << ",\n";
    ss << "    \"acceleration_uncertainty\": " << config.prediction.acceleration_uncertainty << ",\n";
    ss << "    \"min_confidence_threshold\": " << config.prediction.min_confidence_threshold << "\n";
    ss << "  }\n";
    
    ss << "}\n";
    
    return ss.str();
}

bool ConfigManager::importConfigFromJson(const std::string& json, ClientConfig& config) {
    // 简单JSON解析函数
    // 注意：这是一个非常简化的JSON解析器，仅用于此项目
    // 实际应用中应使用专业的JSON库
    
    try {
        // 使用默认值初始化配置
        config = ClientConfig();
        
        // 查找键值对
        auto findValue = [&json](const std::string& key) -> std::string {
            size_t pos = json.find("\"" + key + "\"");
            if (pos == std::string::npos) {
                return "";
            }
            
            pos = json.find(":", pos);
            if (pos == std::string::npos) {
                return "";
            }
            
            pos = json.find_first_not_of(" \t\r\n", pos + 1);
            if (pos == std::string::npos) {
                return "";
            }
            
            if (json[pos] == '"') {
                // 字符串值
                size_t end_pos = json.find("\"", pos + 1);
                if (end_pos == std::string::npos) {
                    return "";
                }
                return json.substr(pos + 1, end_pos - pos - 1);
            } else {
                // 数字或布尔值
                size_t end_pos = json.find_first_of(",}\r\n", pos);
                if (end_pos == std::string::npos) {
                    return "";
                }
                return json.substr(pos, end_pos - pos);
            }
        };
        
        // 解析值
        std::string value;
        
        value = findValue("server_ip");
        if (!value.empty()) {
            config.server_ip = value;
        }
        
        value = findValue("server_port");
        if (!value.empty()) {
            config.server_port = static_cast<uint16_t>(std::stoi(value));
        }
        
        value = findValue("game_id");
        if (!value.empty()) {
            config.game_id = static_cast<uint8_t>(std::stoi(value));
        }
        
        value = findValue("target_fps");
        if (!value.empty()) {
            config.target_fps = static_cast<uint32_t>(std::stoi(value));
        }
        
        value = findValue("screen_width");
        if (!value.empty()) {
            config.screen_width = static_cast<uint16_t>(std::stoi(value));
        }
        
        value = findValue("screen_height");
        if (!value.empty()) {
            config.screen_height = static_cast<uint16_t>(std::stoi(value));
        }
        
        value = findValue("auto_connect");
        if (!value.empty()) {
            config.auto_connect = (value == "true");
        }
        
        value = findValue("auto_start");
        if (!value.empty()) {
            config.auto_start = (value == "true");
        }
        
        value = findValue("enable_aim_assist");
        if (!value.empty()) {
            config.enable_aim_assist = (value == "true");
        }
        
        value = findValue("enable_esp");
        if (!value.empty()) {
            config.enable_esp = (value == "true");
        }
        
        value = findValue("enable_recoil_control");
        if (!value.empty()) {
            config.enable_recoil_control = (value == "true");
        }
        
        value = findValue("use_high_priority");
        if (!value.empty()) {
            config.use_high_priority = (value == "true");
        }
        
        // 压缩设置
        value = findValue("quality");
        if (!value.empty()) {
            config.compression.quality = static_cast<uint8_t>(std::stoi(value));
        }
        
        value = findValue("keyframe_interval");
        if (!value.empty()) {
            config.compression.keyframe_interval = static_cast<uint8_t>(std::stoi(value));
        }
        
        value = findValue("use_difference_encoding");
        if (!value.empty()) {
            config.compression.use_difference_encoding = (value == "true");
        }
        
        value = findValue("use_roi_encoding");
        if (!value.empty()) {
            config.compression.use_roi_encoding = (value == "true");
        }
        
        value = findValue("roi_padding");
        if (!value.empty()) {
            config.compression.roi_padding = static_cast<uint8_t>(std::stoi(value));
        }
        
        // 预测设置
        value = findValue("max_prediction_time");
        if (!value.empty()) {
            config.prediction.max_prediction_time = std::stof(value);
        }
        
        value = findValue("position_uncertainty");
        if (!value.empty()) {
            config.prediction.position_uncertainty = std::stof(value);
        }
        
        value = findValue("velocity_uncertainty");
        if (!value.empty()) {
            config.prediction.velocity_uncertainty = std::stof(value);
        }
        
        value = findValue("acceleration_uncertainty");
        if (!value.empty()) {
            config.prediction.acceleration_uncertainty = std::stof(value);
        }
        
        value = findValue("min_confidence_threshold");
        if (!value.empty()) {
            config.prediction.min_confidence_threshold = std::stof(value);
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "解析JSON失败: " << e.what() << std::endl;
        return false;
    }
}

} // namespace zero_latency