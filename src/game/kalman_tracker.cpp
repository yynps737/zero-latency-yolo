#include "kalman_tracker.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include "../common/logger.h"

namespace zero_latency {

//
// KalmanTracker实现
//

KalmanTracker::KalmanTracker(const Detection& detection, uint32_t track_id)
    : track_id_(track_id), 
      class_id_(detection.class_id),
      confidence_(detection.confidence),
      last_update_time_(detection.timestamp),
      creation_time_(detection.timestamp),
      hit_count_(1),
      miss_count_(0),
      max_history_size_(30),
      process_noise_pos_(1e-2),
      process_noise_vel_(5e-2),
      process_noise_acc_(1e-1),
      measurement_noise_(1e-1)
{
    // 初始化卡尔曼滤波器
    // 状态向量: [x, y, w, h, vx, vy, vw, vh]
    // 测量向量: [x, y, w, h]
    kf_.init(8, 4, 0);
    
    // 设置卡尔曼滤波参数
    setupKalmanFilter();
    
    // 初始化状态向量
    kf_.statePost.at<float>(0) = detection.box.x;
    kf_.statePost.at<float>(1) = detection.box.y;
    kf_.statePost.at<float>(2) = detection.box.width;
    kf_.statePost.at<float>(3) = detection.box.height;
    kf_.statePost.at<float>(4) = 0.0f; // vx
    kf_.statePost.at<float>(5) = 0.0f; // vy
    kf_.statePost.at<float>(6) = 0.0f; // vw
    kf_.statePost.at<float>(7) = 0.0f; // vh
    
    // 添加到历史记录
    history_.push_back(detection.box);
}

void KalmanTracker::setupKalmanFilter() {
    // 状态转移矩阵 (A) - 初始化为单位矩阵
    kf_.transitionMatrix = cv::Mat::eye(8, 8, CV_32F);
    
    // 更新状态转移矩阵，使其包含速度分量
    // x' = x + vx*dt
    // y' = y + vy*dt
    // w' = w + vw*dt
    // h' = h + vh*dt
    // 初始dt=1
    float dt = 1.0f;
    updateTransitionMatrix(dt);
    
    // 测量矩阵 (H) - 将状态向量映射到测量向量
    cv::Mat measurement_matrix = cv::Mat::zeros(4, 8, CV_32F);
    measurement_matrix.at<float>(0, 0) = 1.0f; // x
    measurement_matrix.at<float>(1, 1) = 1.0f; // y
    measurement_matrix.at<float>(2, 2) = 1.0f; // w
    measurement_matrix.at<float>(3, 3) = 1.0f; // h
    kf_.measurementMatrix = measurement_matrix;
    
    // 设置噪声参数
    setNoiseParameters(process_noise_pos_, process_noise_vel_, process_noise_acc_, measurement_noise_);
    
    // 初始化后验误差协方差矩阵 (P)
    cv::setIdentity(kf_.errorCovPost, cv::Scalar::all(1));
}

void KalmanTracker::setNoiseParameters(float pos, float vel, float acc, float meas) {
    // 设置过程噪声协方差矩阵 (Q)
    cv::Mat process_noise_cov = cv::Mat::zeros(8, 8, CV_32F);
    // 位置噪声
    process_noise_cov.at<float>(0, 0) = pos;
    process_noise_cov.at<float>(1, 1) = pos;
    process_noise_cov.at<float>(2, 2) = pos;
    process_noise_cov.at<float>(3, 3) = pos;
    // 速度噪声
    process_noise_cov.at<float>(4, 4) = vel;
    process_noise_cov.at<float>(5, 5) = vel;
    process_noise_cov.at<float>(6, 6) = vel;
    process_noise_cov.at<float>(7, 7) = vel;
    kf_.processNoiseCov = process_noise_cov;
    
    // 设置测量噪声协方差矩阵 (R)
    cv::setIdentity(kf_.measurementNoiseCov, cv::Scalar::all(meas));
}

void KalmanTracker::updateTransitionMatrix(float dt) {
    // 更新状态转移矩阵中的时间增量
    kf_.transitionMatrix.at<float>(0, 4) = dt; // x += vx*dt
    kf_.transitionMatrix.at<float>(1, 5) = dt; // y += vy*dt
    kf_.transitionMatrix.at<float>(2, 6) = dt; // w += vw*dt
    kf_.transitionMatrix.at<float>(3, 7) = dt; // h += vh*dt
}

void KalmanTracker::update(const Detection& detection) {
    // 创建测量矩阵
    cv::Mat measurement = (cv::Mat_<float>(4, 1) <<
        detection.box.x,
        detection.box.y,
        detection.box.width,
        detection.box.height
    );
    
    // 计算自上次更新以来的时间差 (秒)
    float dt = (detection.timestamp - last_update_time_) / 1000.0f;
    if (dt > 0) {
        // 更新状态转移矩阵
        updateTransitionMatrix(dt);
    }
    
    // 使用新的测量更新卡尔曼滤波器
    kf_.correct(measurement);
    
    // 更新跟踪器状态
    last_update_time_ = detection.timestamp;
    hit_count_++;
    miss_count_ = 0;
    
    // 更新目标类别和置信度
    class_id_ = detection.class_id;
    confidence_ = 0.7f * confidence_ + 0.3f * detection.confidence; // 平滑更新置信度
    
    // 更新历史记录
    history_.push_back(detection.box);
    if (history_.size() > max_history_size_) {
        history_.erase(history_.begin());
    }
}

BoundingBox KalmanTracker::predict(uint64_t timestamp) {
    // 计算自上次更新以来的时间差 (秒)
    float dt = (timestamp - last_update_time_) / 1000.0f;
    
    // 确保时间增量合理 (防止除零或过大值)
    dt = std::max(0.001f, std::min(dt, 1.0f));
    
    // 更新状态转移矩阵
    updateTransitionMatrix(dt);
    
    // 预测下一状态
    cv::Mat prediction = kf_.predict();
    
    // 创建预测的边界框
    BoundingBox predicted_box;
    predicted_box.x = prediction.at<float>(0);
    predicted_box.y = prediction.at<float>(1);
    predicted_box.width = prediction.at<float>(2);
    predicted_box.height = prediction.at<float>(3);
    
    // 确保边界框参数有效
    return clampBoundingBox(predicted_box);
}

uint64_t KalmanTracker::getAge(uint64_t current_time) const {
    return current_time - creation_time_;
}

bool KalmanTracker::isExpired(uint64_t current_time, uint64_t max_age) const {
    // 跟踪器年龄过大 或 长时间未更新
    return (current_time - creation_time_ > max_age) || 
           (current_time - last_update_time_ > max_age / 2);
}

Vector2D KalmanTracker::getVelocity() const {
    Vector2D velocity;
    velocity.x = kf_.statePost.at<float>(4);
    velocity.y = kf_.statePost.at<float>(5);
    return velocity;
}

Vector2D KalmanTracker::getAcceleration() const {
    // 如果历史记录不足，无法计算加速度
    if (history_.size() < 3) {
        return {0.0f, 0.0f};
    }
    
    // 使用最近三个位置估计加速度
    const auto& pos1 = history_[history_.size() - 3];
    const auto& pos2 = history_[history_.size() - 2];
    const auto& pos3 = history_[history_.size() - 1];
    
    // 计算两个相邻速度
    float vx1 = pos2.x - pos1.x;
    float vy1 = pos2.y - pos1.y;
    float vx2 = pos3.x - pos2.x;
    float vy2 = pos3.y - pos2.y;
    
    // 估计加速度
    Vector2D acceleration;
    acceleration.x = vx2 - vx1;
    acceleration.y = vy2 - vy1;
    
    return acceleration;
}

std::vector<Point2D> KalmanTracker::predictTrajectory(size_t time_steps, uint64_t interval) const {
    std::vector<Point2D> trajectory;
    trajectory.reserve(time_steps);
    
    // 创建卡尔曼滤波器的副本
    cv::KalmanFilter kf_copy = kf_;
    
    // 间隔转换为秒
    float dt = interval / 1000.0f;
    
    // 更新副本的状态转移矩阵
    kf_copy.transitionMatrix.at<float>(0, 4) = dt; // x += vx*dt
    kf_copy.transitionMatrix.at<float>(1, 5) = dt; // y += vy*dt
    kf_copy.transitionMatrix.at<float>(2, 6) = dt; // w += vw*dt
    kf_copy.transitionMatrix.at<float>(3, 7) = dt; // h += vh*dt
    
    // 使用副本进行多步预测
    cv::Mat state = kf_copy.statePost.clone();
    for (size_t i = 0; i < time_steps; i++) {
        // 预测下一状态
        state = kf_copy.transitionMatrix * state;
        
        // 添加预测点
        Point2D point;
        point.x = state.at<float>(0);
        point.y = state.at<float>(1);
        trajectory.push_back(point);
    }
    
    return trajectory;
}

cv::Mat KalmanTracker::getCovariance() const {
    return kf_.errorCovPost.clone();
}

BoundingBox KalmanTracker::clampBoundingBox(const BoundingBox& box) {
    BoundingBox clamped = box;
    
    // 确保坐标在[0,1]范围内
    clamped.x = std::clamp(clamped.x, 0.0f, 1.0f);
    clamped.y = std::clamp(clamped.y, 0.0f, 1.0f);
    
    // 确保尺寸为正且合理
    clamped.width = std::clamp(clamped.width, 0.01f, 1.0f);
    clamped.height = std::clamp(clamped.height, 0.01f, 1.0f);
    
    // 确保边界框不会超出画面
    if (clamped.x + clamped.width/2 > 1.0f) clamped.x = 1.0f - clamped.width/2;
    if (clamped.x - clamped.width/2 < 0.0f) clamped.x = clamped.width/2;
    if (clamped.y + clamped.height/2 > 1.0f) clamped.y = 1.0f - clamped.height/2;
    if (clamped.y - clamped.height/2 < 0.0f) clamped.y = clamped.height/2;
    
    return clamped;
}

//
// MultiObjectTracker实现
//

MultiObjectTracker::MultiObjectTracker(uint64_t max_age, int min_hits, float iou_threshold)
    : max_age_(max_age), 
      min_hits_(min_hits), 
      iou_threshold_(iou_threshold),
      next_track_id_(1)
{
}

std::vector<Detection> MultiObjectTracker::update(
    const std::vector<Detection>& detections, 
    uint64_t timestamp)
{
    // 准备当前活跃的跟踪器
    std::vector<std::shared_ptr<KalmanTracker>> active_trackers;
    for (auto& [id, tracker] : trackers_) {
        // 预测当前位置 (不管有没有测量)
        tracker->predict(timestamp);
        active_trackers.push_back(tracker);
    }
    
    // 匹配检测结果与跟踪器
    auto matches = matchDetectionsToTrackers(detections, active_trackers);
    
    // 跟踪未匹配的检测 (创建新的跟踪器)
    std::vector<bool> detection_matched(detections.size(), false);
    for (const auto& [det_idx, track_idx] : matches) {
        detection_matched[det_idx] = true;
    }
    
    for (size_t i = 0; i < detections.size(); i++) {
        if (!detection_matched[i]) {
            // 创建新的跟踪器
            auto tracker = std::make_shared<KalmanTracker>(detections[i], next_track_id_++);
            trackers_[tracker->getTrackId()] = tracker;
        }
    }
    
    // 更新匹配上的跟踪器
    for (const auto& [det_idx, track_idx] : matches) {
        active_trackers[track_idx]->update(detections[det_idx]);
    }
    
    // 更新未匹配上的跟踪器状态
    std::vector<uint32_t> trackers_to_remove;
    for (auto& [id, tracker] : trackers_) {
        // 检查是否过期
        if (tracker->isExpired(timestamp, max_age_)) {
            trackers_to_remove.push_back(id);
        }
    }
    
    // 移除过期跟踪器
    for (uint32_t id : trackers_to_remove) {
        trackers_.erase(id);
    }
    
    // 创建结果检测列表 (将跟踪ID关联到检测结果)
    std::vector<Detection> tracked_detections;
    
    // 首先添加匹配的检测
    for (const auto& [det_idx, track_idx] : matches) {
        // 直接使用原始检测，但添加跟踪ID
        Detection tracked_det = detections[det_idx];
        tracked_det.track_id = active_trackers[track_idx]->getTrackId();
        tracked_detections.push_back(tracked_det);
    }
    
    // 然后添加确认的（已达到最小命中数）但未匹配的跟踪器预测
    for (auto& [id, tracker] : trackers_) {
        bool is_matched = false;
        for (const auto& [det_idx, track_idx] : matches) {
            if (active_trackers[track_idx]->getTrackId() == id) {
                is_matched = true;
                break;
            }
        }
        
        if (!is_matched && tracker->hit_count_ >= min_hits_) {
            // 创建从跟踪器预测的检测
            Detection pred_det;
            pred_det.box = tracker->predict(timestamp);
            pred_det.confidence = tracker->getConfidence() * 0.9f; // 轻微降低置信度
            pred_det.class_id = tracker->getClassId();
            pred_det.track_id = tracker->getTrackId();
            pred_det.timestamp = timestamp;
            tracked_detections.push_back(pred_det);
        }
    }
    
    return tracked_detections;
}

std::vector<Detection> MultiObjectTracker::predict(uint64_t timestamp) {
    std::vector<Detection> predictions;
    
    for (auto& [id, tracker] : trackers_) {
        if (tracker->hit_count_ >= min_hits_) {
            // 创建从跟踪器预测的检测
            Detection pred_det;
            pred_det.box = tracker->predict(timestamp);
            pred_det.confidence = tracker->getConfidence() * 0.95f; // 轻微降低置信度
            pred_det.class_id = tracker->getClassId();
            pred_det.track_id = tracker->getTrackId();
            pred_det.timestamp = timestamp;
            predictions.push_back(pred_det);
        }
    }
    
    return predictions;
}

const std::unordered_map<uint32_t, std::shared_ptr<KalmanTracker>>& 
MultiObjectTracker::getTrackers() const {
    return trackers_;
}

std::shared_ptr<KalmanTracker> MultiObjectTracker::getTrackerById(uint32_t track_id) const {
    auto it = trackers_.find(track_id);
    if (it != trackers_.end()) {
        return it->second;
    }
    return nullptr;
}

void MultiObjectTracker::clear() {
    trackers_.clear();
}

size_t MultiObjectTracker::count() const {
    return trackers_.size();
}

float MultiObjectTracker::calculateIoU(const BoundingBox& box1, const BoundingBox& box2) const {
    // 计算每个边界框的左上角和右下角
    float x1_min = box1.x - box1.width / 2;
    float y1_min = box1.y - box1.height / 2;
    float x1_max = box1.x + box1.width / 2;
    float y1_max = box1.y + box1.height / 2;
    
    float x2_min = box2.x - box2.width / 2;
    float y2_min = box2.y - box2.height / 2;
    float x2_max = box2.x + box2.width / 2;
    float y2_max = box2.y + box2.height / 2;
    
    // 计算交集区域
    float x_overlap = std::max(0.0f, std::min(x1_max, x2_max) - std::max(x1_min, x2_min));
    float y_overlap = std::max(0.0f, std::min(y1_max, y2_max) - std::max(y1_min, y2_min));
    float intersection = x_overlap * y_overlap;
    
    // 计算并集区域
    float area1 = box1.width * box1.height;
    float area2 = box2.width * box2.height;
    float union_area = area1 + area2 - intersection;
    
    // 计算IoU
    if (union_area > 0) {
        return intersection / union_area;
    }
    
    return 0.0f;
}

std::vector<std::pair<int, int>> MultiObjectTracker::matchDetectionsToTrackers(
    const std::vector<Detection>& detections,
    const std::vector<std::shared_ptr<KalmanTracker>>& trackers)
{
    if (detections.empty() || trackers.empty()) {
        return {};
    }
    
    // 计算IoU成本矩阵
    std::vector<std::vector<float>> cost_matrix(detections.size(), 
                                              std::vector<float>(trackers.size(), 0));
    
    for (size_t i = 0; i < detections.size(); i++) {
        for (size_t j = 0; j < trackers.size(); j++) {
            // 使用预测的边界框计算IoU
            BoundingBox predicted_box = trackers[j]->predict(detections[i].timestamp);
            float iou = calculateIoU(detections[i].box, predicted_box);
            
            // 转换为成本 (1-IoU)
            cost_matrix[i][j] = 1.0f - iou;
        }
    }
    
    // 使用匈牙利算法求解最优匹配
    std::vector<std::pair<int, int>> matches = hungarianMatching(cost_matrix);
    
    // 过滤低IoU匹配
    std::vector<std::pair<int, int>> good_matches;
    for (const auto& [i, j] : matches) {
        if (cost_matrix[i][j] <= 1.0f - iou_threshold_) {
            good_matches.emplace_back(i, j);
        }
    }
    
    return good_matches;
}

// 匈牙利算法实现 - 基于Munkres算法
std::vector<std::pair<int, int>> MultiObjectTracker::hungarianMatching(
    const std::vector<std::vector<float>>& cost_matrix)
{
    // 如果矩阵为空，返回空结果
    if (cost_matrix.empty() || cost_matrix[0].empty()) {
        return {};
    }
    
    const size_t rows = cost_matrix.size();
    const size_t cols = cost_matrix[0].size();
    
    // 创建OpenCV矩阵
    cv::Mat costs(rows, cols, CV_32F);
    for (size_t i = 0; i < rows; i++) {
        for (size_t j = 0; j < cols; j++) {
            costs.at<float>(i, j) = cost_matrix[i][j];
        }
    }
    
    // 使用OpenCV匈牙利算法
    cv::Mat assignment;
    cv::hungarian(costs, assignment);
    
    // 构建结果
    std::vector<std::pair<int, int>> matches;
    for (int i = 0; i < assignment.rows; i++) {
        int j = assignment.at<int>(i, 0);
        if (j >= 0) {
            matches.emplace_back(i, j);
        }
    }
    
    return matches;
}

} // namespace zero_latency