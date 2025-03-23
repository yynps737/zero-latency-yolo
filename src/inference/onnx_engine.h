#pragma once

#include "inference_engine.h"
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <onnxruntime_cxx_api.h>
#include <future>
#include <deque>
#include <unordered_map>
#include <opencv2/opencv.hpp>

namespace zero_latency {

// 前向声明
struct BoundingBox;
struct Detection;

// 模型版本和信息
struct ModelInfo {
    std::string path;                 // 模型路径
    std::string hash;                 // 模型哈希值
    float version;                    // 版本号
    uint64_t timestamp;               // 加载时间戳
    uint16_t input_height;            // 输入高度
    uint16_t input_width;             // 输入宽度
    bool is_quantized;                // 是否量化
    bool is_opset15_compatible;       // 是否兼容ONNX OpSet 15
};

// 线程安全且无锁的工作队列
template<typename T>
class WorkQueue {
private:
    // 内部节点结构
    struct Node {
        std::shared_ptr<T> data;
        std::atomic<Node*> next;
        Node() : next(nullptr) {}
    };

    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    std::atomic<size_t> size_;
    
public:
    WorkQueue() : size_(0) {
        Node* dummy = new Node();
        head_.store(dummy);
        tail_.store(dummy);
    }
    
    ~WorkQueue() {
        while(try_pop() != nullptr) {}
        Node* dummy = head_.load();
        delete dummy;
    }
    
    // 添加任务至队列
    void push(T item) {
        Node* new_node = new Node();
        new_node->data = std::make_shared<T>(std::move(item));
        
        // 原子操作，确保线程安全
        Node* prev_tail = tail_.load(std::memory_order_relaxed);
        Node* expected_next = nullptr;
        
        while(!prev_tail->next.compare_exchange_weak(
            expected_next, new_node,
            std::memory_order_release,
            std::memory_order_relaxed)) {
            // 队列被其他线程修改，读取新尾节点
            prev_tail = tail_.load(std::memory_order_relaxed);
            expected_next = nullptr;
        }
        
        // 更新尾指针
        tail_.compare_exchange_strong(prev_tail, new_node);
        size_.fetch_add(1, std::memory_order_release);
    }
    
    // 尝试从队列中取出任务，如果为空则返回nullptr
    std::shared_ptr<T> try_pop() {
        Node* old_head = head_.load(std::memory_order_relaxed);
        Node* next = old_head->next.load(std::memory_order_acquire);
        
        if (next == nullptr) {
            return nullptr;  // 队列为空
        }
        
        // 尝试更新头节点
        if (head_.compare_exchange_strong(old_head, next, 
                                        std::memory_order_release,
                                        std::memory_order_relaxed)) {
            std::shared_ptr<T> result = next->data;
            delete old_head;  // 删除旧头节点
            size_.fetch_sub(1, std::memory_order_release);
            return result;
        }
        
        return nullptr;  // 其他线程已修改队列
    }
    
    // 获取当前队列大小
    size_t size() const {
        return size_.load(std::memory_order_acquire);
    }
    
    // 检查队列是否为空
    bool empty() const {
        return size() == 0;
    }
};

// ONNX推理引擎优化实现
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
    // 推理任务定义
    struct InferenceTask {
        InferenceRequest request;
        std::promise<Result<GameState>> promise;
        uint64_t enqueue_time;
        uint8_t priority;
        
        InferenceTask() : enqueue_time(0), priority(0) {}
    };
    
    // 推理线程函数
    void inferenceThreadFunc();
    
    // 工作线程函数
    void workerThreadFunc();
    
    // 模型监控线程函数
    void modelMonitorThreadFunc();
    
    // 执行单个推理请求
    Result<GameState> runInference(const InferenceRequest& request);
    
    // 图像预处理
    Result<std::vector<float>> preProcess(
        const std::vector<uint8_t>& image_data, 
        int width, 
        int height,
        ReusableBuffer<float>& buffer);
    
    // 零拷贝预处理
    Result<std::vector<float>> preProcessZeroCopy(
        const uint8_t* image_data,
        size_t data_size, 
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
    
    // 模型版本检查和加载
    Result<void> loadModel(const std::string& model_path, bool force_reload = false);
    
    // 计算模型哈希值
    std::string calculateModelHash(const std::string& model_path);
    
    // 配置INT8量化
    Result<void> configureQuantization();
    
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
    
    // 模型信息
    ModelInfo model_info_;
    
    // 线程控制
    std::atomic<bool> running_;
    std::thread inference_thread_;
    std::vector<std::thread> worker_threads_;
    std::thread model_monitor_thread_;
    
    // 高性能无锁任务队列
    WorkQueue<InferenceTask> tasks_;
    
    // 线程同步原语
    std::mutex session_mutex_;  // 保护模型访问
    std::condition_variable workers_cv_;
    std::mutex notify_mutex_;
    
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
    std::atomic<uint64_t> inference_errors_;
    std::atomic<uint64_t> dropped_frames_;
    std::atomic<uint64_t> avg_inference_latency_ms_;
    std::atomic<uint64_t> p99_inference_latency_ms_;
    
    // 性能监控
    std::deque<uint64_t> latency_history_;  // 最近100个推理延迟
    std::mutex latency_mutex_;
    
    // 功能开关
    bool simulation_mode_;            // 模拟模式
    bool use_int8_quantization_;      // 使用INT8量化
    bool use_zero_copy_;              // 使用零拷贝
    bool use_dynamic_batching_;       // 使用动态批处理
    bool use_model_monitor_;          // 启用模型监控
    bool use_priority_scheduling_;    // 启用优先级调度
    
    // 动态批处理
    struct BatchContext {
        std::vector<InferenceTask> tasks;
        std::chrono::steady_clock::time_point batch_start;
        uint32_t max_batch_size;
        uint32_t min_batch_size; 
        bool ready;
        
        BatchContext() : max_batch_size(4), min_batch_size(1), ready(false) {}
    };
    BatchContext batch_context_;
    std::mutex batch_mutex_;
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