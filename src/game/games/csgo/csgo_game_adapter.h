#pragma once

#include <random>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <functional>
#include "../../base/game_adapter_base.h"
#include "../../../common/memory_pool.h"
#include "../../../common/constants.h"

namespace zero_latency {

class CSGOGameAdapter : public GameAdapterBase {
public:
    CSGOGameAdapter();
    ~CSGOGameAdapter() override = default;
    
    Result<void> initialize(const GameAdaptersConfig& config) override;
    Result<GameState> processDetections(uint32_t clientId, const GameState& rawState, uint8_t gameId) override;
    Result<int> calculateBestTarget(const std::vector<Detection>& detections) override;
    Result<BoundingBox> predictMotion(const Detection& detection, uint64_t currentTime, uint64_t targetTime) override;
    Result<Point2D> getAimPoint(const Detection& detection, int weaponId = -1) override;
    Result<Vector2D> calculateRecoilCompensation(int weaponId, int shotCount, uint64_t timeDelta) override;
    std::shared_ptr<IWeaponInfo> getWeaponInfo(int weaponId) override;
    std::vector<std::string> getSupportedGames() const override;
    std::unordered_map<std::string, std::string> getStatus() const override;

private:
    GameState processCSGODetections(const GameState& rawState);
    void loadWeaponData(const std::unordered_map<std::string, GameAdaptersConfig::WeaponConfig>& weaponConfigs);

private:
    std::mt19937 random_generator_;
    std::unordered_map<int, std::shared_ptr<IWeaponInfo>> weapons_;
    float aim_target_offset_y_;
    float head_size_factor_;
};

class CSGOGameAdapterFactory : public GameAdapterFactory {
public:
    std::unique_ptr<GameAdapterBase> createAdapter() override {
        return std::make_unique<CSGOGameAdapter>();
    }
    
    std::string getName() const override {
        return "csgo";
    }
    
    std::vector<std::string> getSupportedGames() const override {
        return {"csgo"};
    }
};

} // namespace zero_latency