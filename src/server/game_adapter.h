#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include "../common/types.h"

namespace zero_latency {

// 武器信息
struct WeaponInfo {
    int id;
    std::string name;
    float recoil_factor;
    float damage;
    float fire_rate;
    bool is_auto;
};

// 游戏特定处理适配器
class GameAdapter {
public:
    GameAdapter();
    ~GameAdapter();
    
    // 初始化适配器
    bool initialize();
    
    // 处理检测结果
    GameState processDetections(uint32_t client_id, const GameState& raw_state, uint8_t game_id);
    
    // 计算最优目标
    int calculateBestTarget(const std::vector<Detection>& detections);
    
    // 预测目标运动
    BoundingBox predictMotion(const Detection& detection, uint64_t current_time, uint64_t target_time);
    
    // 获取瞄准点
    Point2D getAimPoint(const Detection& detection, int weapon_id = -1);
    
    // 计算后座力补偿
    Vector2D calculateRecoilCompensation(int weapon_id, int shot_count, uint64_t time_delta);
    
    // 注册客户端
    void registerClient(uint32_t client_id, uint8_t game_id);
    
    // 注销客户端
    void unregisterClient(uint32_t client_id);
    
    // 更新客户端武器
    void updateClientWeapon(uint32_t client_id, int weapon_id);
    
    // 更新客户端射击状态
    void updateClientShooting(uint32_t client_id, bool is_shooting, int shot_count);
    
private:
    // CS 1.6特定处理
    GameState processCS16(const GameState& raw_state);
    
    // 计算特定游戏的瞄准点
    Point2D getCS16AimPoint(const Detection& detection, int weapon_id);
    
    // 加载武器配置数据
    void loadWeaponData();

private:
    struct ClientState {
        uint8_t game_id;
        int current_weapon_id;
        bool is_shooting;
        int shot_count;
        uint64_t last_shot_time;
        std::unordered_map<uint32_t, Detection> tracked_objects;
    };
    
    std::mutex clients_mutex_;
    std::unordered_map<uint32_t, ClientState> clients_;
    std::unordered_map<int, WeaponInfo> weapons_;
};

} // namespace zero_latency