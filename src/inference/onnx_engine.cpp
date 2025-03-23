#include "onnx_engine.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <future>
#include <random>
#include <functional>
#include <openssl/sha.h>
#include <sched.h>
#include "../common/event_bus.h"
#include "../common/concurrent_queue.h"
#include "../common/memory_pool.h"
#include "../common/types.h"

namespace zero_latency {

// 构造函数
OnnxInferenceEngine::OnnxInferenceEngine(const ServerConfig& config)
    : config_(config),
      env_(ORT_LOGGING_LEVEL_WARNING, "OnnxInferenceEngine"),
      running_(false),
      inference_count_(0),
      queue_high_water_mark_(0),
      total_inference_time_ms_(0),
      total_preprocessing_time_ms_(0),
      total_postprocessing_time_ms_(0),
      inference_errors_(0),
      dropped_frames_(0),
      avg_inference_latency_ms_(0),
      p99_inference_latency_ms_(0),
      simulation_mode_(false),
      use_int8_quantization_(config.optimization.use_int8_quantization),
      use_zero_copy_(config.optimization.use_zero_copy),
      use_dynamic_batching_(config.optimization.use_dynamic_batching),
      use_model_monitor_(config.optimization.use_model_monitor),
      use_priority_scheduling_(config.optimization.use_priority_scheduling) {
    
    // 初始化随机数种子
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    
    // 初始化内存池
    input_buffer_pool_ = std::make_unique<ThreadLocalBufferPool<float>>(
        config_.detection.model_width * config_.detection.model_height * 3);
    
    // 初始化输入输出名称
    input_names_owned_.push_back("images");  // YOLOv8 默认输入名称
    output_names_owned_.push_back("output0"); // YOLOv8 默认输出名称
    
    for (const auto& name : input_names_owned_) {
        input_names_.push_back(name.c_str());
    }
    
    for (const auto& name : output_names_owned_) {
        output_names_.push_back(name.c_str());
    }
}

// 析构函数
OnnxInferenceEngine::~OnnxInferenceEngine() {
    shutdown();
}

// 初始化引擎
Result<void> OnnxInferenceEngine::initialize() {
    try {
        // 检查模型文件是否存在
        if (!fileExists(config_.model_path)) {
            LOG_ERROR("YOLO model file not found: " + config_.model_path);
            LOG_WARN("Using simulation mode (will generate random detections)");
            simulation_mode_ = true;
            return Result<void>::ok();  // 即使模型不存在，也继续运行，使用模拟模式
        }
        
        // 配置会话选项
        session_options_ = Ort::SessionOptions();
        session_options_.SetIntraOpNumThreads(config_.worker_threads > 0 ? config_.worker_threads : 2);
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options_.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        
        // 如果启用了INT8量化，进行相关配置
        if (use_int8_quantization_) {
            auto quant_result = configureQuantization();
            if (quant_result.hasError()) {
                LOG_WARN("Failed to configure INT8 quantization: " + quant_result.error().message);
                use_int8_quantization_ = false;
            }
        }
        
        // 如果可以使用GPU, 则添加CUDA执行提供者
        #ifdef USE_CUDA
        OrtCUDAProviderOptions cuda_options;
        cuda_options.device_id = 0;
        cuda_options.arena_extend_strategy = 0;
        cuda_options.cudnn_conv_algo_search = OrtCudnnConvAlgoSearch::EXHAUSTIVE;
        cuda_options.do_copy_in_default_stream = 1;
        session_options_.AppendExecutionProvider_CUDA(cuda_options);
        LOG_INFO("CUDA execution provider added");
        #endif
        
        // 加载模型
        auto load_result = loadModel(config_.model_path);
        if (load_result.hasError()) {
            LOG_ERROR("Failed to load model: " + load_result.error().message);
            LOG_WARN("Using simulation mode (will generate random detections)");
            simulation_mode_ = true;
            return Result<void>::ok();
        }
        
        // 启动推理线程
        running_ = true;
        
        // 启动主推理线程
        inference_thread_ = std::thread(&OnnxInferenceEngine::inferenceThreadFunc, this);
        
        // 设置推理线程优先级 (如果启用了高优先级)
        if (config_.use_high_priority) {
            struct sched_param param;
            param.sched_priority = 99; // 最高实时优先级
            
            if (pthread_setschedparam(inference_thread_.native_handle(), SCHED_FIFO, &param) != 0) {
                LOG_WARN("Failed to set thread priority for inference thread");
            }
        }
        
        // 设置CPU亲和性 (如果启用了CPU亲和性)
        if (config_.use_cpu_affinity) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(config_.cpu_core_id, &cpuset);
            
            if (pthread_setaffinity_np(inference_thread_.native_handle(), sizeof(cpu_set_t), &cpuset) != 0) {
                LOG_WARN("Failed to set CPU affinity for inference thread");
            }
        }
        
        // 创建工作线程池
        for (uint8_t i = 0; i < config_.worker_threads; ++i) {
            worker_threads_.emplace_back(&OnnxInferenceEngine::workerThreadFunc, this);
        }
        
        // 启动模型监控线程 (如果启用)
        if (use_model_monitor_) {
            model_monitor_thread_ = std::thread(&OnnxInferenceEngine::modelMonitorThreadFunc, this);
        }
        
        LOG_INFO("ONNX inference engine started with " + std::to_string(config_.worker_threads) + " worker threads");
        
        if (simulation_mode_) {
            LOG_INFO("Engine running in simulation mode");
        } else {
            LOG_INFO("Engine running in normal mode" + 
                     (use_int8_quantization_ ? " with INT8 quantization" : "") +
                     (use_zero_copy_ ? " and zero-copy optimization" : ""));
        }
        
        // 发布引擎初始化事件
        Event event(events::SYSTEM_STARTUP);
        event.setSource("OnnxInferenceEngine");
        publishEvent(event);
        
        return Result<void>::ok();
    } catch (const std::exception& e) {
        std::string errorMsg = "Failed to initialize ONNX engine: " + std::string(e.what());
        LOG_ERROR(errorMsg);
        return Result<void>::error(ErrorCode::INFERENCE_ERROR, errorMsg);
    }
}

// 关闭引擎
Result<void> OnnxInferenceEngine::shutdown() {
    if (running_) {
        running_ = false;
        
        // 设置任务以唤醒工作线程
        for (size_t i = 0; i < worker_threads_.size(); ++i) {
            auto task = std::make_shared<InferenceTask>();
            task->promise.set_value(Result<GameState>::error(ErrorCode::SYSTEM_ERROR, "Engine shutting down"));
            tasks_.push(std::move(*task));
        }
        
        // 通知所有工作线程
        {
            std::unique_lock<std::mutex> lock(notify_mutex_);
            workers_cv_.notify_all();
        }
        
        // 等待推理线程退出
        if (inference_thread_.joinable()) {
            inference_thread_.join();
        }
        
        // 等待所有工作线程退出
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        worker_threads_.clear();
        
        // 等待模型监控线程退出
        if (use_model_monitor_ && model_monitor_thread_.joinable()) {
            model_monitor_thread_.join();
        }
        
        // 释放ONNX会话
        session_.reset();
        
        LOG_INFO("ONNX inference engine shutdown completed");
        
        // 发布引擎关闭事件
        Event event(events::SYSTEM_SHUTDOWN);
        event.setSource("OnnxInferenceEngine");
        publishEvent(event);
    }
    
    return Result<void>::ok();
}

// 提交推理请求
Result<void> OnnxInferenceEngine::submitInference(const InferenceRequest& request) {
    if (!running_) {
        return Result<void>::error(ErrorCode::NOT_INITIALIZED, "Engine not running");
    }
    
    // 发布推理请求事件
    EventBus::getInstance().publishInferenceEvent(events::INFERENCE_REQUESTED, 
                                                 request.client_id, 
                                                 request.frame_id);
    
    // 创建推理任务
    auto task = std::make_shared<InferenceTask>();
    task->request = request;
    task->enqueue_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // 设置优先级 (如果启用)
    if (use_priority_scheduling_) {
        // 关键帧具有更高优先级
        task->priority = request.is_keyframe ? 10 : 5;
    }
    
    // 将任务添加到队列
    tasks_.push(std::move(*task));
    
    // 更新高水位标记
    if (tasks_.size() > queue_high_water_mark_) {
        queue_high_water_mark_ = tasks_.size();
    }
    
    // 通知一个工作线程处理任务
    {
        std::unique_lock<std::mutex> lock(notify_mutex_);
        workers_cv_.notify_one();
    }
    
    return Result<void>::ok();
}

// 设置推理结果回调
void OnnxInferenceEngine::setCallback(InferenceCallback callback) {
    callback_ = std::move(callback);
}

// 获取当前推理队列大小
size_t OnnxInferenceEngine::getQueueSize() const {
    return tasks_.size();
}

// 获取引擎名称
std::string OnnxInferenceEngine::getName() const {
    return "onnx";
}

// 获取引擎状态信息
std::unordered_map<std::string, std::string> OnnxInferenceEngine::getStatus() const {
    std::unordered_map<std::string, std::string> status;
    
    status["name"] = getName();
    status["simulation_mode"] = simulation_mode_ ? "true" : "false";
    status["running"] = running_ ? "true" : "false";
    status["model_path"] = config_.model_path;
    status["model_version"] = std::to_string(model_info_.version);
    status["model_hash"] = model_info_.hash;
    status["queue_size"] = std::to_string(getQueueSize());
    status["queue_high_water_mark"] = std::to_string(queue_high_water_mark_);
    status["inference_count"] = std::to_string(inference_count_);
    status["inference_errors"] = std::to_string(inference_errors_);
    status["dropped_frames"] = std::to_string(dropped_frames_);
    status["int8_quantization"] = use_int8_quantization_ ? "enabled" : "disabled";
    status["zero_copy"] = use_zero_copy_ ? "enabled" : "disabled";
    status["dynamic_batching"] = use_dynamic_batching_ ? "enabled" : "disabled";
    
    if (inference_count_ > 0) {
        status["avg_inference_time_ms"] = std::to_string(avg_inference_latency_ms_.load());
        status["p99_inference_time_ms"] = std::to_string(p99_inference_latency_ms_.load());
        status["avg_preprocessing_time_ms"] = std::to_string(total_preprocessing_time_ms_ / inference_count_);
        status["avg_postprocessing_time_ms"] = std::to_string(total_postprocessing_time_ms_ / inference_count_);
    } else {
        status["avg_inference_time_ms"] = "0";
        status["p99_inference_time_ms"] = "0";
        status["avg_preprocessing_time_ms"] = "0";
        status["avg_postprocessing_time_ms"] = "0";
    }
    
    status["worker_threads"] = std::to_string(worker_threads_.size());
    
    return status;
}

// 推理线程函数
void OnnxInferenceEngine::inferenceThreadFunc() {
    LOG_INFO("Inference thread started");
    
    while (running_) {
        // 使用动态批处理模式
        if (use_dynamic_batching_) {
            // 实现批处理逻辑
            std::vector<std::shared_ptr<InferenceTask>> batch_tasks;
            uint32_t max_batch_size = batch_context_.max_batch_size;
            uint32_t current_batch_size = 0;
            
            // 收集批处理任务
            std::chrono::steady_clock::time_point batch_timeout = 
                std::chrono::steady_clock::now() + std::chrono::milliseconds(5); // 5ms最大等待
                
            while (current_batch_size < max_batch_size && 
                   std::chrono::steady_clock::now() < batch_timeout) {
                auto task = tasks_.try_pop();
                if (task) {
                    batch_tasks.push_back(task);
                    current_batch_size++;
                } else if (current_batch_size == 0) {
                    // 队列为空，短暂休眠
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                } else {
                    // 已收集部分批次，中断收集
                    break;
                }
            }
            
            // 处理批处理任务
            if (!batch_tasks.empty()) {
                // TODO: 实现批处理推理
                // 目前逐个处理
                for (auto& task : batch_tasks) {
                    auto result = runInference(task->request);
                    task->promise.set_value(result);
                    
                    // 调用回调
                    if (result.isOk() && callback_) {
                        callback_(task->request.client_id, result.value());
                        
                        // 发布推理完成事件
                        EventBus::getInstance().publishInferenceEvent(
                            events::INFERENCE_COMPLETED, 
                            task->request.client_id, 
                            task->request.frame_id
                        );
                    }
                }
            } else {
                // 空闲时短暂休眠，减少CPU使用
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } 
        // 使用单任务处理模式
        else {
            auto task = tasks_.try_pop();
            
            if (task) {
                auto result = runInference(task->request);
                task->promise.set_value(result);
                
                // 调用回调
                if (result.isOk() && callback_) {
                    callback_(task->request.client_id, result.value());
                    
                    // 发布推理完成事件
                    EventBus::getInstance().publishInferenceEvent(
                        events::INFERENCE_COMPLETED, 
                        task->request.client_id, 
                        task->request.frame_id
                    );
                }
            } else {
                // 空闲时短暂休眠，减少CPU使用
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
    
    LOG_INFO("Inference thread stopped");
}

// 工作线程函数
void OnnxInferenceEngine::workerThreadFunc() {
    LOG_INFO("Worker thread started");
    
    while (running_) {
        std::shared_ptr<InferenceTask> task;
        
        // 获取任务
        task = tasks_.try_pop();
        
        // 如果没有任务，等待通知
        if (!task) {
            std::unique_lock<std::mutex> lock(notify_mutex_);
            workers_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] { 
                return !running_ || tasks_.size() > 0; 
            });
            continue;
        }
        
        // 执行推理
        auto start_time = std::chrono::steady_clock::now();
        auto result = runInference(task->request);
        auto end_time = std::chrono::steady_clock::now();
        
        // 计算延迟
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
        
        // 更新延迟历史
        {
            std::lock_guard<std::mutex> lock(latency_mutex_);
            latency_history_.push_back(duration_ms);
            if (latency_history_.size() > 100) {
                latency_history_.pop_front();
            }
            
            // 计算平均延迟
            uint64_t sum = 0;
            for (auto latency : latency_history_) {
                sum += latency;
            }
            avg_inference_latency_ms_.store(sum / latency_history_.size());
            
            // 计算P99延迟
            if (latency_history_.size() >= 100) {
                std::vector<uint64_t> sorted_latencies(latency_history_.begin(), latency_history_.end());
                std::sort(sorted_latencies.begin(), sorted_latencies.end());
                p99_inference_latency_ms_.store(sorted_latencies[99 * sorted_latencies.size() / 100]);
            }
        }
        
        // 更新统计信息
        inference_count_++;
        total_inference_time_ms_ += duration_ms;
        
        if (result.hasError()) {
            inference_errors_++;
        }
        
        // 设置结果
        task->promise.set_value(result);
        
        // 限制处理速度以维持稳定的帧率
        int target_frame_time = 1000 / config_.target_fps;
        if (duration_ms < target_frame_time) {
            std::this_thread::sleep_for(std::chrono::milliseconds(target_frame_time - duration_ms));
        }
    }
    
    LOG_INFO("Worker thread stopped");
}

// 模型监控线程函数
void OnnxInferenceEngine::modelMonitorThreadFunc() {
    LOG_INFO("Model monitor thread started");
    
    std::string last_hash = model_info_.hash;
    
    while (running_) {
        // 每10秒检查一次模型文件变化
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        if (!running_) break;
        
        // 检查模型文件是否存在
        if (!fileExists(config_.model_path)) {
            LOG_WARN("Model file not found: " + config_.model_path);
            continue;
        }
        
        // 计算当前模型哈希
        std::string current_hash = calculateModelHash(config_.model_path);
        
        // 如果哈希值变化，重新加载模型
        if (current_hash != last_hash) {
            LOG_INFO("Model file changed, reloading...");
            
            auto result = loadModel(config_.model_path, true);
            if (result.isOk()) {
                last_hash = current_hash;
                LOG_INFO("Model reloaded successfully");
                
                // 发布模型更新事件
                Event event("MODEL_UPDATED");
                event.setSource("OnnxInferenceEngine");
                event.setData("model_path", config_.model_path);
                event.setData("model_hash", current_hash);
                publishEvent(event);
            } else {
                LOG_ERROR("Failed to reload model: " + result.error().message);
            }
        }
    }
    
    LOG_INFO("Model monitor thread stopped");
}

// 执行单个推理请求
Result<GameState> OnnxInferenceEngine::runInference(const InferenceRequest& request) {
    GameState state;
    state.frame_id = request.frame_id;
    state.timestamp = request.timestamp;
    
    try {
        if (simulation_mode_) {
            // 在模拟模式下生成随机检测结果
            state.detections = generateRandomDetections(request.width, request.height);
            return Result<GameState>::ok(state);
        }
        
        auto preprocess_start = std::chrono::steady_clock::now();
        
        // 预处理图像数据
        std::vector<float> input_tensor_values;
        Result<std::vector<float>> preprocess_result;
        
        // 获取线程本地缓冲区
        auto& buffer = input_buffer_pool_->getBuffer();
        
        // 使用零拷贝或标准处理
        if (use_zero_copy_ && request.data_ptr != nullptr) {
            preprocess_result = preProcessZeroCopy(
                request.data_ptr, request.data_size, request.width, request.height, buffer);
        } else {
            preprocess_result = preProcess(
                request.data, request.width, request.height, buffer);
        }
        
        if (preprocess_result.hasError()) {
            return Result<GameState>::error(preprocess_result.error());
        }
        
        input_tensor_values = preprocess_result.value();
        
        auto preprocess_end = std::chrono::steady_clock::now();
        auto preprocess_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            preprocess_end - preprocess_start).count();
        total_preprocessing_time_ms_ += preprocess_time;
        
        // 创建输入tensor
        std::vector<int64_t> input_shape = {1, 3, config_.detection.model_height, config_.detection.model_width};
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, 
            input_tensor_values.data(), 
            input_tensor_values.size(), 
            input_shape.data(), 
            input_shape.size()
        );
        
        // 执行推理
        auto inference_start = std::chrono::steady_clock::now();
        
        std::vector<Ort::Value> output_tensors;
        {
            // 使用互斥锁保护会话访问
            std::lock_guard<std::mutex> lock(session_mutex_);
            output_tensors = session_->Run(
                Ort::RunOptions{nullptr}, 
                input_names_.data(), 
                &input_tensor, 
                1, 
                output_names_.data(), 
                1
            );
        }
        
        auto inference_end = std::chrono::steady_clock::now();
        
        // 检查输出
        if (output_tensors.size() != 1) {
            return Result<GameState>::error(ErrorCode::INFERENCE_ERROR, "Invalid output tensor count");
        }
        
        // 后处理结果
        auto postprocess_start = std::chrono::steady_clock::now();
        
        auto postprocess_result = postProcess(output_tensors[0], request.width, request.height);
        if (postprocess_result.hasError()) {
            return Result<GameState>::error(postprocess_result.error());
        }
        
        state.detections = postprocess_result.value();
        
        auto postprocess_end = std::chrono::steady_clock::now();
        
        // 更新统计信息
        auto inference_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            inference_end - inference_start).count();
        auto postprocess_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            postprocess_end - postprocess_start).count();
        
        total_postprocessing_time_ms_ += postprocess_time;
        
        LOG_DEBUG("Inference stats: pre=" + std::to_string(preprocess_time) + 
                 "ms, inf=" + std::to_string(inference_time) + 
                 "ms, post=" + std::to_string(postprocess_time) + 
                 "ms, detections=" + std::to_string(state.detections.size()));
        
        return Result<GameState>::ok(state);
    } catch (const Ort::Exception& e) {
        std::string errorMsg = "ONNX inference error: " + std::string(e.what());
        LOG_ERROR(errorMsg);
        inference_errors_++;
        
        // 出错时使用模拟检测结果
        if (simulation_mode_) {
            state.detections = generateRandomDetections(request.width, request.height);
            return Result<GameState>::ok(state);
        }
        
        return Result<GameState>::error(ErrorCode::INFERENCE_ERROR, errorMsg);
    } catch (const std::exception& e) {
        std::string errorMsg = "Inference error: " + std::string(e.what());
        LOG_ERROR(errorMsg);
        inference_errors_++;
        
        // 出错时使用模拟检测结果
        if (simulation_mode_) {
            state.detections = generateRandomDetections(request.width, request.height);
            return Result<GameState>::ok(state);
        }
        
        return Result<GameState>::error(ErrorCode::INFERENCE_ERROR, errorMsg);
    }
}

// 图像预处理
Result<std::vector<float>> OnnxInferenceEngine::preProcess(
    const std::vector<uint8_t>& image_data, 
    int width, 
    int height,
    ReusableBuffer<float>& buffer) {
    
    const int target_height = config_.detection.model_height;
    const int target_width = config_.detection.model_width;
    
    // 确保图像数据正确
    if (image_data.size() != width * height * 3) {
        return Result<std::vector<float>>::error(
            ErrorCode::INVALID_INPUT, 
            "Invalid image data size: expected " + std::to_string(width * height * 3) + 
            ", got " + std::to_string(image_data.size())
        );
    }
    
    // 重置并调整缓冲区大小
    buffer.reset();
    buffer.resize(3 * target_height * target_width);
    std::vector<float>& result = buffer.getBuffer();
    
    // 计算缩放比例
    float scale_w = static_cast<float>(width) / target_width;
    float scale_h = static_cast<float>(height) / target_height;
    
    // 执行调整大小和归一化
    for (int c = 0; c < 3; c++) {
        for (int h = 0; h < target_height; h++) {
            for (int w = 0; w < target_width; w++) {
                // 计算原始图像中对应的位置
                int src_h = std::min(static_cast<int>(h * scale_h), height - 1);
                int src_w = std::min(static_cast<int>(w * scale_w), width - 1);
                
                // 在BGR格式图像中的索引
                int src_idx = (src_h * width + src_w) * 3 + (2 - c); // BGR转RGB
                
                // 在目标张量中的索引
                int dst_idx = c * target_height * target_width + h * target_width + w;
                
                // 检查索引是否有效
                if (src_idx >= 0 && src_idx < image_data.size() && dst_idx >= 0 && dst_idx < result.size()) {
                    // 归一化到[0,1]
                    result[dst_idx] = image_data[src_idx] / 255.0f;
                }
            }
        }
    }
    
    return Result<std::vector<float>>::ok(result);
}

// 零拷贝预处理
Result<std::vector<float>> OnnxInferenceEngine::preProcessZeroCopy(
    const uint8_t* image_data,
    size_t data_size, 
    int width, 
    int height,
    ReusableBuffer<float>& buffer) {
    
    const int target_height = config_.detection.model_height;
    const int target_width = config_.detection.model_width;
    
    // 确保图像数据正确
    if (data_size != width * height * 3) {
        return Result<std::vector<float>>::error(
            ErrorCode::INVALID_INPUT, 
            "Invalid image data size: expected " + std::to_string(width * height * 3) + 
            ", got " + std::to_string(data_size)
        );
    }
    
    // 重置并调整缓冲区大小
    buffer.reset();
    buffer.resize(3 * target_height * target_width);
    std::vector<float>& result = buffer.getBuffer();
    
    // 计算缩放比例
    float scale_w = static_cast<float>(width) / target_width;
    float scale_h = static_cast<float>(height) / target_height;
    
    // 执行调整大小和归一化
    for (int c = 0; c < 3; c++) {
        for (int h = 0; h < target_height; h++) {
            for (int w = 0; w < target_width; w++) {
                // 计算原始图像中对应的位置
                int src_h = std::min(static_cast<int>(h * scale_h), height - 1);
                int src_w = std::min(static_cast<int>(w * scale_w), width - 1);
                
                // 在BGR格式图像中的索引
                int src_idx = (src_h * width + src_w) * 3 + (2 - c); // BGR转RGB
                
                // 在目标张量中的索引
                int dst_idx = c * target_height * target_width + h * target_width + w;
                
                // 检查索引是否有效
                if (src_idx >= 0 && src_idx < data_size && dst_idx >= 0 && dst_idx < result.size()) {
                    // 归一化到[0,1]
                    result[dst_idx] = image_data[src_idx] / 255.0f;
                }
            }
        }
    }
    
    return Result<std::vector<float>>::ok(result);
}

// 模型输出后处理
Result<std::vector<Detection>> OnnxInferenceEngine::postProcess(
    Ort::Value& output_tensor, 
    int img_width, 
    int img_height) {
    
    try {
        // 获取输出维度
        auto output_dims = output_tensor.GetTensorTypeAndShapeInfo().GetShape();
        
        // YOLOv8模型输出形状为 [1, 84, 8400] 
        // 其中84 = 4(box) + 80(classes), 8400为候选框数量
        // 但我们的模型可能为CS 1.6特化，类别更少
        
        auto* output_data = output_tensor.GetTensorData<float>();
        
        const size_t num_classes = output_dims[1] - 4; // 减去box坐标
        const size_t num_boxes = output_dims[2];       // 候选框数量
        
        std::vector<Detection> detections;
        
        // 遍历所有候选框
        for (size_t i = 0; i < num_boxes; i++) {
            // 提取边界框坐标 (YOLOv8格式: cx, cy, w, h)
            float cx = output_data[0 * num_boxes + i];  // center x
            float cy = output_data[1 * num_boxes + i];  // center y
            float w = output_data[2 * num_boxes + i];   // width
            float h = output_data[3 * num_boxes + i];   // height
            
            // 找到最高置信度的类别
            float max_conf = 0.0f;
            int max_class_id = -1;
            
            for (size_t j = 0; j < num_classes; j++) {
                float class_conf = output_data[(j + 4) * num_boxes + i];
                if (class_conf > max_conf) {
                    max_conf = class_conf;
                    max_class_id = j;
                }
            }
            
            // 应用置信度阈值
            if (max_conf >= config_.confidence_threshold && max_class_id >= 0) {
                // 将坐标转换为标准格式
                BoundingBox box;
                box.x = cx / img_width;      // 归一化中心 x
                box.y = cy / img_height;     // 归一化中心 y
                box.width = w / img_width;   // 归一化宽度
                box.height = h / img_height; // 归一化高度
                
                // 创建检测对象
                Detection det;
                det.box = box;
                det.confidence = max_conf;
                det.class_id = max_class_id;
                det.track_id = 0;  // 暂不支持跟踪，将由GameAdapter处理
                det.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                
                detections.push_back(det);
            }
        }
        
        // 应用非极大值抑制
        if (!detections.empty()) {
            detections = applyNMS(detections, config_.nms_threshold);
        }
        
        return Result<std::vector<Detection>>::ok(detections);
    } catch (const std::exception& e) {
        inference_errors_++;
        return Result<std::vector<Detection>>::error(
            ErrorCode::INFERENCE_ERROR, 
            "Post-processing error: " + std::string(e.what())
        );
    }
}

// 应用非极大值抑制
std::vector<Detection> OnnxInferenceEngine::applyNMS(std::vector<Detection>& detections, float iou_threshold) {
    std::vector<Detection> result;
    
    // 如果只有一个检测或没有检测，直接返回
    if (detections.size() <= 1) {
        return detections;
    }
    
    // 按类别和置信度排序
    std::sort(detections.begin(), detections.end(), [](const Detection& a, const Detection& b) {
        if (a.class_id != b.class_id) {
            return a.class_id < b.class_id;
        }
        return a.confidence > b.confidence;
    });
    
    std::vector<bool> is_removed(detections.size(), false);
    
    // 对每个类别应用NMS
    for (size_t i = 0; i < detections.size(); i++) {
        if (is_removed[i]) {
            continue;
        }
        
        int current_class = detections[i].class_id;
        result.push_back(detections[i]);
        
        // 检查同类别剩余检测
        for (size_t j = i + 1; j < detections.size(); j++) {
            if (is_removed[j] || detections[j].class_id != current_class) {
                continue;
            }
            
            float iou = calculateIoU(detections[i].box, detections[j].box);
            if (iou > iou_threshold) {
                is_removed[j] = true;
            }
        }
    }
    
    return result;
}

// 计算两个边界框的IoU
float OnnxInferenceEngine::calculateIoU(const BoundingBox& box1, const BoundingBox& box2) {
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

// 将BGR格式转换为RGB
void OnnxInferenceEngine::bgrToRgb(std::vector<uint8_t>& data) {
    for (size_t i = 0; i < data.size(); i += 3) {
        std::swap(data[i], data[i + 2]);
    }
}

// 预热模型
Result<void> OnnxInferenceEngine::warmupModel() {
    LOG_INFO("Warming up model...");
    
    // 创建一个空白图像进行预热
    int width = config_.detection.model_width;
    int height = config_.detection.model_height;
    std::vector<uint8_t> dummy_image(width * height * 3, 128);  // 中灰色图像
    
    try {
        // 创建一个虚拟请求
        InferenceRequest dummy_request;
        dummy_request.client_id = 0;
        dummy_request.frame_id = 0;
        dummy_request.timestamp = 0;
        dummy_request.width = width;
        dummy_request.height = height;
        dummy_request.data = dummy_image;
        dummy_request.is_keyframe = true;
        
        // 运行几次推理以预热模型
        for (int i = 0; i < 3; i++) {
            auto result = runInference(dummy_request);
            if (result.hasError()) {
                return Result<void>::error(result.error());
            }
        }
        
        LOG_INFO("Model warmup completed");
        return Result<void>::ok();
    } catch (const std::exception& e) {
        return Result<void>::error(
            ErrorCode::INFERENCE_ERROR, 
            "Model warmup failed: " + std::string(e.what())
        );
    }
}

// 加载模型
Result<void> OnnxInferenceEngine::loadModel(const std::string& model_path, bool force_reload) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    
    try {
        // 检查是否需要重新加载
        if (session_ && !force_reload) {
            LOG_INFO("Model already loaded, skipping");
            return Result<void>::ok();
        }
        
        LOG_INFO("Loading YOLO model: " + model_path);
        
        // 计算模型哈希值
        model_info_.hash = calculateModelHash(model_path);
        model_info_.path = model_path;
        model_info_.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        // 创建会话
        session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), session_options_);
        
        LOG_INFO("ONNX model loaded successfully");
        
        // 获取模型信息
        try {
            // 获取输入信息
            size_t num_input_nodes = session_->GetInputCount();
            input_dims_.resize(num_input_nodes);
            
            for (size_t i = 0; i < num_input_nodes; i++) {
                Ort::AllocatedStringPtr input_name_ptr = session_->GetInputNameAllocated(i, allocator_);
                input_names_owned_[i] = input_name_ptr.get();
                input_names_[i] = input_names_owned_[i].c_str();
                
                Ort::TypeInfo type_info = session_->GetInputTypeInfo(i);
                auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
                input_dims_[i] = tensor_info.GetShape();
                
                // 设置模型输入尺寸信息
                if (input_dims_[i].size() == 4) {
                    // 通常NCHW格式
                    model_info_.input_height = input_dims_[i][2];
                    model_info_.input_width = input_dims_[i][3];
                }
                
                // 打印输入尺寸
                std::string dims_str = "[";
                for (size_t j = 0; j < input_dims_[i].size(); j++) {
                    dims_str += std::to_string(input_dims_[i][j]);
                    if (j < input_dims_[i].size() - 1) {
                        dims_str += ", ";
                    }
                }
                dims_str += "]";
                
                LOG_INFO("Input #" + std::to_string(i) + ": " + input_names_owned_[i] + " " + dims_str);
            }
            
            // 获取输出信息
            size_t num_output_nodes = session_->GetOutputCount();
            output_dims_.resize(num_output_nodes);
            
            for (size_t i = 0; i < num_output_nodes; i++) {
                Ort::AllocatedStringPtr output_name_ptr = session_->GetOutputNameAllocated(i, allocator_);
                output_names_owned_[i] = output_name_ptr.get();
                output_names_[i] = output_names_owned_[i].c_str();
                
                Ort::TypeInfo type_info = session_->GetOutputTypeInfo(i);
                auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
                output_dims_[i] = tensor_info.GetShape();
                
                // 打印输出尺寸
                std::string dims_str = "[";
                for (size_t j = 0; j < output_dims_[i].size(); j++) {
                    dims_str += std::to_string(output_dims_[i][j]);
                    if (j < output_dims_[i].size() - 1) {
                        dims_str += ", ";
                    }
                }
                dims_str += "]";
                
                LOG_INFO("Output #" + std::to_string(i) + ": " + output_names_owned_[i] + " " + dims_str);
            }
            
            // 预热模型
            auto warmup_result = warmupModel();
            if (warmup_result.hasError()) {
                LOG_WARN("Model warmup failed: " + warmup_result.error().message);
                LOG_WARN("This may lead to higher latency for first inference");
            } else {
                LOG_INFO("Model warmup completed successfully");
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to initialize model info: " + std::string(e.what()));
            // 虽然获取模型信息失败，但模型本身已加载成功
            // 继续执行，而不是回退到模拟模式
        }
        
        return Result<void>::ok();
    } catch (const std::exception& e) {
        std::string errorMsg = "Failed to load model: " + std::string(e.what());
        LOG_ERROR(errorMsg);
        return Result<void>::error(ErrorCode::MODEL_LOAD_FAILED, errorMsg);
    }
}

// 配置INT8量化
Result<void> OnnxInferenceEngine::configureQuantization() {
    try {
        // 设置图优化级别
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        
        // 配置INT8量化
        session_options_.AddConfigEntry("session.use_int8_inference", "1");
        
        // 添加CPU执行提供者
        OrtCpuProviderOptions cpu_options;
        
        LOG_INFO("INT8量化模型支持已启用");
        return Result<void>::ok();
    } catch (const Ort::Exception& e) {
        return Result<void>::error(
            ErrorCode::INFERENCE_ERROR,
            "配置INT8量化失败: " + std::string(e.what())
        );
    }
}

// 计算模型哈希值
std::string OnnxInferenceEngine::calculateModelHash(const std::string& model_path) {
    try {
        // 打开文件
        std::ifstream file(model_path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open model file for hashing: " + model_path);
            return "";
        }
        
        // 创建SHA-256上下文
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        
        // 分块读取文件并更新哈希
        const size_t buffer_size = 8192;
        char buffer[buffer_size];
        
        while (file.good()) {
            file.read(buffer, buffer_size);
            SHA256_Update(&sha256, buffer, file.gcount());
        }
        
        // 获取最终哈希值
        SHA256_Final(hash, &sha256);
        
        // 转换为十六进制字符串
        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        
        return ss.str();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to calculate model hash: " + std::string(e.what()));
        return "";
    }
}

// 检查文件是否存在
bool OnnxInferenceEngine::fileExists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.good();
}

// 生成随机检测结果（模拟模式）
std::vector<Detection> OnnxInferenceEngine::generateRandomDetections(int img_width, int img_height) {
    // 用于模拟模式，生成随机检测结果
    std::vector<Detection> detections;
    
    // 使用真正的随机数生成器
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // 设置随机分布
    std::uniform_int_distribution<> num_dist(0, 5); // 检测数量范围0-5
    std::uniform_real_distribution<> pos_dist(0.1, 0.9); // 位置范围 0.1-0.9
    std::uniform_real_distribution<> size_dist(0.05, 0.2); // 大小范围 0.05-0.2
    std::uniform_real_distribution<> conf_dist(0.6, 1.0); // 置信度范围 0.6-1.0
    std::uniform_int_distribution<> class_dist(0, 3); // 类别范围 0-3
    
    // 生成随机检测数量
    int num_detections = num_dist(gen);
    
    // 生成当前时间戳
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    // 生成检测结果
    for (int i = 0; i < num_detections; i++) {
        // 创建随机边界框
        BoundingBox box;
        box.x = pos_dist(gen);
        box.y = pos_dist(gen);
        box.width = size_dist(gen);
        box.height = size_dist(gen) * 1.5f; // 使高度稍大一些，更像人形
        
        // 创建检测对象
        Detection det;
        det.box = box;
        det.confidence = conf_dist(gen);
        det.class_id = class_dist(gen);
        det.track_id = i + 1; // 从1开始的跟踪ID
        det.timestamp = current_time;
        
        detections.push_back(det);
    }
    
    return detections;
}

} // namespace zero_latency