#pragma once
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <string>
#include "../../common/types.h"
#include "../../common/result.h"
#include "../../common/logger.h"
#include "../../common/event_bus.h"
#include "../../server/config.h"

namespace zero_latency {

struct GameState;
struct Detection;
struct BoundingBox;
struct Point2D;
struct Vector2D;

class IWeaponInfo {
public:
    virtual ~IWeaponInfo() = default;
    virtual int getId() const = 0;
    virtual std::string getName() const = 0;
    virtual float getRecoilFactor() const = 0;
    virtual float getDamage() const = 0;
    virtual float getFireRate() const = 0;
    virtual bool isAutomatic() const = 0;
    virtual float getPriority() const = 0;
};

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
    float recoil_factor_, damage_, fire_rate_, priority_;
    bool is_auto_;
};

class IClientState {
public:
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

class ClientState : public IClientState {
public:
    ClientState() : game_id_(0), current_weapon_id_(0), is_shooting_(false), shot_count_(0), last_shot_time_(0) {}
    ClientState(uint8_t gameId) : game_id_(gameId), current_weapon_id_(0), is_shooting_(false), shot_count_(0), last_shot_time_(0) {}
    
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
            shot_count_ = 0;
        }
    }
    
    void setShooting(bool isShooting) override { 
        bool was_shooting = is_shooting_;
        is_shooting_ = isShooting;
        if (!is_shooting_ && was_shooting) shot_count_ = 0;
    }
    
    void setShotCount(int shotCount) override { shot_count_ = shotCount; }
    void setLastShotTime(uint64_t timestamp) override { last_shot_time_ = timestamp; }
    void addTrackedObject(const Detection& detection) override { tracked_objects_[detection.track_id] = detection; }
    void removeTrackedObject(uint32_t trackId) override { tracked_objects_.erase(trackId); }
    void clearTrackedObjects() override { tracked_objects_.clear(); }

protected:
    uint8_t game_id_;
    int current_weapon_id_;
    bool is_shooting_;
    int shot_count_;
    uint64_t last_shot_time_;
    std::unordered_map<uint32_t, Detection> tracked_objects_;
};

class GameAdapterBase {
public:
    GameAdapterBase() : initialized_(false), next_track_id_(1) {}
    virtual ~GameAdapterBase() = default;
    
    virtual Result<void> initialize(const GameAdaptersConfig& config) {
        initialized_ = true;
        return Result<void>::ok();
    }
    
    virtual Result<GameState> processDetections(uint32_t clientId, const GameState& rawState, uint8_t gameId) = 0;
    virtual Result<int> calculateBestTarget(const std::vector<Detection>& detections) = 0;
    virtual Result<BoundingBox> predictMotion(const Detection& detection, uint64_t currentTime, uint64_t targetTime) = 0;
    virtual Result<Point2D> getAimPoint(const Detection& detection, int weaponId = -1) = 0;
    virtual Result<Vector2D> calculateRecoilCompensation(int weaponId, int shotCount, uint64_t timeDelta) = 0;
    
    virtual Result<void> registerClient(uint32_t clientId, uint8_t gameId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto client_state = std::make_shared<ClientState>(gameId);
        clients_[clientId] = client_state;
        LOG_INFO("Client #" + std::to_string(clientId) + " registered with GameAdapter");
        EventBus::getInstance().publishClientEvent(events::CLIENT_CONNECTED, clientId);
        return Result<void>::ok();
    }
    
    virtual Result<void> unregisterClient(uint32_t clientId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clients_.find(clientId);
        if (it == clients_.end())
            return Result<void>::error(ErrorCode::INVALID_ARGUMENT, "Client not found: " + std::to_string(clientId));
        
        clients_.erase(it);
        LOG_INFO("Client #" + std::to_string(clientId) + " unregistered from GameAdapter");
        EventBus::getInstance().publishClientEvent(events::CLIENT_DISCONNECTED, clientId);
        return Result<void>::ok();
    }
    
    virtual Result<void> updateClientWeapon(uint32_t clientId, int weaponId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto client_state = getOrCreateClientState(clientId);
        client_state->setCurrentWeaponId(weaponId);
        return Result<void>::ok();
    }
    
    virtual Result<void> updateClientShooting(uint32_t clientId, bool isShooting, int shotCount = -1) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto client_state = getOrCreateClientState(clientId);
        client_state->setShooting(isShooting);
        
        if (isShooting) {
            if (shotCount >= 0) client_state->setShotCount(shotCount);
            else if (!client_state->isShooting()) client_state->setShotCount(0);
            else client_state->setShotCount(client_state->getShotCount() + 1);
            
            client_state->setLastShotTime(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count());
        }
        
        return Result<void>::ok();
    }
    
    virtual std::shared_ptr<IClientState> getClientState(uint32_t clientId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clients_.find(clientId);
        if (it != clients_.end()) return it->second;
        return nullptr;
    }
    
    virtual std::shared_ptr<IWeaponInfo> getWeaponInfo(int weaponId) = 0;
    virtual std::vector<std::string> getSupportedGames() const = 0;
    
    virtual std::unordered_map<std::string, std::string> getStatus() const {
        std::unordered_map<std::string, std::string> status;
        std::lock_guard<std::mutex> lock(mutex_);
        status["initialized"] = initialized_ ? "true" : "false";
        status["client_count"] = std::to_string(clients_.size());
        status["next_track_id"] = std::to_string(next_track_id_.load());
        return status;
    }

protected:
    virtual std::shared_ptr<ClientState> getOrCreateClientState(uint32_t clientId) {
        auto it = clients_.find(clientId);
        if (it != clients_.end()) return std::static_pointer_cast<ClientState>(it->second);
        
        auto client_state = std::make_shared<ClientState>(0);
        clients_[clientId] = client_state;
        return client_state;
    }
    
    mutable std::mutex mutex_;
    bool initialized_;
    std::atomic<uint32_t> next_track_id_;
    std::unordered_map<uint32_t, std::shared_ptr<IClientState>> clients_;
};

class GameAdapterFactory {
public:
    virtual ~GameAdapterFactory() = default;
    virtual std::unique_ptr<GameAdapterBase> createAdapter() = 0;
    virtual std::string getName() const = 0;
    virtual std::vector<std::string> getSupportedGames() const = 0;
};

}