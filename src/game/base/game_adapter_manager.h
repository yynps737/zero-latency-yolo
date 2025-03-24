#pragma once
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "game_adapter_base.h"
#include "../../common/logger.h"

namespace zero_latency {

class GameAdapterManager {
public:
    static GameAdapterManager& getInstance() {
        static GameAdapterManager instance;
        return instance;
    }
    
    void registerFactory(std::shared_ptr<GameAdapterFactory> factory) {
        if (!factory) return;
        std::string name = factory->getName();
        factories_[name] = factory;
        LOG_INFO("Registered game adapter factory: " + name);
    }
    
    std::unique_ptr<GameAdapterBase> createAdapter(const std::string& name) {
        auto it = factories_.find(name);
        if (it == factories_.end()) {
            LOG_ERROR("Game adapter factory not found: " + name);
            return nullptr;
        }
        return it->second->createAdapter();
    }
    
    std::vector<std::string> getAvailableAdapters() const {
        std::vector<std::string> result;
        for (const auto& [name, _] : factories_) result.push_back(name);
        return result;
    }
    
    bool isAdapterAvailable(const std::string& name) const {
        return factories_.find(name) != factories_.end();
    }
    
    std::unique_ptr<GameAdapterBase> createAdapterForGame(uint8_t gameId) {
        for (const auto& [name, factory] : factories_) {
            auto supportedGames = factory->getSupportedGames();
            bool supported = false;
            for (const auto& game : supportedGames) {
                if (game == "cs16" && gameId == static_cast<uint8_t>(GameType::CS_1_6)) {
                    supported = true;
                    break;
                } else if (game == "csgo" && gameId == static_cast<uint8_t>(GameType::CSGO)) {
                    supported = true;
                    break;
                } else if (game == "valorant" && gameId == static_cast<uint8_t>(GameType::VALORANT)) {
                    supported = true;
                    break;
                }
            }
            
            if (supported) return factory->createAdapter();
        }
        
        LOG_ERROR("No adapter available for game ID: " + std::to_string(gameId));
        return nullptr;
    }

private:
    GameAdapterManager() = default;
    ~GameAdapterManager() = default;
    GameAdapterManager(const GameAdapterManager&) = delete;
    GameAdapterManager& operator=(const GameAdapterManager&) = delete;
    
    std::unordered_map<std::string, std::shared_ptr<GameAdapterFactory>> factories_;
};

#define REGISTER_GAME_ADAPTER(factory_class) \
    namespace { \
        struct RegisterGameAdapter##factory_class { \
            RegisterGameAdapter##factory_class() { \
                auto factory = std::make_shared<factory_class>(); \
                zero_latency::GameAdapterManager::getInstance().registerFactory(factory); \
            } \
        }; \
        static RegisterGameAdapter##factory_class register_game_adapter_##factory_class; \
    }

}