#include "cs16_game_adapter.h"

namespace zero_latency {

CS16GameAdapter::CS16GameAdapter() : GameAdapterBase(), aim_target_offset_y_(-0.15f), head_size_factor_(0.7f) {
    random_generator_ = std::mt19937(std::random_device{}());
}

Result<void> CS16GameAdapter::initialize(const GameAdaptersConfig& config) {
    auto result = GameAdapterBase::initialize(config);
    if (result.hasError()) {
        return result;
    }
    
    auto it = config.games.find("cs16");
    if (it == config.games.end() || !it->second.enabled) {
        return Result<void>::error(ErrorCode::INVALID_ARGUMENT, "CS16 game adapter configuration not found or disabled");
    }
    
    const auto& cs16_config = it->second;
    
    aim_target_offset_y_ = cs16_config.aim_target_offset_y;
    head_size_factor_ = cs16_config.head_size_factor;
    
    loadWeaponData(cs16_config.weapons);
    
    LOG_INFO("CS16GameAdapter initialized successfully");
    
    Event event(events::SYSTEM_STARTUP);
    event.setSource("CS16GameAdapter");
    publishEvent(event);
    
    return Result<void>::ok();
}

Result<GameState> CS16GameAdapter::processDetections(uint32_t clientId, const GameState& rawState, uint8_t gameId) {
    if (!initialized_) {
        return Result<GameState>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
    }
    
    if (gameId != static_cast<uint8_t>(GameType::CS_1_6)) {
        return Result<GameState>::error(ErrorCode::INVALID_ARGUMENT, "Unsupported game ID for CS16GameAdapter");
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto client_state = getOrCreateClientState(clientId);
    
    GameState processed_state = processCS16Detections(rawState);
    
    for (const auto& detection : processed_state.detections) {
        client_state->addTrackedObject(detection);
    }
    
    uint64_t current_time = processed_state.timestamp;
    std::vector<uint32_t> expired_tracks;
    
    for (const auto& [track_id, detection] : client_state->getTrackedObjects()) {
        if (current_time - detection.timestamp > 100) {
            expired_tracks.push_back(track_id);
        }
    }
    
    for (uint32_t track_id : expired_tracks) {
        client_state->removeTrackedObject(track_id);
    }
    
    return Result<GameState>::ok(processed_state);
}

Result<int> CS16GameAdapter::calculateBestTarget(const std::vector<Detection>& detections) {
    if (!initialized_) {
        return Result<int>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
    }
    
    if (detections.empty()) {
        return Result<int>::ok(-1);
    }
    
    float center_x = 0.5f;
    float center_y = 0.5f;
    
    int best_index = -1;
    float best_distance = std::numeric_limits<float>::max();
    
    for (size_t i = 0; i < detections.size(); i++) {
        const auto& det = detections[i];
        
        if (det.class_id != constants::cs16::CLASS_T && 
            det.class_id != constants::cs16::CLASS_HEAD) {
            continue;
        }
        
        float dx = det.box.x - center_x;
        float dy = det.box.y - center_y;
        float distance = std::sqrt(dx * dx + dy * dy);
        
        if (det.class_id == constants::cs16::CLASS_HEAD) {
            distance *= 0.5f;
        }
        
        if (distance < best_distance) {
            best_distance = distance;
            best_index = i;
        }
    }
    
    return Result<int>::ok(best_index);
}

Result<BoundingBox> CS16GameAdapter::predictMotion(const Detection& detection, uint64_t currentTime, uint64_t targetTime) {
    if (!initialized_) {
        return Result<BoundingBox>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
    }
    
    BoundingBox predicted_box = detection.box;
    
    if (targetTime <= currentTime || targetTime - currentTime > constants::dual_engine::MAX_PREDICTION_FRAMES * 16) {
        return Result<BoundingBox>::ok(predicted_box);
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [client_id, state] : clients_) {
        const auto& tracked_objects = state->getTrackedObjects();
        auto it = tracked_objects.find(detection.track_id);
        
        if (it != tracked_objects.end()) {
            const Detection& previous = it->second;
            
            float time_diff_ms = currentTime - previous.timestamp;
            if (time_diff_ms > 0) {
                float vx = (detection.box.x - previous.box.x) / time_diff_ms;
                float vy = (detection.box.y - previous.box.y) / time_diff_ms;
                
                float future_time_ms = targetTime - currentTime;
                predicted_box.x += vx * future_time_ms;
                predicted_box.y += vy * future_time_ms;
            }
            break;
        }
    }
    
    return Result<BoundingBox>::ok(predicted_box);
}

Result<Point2D> CS16GameAdapter::getAimPoint(const Detection& detection, int weaponId) {
    if (!initialized_) {
        return Result<Point2D>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    Point2D aim_point = {detection.box.x, detection.box.y};
    
    switch (detection.class_id) {
        case constants::cs16::CLASS_T:
        case constants::cs16::CLASS_CT:
            aim_point.y = detection.box.y - detection.box.height * 0.2f;
            break;
            
        case constants::cs16::CLASS_HEAD:
            break;
            
        default:
            break;
    }
    
    if (weaponId > 0) {
        auto weapon_info = getWeaponInfo(weaponId);
        if (weapon_info) {
            if (weapon_info->getName() == "AWP" || weapon_info->getName() == "Scout") {
                if (detection.class_id != constants::cs16::CLASS_HEAD) {
                    aim_point.y = detection.box.y - detection.box.height * 0.3f;
                }
            }
        }
    }
    
    return Result<Point2D>::ok(aim_point);
}

Result<Vector2D> CS16GameAdapter::calculateRecoilCompensation(int weaponId, int shotCount, uint64_t timeDelta) {
    if (!initialized_) {
        return Result<Vector2D>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
    }
    
    Vector2D compensation = {0.0f, 0.0f};
    
    auto weapon_info = getWeaponInfo(weaponId);
    if (!weapon_info) {
        return Result<Vector2D>::ok(compensation);
    }
    
    if (weapon_info->isAutomatic() && shotCount > 0) {
        float vertical_recoil = weapon_info->getRecoilFactor() * std::min(shotCount, 10) * 0.01f;
        
        float horizontal_recoil = 0.0f;
        if (shotCount > 3) {
            int pattern_position = shotCount % 8;
            if (pattern_position < 4) {
                horizontal_recoil = weapon_info->getRecoilFactor() * 0.005f * pattern_position;
            } else {
                horizontal_recoil = weapon_info->getRecoilFactor() * 0.005f * (8 - pattern_position);
            }
            
            if (shotCount % 2 == 0) {
                horizontal_recoil = -horizontal_recoil;
            }
        }
        
        compensation.x = horizontal_recoil;
        compensation.y = vertical_recoil;
    }
    
    return Result<Vector2D>::ok(compensation);
}

std::shared_ptr<IWeaponInfo> CS16GameAdapter::getWeaponInfo(int weaponId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = weapons_.find(weaponId);
    if (it != weapons_.end()) {
        return it->second;
    }
    
    return nullptr;
}

std::vector<std::string> CS16GameAdapter::getSupportedGames() const {
    return {"cs16"};
}

std::unordered_map<std::string, std::string> CS16GameAdapter::getStatus() const {
    auto status = GameAdapterBase::getStatus();
    
    status["game"] = "Counter-Strike 1.6";
    status["weapon_count"] = std::to_string(weapons_.size());
    
    return status;
}

GameState CS16GameAdapter::processCS16Detections(const GameState& rawState) {
    GameState processed_state = rawState;
    
    for (auto& detection : processed_state.detections) {
        if (detection.track_id == 0) {
            detection.track_id = next_track_id_++;
        }
        
        switch (detection.class_id) {
            case constants::cs16::CLASS_HEAD:
                detection.box.height *= head_size_factor_;
                break;
            
            default:
                break;
        }
    }
    
    return processed_state;
}

void CS16GameAdapter::loadWeaponData(const std::unordered_map<std::string, GameAdaptersConfig::WeaponConfig>& weaponConfigs) {
    weapons_.clear();
    
    weapons_[1] = std::make_shared<WeaponInfo>(
        1, "AK47", constants::cs16::WeaponRecoil::AK47, 36.0f, 0.1f, true, 1.0f
    );
    
    weapons_[2] = std::make_shared<WeaponInfo>(
        2, "M4A1", constants::cs16::WeaponRecoil::M4A1, 33.0f, 0.09f, true, 1.0f
    );
    
    weapons_[3] = std::make_shared<WeaponInfo>(
        3, "AWP", constants::cs16::WeaponRecoil::AWP, 115.0f, 1.5f, false, 1.5f
    );
    
    weapons_[4] = std::make_shared<WeaponInfo>(
        4, "Deagle", constants::cs16::WeaponRecoil::DEAGLE, 54.0f, 0.4f, false, 1.2f
    );
    
    int weaponId = 5;
    
    for (const auto& [name, config] : weaponConfigs) {
        bool found = false;
        for (const auto& [id, weapon] : weapons_) {
            if (weapon->getName() == name) {
                weapons_[id] = std::make_shared<WeaponInfo>(
                    id, name, config.recoil_factor, 0.0f, 0.0f, true, config.priority
                );
                found = true;
                break;
            }
        }
        
        if (!found) {
            weapons_[weaponId] = std::make_shared<WeaponInfo>(
                weaponId, name, config.recoil_factor, 0.0f, 0.0f, true, config.priority
            );
            weaponId++;
        }
    }
    
    LOG_INFO("Loaded " + std::to_string(weapons_.size()) + " weapons for CS16GameAdapter");
}

REGISTER_GAME_ADAPTER(CS16GameAdapterFactory)

} // namespace zero_latency