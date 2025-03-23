#include "config.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace zero_latency {

ConfigManager::ConfigManager() {
}

ConfigManager::~ConfigManager() {
}

bool ConfigManager::loadServerConfig(const std::string& path, ServerConfig& config) {
    // 检查文件是否存在
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0) {
        std::cerr << "配置文件不存在: " << path << std::endl;
        
        // 创建默认配置
        createDefaultServerConfig(path);
        
        // 返回默认配置
        config = ServerConfig();
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
            if (key == "model_path") {
                config.model_path = value;
            } else if (key == "port") {
                config.port = static_cast<uint16_t>(std::stoi(value));
            } else if (key == "web_port") {
                config.web_port = static_cast<uint16_t>(std::stoi(value));
            } else if (key == "max_clients") {
                config.max_clients = static_cast<uint8_t>(std::stoi(value));
            } else if (key == "target_fps") {
                config.target_fps = static_cast<uint32_t>(std::stoi(value));
            } else if (key == "confidence_threshold") {
                config.confidence_threshold = std::stof(value);
            } else if (key == "nms_threshold") {
                config.nms_threshold = std::stof(value);
            } else if (key == "max_queue_size") {
                config.max_queue_size = static_cast<size_t>(std::stoi(value));
            } else if (key == "use_cpu_affinity") {
                config.use_cpu_affinity = (value == "true" || value == "1");
            } else if (key == "cpu_core_id") {
                config.cpu_core_id = std::stoi(value);
            } else if (key == "use_high_priority") {
                config.use_high_priority = (value == "true" || value == "1");
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

bool ConfigManager::saveServerConfig(const std::string& path, const ServerConfig& config) {
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
        file << "# 零延迟YOLO FPS云辅助系统服务器配置文件\n\n";
        
        file << "# 模型路径\n";
        file << "model_path=" << config.model_path << "\n\n";
        
        file << "# 网络设置\n";
        file << "port=" << config.port << "\n";
        file << "web_port=" << config.web_port << "\n";
        file << "max_clients=" << static_cast<int>(config.max_clients) << "\n\n";
        
        file << "# 性能设置\n";
        file << "target_fps=" << config.target_fps << "\n";
        file << "max_queue_size=" << config.max_queue_size << "\n\n";
        
        file << "# 检测设置\n";
        file << "confidence_threshold=" << config.confidence_threshold << "\n";
        file << "nms_threshold=" << config.nms_threshold << "\n\n";
        
        file << "# 系统优化设置\n";
        file << "use_cpu_affinity=" << (config.use_cpu_affinity ? "true" : "false") << "\n";
        file << "cpu_core_id=" << config.cpu_core_id << "\n";
        file << "use_high_priority=" << (config.use_high_priority ? "true" : "false") << "\n";
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "写入配置文件失败: " << e.what() << std::endl;
        file.close();
        return false;
    }
}

bool ConfigManager::loadClientConfig(const std::string& path, ClientConfig& config) {
    // 检查文件是否存在
    struct stat buffer;
    if (stat(path.c_str(), &buffer) != 0) {
        std::cerr << "配置文件不存在: " << path << std::endl;
        
        // 创建默认配置
        createDefaultClientConfig(path);
        
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

void ConfigManager::createDefaultServerConfig(const std::string& path) {
    // 创建默认配置
    ServerConfig config;
    
    // 保存默认配置
    saveServerConfig(path, config);
}

void ConfigManager::createDefaultClientConfig(const std::string& path) {
    // 创建默认配置
    ClientConfig config;
    
    // 保存默认配置
    saveClientConfig(path, config);
}

} // namespace zero_latency