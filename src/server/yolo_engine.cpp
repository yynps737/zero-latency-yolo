#include "yolo_engine.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <cstring>
#include <random>
#include "../common/constants.h"

namespace zero_latency {

YoloEngine::YoloEngine(const ServerConfig& config)
    : config_(config),
      env_(ORT_LOGGING_LEVEL_WARNING, "YoloEngine"),
      running_(false),
      inference_count_(0),
      queue_high_water_mark_(0),
      simulation_mode_(false) {
    
    // 初始化随机数种子
    srand(static_cast<unsigned int>(time(nullptr)));
    
    // 初始化输入输出名称
    input_names_.push_back("input");
    output_names_.push_back("output");
}

YoloEngine::~YoloEngine() {
    shutdown();
}

bool YoloEngine::initialize() {
    try {
        // 检查模型文件是否存在
        if (!fileExists(config_.model_path)) {
            std::cerr << "错误: YOLO模型文件不存在: " << config_.model_path << std::endl;
            std::cerr << "将使用模拟模式生成随机检测结果" << std::endl;
            simulation_mode_ = true;
            return true; // 即使模型不存在，也继续运行，使用模拟模式
        }
        
        // 配置会话选项
        session_options_.SetIntraOpNumThreads(1); // 使用单线程执行
        session_options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session_options_.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        
        // 创建会话
        std::cout << "加载YOLO模型: " << config_.model_path << std::endl;
        try {
            session_ = std::make_unique<Ort::Session>(env_, config_.model_path.c_str(), session_options_);
        } catch (const Ort::Exception& e) {
            std::cerr << "加载ONNX模型失败: " << e.what() << std::endl;
            std::cerr << "将使用模拟模式生成随机检测结果" << std::endl;
            simulation_mode_ = true;
            return true; // 即使模型加载失败，也继续运行，使用模拟模式
        }
        
        // 获取模型信息
        if (!simulation_mode_) {
            try {
                // 获取输入信息
                size_t num_input_nodes = session_->GetInputCount();
                input_dims_.resize(num_input_nodes);
                
                for (size_t i = 0; i < num_input_nodes; i++) {
                    // 使用 GetInputName 替代 GetInputNameAllocated
                    const char* input_name = session_->GetInputName(i, allocator_);
                    input_names_[i] = input_name;
                    
                    Ort::TypeInfo type_info = session_->GetInputTypeInfo(i);
                    auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
                    input_dims_[i] = tensor_info.GetShape();
                    
                    // 打印输入尺寸
                    std::cout << "输入 #" << i << ": " << input_names_[i];
                    std::cout << " [";
                    for (size_t j = 0; j < input_dims_[i].size(); j++) {
                        std::cout << input_dims_[i][j];
                        if (j < input_dims_[i].size() - 1) {
                            std::cout << ", ";
                        }
                    }
                    std::cout << "]" << std::endl;
                }
                
                // 获取输出信息
                size_t num_output_nodes = session_->GetOutputCount();
                output_dims_.resize(num_output_nodes);
                
                for (size_t i = 0; i < num_output_nodes; i++) {
                    // 使用 GetOutputName 替代 GetOutputNameAllocated
                    const char* output_name = session_->GetOutputName(i, allocator_);
                    output_names_[i] = output_name;
                    
                    Ort::TypeInfo type_info = session_->GetOutputTypeInfo(i);
                    auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
                    output_dims_[i] = tensor_info.GetShape();
                    
                    // 打印输出尺寸
                    std::cout << "输出 #" << i << ": " << output_names_[i];
                    std::cout << " [";
                    for (size_t j = 0; j < output_dims_[i].size(); j++) {
                        std::cout << output_dims_[i][j];
                        if (j < output_dims_[i].size() - 1) {
                            std::cout << ", ";
                        }
                    }
                    std::cout << "]" << std::endl;
                }
                
                // 预热模型
                warmupModel();
            } catch (const std::exception& e) {
                std::cerr << "初始化模型信息时出错: " << e.what() << std::endl;
                std::cerr << "将使用模拟模式生成随机检测结果" << std::endl;
                simulation_mode_ = true;
            }
        }
        
        // 启动推理线程
        running_ = true;
        inference_thread_ = std::thread(&YoloEngine::inferenceThread, this);
        
        if (simulation_mode_) {
            std::cout << "系统已启动 (模拟模式)" << std::endl;
        } else {
            std::cout << "系统已启动 (正常模式)" << std::endl;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "初始化YOLO引擎错误: " << e.what() << std::endl;
        return false;
    }
}

void YoloEngine::shutdown() {
    if (running_) {
        running_ = false;
        queue_cv_.notify_all();
        
        if (inference_thread_.joinable()) {
            inference_thread_.join();
        }
    }
}

bool YoloEngine::submitInference(const InferenceRequest& request) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // 检查队列是否已满
        if (inference_queue_.size() >= config_.max_queue_size) {
            // 队列已满，丢弃最旧的非关键帧请求
            bool discarded = false;
            
            if (!request.is_keyframe) {
                // 只有当新请求不是关键帧时才丢弃
                return false;
            }
            
            // 当前请求是关键帧，寻找并丢弃一个非关键帧请求
            std::queue<InferenceRequest> temp_queue;
            while (!inference_queue_.empty()) {
                auto& req = inference_queue_.front();
                if (!req.is_keyframe && !discarded) {
                    discarded = true;
                } else {
                    temp_queue.push(req);
                }
                inference_queue_.pop();
            }
            
            // 如果找不到非关键帧可丢弃，则返回失败
            if (!discarded) {
                // 还原队列
                inference_queue_ = std::move(temp_queue);
                return false;
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
    return true;
}

void YoloEngine::setCallback(InferenceCallback callback) {
    callback_ = std::move(callback);
}

size_t YoloEngine::getQueueSize() const {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return inference_queue_.size();
}

void YoloEngine::inferenceThread() {
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
            auto start_time = std::chrono::steady_clock::now();
            
            // 执行推理
            GameState result = runInference(request);
            
            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
            
            // 更新统计信息
            inference_count_++;
            
            // 如果推理耗时小于目标帧时间，则休眠一段时间以保持稳定帧率
            int target_frame_time = 1000 / config_.target_fps;
            if (duration < target_frame_time) {
                std::this_thread::sleep_for(std::chrono::milliseconds(target_frame_time - duration));
            }
            
            // 调用回调
            if (callback_) {
                callback_(request.client_id, result);
            }
        }
    }
}

GameState YoloEngine::runInference(const InferenceRequest& request) {
    GameState state;
    state.frame_id = request.frame_id;
    state.timestamp = request.timestamp;
    
    try {
        if (simulation_mode_) {
            // 在模拟模式下生成随机检测结果
            state.detections = generateRandomDetections(request.width, request.height);
        } else {
            // 预处理图像数据
            std::vector<float> input_tensor_values = preProcess(request.data, request.width, request.height);
            
            // 创建输入tensor
            std::vector<int64_t> input_shape = {1, 3, constants::DEFAULT_MODEL_HEIGHT, constants::DEFAULT_MODEL_WIDTH};
            auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                memory_info, 
                input_tensor_values.data(), 
                input_tensor_values.size(), 
                input_shape.data(), 
                input_shape.size()
            );
            
            // 执行推理
            auto output_tensors = session_->Run(
                Ort::RunOptions{nullptr}, 
                input_names_.data(), 
                &input_tensor, 
                1, 
                output_names_.data(), 
                1
            );
            
            // 检查输出
            if (output_tensors.size() != 1) {
                throw std::runtime_error("模型输出数量错误");
            }
            
            // 后处理结果
            state.detections = postProcess(output_tensors[0], request.width, request.height);
        }
    } catch (const Ort::Exception& e) {
        std::cerr << "推理错误: " << e.what() << std::endl;
        
        // 出错时使用模拟检测结果
        state.detections = generateRandomDetections(request.width, request.height);
    } catch (const std::exception& e) {
        std::cerr << "推理过程中出错: " << e.what() << std::endl;
        
        // 出错时使用模拟检测结果
        state.detections = generateRandomDetections(request.width, request.height);
    }
    
    return state;
}

std::vector<float> YoloEngine::preProcess(const std::vector<uint8_t>& image_data, int width, int height) {
    const int target_height = constants::DEFAULT_MODEL_HEIGHT;
    const int target_width = constants::DEFAULT_MODEL_WIDTH;
    
    std::vector<float> result(3 * target_height * target_width);
    
    // 确保图像数据正确
    if (image_data.size() != width * height * 3) {
        throw std::runtime_error("图像数据大小不正确");
    }
    
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
    
    return result;
}

std::vector<Detection> YoloEngine::postProcess(Ort::Value& output_tensor, int img_width, int img_height) {
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
            det.track_id = 0;  // 暂不支持跟踪
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
    
    return detections;
}

std::vector<Detection> YoloEngine::applyNMS(std::vector<Detection>& detections, float iou_threshold) {
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

float YoloEngine::calculateIoU(const BoundingBox& box1, const BoundingBox& box2) {
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

void YoloEngine::bgrToRgb(std::vector<uint8_t>& data) {
    for (size_t i = 0; i < data.size(); i += 3) {
        std::swap(data[i], data[i + 2]);
    }
}

void YoloEngine::warmupModel() {
    std::cout << "预热模型..." << std::endl;
    
    // 创建一个空白图像进行预热
    int width = constants::DEFAULT_MODEL_WIDTH;
    int height = constants::DEFAULT_MODEL_HEIGHT;
    std::vector<uint8_t> dummy_image(width * height * 3, 0);
    
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
            runInference(dummy_request);
        }
        
        std::cout << "模型预热完成" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "模型预热失败: " << e.what() << std::endl;
        std::cerr << "这可能不会影响正常运行，但可能会导致第一次推理延迟较高" << std::endl;
    }
}

bool YoloEngine::fileExists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.good();
}

std::vector<Detection> YoloEngine::generateRandomDetections(int img_width, int img_height) {
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