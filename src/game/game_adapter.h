#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <string>
#include "../common/types.h"
#include "../common/result.h"
#include "../common/logger.h"
#include "../common/event_bus.h"
#include "../server/config.h"

namespace zero_latency {

// 前向声明
struct GameState;
struct Detection;
struct BoundingBox;
struct Point2D;
struct Vector2D;

// 武器信息接口
struct IWeaponInfo {
    virtual ~IWeaponInfo() = default;
    
    virtual int getId() const = 0;
    virtual std::string getName() const = 0;
    virtual float getRecoilFactor() const = 0;
    virtual float getDamage() const = 0;
    virtual float getFireRate() const = 0;
    virtual bool isAutomatic() const = 0;
    virtual float getPriority() const = 0;
};

// 武器信息实现
class WeaponInfo : public IWeaponInfo {
public:
    WeaponInfo(int id, std::string name, float recoilFactor, float damage, 
               float fireRate, bool isAuto, float priority)
        : id_(id), name_(std::move(name)), recoil_factor_(recoilFactor), 
          damage_(damage), fire_rate_(fireRate), is_auto_(isAuto), priority_(priority) {}
    
    int getId() const override { return id_; }
    std::string getName() const override { return name_; }
    float getRecoilFactor() const override { return recoil_factor_; }
    float getDamage() const override { return damage_; }
    float getFireRate() const override { return fire_rate_; }
    bool isAutomatic() const override { return is_auto_; }
    float getPriority() const override { return priority_; }

private:
    int id_;
    std::string name_;
    float recoil_factor_;
    float damage_;
    float fire_rate_;
    bool is_auto_;
    float priority_;
};

// 客户端状态接口
struct IClientState {
    virtual ~IClientState() = default;
    
    virtual uint8_t getGameId() const = 0;
    virtual int getCurrentWeaponId() const = 0;
    virtual bool isShooting() const = 0;
    virtual int getShotCount() const = 0;
    virtual uint64_t getLastShotTime() const = 0;
    virtual const std::unordered_map<uint32_t, Detection>& getTrackedObjects() const = 0;
    
    virtual void setGameId(uint8_t gameId) = 0;
    virtual void setCurrentWeaponId(int weaponId) = 0;
    virtual void setShooting(bool isShooting) = 0;
    virtual void setShotCount(int shotCount) = 0;
    virtual void setLastShotTime(uint64_t timestamp) = 0;
    virtual void addTrackedObject(const Detection& detection) = 0;
    virtual void removeTrackedObject(uint32_t trackId) = 0;
    virtual void clearTrackedObjects() = 0;
};

// 客户端状态实现
class ClientState : public IClientState {
public:
    ClientState() 
        : game_id_(0), current_weapon_id_(0), is_shooting_(false), shot_count_(0), last_shot_time_(0) {}
    
    ClientState(uint8_t gameId)
        : game_id_(gameId), current_weapon_id_(0), is_shooting_(false), shot_count_(0), last_shot_time_(0) {}
    
    uint8_t getGameId() const override { return game_id_; }
    int getCurrentWeaponId() const override { return current_weapon_id_; }
    bool isShooting() const override { return is_shooting_; }
    int getShotCount() const override { return shot_count_; }
    uint64_t getLastShotTime() const override { return last_shot_time_; }
    const std::unordered_map<uint32_t, Detection>& getTrackedObjects() const override { return tracked_objects_; }
    
    void setGameId(uint8_t gameId) override { game_id_ = gameId; }
    void setCurrentWeaponId(int weaponId) override { 
        if (current_weapon_id_ != weaponId) {
            current_weapon_id_ = weaponId;
            shot_count_ = 0; // 武器改变，重置射击计数
        }
    }
    
    void setShooting(bool isShooting) override { 
        bool was_shooting = is_shooting_;
        is_shooting_ = isShooting;
        
        // 停止射击时重置计数
        if (!is_shooting_ && was_shooting) {
            shot_count_ = 0;
        }
    }
    
    void setShotCount(int shotCount) override { shot_count_ = shotCount; }
    
    void setLastShotTime(uint64_t timestamp) override { last_shot_time_ = timestamp; }
    
    void addTrackedObject(const Detection& detection) override {
        tracked_objects_[detection.track_id] = detection;
    }
    
    void removeTrackedObject(uint32_t trackId) override {
        tracked_objects_.erase(trackId);
    }
    
    void clearTrackedObjects() override {
        tracked_objects_.clear();
    }

private:
    uint8_t game_id_;
    int current_weapon_id_;
    bool is_shooting_;
    int shot_count_;
    uint64_t last_shot_time_;
    std::unordered_map<uint32_t, Detection> tracked_objects_;
};

// 游戏适配器接口
class IGameAdapter {
public:
    virtual ~IGameAdapter() = default;
    
    // 初始化适配器
    virtual Result<void> initialize(const GameAdaptersConfig& config) = 0;
    
    // 处理检测结果
    virtual Result<GameState> processDetections(uint32_t clientId, const GameState& rawState, uint8_t gameId) = 0;
    
    // 计算最优目标
    virtual Result<int> calculateBestTarget(const std::vector<Detection>& detections) = 0;
    
    // 预测目标运动
    virtual Result<BoundingBox> predictMotion(const Detection& detection, uint64_t currentTime, uint64_t targetTime) = 0;
    
    // 获取瞄准点
    virtual Result<Point2D> getAimPoint(const Detection& detection, int weaponId = -1) = 0;
    
    // 计算后座力补偿
    virtual Result<Vector2D> calculateRecoilCompensation(int weaponId, int shotCount, uint64_t timeDelta) = 0;
    
    // 注册客户端
    virtual Result<void> registerClient(uint32_t clientId, uint8_t gameId) = 0;
    
    // 注销客户端
    virtual Result<void> unregisterClient(uint32_t clientId) = 0;
    
    // 更新客户端武器
    virtual Result<void> updateClientWeapon(uint32_t clientId, int weaponId) = 0;
    
    // 更新客户端射击状态
    virtual Result<void> updateClientShooting(uint32_t clientId, bool isShooting, int shotCount = -1) = 0;
    
    // 获取客户端状态
    virtual std::shared_ptr<IClientState> getClientState(uint32_t clientId) = 0;
    
    // 获取武器信息
    virtual std::shared_ptr<IWeaponInfo> getWeaponInfo(int weaponId) = 0;
    
    // 获取支持的游戏列表
    virtual std::vector<std::string> getSupportedGames() const = 0;
    
    // 获取适配器状态
    virtual std::unordered_map<std::string, std::string> getStatus() const = 0;
};

// 游戏适配器工厂接口
class IGameAdapterFactory {
public:
    virtual ~IGameAdapterFactory() = default;
    
    // 创建游戏适配器
    virtual std::unique_ptr<IGameAdapter> createAdapter() = 0;
    
    // 获取工厂名称
    virtual std::string getName() const = 0;
    
    // 获取支持的游戏列表
    virtual std::vector<std::string> getSupportedGames() const = 0;
};

// 游戏适配器管理器 (单例)
class GameAdapterManager {
public:
    static GameAdapterManager& getInstance() {
        static GameAdapterManager instance;
        return instance;
    }
    
    // 注册适配器工厂
    void registerFactory(std::shared_ptr<IGameAdapterFactory> factory) {
        if (!factory) return;
        
        std::string name = factory->getName();
        factories_[name] = factory;
        LOG_INFO("Registered game adapter factory: " + name);
    }
    
    // 创建适配器
    std::unique_ptr<IGameAdapter> createAdapter(const std::string& name) {
        auto it = factories_.find(name);
        if (it == factories_.end()) {
            LOG_ERROR("Game adapter factory not found: " + name);
            return nullptr;
        }
        
        return it->second->createAdapter();
    }
    
    // 获取可用适配器名称列表
    std::vector<std::string> getAvailableAdapters() const {
        std::vector<std::string> result;
        for (const auto& [name, _] : factories_) {
            result.push_back(name);
        }
        return result;
    }
    
    // 检查适配器是否可用
    bool isAdapterAvailable(const std::string& name) const {
        return factories_.find(name) != factories_.end();
    }
    
    // 为游戏ID获取适配器
    std::unique_ptr<IGameAdapter> createAdapterForGame(uint8_t gameId) {
        // 为每种游戏类型查找适配器
        for (const auto& [name, factory] : factories_) {
            auto supportedGames = factory->getSupportedGames();
            
            // 检查游戏ID是否支持
            bool supported = false;
            for (const auto& game : supportedGames) {
                if (game == "cs16" && gameId == static_cast<uint8_t>(GameType::CS_1_6)) {
                    supported = true;
                    break;
                } else if (game == "csgo" && gameId == static_cast<uint8_t>(GameType::CSGO)) {
                    supported = true;
                    break;
                }
            }
            
            if (supported) {
                return factory->createAdapter();
            }
        }
        
        LOG_ERROR("No adapter available for game ID: " + std::to_string(gameId));
        return nullptr;
    }

private:
    GameAdapterManager() = default;
    ~GameAdapterManager() = default;
    GameAdapterManager(const GameAdapterManager&) = delete;
    GameAdapterManager& operator=(const GameAdapterManager&) = delete;
    
    std::unordered_map<std::string, std::shared_ptr<IGameAdapterFactory>> factories_;
};

// 便捷的工厂注册宏
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

} // namespace zero_latency