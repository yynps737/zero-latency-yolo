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

struct GameState;

struct InferenceRequest {
    uint32_t client_id;
    uint32_t frame_id;
    uint64_t timestamp;
    uint16_t width, height;
    std::vector<uint8_t> data;
    bool is_keyframe;
    
    InferenceRequest() = default;
    InferenceRequest(const InferenceRequest& other) = default;
    InferenceRequest(InferenceRequest&& other) noexcept = default;
    InferenceRequest& operator=(const InferenceRequest& other) = default;
    InferenceRequest& operator=(InferenceRequest&& other) noexcept = default;
};

using InferenceCallback = std::function<void(uint32_t client_id, const GameState& state)>;

class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;
    virtual Result<void> initialize() = 0;
    virtual Result<void> shutdown() = 0;
    virtual Result<void> submitInference(const InferenceRequest& request) = 0;
    virtual void setCallback(InferenceCallback callback) = 0;
    virtual size_t getQueueSize() const = 0;
    virtual std::string getName() const = 0;
    virtual std::unordered_map<std::string, std::string> getStatus() const = 0;
};

class IInferenceEngineFactory {
public:
    virtual ~IInferenceEngineFactory() = default;
    virtual std::unique_ptr<IInferenceEngine> createEngine(const ServerConfig& config) = 0;
    virtual std::string getName() const = 0;
};

class InferenceEngineManager {
public:
    static InferenceEngineManager& getInstance() {
        static InferenceEngineManager instance;
        return instance;
    }
    
    void registerFactory(std::shared_ptr<IInferenceEngineFactory> factory) {
        if (!factory) return;
        std::string name = factory->getName();
        factories_[name] = factory;
        LOG_INFO("Registered inference engine factory: " + name);
    }
    
    std::unique_ptr<IInferenceEngine> createEngine(const std::string& name, const ServerConfig& config) {
        auto it = factories_.find(name);
        if (it == factories_.end()) {
            LOG_ERROR("Inference engine factory not found: " + name);
            return nullptr;
        }
        return it->second->createEngine(config);
    }
    
    std::vector<std::string> getAvailableEngines() const {
        std::vector<std::string> result;
        for (const auto& [name, _] : factories_) result.push_back(name);
        return result;
    }
    
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

}