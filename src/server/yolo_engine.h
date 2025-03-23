#ifndef ZERO_LATENCY_YOLO_ENGINE_H
#define ZERO_LATENCY_YOLO_ENGINE_H

// 标准库头文件
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <cstring>

// ONNX Runtime 头文件包含 - 尝试多种可能的路径
#if defined(ONNXRUNTIME_ROOT_DIR) && __has_include(ONNXRUNTIME_ROOT_DIR "/include/onnxruntime_cxx_api.h")
    #include ONNXRUNTIME_ROOT_DIR "/include/onnxruntime_cxx_api.h"
#elif __has_include(<onnxruntime/core/session/onnxruntime_cxx_api.h>)
    #include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#elif __has_include("onnxruntime_cxx_api.h")
    #include "onnxruntime_cxx_api.h"
#else
    #error "Cannot find ONNX Runtime header. Please set ONNXRUNTIME_ROOT_DIR or ensure onnxruntime_cxx_api.h is in include path."
#endif

// 项目头文件
#include "../common/types.h"
#include "config.h"

namespace zero_latency {

/**
 * 推理请求结构 - 包含送入模型推理的所有必要信息
 */
struct InferenceRequest {
    uint32_t client_id;    // 客户端ID
    uint32_t frame_id;     // 帧ID
    uint64_t timestamp;    // 时间戳(毫秒)
    uint16_t width;        // 图像宽度
    uint16_t height;       // 图像高度
    std::vector<uint8_t> data;  // 图像数据(RGB格式)
    bool is_keyframe;      // 是否为关键帧
};

/**
 * 推理结果回调函数类型
 * 用于在推理完成时通知调用者
 */
typedef std::function<void(uint32_t client_id, const GameState& state)> InferenceCallback;

/**
 * YoloEngine类 - 负责模型加载、推理和后处理
 */
class YoloEngine {
public:
    /**
     * 构造函数
     * @param config 服务器配置
     */
    explicit YoloEngine(const ServerConfig& config);
    
    /**
     * 析构函数 - 确保关闭所有资源
     */
    ~YoloEngine();
    
    /**
     * 禁用拷贝构造函数和赋值操作符
     */
    YoloEngine(const YoloEngine&) = delete;
    YoloEngine& operator=(const YoloEngine&) = delete;
    
    /**
     * 初始化YOLO引擎
     * @return 初始化是否成功
     */
    bool initialize();
    
    /**
     * 关闭引擎并释放资源
     */
    void shutdown();
    
    /**
     * 提交推理请求
     * @param request 推理请求
     * @return 请求是否成功加入队列
     */
    bool submitInference(const InferenceRequest& request);
    
    /**
     * 设置推理结果回调函数
     * @param callback 回调函数
     */
    void setCallback(InferenceCallback callback);
    
    /**
     * 获取当前推理队列大小
     * @return 队列中等待处理的请求数量
     */
    size_t getQueueSize() const;

private:
    /**
     * 推理线程函数 - 处理队列中的请求
     */
    void inferenceThread();
    
    /**
     * 执行单个推理请求
     * @param request 推理请求
     * @return 游戏状态，包含检测结果
     */
    GameState runInference(const InferenceRequest& request);
    
    /**
     * 图像预处理
     * @param image_data 原始图像数据
     * @param width 图像宽度
     * @param height 图像高度
     * @return 预处理后的浮点数据
     */
    std::vector<float> preProcess(const std::vector<uint8_t>& image_data, int width, int height);
    
    /**
     * 模型输出后处理
     * @param output_tensor 模型输出张量
     * @param img_width 原始图像宽度
     * @param img_height 原始图像高度
     * @return 检测结果列表
     */
    std::vector<Detection> postProcess(Ort::Value& output_tensor, int img_width, int img_height);
    
    /**
     * 应用非极大值抑制
     * @param detections 检测结果列表
     * @param iou_threshold IoU阈值
     * @return 经过NMS处理的检测结果
     */
    std::vector<Detection> applyNMS(std::vector<Detection>& detections, float iou_threshold);
    
    /**
     * 计算两个边界框的IoU
     * @param box1 第一个边界框
     * @param box2 第二个边界框
     * @return IoU值[0,1]
     */
    float calculateIoU(const BoundingBox& box1, const BoundingBox& box2);
    
    /**
     * 将BGR格式转换为RGB
     * @param data 图像数据
     */
    void bgrToRgb(std::vector<uint8_t>& data);
    
    /**
     * 预热模型
     */
    void warmupModel();
    
    /**
     * 检查文件是否存在
     * @param path 文件路径
     * @return 文件是否存在
     */
    bool fileExists(const std::string& path);

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
    
    // 输入输出信息 - 使用向量存储，避免手动内存管理
    std::vector<const char*> input_names_;
    std::vector<const char*> output_names_;
    std::vector<std::vector<int64_t>> input_dims_;
    std::vector<std::vector<int64_t>> output_dims_;
    
    // 线程控制
    std::atomic<bool> running_;
    std::thread inference_thread_;
    mutable std::mutex queue_mutex_;
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

#endif // ZERO_LATENCY_YOLO_ENGINE_H