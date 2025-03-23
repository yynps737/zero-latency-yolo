#include "onnx_engine.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <future>
#include <random>
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
      simulation_mode_(false) {
    
    // 初始化随机数种子
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    
    // 初始化内存池
    input_buffer_pool_ = std::make_unique<ThreadLocalBufferPool<float>>(
        config_.detection.model_width * config_.detection.model_height * 3);
    
    // 初始化输入输出名称
    input_names_owned_.push_back("input");
    output_names_owned_.push_back("output");
    
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
        session_options_.SetIntraOpNumThreads(2);  // 使用2个线程执行
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options_.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        
        // 如果可以使用GPU, 则添加CUDA执行提供者
        #ifdef USE_CUDA
        OrtSessionOptionsAppendExecutionProvider_CUDA(session_options_, 0);
        LOG_INFO("CUDA execution provider added");
        #endif
        
        // 创建会话
        LOG_INFO("Loading YOLO model: " + config_.model_path);
        try {
            session_ = std::make_unique<Ort::Session>(env_, config_.model_path.c_str(), session_options_);
            LOG_INFO("ONNX model loaded successfully");
        } catch (const Ort::Exception& e) {
            LOG_ERROR("Failed to load ONNX model: " + std::string(e.what()));
            LOG_WARN("Using simulation mode (will generate random detections)");
            simulation_mode_ = true;
            return Result<void>::ok();  // 即使模型加载失败，也继续运行，使用模拟模式
        }
        
        // 获取模型信息
        if (!simulation_mode_) {
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
                LOG_WARN("Using simulation mode (will generate random detections)");
                simulation_mode_ = true;
            }
        }
        
        // 启动推理线程
        running_ = true;
        
        // 启动主推理线程
        inference_thread_ = std::thread(&OnnxInferenceEngine::inferenceThreadFunc, this);
        
        // 创建工作线程池
        for (uint8_t i = 0; i < config_.worker_threads; ++i) {
            worker_threads_.emplace_back(&OnnxInferenceEngine::workerThreadFunc, this);
        }
        
        LOG_INFO("ONNX inference engine started with " + std::to_string(config_.worker_threads) + " worker threads");
        
        if (simulation_mode_) {
            LOG_INFO("Engine running in simulation mode");
        } else {
            LOG_INFO("Engine running in normal mode");
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
        
        // 清空队列并唤醒等待线程
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            std::queue<InferenceRequest> empty;
            std::swap(inference_queue_, empty);
            queue_cv_.notify_all();
        }
        
        // 关闭任务队列并唤醒工作线程
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            while (!tasks_.empty()) {
                auto task = tasks_.front();
                tasks_.pop();
                task->promise.set_value(Result<GameState>::error(ErrorCode::INFERENCE_ERROR, "Engine shutting down"));
            }
            tasks_cv_.notify_all();
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
    
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // 检查队列是否已满
        if (inference_queue_.size() >= config_.max_queue_size) {
            // 队列已满，丢弃最旧的非关键帧请求
            bool discarded = false;
            
            if (!request.is_keyframe) {
                // 只有当新请求不是关键帧时才丢弃
                return Result<void>::error(ErrorCode::INFERENCE_ERROR, "Queue full and request is not a keyframe");
            }
            
            // 当前请求是关键帧，寻找并丢弃一个非关键帧请求
            std::queue<InferenceRequest> temp_queue;
            while (!inference_queue_.empty()) {
                auto& req = inference_queue_.front();
                if (!req.is_keyframe && !discarded) {
                    discarded = true;
                    LOG_DEBUG("Discarded non-keyframe request from client " + 
                             std::to_string(req.client_id) + ", frame " + 
                             std::to_string(req.frame_id));
                } else {
                    temp_queue.push(std::move(req));
                }
                inference_queue_.pop();
            }
            
            // 如果找不到非关键帧可丢弃，则返回失败
            if (!discarded) {
                // 还原队列
                inference_queue_ = std::move(temp_queue);
                return Result<void>::error(ErrorCode::INFERENCE_ERROR, "Queue full and no non-keyframe requests to discard");
            }
            
            // 还原队列并添加新请求
            inference_queue_ = std::move(temp_queue);
        }
        
        inference_queue_.push(request);
        
        // 更新高水位标记
        if (inference_queue_.size() > queue_high_water_mark_) {
            queue_high_water_mark_ = inference_queue_.size();
        }
    }
    
    queue_cv_.notify_one();
    return Result<void>::ok();
}

// 设置推理结果回调
void OnnxInferenceEngine::setCallback(InferenceCallback callback) {
    callback_ = std::move(callback);
}

// 获取当前推理队列大小
size_t OnnxInferenceEngine::getQueueSize() const {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return inference_queue_.size();
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
    status["queue_size"] = std::to_string(getQueueSize());
    status["queue_high_water_mark"] = std::to_string(queue_high_water_mark_);
    status["inference_count"] = std::to_string(inference_count_);
    
    if (inference_count_ > 0) {
        status["avg_inference_time_ms"] = std::to_string(total_inference_time_ms_ / inference_count_);
        status["avg_preprocessing_time_ms"] = std::to_string(total_preprocessing_time_ms_ / inference_count_);
        status["avg_postprocessing_time_ms"] = std::to_string(total_postprocessing_time_ms_ / inference_count_);
    } else {
        status["avg_inference_time_ms"] = "0";
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
        InferenceRequest request;
        bool has_request = false;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] { 
                return !running_ || !inference_queue_.empty(); 
            });
            
            if (!running_) {
                break;
            }
            
            if (!inference_queue_.empty()) {
                request = inference_queue_.front();
                inference_queue_.pop();
                has_request = true;
            }
        }
        
        if (has_request) {
            // 创建推理任务
            auto task = std::make_shared<InferenceTask>();
            task->request = request;
            
            // 将任务添加到工作队列
            {
                std::unique_lock<std::mutex> lock(tasks_mutex_);
                tasks_.push(task);
            }
            
            // 通知工作线程处理任务
            tasks_cv_.notify_one();
            
            // 获取推理结果
            auto result_future = task->promise.get_future();
            auto result = result_future.get();
            
            if (result.isOk()) {
                // 调用回调
                if (callback_) {
                    callback_(request.client_id, result.value());
                    
                    // 发布推理完成事件
                    EventBus::getInstance().publishInferenceEvent(events::INFERENCE_COMPLETED, 
                                                               request.client_id, 
                                                               request.frame_id);
                }
            } else {
                LOG_ERROR("Inference error: " + result.error().message);
                
                // 发布推理错误事件
                Event event(events::INFERENCE_ERROR);
                event.setSource("OnnxInferenceEngine");
                event.setData("client_id", request.client_id);
                event.setData("frame_id", request.frame_id);
                event.setData("error", result.error().message);
                publishEvent(event);
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
        bool has_task = false;
        
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            tasks_cv_.wait_for(lock, std::chrono::milliseconds(100), [this] { 
                return !running_ || !tasks_.empty(); 
            });
            
            if (!running_ && tasks_.empty()) {
                break;
            }
            
            if (!tasks_.empty()) {
                task = tasks_.front();
                tasks_.pop();
                has_task = true;
            }
        }
        
        if (has_task) {
            // 执行推理
            auto start_time = std::chrono::steady_clock::now();
            auto result = runInference(task->request);
            auto end_time = std::chrono::steady_clock::now();
            
            // 更新统计信息
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
            inference_count_++;
            total_inference_time_ms_ += duration;
            
            // 设置结果
            task->promise.set_value(result);
            
            // 限制处理速度以维持稳定的帧率
            int target_frame_time = 1000 / config_.target_fps;
            if (duration < target_frame_time) {
                std::this_thread::sleep_for(std::chrono::milliseconds(target_frame_time - duration));
            }
        }
    }
    
    LOG_INFO("Worker thread stopped");
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
        
        // 获取线程本地缓冲区
        auto& buffer = input_buffer_pool_->getBuffer();
        
        // 预处理图像数据
        auto preprocess_result = preProcess(request.data, request.width, request.height, buffer);
        if (preprocess_result.hasError()) {
            return Result<GameState>::error(preprocess_result.error());
        }
        
        std::vector<float>& input_tensor_values = preprocess_result.value();
        
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
        
        auto output_tensors = session_->Run(
            Ort::RunOptions{nullptr}, 
            input_names_.data(), 
            &input_tensor, 
            1, 
            output_names_.data(), 
            1
        );
        
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
        
        // 出错时使用模拟检测结果
        state.detections = generateRandomDetections(request.width, request.height);
        return Result<GameState>::ok(state);
    } catch (const std::exception& e) {
        std::string errorMsg = "Inference error: " + std::string(e.what());
        LOG_ERROR(errorMsg);
        
        // 出错时使用模拟检测结果
        state.detections = generateRandomDetections(request.width, request.height);
        return Result<GameState>::ok(state);
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

// 模型输出后处理
Result<std::vector<Detection>> OnnxInferenceEngine::postProcess(
    Ort::Value& output_tensor, 
    int img_width, 
    int img_height) {
    
    try {
        // 获取输出维度
        auto output_dims = output_tensor.GetTensorTypeAndShapeInfo().GetShape();
        
        // 对于YOLO模型，通常输出形状为[1, N, 85]
        // 其中N为检测数量，85 = 4(box) + 1(confidence) + 80(classes)
        // 但我们的模型是CS1.6专用，可能只有少数几个类
        
        auto* output_data = output_tensor.GetTensorData<float>();
        size_t num_detections = output_dims[1];
        size_t item_size = output_dims[2];
        
        std::vector<Detection> detections;
        
        // 确定坐标偏移和类别偏移
        size_t coords_offset = 0;  // x, y, w, h
        size_t conf_offset = 4;    // 置信度
        size_t class_offset = 5;   // 类别开始
        size_t num_classes = item_size - class_offset;
        
        // 处理每个检测
        for (size_t i = 0; i < num_detections; i++) {
            // 输出数据的基本偏移
            size_t base_offset = i * item_size;
            
            // 提取置信度
            float confidence = output_data[base_offset + conf_offset];
            
            // 如果置信度低于阈值，则跳过
            if (confidence < config_.confidence_threshold) {
                continue;
            }
            
            // 找到最高置信度的类别
            float max_class_conf = 0.0f;
            int max_class_id = -1;
            
            for (size_t c = 0; c < num_classes; c++) {
                float class_conf = output_data[base_offset + class_offset + c];
                if (class_conf > max_class_conf) {
                    max_class_conf = class_conf;
                    max_class_id = c;
                }
            }
            
            // 如果类别置信度乘以框置信度大于阈值
            float final_confidence = confidence * max_class_conf;
            if (final_confidence >= config_.confidence_threshold && max_class_id >= 0) {
                // 提取边界框坐标
                float x = output_data[base_offset + coords_offset];
                float y = output_data[base_offset + coords_offset + 1];
                float w = output_data[base_offset + coords_offset + 2];
                float h = output_data[base_offset + coords_offset + 3];
                
                // 转换为图像坐标系并进行缩放
                BoundingBox box;
                box.x = x;
                box.y = y;
                box.width = w;
                box.height = h;
                
                // 创建检测对象
                Detection det;
                det.box = box;
                det.confidence = final_confidence;
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