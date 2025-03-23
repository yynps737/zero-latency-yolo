#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>

#include "../common/types.h"
#include "config.h"

namespace zero_latency {

// 推理请求结构
struct InferenceRequest {
    uint32_t client_id;
    uint32_t frame_id;
    uint64_t timestamp;
    uint16_t width;
    uint16_t height;
    std::vector<uint8_t> data;
    bool is_keyframe;
};

// 推理结果回调函数类型
using InferenceCallback = std::function<void(uint32_t client_id, const GameState& state)>;

class YoloEngine {
public:
    YoloEngine(const ServerConfig& config);
    ~YoloEngine();
    
    // 初始化YOLO引擎
    bool initialize();
    
    // 关闭引擎
    void shutdown();
    
    // 提交推理请求
    bool submitInference(const InferenceRequest& request);
    
    // 设置结果回调
    void setCallback(InferenceCallback callback);
    
    // 获取当前队列大小
    size_t getQueueSize() const;

private:
    // 推理线程函数
    void inferenceThread();
    
    // 执行推理
    GameState runInference(const InferenceRequest& request);
    
    // 预处理图像
    std::vector<float> preProcess(const std::vector<uint8_t>& image_data, int width, int height);
    
    // 后处理结果
    std::vector<Detection> postProcess(Ort::Value& output_tensor, int img_width, int img_height);
    
    // 应用非极大值抑制
    std::vector<Detection> applyNMS(std::vector<Detection>& detections, float iou_threshold);
    
    // 计算IoU(交并比)
    float calculateIoU(const BoundingBox& box1, const BoundingBox& box2);
    
    // BGR转RGB转换
    void bgrToRgb(std::vector<uint8_t>& data);
    
    // 预热模型
    void warmupModel();

private:
    // 配置
    ServerConfig config_;
    
    // ONNX运行时环境
    Ort::Env env_;
    
    // ONNX会话选项
    Ort::SessionOptions session_options_;
    
    // ONNX会话
    std::unique_ptr<Ort::Session> session_;
    
    // ONNX内存分配器
    Ort::AllocatorWithDefaultOptions allocator_;
    
    // 输入输出信息
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;
    std::vector<std::vector<int64_t>> input_dims_;
    std::vector<std::vector<int64_t>> output_dims_;
    
    // 线程控制
    std::atomic<bool> running_;
    std::thread inference_thread_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 推理队列
    std::queue<InferenceRequest> inference_queue_;
    
    // 回调函数
    InferenceCallback callback_;
    
    // 统计信息
    std::atomic<uint64_t> inference_count_;
    std::atomic<uint64_t> queue_high_water_mark_;
};

} // namespace zero_latency