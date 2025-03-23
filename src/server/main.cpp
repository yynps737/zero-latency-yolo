#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <sched.h>
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>

#include "yolo_engine.h"
#include "game_adapter.h"
#include "network.h"
#include "config.h"
#include "../common/constants.h"

using namespace zero_latency;

// 全局变量
std::atomic<bool> g_running(true);
std::atomic<uint64_t> g_frame_count(0);
std::atomic<uint64_t> g_detection_count(0);

// 信号处理函数
void signalHandler(int signal) {
    std::cout << "接收到信号: " << signal << ", 正在关闭服务器..." << std::endl;
    g_running = false;
}

// 设置CPU亲和性
bool setCpuAffinity(int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    
    pthread_t current_thread = pthread_self();
    int result = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    
    if (result != 0) {
        std::cerr << "设置CPU亲和性失败: " << result << std::endl;
        return false;
    }
    
    return true;
}

// 设置线程优先级
bool setThreadPriority(int priority) {
    struct sched_param param;
    param.sched_priority = priority;
    
    int result = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    
    if (result != 0) {
        std::cerr << "设置线程优先级失败: " << result << std::endl;
        return false;
    }
    
    return true;
}

// 设置进程优先级
bool setProcessPriority() {
    int result = setpriority(PRIO_PROCESS, 0, -20);
    
    if (result != 0) {
        std::cerr << "设置进程优先级失败: " << result << std::endl;
        return false;
    }
    
    return true;
}

// 输出系统信息
void printSystemInfo(const ServerConfig& config) {
    std::cout << "===== 零延迟YOLO FPS云辅助系统服务器 =====" << std::endl;
    std::cout << "版本: 1.0.0" << std::endl;
    std::cout << "系统信息:" << std::endl;
    
    // CPU信息
    std::cout << "  - CPU核心数: " << std::thread::hardware_concurrency() << std::endl;
    
    // 内存信息
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    long memory_mb = (pages * page_size) / (1024 * 1024);
    std::cout << "  - 系统内存: " << memory_mb << " MB" << std::endl;
    
    // 配置信息
    std::cout << "配置信息:" << std::endl;
    std::cout << "  - 模型路径: " << config.model_path << std::endl;
    std::cout << "  - 服务端口: " << config.port << std::endl;
    std::cout << "  - 目标FPS: " << config.target_fps << std::endl;
    std::cout << "  - 最大客户端数: " << config.max_clients << std::endl;
    std::cout << "  - 检测阈值: " << config.confidence_threshold << std::endl;
    
    std::cout << "=======================================" << std::endl;
}

// 状态监控线程函数
void monitorThread(YoloEngine* yolo, NetworkServer* network) {
    uint64_t last_frame_count = 0;
    uint64_t last_detection_count = 0;
    auto last_time = std::chrono::steady_clock::now();
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - last_time).count();
        
        if (elapsed > 0) {
            uint64_t current_frame_count = g_frame_count.load();
            uint64_t current_detection_count = g_detection_count.load();
            
            float fps = static_cast<float>(current_frame_count - last_frame_count) / elapsed;
            float dps = static_cast<float>(current_detection_count - last_detection_count) / elapsed;
            
            std::cout << "状态: FPS=" << fps << ", 检测/秒=" << dps 
                      << ", 客户端=" << network->getClientCount()
                      << ", 队列=" << yolo->getQueueSize() << std::endl;
            
            last_frame_count = current_frame_count;
            last_detection_count = current_detection_count;
            last_time = current_time;
        }
    }
}

int main(int argc, char* argv[]) {
    // 注册信号处理
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    // 加载配置
    ServerConfig config;
    ConfigManager configManager;
    
    if (!configManager.loadServerConfig("configs/server.json", config)) {
        std::cerr << "加载配置失败，使用默认配置" << std::endl;
        config = ServerConfig(); // 使用默认值
    }
    
    // 初始化系统优化
    if (config.use_cpu_affinity) {
        if (!setCpuAffinity(config.cpu_core_id)) {
            std::cerr << "警告: 无法设置CPU亲和性" << std::endl;
        }
    }
    
    if (config.use_high_priority) {
        if (!setProcessPriority()) {
            std::cerr << "警告: 无法设置进程优先级，可能需要root权限" << std::endl;
        }
    }
    
    // 显示系统信息
    printSystemInfo(config);
    
    // 创建YOLO引擎
    YoloEngine yolo(config);
    if (!yolo.initialize()) {
        std::cerr << "初始化YOLO引擎失败" << std::endl;
        return 1;
    }
    
    // 创建游戏适配器
    GameAdapter gameAdapter;
    gameAdapter.initialize();
    
    // 创建网络服务器
    NetworkServer network(config.port, &yolo, &gameAdapter);
    if (!network.initialize()) {
        std::cerr << "初始化网络服务器失败" << std::endl;
        return 1;
    }
    
    // 启动监控线程
    std::thread monitor_thread(monitorThread, &yolo, &network);
    
    // 启动网络服务
    std::cout << "服务器启动成功，监听端口: " << config.port << std::endl;
    network.run(g_running);
    
    // 等待监控线程结束
    if (monitor_thread.joinable()) {
        monitor_thread.join();
    }
    
    // 清理资源
    network.shutdown();
    yolo.shutdown();
    
    std::cout << "服务器已关闭" << std::endl;
    std::cout << "总共处理帧数: " << g_frame_count.load() << std::endl;
    std::cout << "总共检测对象: " << g_detection_count.load() << std::endl;
    
    return 0;
}