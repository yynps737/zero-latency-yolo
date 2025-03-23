#pragma once

#include "game_adapter.h"
#include <random>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <functional>
#include "../common/memory_pool.h"

namespace zero_latency {

// CS 1.6 游戏适配器实现
class CS16GameAdapter : public IGameAdapter {
public:
    CS16GameAdapter() : initialized_(false), next_track_id_(1) {}
    
    ~CS16GameAdapter() override = default;
    
    // 初始化适配器
    Result<void> initialize(const GameAdaptersConfig& config) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 检查CS16配置是否存在
        auto it = config.games.find("cs16");
        if (it == config.games.end() || !it->second.enabled) {
            return Result<void>::error(ErrorCode::INVALID_ARGUMENT, "CS16 game adapter configuration not found or disabled");
        }
        
        // 获取CS16配置
        const auto& cs16_config = it->second;
        
        // 初始化配置
        aim_target_offset_y_ = cs16_config.aim_target_offset_y;
        head_size_factor_ = cs16_config.head_size_factor;
        
        // 加载武器数据
        loadWeaponData(cs16_config.weapons);
        
        // 初始化随机数生成器
        random_generator_ = std::mt19937(std::random_device{}());
        
        initialized_ = true;
        LOG_INFO("CS16GameAdapter initialized successfully");
        
        // 发布初始化事件
        Event event(events::SYSTEM_STARTUP);
        event.setSource("CS16GameAdapter");
        publishEvent(event);
        
        return Result<void>::ok();
    }
    
    // 处理检测结果
    Result<GameState> processDetections(uint32_t clientId, const GameState& rawState, uint8_t gameId) override {
        if (!initialized_) {
            return Result<GameState>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
        }
        
        if (gameId != static_cast<uint8_t>(GameType::CS_1_6)) {
            return Result<GameState>::error(ErrorCode::INVALID_ARGUMENT, "Unsupported game ID for CS16GameAdapter");
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 获取客户端状态，如果不存在则创建
        auto client_state = getOrCreateClientState(clientId);
        
        // 处理特定于CS 1.6的检测结果
        GameState processed_state = processCS16Detections(rawState);
        
        // 更新客户端跟踪对象
        for (const auto& detection : processed_state.detections) {
            client_state->addTrackedObject(detection);
        }
        
        // 移除过期的跟踪对象(超过100ms未更新)
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
    
    // 计算最优目标
    Result<int> calculateBestTarget(const std::vector<Detection>& detections) override {
        if (!initialized_) {
            return Result<int>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
        }
        
        if (detections.empty()) {
            return Result<int>::ok(-1);
        }
        
        // 计算屏幕中心
        float center_x = 0.5f;
        float center_y = 0.5f;
        
        // 寻找最接近中心的敌人
        int best_index = -1;
        float best_distance = std::numeric_limits<float>::max();
        
        for (size_t i = 0; i < detections.size(); i++) {
            const auto& det = detections[i];
            
            // 只考虑敌方玩家或头部
            if (det.class_id != constants::cs16::CLASS_T && 
                det.class_id != constants::cs16::CLASS_HEAD) {
                continue;
            }
            
            // 计算到中心的距离
            float dx = det.box.x - center_x;
            float dy = det.box.y - center_y;
            float distance = std::sqrt(dx * dx + dy * dy);
            
            // 优先选择头部，其次是距离最近的
            if (det.class_id == constants::cs16::CLASS_HEAD) {
                distance *= 0.5f; // 给头部更高优先级
            }
            
            if (distance < best_distance) {
                best_distance = distance;
                best_index = i;
            }
        }
        
        return Result<int>::ok(best_index);
    }
    
    // 预测目标运动
    Result<BoundingBox> predictMotion(const Detection& detection, uint64_t currentTime, uint64_t targetTime) override {
        if (!initialized_) {
            return Result<BoundingBox>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
        }
        
        BoundingBox predicted_box = detection.box;
        
        // 检查时间有效性
        if (targetTime <= currentTime || targetTime - currentTime > constants::dual_engine::MAX_PREDICTION_FRAMES * 16) {
            return Result<BoundingBox>::ok(predicted_box);
        }
        
        // 简单的线性预测 - 在实际实现中可以替换为更复杂的卡尔曼滤波器
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 获取所有客户端
        for (auto& [client_id, state] : clients_) {
            const auto& tracked_objects = state->getTrackedObjects();
            auto it = tracked_objects.find(detection.track_id);
            
            if (it != tracked_objects.end()) {
                // 获取历史检测
                const Detection& previous = it->second;
                
                // 计算时间差
                float time_diff_ms = currentTime - previous.timestamp;
                if (time_diff_ms > 0) {
                    // 计算速度
                    float vx = (detection.box.x - previous.box.x) / time_diff_ms;
                    float vy = (detection.box.y - previous.box.y) / time_diff_ms;
                    
                    // 预测未来位置
                    float future_time_ms = targetTime - currentTime;
                    predicted_box.x += vx * future_time_ms;
                    predicted_box.y += vy * future_time_ms;
                }
                break;
            }
        }
        
        return Result<BoundingBox>::ok(predicted_box);
    }
    
    // 获取瞄准点
    Result<Point2D> getAimPoint(const Detection& detection, int weaponId) override {
        if (!initialized_) {
            return Result<Point2D>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 获取CS 1.6瞄准点
        Point2D aim_point = {detection.box.x, detection.box.y};
        
        // 根据检测类别确定瞄准点
        switch (detection.class_id) {
            case constants::cs16::CLASS_T:
            case constants::cs16::CLASS_CT:
                // 瞄准身体上部
                aim_point.y = detection.box.y - detection.box.height * 0.2f;
                break;
                
            case constants::cs16::CLASS_HEAD:
                // 瞄准头部中心
                // 对于CS 1.6的头部检测，已经比较精确，不需要额外偏移
                break;
                
            default:
                // 其他类别瞄准中心
                break;
        }
        
        // 根据武器调整瞄准点
        if (weaponId > 0) {
            auto weapon_info = getWeaponInfo(weaponId);
            if (weapon_info) {
                // 狙击枪瞄准头部，其他武器可能需要考虑后座力
                if (weapon_info->getName() == "AWP" || weapon_info->getName() == "Scout") {
                    // 精确瞄准头部中心
                    if (detection.class_id != constants::cs16::CLASS_HEAD) {
                        // 如果没有检测到头部，尝试根据身体估计头部位置
                        aim_point.y = detection.box.y - detection.box.height * 0.3f;
                    }
                }
            }
        }
        
        return Result<Point2D>::ok(aim_point);
    }
    
    // 计算后座力补偿
    Result<Vector2D> calculateRecoilCompensation(int weaponId, int shotCount, uint64_t timeDelta) override {
        if (!initialized_) {
            return Result<Vector2D>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
        }
        
        Vector2D compensation = {0.0f, 0.0f};
        
        // 查找武器信息
        auto weapon_info = getWeaponInfo(weaponId);
        if (!weapon_info) {
            return Result<Vector2D>::ok(compensation);
        }
        
        // 根据武器和射击次数计算后座力补偿
        // 这是一个简化的后座力模型
        if (weapon_info->isAutomatic() && shotCount > 0) {
            // 计算垂直后座力 - 通常随射击次数增加
            float vertical_recoil = weapon_info->getRecoilFactor() * std::min(shotCount, 10) * 0.01f;
            
            // 添加一些水平随机性
            float horizontal_recoil = 0.0f;
            if (shotCount > 3) {
                // 简单模拟一个弹道模式
                int pattern_position = shotCount % 8;
                if (pattern_position < 4) {
                    horizontal_recoil = weapon_info->getRecoilFactor() * 0.005f * pattern_position;
                } else {
                    horizontal_recoil = weapon_info->getRecoilFactor() * 0.005f * (8 - pattern_position);
                }
                
                // 随机决定方向
                if (shotCount % 2 == 0) {
                    horizontal_recoil = -horizontal_recoil;
                }
            }
            
            compensation.x = horizontal_recoil;
            compensation.y = vertical_recoil;
        }
        
        return Result<Vector2D>::ok(compensation);
    }
    
    // 注册客户端
    Result<void> registerClient(uint32_t clientId, uint8_t gameId) override {
        if (!initialized_) {
            return Result<void>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
        }
        
        if (gameId != static_cast<uint8_t>(GameType::CS_1_6)) {
            return Result<void>::error(ErrorCode::INVALID_ARGUMENT, "Unsupported game ID for CS16GameAdapter");
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 创建并初始化客户端状态
        auto client_state = std::make_shared<ClientState>(gameId);
        clients_[clientId] = client_state;
        
        LOG_INFO("Client #" + std::to_string(clientId) + " registered with CS16GameAdapter");
        
        // 发布客户端注册事件
        EventBus::getInstance().publishClientEvent(events::CLIENT_CONNECTED, clientId);
        
        return Result<void>::ok();
    }
    
    // 注销客户端
    Result<void> unregisterClient(uint32_t clientId) override {
        if (!initialized_) {
            return Result<void>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 检查客户端是否存在
        auto it = clients_.find(clientId);
        if (it == clients_.end()) {
            return Result<void>::error(ErrorCode::INVALID_ARGUMENT, "Client not found: " + std::to_string(clientId));
        }
        
        // 移除客户端
        clients_.erase(it);
        
        LOG_INFO("Client #" + std::to_string(clientId) + " unregistered from CS16GameAdapter");
        
        // 发布客户端注销事件
        EventBus::getInstance().publishClientEvent(events::CLIENT_DISCONNECTED, clientId);
        
        return Result<void>::ok();
    }
    
    // 更新客户端武器
    Result<void> updateClientWeapon(uint32_t clientId, int weaponId) override {
        if (!initialized_) {
            return Result<void>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 获取客户端状态
        auto client_state = getOrCreateClientState(clientId);
        
        // 更新武器ID
        client_state->setCurrentWeaponId(weaponId);
        
        return Result<void>::ok();
    }
    
    // 更新客户端射击状态
    Result<void> updateClientShooting(uint32_t clientId, bool isShooting, int shotCount) override {
        if (!initialized_) {
            return Result<void>::error(ErrorCode::NOT_INITIALIZED, "Game adapter not initialized");
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 获取客户端状态
        auto client_state = getOrCreateClientState(clientId);
        
        // 更新射击状态
        client_state->setShooting(isShooting);
        
        // 更新射击次数
        if (isShooting) {
            if (shotCount >= 0) {
                // 直接设置射击次数
                client_state->setShotCount(shotCount);
            } else if (!client_state->isShooting()) {
                // 刚开始射击，重置射击次数
                client_state->setShotCount(0);
            } else {
                // 增加射击次数
                client_state->setShotCount(client_state->getShotCount() + 1);
            }
            
            // 更新最后射击时间
            client_state->setLastShotTime(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count());
        }
        
        return Result<void>::ok();
    }
    
    // 获取客户端状态
    std::shared_ptr<IClientState> getClientState(uint32_t clientId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = clients_.find(clientId);
        if (it != clients_.end()) {
            return it->second;
        }
        
        return nullptr;
    }
    
    // 获取武器信息
    std::shared_ptr<IWeaponInfo> getWeaponInfo(int weaponId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = weapons_.find(weaponId);
        if (it != weapons_.end()) {
            return it->second;
        }
        
        return nullptr;
    }
    
    // 获取支持的游戏列表
    std::vector<std::string> getSupportedGames() const override {
        return {"cs16"};
    }
    
    // 获取适配器状态
    std::unordered_map<std::string, std::string> getStatus() const override {
        std::unordered_map<std::string, std::string> status;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        status["initialized"] = initialized_ ? "true" : "false";
        status["client_count"] = std::to_string(clients_.size());
        status["weapon_count"] = std::to_string(weapons_.size());
        status["next_track_id"] = std::to_string(next_track_id_.load());
        
        return status;
    }

private:
    // 处理CS 1.6检测结果
    GameState processCS16Detections(const GameState& rawState) {
        GameState processed_state = rawState;
        
        // 分配跟踪ID
        for (auto& detection : processed_state.detections) {
            if (detection.track_id == 0) {
                // 为没有跟踪ID的检测分配新ID
                detection.track_id = next_track_id_++;
            }
            
            // 根据检测类别设置特定属性
            switch (detection.class_id) {
                case constants::cs16::CLASS_HEAD:
                    // 调整头部框以提高瞄准准确性
                    detection.box.height *= head_size_factor_;
                    break;
                
                default:
                    break;
            }
        }
        
        return processed_state;
    }
    
    // 获取或创建客户端状态
    std::shared_ptr<ClientState> getOrCreateClientState(uint32_t clientId) {
        auto it = clients_.find(clientId);
        if (it != clients_.end()) {
            return std::static_pointer_cast<ClientState>(it->second);
        }
        
        // 创建新客户端状态
        auto client_state = std::make_shared<ClientState>(static_cast<uint8_t>(GameType::CS_1_6));
        clients_[clientId] = client_state;
        
        return client_state;
    }
    
    // 加载武器数据
    void loadWeaponData(const std::unordered_map<std::string, GameAdaptersConfig::WeaponConfig>& weaponConfigs) {
        weapons_.clear();
        
        // 添加默认武器数据
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
        
        // 更新配置中的武器数据
        int weaponId = 5;  // 从5开始，避免与默认武器冲突
        
        for (const auto& [name, config] : weaponConfigs) {
            // 检查是否已经存在默认武器
            bool found = false;
            for (const auto& [id, weapon] : weapons_) {
                if (weapon->getName() == name) {
                    // 更新默认武器
                    weapons_[id] = std::make_shared<WeaponInfo>(
                        id, name, config.recoil_factor, 0.0f, 0.0f, true, config.priority
                    );
                    found = true;
                    break;
                }
            }
            
            if (!found) {
                // 添加新武器
                weapons_[weaponId] = std::make_shared<WeaponInfo>(
                    weaponId, name, config.recoil_factor, 0.0f, 0.0f, true, config.priority
                );
                weaponId++;
            }
        }
        
        LOG_INFO("Loaded " + std::to_string(weapons_.size()) + " weapons for CS16GameAdapter");
    }

private:
    // 线程安全
    mutable std::mutex mutex_;
    
    // 初始化标志
    bool initialized_;
    
    // 随机数生成器
    std::mt19937 random_generator_;
    
    // 客户端状态
    std::unordered_map<uint32_t, std::shared_ptr<IClientState>> clients_;
    
    // 武器信息
    std::unordered_map<int, std::shared_ptr<IWeaponInfo>> weapons_;
    
    // 跟踪ID计数器
    std::atomic<uint32_t> next_track_id_;
    
    // CS 1.6特定参数
    float aim_target_offset_y_;
    float head_size_factor_;
};

// CS 1.6 游戏适配器工厂
class CS16GameAdapterFactory : public IGameAdapterFactory {
public:
    std::unique_ptr<IGameAdapter> createAdapter() override {
        return std::make_unique<CS16GameAdapter>();
    }
    
    std::string getName() const override {
        return "cs16";
    }
    
    std::vector<std::string> getSupportedGames() const override {
        return {"cs16"};
    }
};

// 注册工厂
REGISTER_GAME_ADAPTER(CS16GameAdapterFactory)

} // namespace zero_latency