#pragma once

#include <vector>
#include <mutex>
#include <chrono>
#include <atomic>
#include <unordered_map>

#include "prediction_engine.h"
#include "../common/types.h"

namespace zero_latency {

// 双引擎协调系统类
class DualEngine {
public:
    DualEngine(PredictionEngine* prediction_engine);
    ~DualEngine();
    
    // 添加服务器检测结果
    void addServerDetections(const GameState& state);
    
    // 更新系统状态
    void update();
    
    // 获取当前状态
    GameState getCurrentState() const;
    
    // 获取检测数量
    size_t getDetectionCount() const;
    
    // 获取预测数量
    size_t getPredictionCount() const;
    
    // 清除所有数据
    void clear();
    
private:
    // 融合服务器和本地结果
    GameState fuseDetections(const GameState& server_state, const std::vector<Detection>& local_predictions);
    
    // 计算对象间的相似度
    float calculateSimilarity(const Detection& det1, const Detection& det2) const;
    
    // 平滑过渡算法
    Detection smoothTransition(const Detection& current, const Detection& target, float weight) const;
    
    // 计算服务器检测与本地预测的最佳匹配
    std::vector<std::pair<size_t, size_t>> findBestMatches(
        const std::vector<Detection>& server_detections, 
        const std::vector<Detection>& local_predictions) const;
    
private:
    PredictionEngine* prediction_engine_;
    
    // 最近的服务器状态
    GameState last_server_state_;
    uint64_t last_server_update_time_;
    
    // 当前融合状态
    GameState current_state_;
    
    // 对象跟踪映射(服务器ID到本地ID)
    std::unordered_map<uint32_t, uint32_t> id_mapping_;
    
    // 置信度权重配置
    float local_prediction_weight_;
    float server_correction_weight_;
    
    // 统计信息
    std::atomic<size_t> detection_count_;
    std::atomic<size_t> prediction_count_;
    
    // 互斥锁
    mutable std::mutex state_mutex_;
};

} // namespace zero_latency