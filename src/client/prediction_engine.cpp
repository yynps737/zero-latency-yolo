#include "prediction_engine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include "../common/constants.h"

namespace zero_latency {

PredictionEngine::PredictionEngine(const PredictionParams& params)
    : params_(params),
      max_track_age_ms_(500),  // 默认500ms为最大跟踪时间
      prediction_horizon_ms_(params.max_prediction_time) {
}

PredictionEngine::~PredictionEngine() {
    clearTracks();
}

void PredictionEngine::addDetection(const Detection& detection) {
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    
    // 忽略无效的跟踪ID
    if (detection.track_id == 0) {
        return;
    }
    
    // 查找或创建跟踪记录
    auto it = tracks_.find(detection.track_id);
    if (it == tracks_.end()) {
        // 新建跟踪记录
        TrackingHistory track;
        track.history.push_back(detection);
        track.velocity = {0.0f, 0.0f};
        track.acceleration = {0.0f, 0.0f};
        track.last_update_time = detection.timestamp;
        track.confidence_decay = constants::dual_engine::LOCAL_CONFIDENCE_DECAY;
        
        tracks_[detection.track_id] = track;
        
        // 初始化卡尔曼滤波器
        KalmanFilter filter;
        filter.state.x = detection.box.x;
        filter.state.y = detection.box.y;
        filter.state.vx = 0.0f;
        filter.state.vy = 0.0f;
        filter.state.w = detection.box.width;
        filter.state.h = detection.box.height;
        filter.position_uncertainty = params_.position_uncertainty;
        filter.velocity_uncertainty = params_.velocity_uncertainty;
        filter.initialized = true;
        
        filters_[detection.track_id] = filter;
    } else {
        // 更新现有跟踪记录
        TrackingHistory& track = it->second;
        
        // 将新检测添加到历史记录
        track.history.push_back(detection);
        
        // 限制历史记录大小
        constexpr size_t MAX_HISTORY = 10;
        if (track.history.size() > MAX_HISTORY) {
            track.history.erase(track.history.begin());
        }
        
        // 更新最后更新时间
        track.last_update_time = detection.timestamp;
        
        // 应用卡尔曼滤波器
        applyKalmanFilter(detection.track_id, track, detection);
    }
}

void PredictionEngine::update() {
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // 更新所有跟踪记录
    for (auto& pair : tracks_) {
        updateTrack(pair.first, pair.second);
    }
    
    // 清除过期的跟踪记录
    pruneOldTracks(current_time);
}

std::vector<Detection> PredictionEngine::predictState(uint64_t target_time) {
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    
    std::vector<Detection> predictions;
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // 检查目标时间是否超出预测视野
    if (target_time > current_time + prediction_horizon_ms_) {
        target_time = current_time + prediction_horizon_ms_;
    }
    
    // 为每个跟踪对象生成预测
    for (const auto& pair : tracks_) {
        const uint32_t track_id = pair.first;
        const TrackingHistory& track = pair.second;
        
        // 如果历史记录为空则跳过
        if (track.history.empty()) {
            continue;
        }
        
        // 获取最新的检测
        const Detection& latest = track.history.back();
        
        // 计算经过的时间
        uint64_t time_delta = target_time - latest.timestamp;
        
        // 如果时间差太大，跳过
        if (time_delta > prediction_horizon_ms_) {
            continue;
        }
        
        // 使用卡尔曼滤波器状态
        auto filter_it = filters_.find(track_id);
        if (filter_it != filters_.end() && filter_it->second.initialized) {
            const KalmanFilter& filter = filter_it->second;
            
            // 使用当前状态和速度进行预测
            float dt = time_delta / 1000.0f;  // 毫秒转秒
            
            // 创建预测检测结果
            Detection prediction = latest;
            prediction.timestamp = target_time;
            
            // 预测位置
            prediction.box.x = filter.state.x + filter.state.vx * dt;
            prediction.box.y = filter.state.y + filter.state.vy * dt;
            
            // 保持宽度和高度
            prediction.box.width = filter.state.w;
            prediction.box.height = filter.state.h;
            
            // 考虑置信度衰减
            float confidence_decay = track.confidence_decay * (time_delta / 16.67f);  // 每帧衰减
            prediction.confidence = std::max(latest.confidence - confidence_decay, 0.0f);
            
            predictions.push_back(prediction);
        } else {
            // 简单预测
            BoundingBox predicted_box = predictMotion(
                latest.box,
                track.velocity,
                track.acceleration,
                time_delta
            );
            
            // 创建预测检测结果
            Detection prediction = latest;
            prediction.box = predicted_box;
            prediction.timestamp = target_time;
            
            // 考虑置信度衰减
            float confidence_decay = track.confidence_decay * (time_delta / 16.67f);  // 每帧衰减
            prediction.confidence = std::max(latest.confidence - confidence_decay, 0.0f);
            
            predictions.push_back(prediction);
        }
    }
    
    return predictions;
}

void PredictionEngine::clearTracks() {
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    tracks_.clear();
    filters_.clear();
}

size_t PredictionEngine::getTrackCount() const {
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    return tracks_.size();
}

float PredictionEngine::getTrackConfidence(uint32_t track_id) const {
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    
    auto it = tracks_.find(track_id);
    if (it != tracks_.end() && !it->second.history.empty()) {
        return it->second.history.back().confidence;
    }
    
    return 0.0f;
}

void PredictionEngine::updateTrack(uint32_t track_id, TrackingHistory& track) {
    // 如果历史记录为空则跳过
    if (track.history.empty()) {
        return;
    }
    
    // 获取当前时间
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // 计算速度和加速度
    constexpr uint64_t VELOCITY_WINDOW_MS = 100;  // 100ms窗口计算速度
    constexpr uint64_t ACCELERATION_WINDOW_MS = 200;  // 200ms窗口计算加速度
    
    track.velocity = calculateVelocity(track.history, VELOCITY_WINDOW_MS);
    track.acceleration = calculateAcceleration(track.history, ACCELERATION_WINDOW_MS);
}

Vector2D PredictionEngine::calculateVelocity(const std::vector<Detection>& history, uint64_t time_window) {
    // 至少需要两个点计算速度
    if (history.size() < 2) {
        return {0.0f, 0.0f};
    }
    
    // 获取最新和最早的检测结果
    const Detection& latest = history.back();
    
    // 寻找时间窗口内最早的检测
    const Detection* earliest = nullptr;
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (latest.timestamp - it->timestamp >= time_window) {
            earliest = &(*it);
            break;
        }
    }
    
    // 如果没有找到足够早的检测，使用最早的一个
    if (earliest == nullptr) {
        earliest = &history.front();
    }
    
    // 计算时间差(秒)
    float dt = (latest.timestamp - earliest->timestamp) / 1000.0f;
    if (dt < 0.001f) {  // 避免除零
        return {0.0f, 0.0f};
    }
    
    // 计算位置差并计算速度(单位/秒)
    float dx = latest.box.x - earliest->box.x;
    float dy = latest.box.y - earliest->box.y;
    
    return {dx / dt, dy / dt};
}

Vector2D PredictionEngine::calculateAcceleration(const std::vector<Detection>& history, uint64_t time_window) {
    // 至少需要三个点计算加速度
    if (history.size() < 3) {
        return {0.0f, 0.0f};
    }
    
    // 使用较短的时间窗口计算当前速度
    constexpr uint64_t RECENT_WINDOW = 50;  // 最近50ms
    constexpr uint64_t OLDER_WINDOW = 100;  // 较早100ms
    
    // 获取最新的检测结果
    const Detection& latest = history.back();
    
    // 寻找较新和较旧的窗口
    std::vector<Detection> recent_history;
    std::vector<Detection> older_history;
    
    for (const auto& det : history) {
        if (latest.timestamp - det.timestamp <= RECENT_WINDOW) {
            recent_history.push_back(det);
        } else if (latest.timestamp - det.timestamp <= OLDER_WINDOW + RECENT_WINDOW) {
            older_history.push_back(det);
        }
    }
    
    // 如果任一窗口样本不足，返回零加速度
    if (recent_history.size() < 2 || older_history.size() < 2) {
        return {0.0f, 0.0f};
    }
    
    // 计算两个窗口的速度
    Vector2D recent_velocity = calculateVelocity(recent_history, RECENT_WINDOW);
    Vector2D older_velocity = calculateVelocity(older_history, OLDER_WINDOW);
    
    // 计算速度差并转换为加速度
    float dt = (RECENT_WINDOW + OLDER_WINDOW / 2) / 1000.0f;  // 两个窗口中点的时间差(秒)
    if (dt < 0.001f) {  // 避免除零
        return {0.0f, 0.0f};
    }
    
    float dvx = recent_velocity.x - older_velocity.x;
    float dvy = recent_velocity.y - older_velocity.y;
    
    return {dvx / dt, dvy / dt};
}

BoundingBox PredictionEngine::predictMotion(const BoundingBox& box, const Vector2D& velocity, const Vector2D& acceleration, uint64_t time_delta) {
    BoundingBox predicted_box = box;
    
    // 将时间差转换为秒
    float dt = time_delta / 1000.0f;
    
    // 使用运动学方程预测位置
    // s = s0 + v*t + 0.5*a*t^2
    predicted_box.x = box.x + velocity.x * dt + 0.5f * acceleration.x * dt * dt;
    predicted_box.y = box.y + velocity.y * dt + 0.5f * acceleration.y * dt * dt;
    
    // 保持宽度和高度不变
    // 在实际应用中可能需要考虑近大远小效应
    
    return predicted_box;
}

void PredictionEngine::pruneOldTracks(uint64_t current_time) {
    std::vector<uint32_t> tracks_to_remove;
    
    for (const auto& pair : tracks_) {
        const uint32_t track_id = pair.first;
        const TrackingHistory& track = pair.second;
        
        // 计算自上次更新以来的时间
        uint64_t time_since_update = current_time - track.last_update_time;
        
        // 如果超过最大跟踪时间，标记为移除
        if (time_since_update > max_track_age_ms_) {
            tracks_to_remove.push_back(track_id);
        }
    }
    
    // 移除过期跟踪
    for (uint32_t track_id : tracks_to_remove) {
        tracks_.erase(track_id);
        filters_.erase(track_id);
    }
}

void PredictionEngine::applyKalmanFilter(uint32_t track_id, TrackingHistory& track, const Detection& detection) {
    auto it = filters_.find(track_id);
    if (it == filters_.end()) {
        return;
    }
    
    KalmanFilter& filter = it->second;
    
    // 如果过去没有历史记录，只是初始化状态
    if (track.history.size() <= 1) {
        filter.state.x = detection.box.x;
        filter.state.y = detection.box.y;
        filter.state.w = detection.box.width;
        filter.state.h = detection.box.height;
        filter.state.vx = 0.0f;
        filter.state.vy = 0.0f;
        filter.initialized = true;
        return;
    }
    
    // 获取上一次检测
    const Detection& prev = track.history[track.history.size() - 2];
    
    // 计算时间差(秒)
    float dt = (detection.timestamp - prev.timestamp) / 1000.0f;
    if (dt < 0.001f) {  // 避免除零
        dt = 0.016f;  // 假设60FPS
    }
    
    // 预测步骤
    float predicted_x = filter.state.x + filter.state.vx * dt;
    float predicted_y = filter.state.y + filter.state.vy * dt;
    
    // 计算当前测量的速度
    float measured_vx = (detection.box.x - prev.box.x) / dt;
    float measured_vy = (detection.box.y - prev.box.y) / dt;
    
    // 增加不确定性
    filter.position_uncertainty += filter.velocity_uncertainty * dt * dt;
    
    // 计算卡尔曼增益
    float position_gain = filter.position_uncertainty / (filter.position_uncertainty + params_.position_uncertainty);
    float velocity_gain = filter.velocity_uncertainty / (filter.velocity_uncertainty + params_.velocity_uncertainty);
    
    // 更新状态
    filter.state.x = predicted_x + position_gain * (detection.box.x - predicted_x);
    filter.state.y = predicted_y + position_gain * (detection.box.y - predicted_y);
    filter.state.vx = filter.state.vx + velocity_gain * (measured_vx - filter.state.vx);
    filter.state.vy = filter.state.vy + velocity_gain * (measured_vy - filter.state.vy);
    
    // 平滑地更新宽度和高度(使用简单的指数平滑)
    float alpha = 0.3f;  // 平滑因子
    filter.state.w = alpha * detection.box.width + (1.0f - alpha) * filter.state.w;
    filter.state.h = alpha * detection.box.height + (1.0f - alpha) * filter.state.h;
    
    // 更新不确定性
    filter.position_uncertainty = (1.0f - position_gain) * filter.position_uncertainty;
    filter.velocity_uncertainty = (1.0f - velocity_gain) * filter.velocity_uncertainty;
}

} // namespace zero_latency