#include "dual_engine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include "../common/constants.h"

namespace zero_latency {

DualEngine::DualEngine(PredictionEngine* prediction_engine)
    : prediction_engine_(prediction_engine),
      last_server_update_time_(0),
      local_prediction_weight_(constants::dual_engine::LOCAL_PREDICTION_WEIGHT),
      server_correction_weight_(constants::dual_engine::SERVER_CORRECTION_WEIGHT),
      detection_count_(0),
      prediction_count_(0) {
}

DualEngine::~DualEngine() {
    clear();
}

void DualEngine::addServerDetections(const GameState& state) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // 获取当前时间
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // 如果收到旧的状态，忽略
    if (state.timestamp < last_server_update_time_) {
        return;
    }
    
    // 更新服务器状态
    last_server_state_ = state;
    last_server_update_time_ = current_time;
    
    // 为每个检测创建本地预测
    for (const auto& detection : state.detections) {
        prediction_engine_->addDetection(detection);
    }
    
    // 更新统计信息
    detection_count_ += state.detections.size();
}

void DualEngine::update() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // 获取当前时间
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // 更新预测引擎
    prediction_engine_->update();
    
    // 预测当前时间点的状态
    std::vector<Detection> local_predictions = prediction_engine_->predictState(current_time);
    
    // 更新统计信息
    prediction_count_ += local_predictions.size();
    
    // 融合服务器状态和本地预测
    current_state_ = fuseDetections(last_server_state_, local_predictions);
    current_state_.timestamp = current_time;
    current_state_.frame_id = last_server_state_.frame_id + 1;
}

GameState DualEngine::getCurrentState() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return current_state_;
}

size_t DualEngine::getDetectionCount() const {
    return detection_count_.load();
}

size_t DualEngine::getPredictionCount() const {
    return prediction_count_.load();
}

void DualEngine::clear() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    last_server_state_.detections.clear();
    current_state_.detections.clear();
    id_mapping_.clear();
    
    // 清除预测引擎
    prediction_engine_->clearTracks();
    
    // 重置统计信息
    detection_count_ = 0;
    prediction_count_ = 0;
}

GameState DualEngine::fuseDetections(const GameState& server_state, 
                                   const std::vector<Detection>& local_predictions) {
    GameState fused_state;
    fused_state.frame_id = server_state.frame_id;
    fused_state.timestamp = server_state.timestamp;
    
    // 计算服务器状态与本地预测的时间差
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    uint64_t server_state_age = current_time - server_state.timestamp;
    
    // 如果没有服务器状态或服务器状态太旧，只使用本地预测
    constexpr uint64_t MAX_SERVER_STATE_AGE = 500; // 500ms
    if (server_state.detections.empty() || server_state_age > MAX_SERVER_STATE_AGE) {
        fused_state.detections = local_predictions;
        return fused_state;
    }
    
    // 如果没有本地预测，只使用服务器状态
    if (local_predictions.empty()) {
        fused_state.detections = server_state.detections;
        return fused_state;
    }
    
    // 查找最佳匹配
    auto matches = findBestMatches(server_state.detections, local_predictions);
    
    // 已匹配的预测
    std::vector<bool> used_predictions(local_predictions.size(), false);
    
    // 处理每个服务器检测
    for (size_t i = 0; i < server_state.detections.size(); i++) {
        const auto& server_detection = server_state.detections[i];
        
        // 检查置信度
        if (server_detection.confidence < constants::dual_engine::MIN_SERVER_CONFIDENCE) {
            continue;
        }
        
        // 寻找此服务器检测的本地匹配
        bool has_match = false;
        for (const auto& match : matches) {
            if (match.first == i) {
                // 找到匹配
                const auto& local_prediction = local_predictions[match.second];
                used_predictions[match.second] = true;
                
                // 计算过渡权重 - 基于时间和置信度
                float time_weight = std::min(1.0f, server_state_age / 100.0f); // 100ms完全过渡
                float weight = server_correction_weight_ * (1.0f - time_weight);
                
                // 创建平滑过渡
                Detection fused_detection = smoothTransition(local_prediction, server_detection, weight);
                fused_state.detections.push_back(fused_detection);
                
                has_match = true;
                break;
            }
        }
        
        // 如果没有匹配，添加服务器检测
        if (!has_match) {
            fused_state.detections.push_back(server_detection);
        }
    }
    
    // 添加未匹配的本地预测
    for (size_t i = 0; i < local_predictions.size(); i++) {
        if (!used_predictions[i]) {
            // 检查置信度是否足够高
            if (local_predictions[i].confidence >= constants::dual_engine::MIN_SERVER_CONFIDENCE) {
                fused_state.detections.push_back(local_predictions[i]);
            }
        }
    }
    
    return fused_state;
}

float DualEngine::calculateSimilarity(const Detection& det1, const Detection& det2) const {
    // 如果类别不同，认为相似度为0
    if (det1.class_id != det2.class_id) {
        return 0.0f;
    }
    
    // 计算中心点距离
    float dx = det1.box.x - det2.box.x;
    float dy = det1.box.y - det2.box.y;
    float center_dist = std::sqrt(dx * dx + dy * dy);
    
    // 计算尺寸差异
    float dw = std::abs(det1.box.width - det2.box.width) / std::max(det1.box.width, det2.box.width);
    float dh = std::abs(det1.box.height - det2.box.height) / std::max(det1.box.height, det2.box.height);
    float size_diff = (dw + dh) / 2.0f;
    
    // 中心距离权重(距离越小越好)
    float center_weight = std::exp(-10.0f * center_dist); // 距离为0.1时权重约为0.37
    
    // 尺寸差异权重(差异越小越好)
    float size_weight = std::exp(-5.0f * size_diff); // 差异为0.2时权重约为0.37
    
    // 置信度权重(置信度乘积)
    float conf_weight = det1.confidence * det2.confidence;
    
    // 计算总相似度
    float similarity = center_weight * 0.6f + size_weight * 0.3f + conf_weight * 0.1f;
    
    return similarity;
}

Detection DualEngine::smoothTransition(const Detection& current, const Detection& target, float weight) const {
    // 创建结果
    Detection result = current;
    
    // 确保权重在[0,1]范围
    weight = std::max(0.0f, std::min(1.0f, weight));
    
    // 线性插值位置和尺寸
    result.box.x = current.box.x * (1.0f - weight) + target.box.x * weight;
    result.box.y = current.box.y * (1.0f - weight) + target.box.y * weight;
    result.box.width = current.box.width * (1.0f - weight) + target.box.width * weight;
    result.box.height = current.box.height * (1.0f - weight) + target.box.height * weight;
    
    // 使用较高的置信度
    result.confidence = std::max(current.confidence, target.confidence);
    
    // 保持目标的类别和跟踪ID
    result.class_id = target.class_id;
    result.track_id = target.track_id;
    
    return result;
}

std::vector<std::pair<size_t, size_t>> DualEngine::findBestMatches(
    const std::vector<Detection>& server_detections, 
    const std::vector<Detection>& local_predictions) const {
    
    std::vector<std::pair<size_t, size_t>> matches;
    
    // 如果任一为空，返回空匹配
    if (server_detections.empty() || local_predictions.empty()) {
        return matches;
    }
    
    // 计算所有可能匹配的相似度
    std::vector<std::tuple<float, size_t, size_t>> similarities;
    for (size_t i = 0; i < server_detections.size(); i++) {
        for (size_t j = 0; j < local_predictions.size(); j++) {
            float similarity = calculateSimilarity(server_detections[i], local_predictions[j]);
            if (similarity > 0.3f) { // 最小相似度阈值
                similarities.emplace_back(similarity, i, j);
            }
        }
    }
    
    // 按相似度降序排序
    std::sort(similarities.begin(), similarities.end(), 
              [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });
    
    // 贪心选择最佳匹配
    std::vector<bool> used_server(server_detections.size(), false);
    std::vector<bool> used_local(local_predictions.size(), false);
    
    for (const auto& [similarity, server_idx, local_idx] : similarities) {
        if (!used_server[server_idx] && !used_local[local_idx]) {
            matches.emplace_back(server_idx, local_idx);
            used_server[server_idx] = true;
            used_local[local_idx] = true;
        }
    }
    
    return matches;
}

} // namespace zero_latency