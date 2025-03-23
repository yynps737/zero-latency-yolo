#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <opencv2/opencv.hpp>
#include "../common/types.h"

namespace zero_latency {

/**
 * 卡尔曼滤波跟踪器，用于目标运动预测
 * 
 * 实现了完整的卡尔曼滤波器跟踪算法，用于更准确地预测游戏中对象的运动。
 * 状态向量包括位置、大小和速度，支持非线性运动模型和多跟踪目标。
 */
class KalmanTracker {
public:
    /**
     * 构造函数
     * 
     * @param detection 初始检测对象
     * @param track_id 跟踪ID
     */
    KalmanTracker(const Detection& detection, uint32_t track_id);
    
    /**
     * 析构函数
     */
    ~KalmanTracker() = default;
    
    /**
     * 使用新的检测结果更新跟踪器
     * 
     * @param detection 检测对象
     */
    void update(const Detection& detection);
    
    /**
     * 预测指定时间点的目标位置
     * 
     * @param timestamp 目标时间戳（毫秒）
     * @return 预测的边界框
     */
    BoundingBox predict(uint64_t timestamp);
    
    /**
     * 获取跟踪ID
     * 
     * @return 跟踪ID
     */
    uint32_t getTrackId() const { return track_id_; }
    
    /**
     * 获取目标类别ID
     * 
     * @return 类别ID
     */
    int getClassId() const { return class_id_; }
    
    /**
     * 获取目标置信度
     * 
     * @return 置信度 (0-1)
     */
    float getConfidence() const { return confidence_; }
    
    /**
     * 获取上次更新时间
     * 
     * @return 时间戳（毫秒）
     */
    uint64_t getLastUpdateTime() const { return last_update_time_; }
    
    /**
     * 获取跟踪年龄（毫秒）
     * 
     * @param current_time 当前时间戳
     * @return 跟踪年龄（毫秒）
     */
    uint64_t getAge(uint64_t current_time) const;
    
    /**
     * 检查跟踪器是否已过期
     * 
     * @param current_time 当前时间戳
     * @param max_age 最大允许年龄（毫秒）
     * @return 是否过期
     */
    bool isExpired(uint64_t current_time, uint64_t max_age) const;
    
    /**
     * 获取运动速度向量
     * 
     * @return X,Y轴速度
     */
    Vector2D getVelocity() const;
    
    /**
     * 获取运动加速度向量
     * 
     * @return X,Y轴加速度
     */
    Vector2D getAcceleration() const;
    
    /**
     * 计算预测的未来轨迹
     * 
     * @param time_steps 预测步数
     * @param interval 每步时间间隔（毫秒）
     * @return 预测轨迹点列表
     */
    std::vector<Point2D> predictTrajectory(size_t time_steps, uint64_t interval) const;
    
    /**
     * 获取状态协方差矩阵，用于评估不确定性
     * 
     * @return 协方差矩阵
     */
    cv::Mat getCovariance() const;

private:
    // 跟踪数据
    uint32_t track_id_;               // 跟踪ID
    int class_id_;                    // 类别ID
    float confidence_;                // 置信度
    uint64_t last_update_time_;       // 上次更新时间
    uint64_t creation_time_;          // 创建时间
    int hit_count_;                   // 命中计数
    int miss_count_;                  // 未命中计数
    
    // 卡尔曼滤波器
    cv::KalmanFilter kf_;             // OpenCV卡尔曼滤波器
    
    // 测量历史
    std::vector<BoundingBox> history_; // 历史边界框
    size_t max_history_size_;         // 最大历史大小
    
    // 运动预测参数
    float process_noise_pos_;         // 位置过程噪声
    float process_noise_vel_;         // 速度过程噪声
    float process_noise_acc_;         // 加速度过程噪声
    float measurement_noise_;         // 测量噪声
    
    // 配置卡尔曼滤波器
    void setupKalmanFilter();
    
    // 设置过程和测量噪声
    void setNoiseParameters(float pos, float vel, float acc, float meas);
    
    // 更新转移矩阵
    void updateTransitionMatrix(float dt);
    
    // 辅助函数：限制边界框参数在有效范围内
    static BoundingBox clampBoundingBox(const BoundingBox& box);
};

/**
 * 多目标跟踪器管理类
 * 
 * 管理多个KalmanTracker实例，实现目标的关联和跟踪
 */
class MultiObjectTracker {
public:
    /**
     * 构造函数
     * 
     * @param max_age 最大跟踪年龄（毫秒）
     * @param min_hits 最小命中次数，用于确认跟踪
     * @param iou_threshold 关联阈值（IoU）
     */
    MultiObjectTracker(uint64_t max_age = 500, int min_hits = 3, float iou_threshold = 0.3);
    
    /**
     * 析构函数
     */
    ~MultiObjectTracker() = default;
    
    /**
     * 更新跟踪器，处理新的检测结果
     * 
     * @param detections 当前帧的检测结果
     * @param timestamp 当前时间戳
     * @return 更新后的跟踪结果
     */
    std::vector<Detection> update(const std::vector<Detection>& detections, uint64_t timestamp);
    
    /**
     * 预测指定时间的所有跟踪对象位置
     * 
     * @param timestamp 目标时间戳
     * @return 预测的检测结果
     */
    std::vector<Detection> predict(uint64_t timestamp);
    
    /**
     * 获取当前所有跟踪器
     * 
     * @return 跟踪器映射表
     */
    const std::unordered_map<uint32_t, std::shared_ptr<KalmanTracker>>& getTrackers() const;
    
    /**
     * 通过ID获取特定跟踪器
     * 
     * @param track_id 跟踪ID
     * @return 跟踪器指针（不存在则为nullptr）
     */
    std::shared_ptr<KalmanTracker> getTrackerById(uint32_t track_id) const;
    
    /**
     * 清除所有跟踪器
     */
    void clear();
    
    /**
     * 获取跟踪器数量
     * 
     * @return 当前跟踪器数量
     */
    size_t count() const;

private:
    uint64_t max_age_;                // 最大跟踪年龄（毫秒）
    int min_hits_;                    // 最小命中次数
    float iou_threshold_;             // 关联阈值
    uint32_t next_track_id_;          // 下一个分配的跟踪ID
    
    // 跟踪器集合
    std::unordered_map<uint32_t, std::shared_ptr<KalmanTracker>> trackers_;
    
    // 计算IoU
    float calculateIoU(const BoundingBox& box1, const BoundingBox& box2) const;
    
    // 匹配检测结果与现有跟踪器
    std::vector<std::pair<int, int>> matchDetectionsToTrackers(
        const std::vector<Detection>& detections,
        const std::vector<std::shared_ptr<KalmanTracker>>& trackers);
        
    // 匈牙利算法求解最优匹配 (基于IoU)
    std::vector<std::pair<int, int>> hungarianMatching(const std::vector<std::vector<float>>& cost_matrix);
};

} // namespace zero_latency