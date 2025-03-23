#pragma once

#include <string>
#include <vector>
#include "../common/types.h"

namespace zero_latency {

// 客户端配置管理类
class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();
    
    // 加载客户端配置
    bool loadClientConfig(const std::string& path, ClientConfig& config);
    
    // 保存客户端配置
    bool saveClientConfig(const std::string& path, const ClientConfig& config);
    
    // 创建默认配置
    void createDefaultConfig(const std::string& path);
    
    // 导出配置到JSON字符串
    std::string exportConfigToJson(const ClientConfig& config);
    
    // 从JSON字符串导入配置
    bool importConfigFromJson(const std::string& json, ClientConfig& config);
};

} // namespace zero_latency