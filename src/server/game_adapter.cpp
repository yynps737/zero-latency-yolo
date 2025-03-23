#include "game_adapter.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include "../common/constants.h"

namespace zero_latency {

GameAdapter::GameAdapter() {
}

GameAdapter::~GameAdapter() {
}

bool GameAdapter::initialize() {
    loadWeaponData();
    return true;
}

GameState GameAdapter::processDetections(uint32_t client_id, const GameState& raw_state, uint8_t game_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    // 查找或创建客户端状态
    auto& client_state = clients_[client_id];
    client_state.game_id = game_id;
    
    GameState processed_state = raw_state;
    
    // 根据游戏类型处理
    switch (game_id) {
        case static_cast<uint8_t>(GameType::CS_1_6):
            processed_state = processCS16(raw_state);
            break;
            
        // 添加其他游戏类型处理...
        
        default:
            // 未知游戏类型，保持原始检测结果
            break;
    }
    
    // 更新跟踪对象
    for (const auto& detection : processed_state.detections) {
        // 更新或添加跟踪
        client_state.tracked_objects[detection.track_id] = detection;
    }
    
    // 移除过期的跟踪对象(超过100ms未更新)
    uint64_t current_time = processed_state.timestamp;
    std::vector<uint32_t> expired_tracks;
    
    for (const auto& [track_id, detection] : client_state.tracked_objects) {
        if (current_time - detection.timestamp > 100) {
            expired_tracks.push_back(track_id);
        }
    }
    
    for (uint32_t track_id : expired_tracks) {
        client_state.tracked_objects.erase(track_id);
    }
    
    return processed_state;
}

int GameAdapter::calculateBestTarget(const std::vector<Detection>& detections) {
    if (detections.empty()) {
        return -1;
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
    
    return best_index;
}

BoundingBox GameAdapter::predictMotion(const Detection& detection, uint64_t current_time, uint64_t target_time) {
    BoundingBox predicted_box = detection.box;
    
    // 检查时间有效性
    if (target_time <= current_time || target_time - current_time > constants::dual_engine::MAX_PREDICTION_FRAMES * 16) {
        return predicted_box;
    }
    
    // 简单的线性预测 - 在实际实现中可以替换为更复杂的卡尔曼滤波器
    // 这里假设我们有跟踪历史和速度估计
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    // 找到这个检测相关的客户端
    for (auto& [client_id, state] : clients_) {
        auto it = state.tracked_objects.find(detection.track_id);
        if (it != state.tracked_objects.end()) {
            // 获取历史检测
            const Detection& previous = it->second;
            
            // 计算时间差
            float time_diff_ms = current_time - previous.timestamp;
            if (time_diff_ms > 0) {
                // 计算速度
                float vx = (detection.box.x - previous.box.x) / time_diff_ms;
                float vy = (detection.box.y - previous.box.y) / time_diff_ms;
                
                // 预测未来位置
                float future_time_ms = target_time - current_time;
                predicted_box.x += vx * future_time_ms;
                predicted_box.y += vy * future_time_ms;
            }
            break;
        }
    }
    
    return predicted_box;
}

Point2D GameAdapter::getAimPoint(const Detection& detection, int weapon_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    // 查找检测相关的客户端
    for (auto& [client_id, state] : clients_) {
        // 根据游戏类型获取瞄准点
        switch (state.game_id) {
            case static_cast<uint8_t>(GameType::CS_1_6):
                return getCS16AimPoint(detection, weapon_id != -1 ? weapon_id : state.current_weapon_id);
                
            // 添加其他游戏类型处理...
                
            default:
                break;
        }
    }
    
    // 默认瞄准中心
    return {detection.box.x, detection.box.y};
}

Vector2D GameAdapter::calculateRecoilCompensation(int weapon_id, int shot_count, uint64_t time_delta) {
    Vector2D compensation = {0.0f, 0.0f};
    
    // 查找武器信息
    auto it = weapons_.find(weapon_id);
    if (it == weapons_.end()) {
        return compensation;
    }
    
    const WeaponInfo& weapon = it->second;
    
    // 根据武器和射击次数计算后座力补偿
    // 这是一个简化的后座力模型
    if (weapon.is_auto && shot_count > 0) {
        // 计算垂直后座力 - 通常随射击次数增加
        float vertical_recoil = weapon.recoil_factor * std::min(shot_count, 10) * 0.01f;
        
        // 添加一些水平随机性
        float horizontal_recoil = 0.0f;
        if (shot_count > 3) {
            // 简单模拟一个弹道模式
            int pattern_position = shot_count % 8;
            if (pattern_position < 4) {
                horizontal_recoil = weapon.recoil_factor * 0.005f * pattern_position;
            } else {
                horizontal_recoil = weapon.recoil_factor * 0.005f * (8 - pattern_position);
            }
            
            // 随机决定方向
            if (shot_count % 2 == 0) {
                horizontal_recoil = -horizontal_recoil;
            }
        }
        
        compensation.x = horizontal_recoil;
        compensation.y = vertical_recoil;
    }
    
    return compensation;
}

void GameAdapter::registerClient(uint32_t client_id, uint8_t game_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    clients_[client_id] = ClientState{
        .game_id = game_id,
        .current_weapon_id = 0,
        .is_shooting = false,
        .shot_count = 0,
        .last_shot_time = 0
    };
}

void GameAdapter::unregisterClient(uint32_t client_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.erase(client_id);
}

void GameAdapter::updateClientWeapon(uint32_t client_id, int weapon_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        it->second.current_weapon_id = weapon_id;
        
        // 如果武器改变，重置射击计数
        if (it->second.current_weapon_id != weapon_id) {
            it->second.shot_count = 0;
        }
    }
}

void GameAdapter::updateClientShooting(uint32_t client_id, bool is_shooting, int shot_count) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        bool was_shooting = it->second.is_shooting;
        it->second.is_shooting = is_shooting;
        
        // 更新射击次数
        if (is_shooting) {
            if (shot_count >= 0) {
                // 直接设置射击次数
                it->second.shot_count = shot_count;
            } else if (!was_shooting) {
                // 重置射击次数
                it->second.shot_count = 0;
            } else {
                // 增加射击次数
                it->second.shot_count++;
            }
            it->second.last_shot_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
        } else if (!is_shooting && was_shooting) {
            // 停止射击，重置计数
            it->second.shot_count = 0;
        }
    }
}

// 私有方法实现

GameState GameAdapter::processCS16(const GameState& raw_state) {
    GameState processed_state = raw_state;
    
    // 检测跟踪ID分配
    uint32_t next_track_id = 1;
    
    for (auto& detection : processed_state.detections) {
        // 根据检测类别设置特定属性
        switch (detection.class_id) {
            case constants::cs16::CLASS_T:
            case constants::cs16::CLASS_CT:
                // 为玩家分配跟踪ID
                detection.track_id = next_track_id++;
                break;
                
            case constants::cs16::CLASS_HEAD:
                // 调整头部框以提高瞄准准确性
                detection.box.height *= 0.7f; // 缩小高度以更精确定位
                detection.track_id = next_track_id++;
                break;
                
            case constants::cs16::CLASS_WEAPON:
                // 武器通常不需要特殊处理
                detection.track_id = next_track_id++;
                break;
                
            default:
                detection.track_id = 0; // 不跟踪其他物体
                break;
        }
    }
    
    return processed_state;
}

Point2D GameAdapter::getCS16AimPoint(const Detection& detection, int weapon_id) {
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
    if (weapon_id > 0) {
        auto it = weapons_.find(weapon_id);
        if (it != weapons_.end()) {
            const WeaponInfo& weapon = it->second;
            
            // 狙击枪瞄准头部，其他武器可能需要考虑后座力
            if (weapon.name == "AWP" || weapon.name == "Scout") {
                // 精确瞄准头部中心
                if (detection.class_id != constants::cs16::CLASS_HEAD) {
                    // 如果没有检测到头部，尝试根据身体估计头部位置
                    aim_point.y = detection.box.y - detection.box.height * 0.3f;
                }
            }
        }
    }
    
    return aim_point;
}

void GameAdapter::loadWeaponData() {
    // 清空现有数据
    weapons_.clear();
    
    // 添加CS 1.6武器数据
    weapons_[1] = WeaponInfo{
        .id = 1,
        .name = "AK47",
        .recoil_factor = constants::cs16::WeaponRecoil::AK47,
        .damage = 36.0f,
        .fire_rate = 0.1f, // 秒/发
        .is_auto = true
    };
    
    weapons_[2] = WeaponInfo{
        .id = 2,
        .name = "M4A1",
        .recoil_factor = constants::cs16::WeaponRecoil::M4A1,
        .damage = 33.0f,
        .fire_rate = 0.09f,
        .is_auto = true
    };
    
    weapons_[3] = WeaponInfo{
        .id = 3,
        .name = "AWP",
        .recoil_factor = constants::cs16::WeaponRecoil::AWP,
        .damage = 115.0f,
        .fire_rate = 1.5f,
        .is_auto = false
    };
    
    weapons_[4] = WeaponInfo{
        .id = 4,
        .name = "Deagle",
        .recoil_factor = constants::cs16::WeaponRecoil::DEAGLE,
        .damage = 54.0f,
        .fire_rate = 0.4f,
        .is_auto = false
    };
    
    // 可以从配置文件加载更多武器数据
    std::cout << "加载了 " << weapons_.size() << " 种武器数据" << std::endl;
}

} // namespace zero_latency