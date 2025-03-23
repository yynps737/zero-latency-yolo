#pragma once

#include "inference_engine.h"
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <onnxruntime_cxx_api.h>

namespace zero_latency {

// 前向声明
struct BoundingBox;
struct Detection;

// ONNX推理引擎实现
class OnnxInferenceEngine : public IInferenceEngine {
public:
    // 构造函数
    explicit OnnxInferenceEngine(const ServerConfig& config);
    
    // 析构函数
    ~OnnxInferenceEngine() override;
    
    // 禁用拷贝和赋值
    OnnxInferenceEngine(const OnnxInferenceEngine&) = delete;
    OnnxInferenceEngine& operator=(const OnnxInferenceEngine&) = delete;
    
    // 实现接口方法
    Result<void> initialize() override;
    Result<void> shutdown() override;
    Result<void> submitInference(const InferenceRequest& request) override;
    void setCallback(InferenceCallback callback) override;
    size_t getQueueSize() const override;
    std::string getName() const override;
    std::unordered_map<std::string, std::string> getStatus() const override;

private:
    // 推理线程函数
    void inferenceThreadFunc();
    
    // 工作线程函数
    void workerThreadFunc();
    
    // 执行单个推理请求
    Result<GameState> runInference(const InferenceRequest& request);
    
    // 图像预处理
    Result<std::vector<float>> preProcess(
        const std::vector<uint8_t>& image_data, 
        int width, 
        int height,
        ReusableBuffer<float>& buffer);
    
    // 模型输出后处理
    Result<std::vector<Detection>> postProcess(
        Ort::Value& output_tensor, 
        int img_width, 
        int img_height);
    
    // 应用非极大值抑制
    std::vector<Detection> applyNMS(
        std::vector<Detection>& detections, 
        float iou_threshold);
    
    // 计算两个边界框的IoU
    float calculateIoU(const BoundingBox& box1, const BoundingBox& box2);
    
    // 将BGR格式转换为RGB
    void bgrToRgb(std::vector<uint8_t>& data);
    
    // 预热模型
    Result<void> warmupModel();
    
    // 检查文件是否存在
    bool fileExists(const std::string& path);
    
    // 生成随机检测结果（模拟模式）
    std::vector<Detection> generateRandomDetections(int img_width, int img_height);

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
    std::vector<std::string> input_names_owned_;
    std::vector<std::string> output_names_owned_;
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;
    std::vector<std::vector<int64_t>> input_dims_;
    std::vector<std::vector<int64_t>> output_dims_;
    
    // 线程控制
    std::atomic<bool> running_;
    std::thread inference_thread_;
    std::vector<std::thread> worker_threads_;
    
    // 请求队列保护
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // 用于工作线程的任务队列
    mutable std::mutex tasks_mutex_;
    std::condition_variable tasks_cv_;
    struct InferenceTask {
        InferenceRequest request;
        std::promise<Result<GameState>> promise;
    };
    std::queue<std::shared_ptr<InferenceTask>> tasks_;
    
    // 推理请求队列
    std::queue<InferenceRequest> inference_queue_;
    
    // 内存池
    std::unique_ptr<ThreadLocalBufferPool<float>> input_buffer_pool_;
    
    // 回调函数
    InferenceCallback callback_;
    
    // 统计信息
    std::atomic<uint64_t> inference_count_;
    std::atomic<uint64_t> queue_high_water_mark_;
    std::atomic<uint64_t> total_inference_time_ms_;
    std::atomic<uint64_t> total_preprocessing_time_ms_;
    std::atomic<uint64_t> total_postprocessing_time_ms_;
    
    // 模拟模式标志
    bool simulation_mode_;
};

// ONNX推理引擎工厂
class OnnxInferenceEngineFactory : public IInferenceEngineFactory {
public:
    std::unique_ptr<IInferenceEngine> createEngine(const ServerConfig& config) override {
        return std::make_unique<OnnxInferenceEngine>(config);
    }
    
    std::string getName() const override {
        return "onnx";
    }
};

// 注册工厂
REGISTER_INFERENCE_ENGINE(OnnxInferenceEngineFactory)

} // namespace zero_latency