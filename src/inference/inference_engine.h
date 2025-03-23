#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include "../common/result.h"
#include "../common/logger.h"
#include "../common/event_bus.h"
#include "../common/memory_pool.h"
#include "../server/config.h"

namespace zero_latency {

// 前向声明
struct GameState;

// 推理请求结构
struct InferenceRequest {
    uint32_t client_id;    // 客户端ID
    uint32_t frame_id;     // 帧ID
    uint64_t timestamp;    // 时间戳(毫秒)
    uint16_t width;        // 图像宽度
    uint16_t height;       // 图像高度
    std::vector<uint8_t> data;  // 图像数据(RGB格式)
    bool is_keyframe;      // 是否为关键帧
    
    // 构造函数
    InferenceRequest() = default;
    
    // 复制构造函数
    InferenceRequest(const InferenceRequest& other) = default;
    
    // 移动构造函数
    InferenceRequest(InferenceRequest&& other) noexcept = default;
    
    // 复制赋值运算符
    InferenceRequest& operator=(const InferenceRequest& other) = default;
    
    // 移动赋值运算符
    InferenceRequest& operator=(InferenceRequest&& other) noexcept = default;
};

// 推理结果回调
using InferenceCallback = std::function<void(uint32_t client_id, const GameState& state)>;

// 推理引擎接口
class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;
    
    // 初始化引擎
    virtual Result<void> initialize() = 0;
    
    // 关闭引擎
    virtual Result<void> shutdown() = 0;
    
    // 提交推理请求
    virtual Result<void> submitInference(const InferenceRequest& request) = 0;
    
    // 设置回调函数
    virtual void setCallback(InferenceCallback callback) = 0;
    
    // 获取当前队列大小
    virtual size_t getQueueSize() const = 0;
    
    // 获取引擎名称
    virtual std::string getName() const = 0;
    
    // 获取引擎状态信息
    virtual std::unordered_map<std::string, std::string> getStatus() const = 0;
};

// 推理引擎工厂接口
class IInferenceEngineFactory {
public:
    virtual ~IInferenceEngineFactory() = default;
    
    // 创建推理引擎
    virtual std::unique_ptr<IInferenceEngine> createEngine(const ServerConfig& config) = 0;
    
    // 获取工厂名称
    virtual std::string getName() const = 0;
};

// 推理引擎管理器 (单例)
class InferenceEngineManager {
public:
    static InferenceEngineManager& getInstance() {
        static InferenceEngineManager instance;
        return instance;
    }
    
    // 注册引擎工厂
    void registerFactory(std::shared_ptr<IInferenceEngineFactory> factory) {
        if (!factory) return;
        
        std::string name = factory->getName();
        factories_[name] = factory;
        LOG_INFO("Registered inference engine factory: " + name);
    }
    
    // 创建引擎
    std::unique_ptr<IInferenceEngine> createEngine(const std::string& name, const ServerConfig& config) {
        auto it = factories_.find(name);
        if (it == factories_.end()) {
            LOG_ERROR("Inference engine factory not found: " + name);
            return nullptr;
        }
        
        return it->second->createEngine(config);
    }
    
    // 获取可用引擎名称列表
    std::vector<std::string> getAvailableEngines() const {
        std::vector<std::string> result;
        for (const auto& [name, _] : factories_) {
            result.push_back(name);
        }
        return result;
    }
    
    // 检查引擎是否可用
    bool isEngineAvailable(const std::string& name) const {
        return factories_.find(name) != factories_.end();
    }

private:
    InferenceEngineManager() = default;
    ~InferenceEngineManager() = default;
    InferenceEngineManager(const InferenceEngineManager&) = delete;
    InferenceEngineManager& operator=(const InferenceEngineManager&) = delete;
    
    std::unordered_map<std::string, std::shared_ptr<IInferenceEngineFactory>> factories_;
};

// 便捷的工厂注册宏
#define REGISTER_INFERENCE_ENGINE(factory_class) \
    namespace { \
        struct Register##factory_class { \
            Register##factory_class() { \
                auto factory = std::make_shared<factory_class>(); \
                zero_latency::InferenceEngineManager::getInstance().registerFactory(factory); \
            } \
        }; \
        static Register##factory_class register_##factory_class; \
    }

} // namespace zero_latency