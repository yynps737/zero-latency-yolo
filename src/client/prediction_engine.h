#pragma once

#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <chrono>

#include "../common/types.h"

namespace zero_latency {

// 跟踪对象的历史记录
struct TrackingHistory {
    std::vector<Detection> history;
    Vector2D velocity;
    Vector2D acceleration;
    uint64_t last_update_time;
    float confidence_decay;
};

// 预测引擎类
class PredictionEngine {
public:
    PredictionEngine(const PredictionParams& params);
    ~PredictionEngine();
    
    // 添加新的检测结果
    void addDetection(const Detection& detection);
    
    // 更新所有跟踪对象
    void update();
    
    // 预测指定时间点的对象位置
    std::vector<Detection> predictState(uint64_t target_time);
    
    // 清除所有跟踪对象
    void clearTracks();
    
    // 获取当前跟踪对象数量
    size_t getTrackCount() const;
    
    // 获取指定跟踪ID的置信度
    float getTrackConfidence(uint32_t track_id) const;
    
private:
    // 更新单个跟踪对象
    void updateTrack(uint32_t track_id, TrackingHistory& track);
    
    // 计算速度和加速度
    Vector2D calculateVelocity(const std::vector<Detection>& history, uint64_t time_window);
    Vector2D calculateAcceleration(const std::vector<Detection>& history, uint64_t time_window);
    
    // 预测单个对象的运动
    BoundingBox predictMotion(const BoundingBox& box, const Vector2D& velocity, const Vector2D& acceleration, uint64_t time_delta);
    
    // 清除过期的跟踪对象
    void pruneOldTracks(uint64_t current_time);
    
    // 应用卡尔曼滤波器进行状态估计
    void applyKalmanFilter(uint32_t track_id, TrackingHistory& track, const Detection& detection);
    
private:
    PredictionParams params_;
    std::mutex tracks_mutex_;
    std::unordered_map<uint32_t, TrackingHistory> tracks_;
    uint64_t max_track_age_ms_;
    uint64_t prediction_horizon_ms_;
    
    // 卡尔曼滤波器参数
    struct KalmanState {
        float x;    // x位置
        float y;    // y位置
        float vx;   // x方向速度
        float vy;   // y方向速度
        float w;    // 宽度
        float h;    // 高度
    };
    
    struct KalmanFilter {
        KalmanState state;
        float position_uncertainty;
        float velocity_uncertainty;
        bool initialized;
    };
    
    std::unordered_map<uint32_t, KalmanFilter> filters_;
};

} // namespace zero_latency