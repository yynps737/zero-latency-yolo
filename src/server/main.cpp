#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <memory>
#include <sched.h>
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>
#include <filesystem>

// 核心组件
#include "../common/logger.h"
#include "../common/event_bus.h"
#include "../common/result.h"
#include "../server/config.h"

// 功能模块
#include "../inference/inference_engine.h"
#include "../inference/onnx_engine.h"
#include "../game/game_adapter.h"
#include "../game/game_adapter_impl.h"
#include "../network/reliable_udp.h"
#include "../network/network_server.h"

using namespace zero_latency;
namespace fs = std::filesystem;

// 全局变量
std::atomic<bool> g_running(true);

// 信号处理函数
void signalHandler(int signal) {
    LOG_INFO("Received signal: " + std::to_string(signal) + ", shutting down server...");
    g_running = false;
}

// 设置CPU亲和性
Result<void> setCpuAffinity(int cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    
    pthread_t current_thread = pthread_self();
    int result = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    
    if (result != 0) {
        return Result<void>::error(
            ErrorCode::SYSTEM_ERROR, 
            "Failed to set CPU affinity: " + std::to_string(result)
        );
    }
    
    return Result<void>::ok();
}

// 设置线程优先级
Result<void> setThreadPriority(int priority) {
    struct sched_param param;
    param.sched_priority = priority;
    
    int result = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    
    if (result != 0) {
        return Result<void>::error(
            ErrorCode::SYSTEM_ERROR, 
            "Failed to set thread priority: " + std::to_string(result)
        );
    }
    
    return Result<void>::ok();
}

// 设置进程优先级
Result<void> setProcessPriority() {
    int result = setpriority(PRIO_PROCESS, 0, -20);
    
    if (result != 0) {
        return Result<void>::error(
            ErrorCode::SYSTEM_ERROR, 
            "Failed to set process priority: " + std::to_string(result) + 
            " (" + std::string(strerror(errno)) + ")"
        );
    }
    
    return Result<void>::ok();
}

// 输出系统信息
void printSystemInfo(const ServerConfig& config) {
    LOG_INFO("===== Zero Latency YOLO FPS Cloud Assist System =====");
    LOG_INFO("Version: 1.0.0");
    LOG_INFO("System information:");
    
    // CPU信息
    LOG_INFO("  - CPU cores: " + std::to_string(std::thread::hardware_concurrency()));
    
    // 内存信息
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    long memory_mb = (pages * page_size) / (1024 * 1024);
    LOG_INFO("  - System memory: " + std::to_string(memory_mb) + " MB");
    
    // 配置信息
    LOG_INFO("Configuration:");
    LOG_INFO("  - Model path: " + config.model_path);
    LOG_INFO("  - Inference engine: " + config.inference_engine);
    LOG_INFO("  - Server port: " + std::to_string(config.network.port));
    LOG_INFO("  - Target FPS: " + std::to_string(config.target_fps));
    LOG_INFO("  - Max clients: " + std::to_string(config.max_clients));
    LOG_INFO("  - Detection threshold: " + std::to_string(config.confidence_threshold));
    LOG_INFO("  - Worker threads: " + std::to_string(config.worker_threads));
    
    LOG_INFO("=================================================");
}

// 状态监控线程函数
void monitorThread(std::shared_ptr<IInferenceEngine> engine, 
                  std::shared_ptr<IGameAdapter> adapter,
                  std::shared_ptr<ReliableUdpServer> network,
                  const ServerConfig& config) {
    LOG_INFO("Status monitor thread started");
    
    uint64_t prev_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    while (g_running) {
        // 每5秒报告一次状态
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        uint64_t current_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        uint64_t elapsed = current_timestamp - prev_timestamp;
        
        if (elapsed > 0) {
            // 获取引擎状态
            auto engine_status = engine->getStatus();
            auto queue_size = engine->getQueueSize();
            
            // 获取网络状态
            auto network_status = network->getStatus();
            auto client_count = network->getClientCount();
            
            // 获取适配器状态
            auto adapter_status = adapter->getStatus();
            
            // 打印状态报告
            LOG_INFO("Status Report:");
            LOG_INFO("  - Runtime: " + std::to_string(elapsed) + "s");
            LOG_INFO("  - Clients: " + std::to_string(client_count));
            LOG_INFO("  - Queue size: " + std::to_string(queue_size));
            
            if (engine_status.find("avg_inference_time_ms") != engine_status.end()) {
                LOG_INFO("  - Avg inference time: " + engine_status["avg_inference_time_ms"] + " ms");
            }
            
            if (network_status.find("packets_sent") != network_status.end() && 
                network_status.find("packets_received") != network_status.end()) {
                LOG_INFO("  - Network: sent=" + network_status["packets_sent"] + 
                        ", received=" + network_status["packets_received"] + 
                        ", dropped=" + network_status["packets_dropped"]);
            }
            
            // 保存统计信息到文件
            if (config.analytics.enable_analytics && config.analytics.save_stats_to_file) {
                // 确保目录存在
                fs::path stats_path(config.analytics.stats_file);
                fs::path stats_dir = stats_path.parent_path();
                if (!stats_dir.empty() && !fs::exists(stats_dir)) {
                    try {
                        fs::create_directories(stats_dir);
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to create stats directory: " + std::string(e.what()));
                    }
                }
                
                // TODO: 将统计信息写入文件
            }
            
            prev_timestamp = current_timestamp;
        }
    }
    
    LOG_INFO("Status monitor thread stopped");
}

// 确保必要目录存在
Result<void> ensureDirectoriesExist() {
    std::vector<std::string> dirs = {"logs", "configs", "models", "bin"};
    for (const auto& dir : dirs) {
        if (!fs::exists(dir)) {
            try {
                fs::create_directories(dir);
                LOG_INFO("Created directory: " + dir);
            } catch (const std::exception& e) {
                return Result<void>::error(
                    ErrorCode::FILE_ACCESS_DENIED,
                    "Failed to create directory " + dir + ": " + std::string(e.what())
                );
            }
        }
    }
    return Result<void>::ok();
}

// 检查ONNX Runtime依赖
Result<void> checkOnnxRuntimeDependencies() {
    const char* onnx_dir = std::getenv("ONNXRUNTIME_ROOT_DIR");
    if (!onnx_dir || strlen(onnx_dir) == 0) {
        return Result<void>::error(
            ErrorCode::SYSTEM_ERROR,
            "ONNXRUNTIME_ROOT_DIR environment variable is not set. "
            "Please run 'source setup_environment.sh' or set the environment variable manually."
        );
    }
    
    fs::path lib_path(onnx_dir);
    lib_path /= "lib";
    
    // 检查库文件 (.so 在 Linux, .dll 在 Windows)
    bool found_lib = false;
    if (fs::exists(lib_path / "libonnxruntime.so")) {
        found_lib = true;
    } else if (fs::exists(lib_path / "onnxruntime.dll")) {
        found_lib = true;
    }
    
    if (!found_lib) {
        return Result<void>::error(
            ErrorCode::SYSTEM_ERROR,
            "ONNX Runtime library not found in " + lib_path.string() + ". "
            "Please make sure ONNX Runtime is correctly installed."
        );
    }
    
    return Result<void>::ok();
}

// 主程序入口
int main(int argc, char* argv[]) {
    try {
        // 初始化日志系统
        initLogger("logs/server.log", LogLevel::INFO, LogLevel::INFO);
        
        // 注册信号处理
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
        
        LOG_INFO("Zero Latency YOLO FPS Cloud Assist System starting up...");
        
        // 确保必要目录存在
        auto dirs_result = ensureDirectoriesExist();
        if (dirs_result.hasError()) {
            LOG_ERROR(dirs_result.error().toString());
            return 1;
        }
        
        // 检查ONNX Runtime依赖
        auto onnx_result = checkOnnxRuntimeDependencies();
        if (onnx_result.hasError()) {
            LOG_ERROR(onnx_result.error().toString());
            LOG_ERROR("Hint: When setting environment variables, make sure to use the correct shell path syntax.");
            LOG_ERROR("Example: export ONNXRUNTIME_ROOT_DIR=/path/to/onnxruntime");
            return 1;
        }
        
        // 加载配置
        ConfigManager& config_manager = ConfigManager::getInstance();
        auto config_result = config_manager.loadServerConfig("configs/server.json");
        
        if (config_result.hasError()) {
            LOG_ERROR("Failed to load configuration: " + config_result.error().message);
            LOG_WARN("Using default configuration");
            config_result = Result<ServerConfig>::ok(ServerConfig());
        }
        
        ServerConfig config = config_result.value();
        
        // 初始化系统优化
        if (config.use_cpu_affinity) {
            auto affinity_result = setCpuAffinity(config.cpu_core_id);
            if (affinity_result.hasError()) {
                LOG_WARN(affinity_result.error().toString());
            } else {
                LOG_INFO("CPU affinity set to core " + std::to_string(config.cpu_core_id));
            }
        }
        
        if (config.use_high_priority) {
            auto priority_result = setProcessPriority();
            if (priority_result.hasError()) {
                LOG_WARN(priority_result.error().toString());
                LOG_WARN("High priority requires root privileges");
            } else {
                LOG_INFO("Process priority set to high");
            }
        }
        
        // 显示系统信息
        printSystemInfo(config);
        
        // 创建推理引擎
        std::shared_ptr<IInferenceEngine> inference_engine;
        
        if (config.inference_engine == "onnx") {
            inference_engine = std::make_shared<OnnxInferenceEngine>(config);
        } else {
            // 尝试使用注册的引擎工厂创建
            LOG_INFO("Attempting to create inference engine: " + config.inference_engine);
            inference_engine = InferenceEngineManager::getInstance().createEngine(config.inference_engine, config);
            
            if (!inference_engine) {
                LOG_ERROR("Failed to create inference engine: " + config.inference_engine);
                LOG_INFO("Available engines: " + 
                         InferenceEngineManager::getInstance().getAvailableEngines()[0]);
                
                LOG_WARN("Falling back to ONNX inference engine");
                inference_engine = std::make_shared<OnnxInferenceEngine>(config);
            }
        }
        
        // 初始化推理引擎
        auto engine_init_result = inference_engine->initialize();
        if (engine_init_result.hasError()) {
            LOG_ERROR("Failed to initialize inference engine: " + engine_init_result.error().message);
            return 1;
        }
        
        // 创建游戏适配器
        std::shared_ptr<IGameAdapter> game_adapter;
        
        game_adapter = GameAdapterManager::getInstance().createAdapter("cs16");
        if (!game_adapter) {
            LOG_ERROR("Failed to create game adapter");
            LOG_ERROR("Available adapters: ");
            
            for (const auto& name : GameAdapterManager::getInstance().getAvailableAdapters()) {
                LOG_ERROR("  - " + name);
            }
            
            return 1;
        }
        
        // 初始化游戏适配器
        auto adapter_init_result = game_adapter->initialize(config.game_adapters);
        if (adapter_init_result.hasError()) {
            LOG_ERROR("Failed to initialize game adapter: " + adapter_init_result.error().message);
            return 1;
        }
        
        // 创建网络服务器
        ReliableUdpConfig udp_config;
        udp_config.port = config.network.port;
        udp_config.recv_buffer_size = config.network.recv_buffer_size;
        udp_config.send_buffer_size = config.network.send_buffer_size;
        udp_config.timeout_ms = config.network.timeout_ms;
        udp_config.heartbeat_interval_ms = config.network.heartbeat_interval_ms;
        udp_config.max_retries = config.network.max_retries;
        
        auto network = std::make_shared<ReliableUdpServer>(udp_config);
        
        // 初始化网络服务器
        auto network_init_result = network->initialize();
        if (network_init_result.hasError()) {
            LOG_ERROR("Failed to initialize network server: " + network_init_result.error().message);
            return 1;
        }
        
        // 创建网络协议服务器
        auto server = std::make_shared<NetworkServer>(network, inference_engine, game_adapter);
        
        // 设置数据包处理器
        network->setPacketHandler([server](const std::vector<uint8_t>& data, const struct sockaddr_in& addr) {
            server->handlePacket(data, addr);
        });
        
        // 启动网络服务器
        auto network_start_result = network->start();
        if (network_start_result.hasError()) {
            LOG_ERROR("Failed to start network server: " + network_start_result.error().message);
            return 1;
        }
        
        // 启动监控线程
        std::thread monitor_thread(monitorThread, inference_engine, game_adapter, network, config);
        
        LOG_INFO("Server started successfully on port " + std::to_string(config.network.port));
        LOG_INFO("Press Ctrl+C to stop the server");
        
        // 主循环
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // 清理资源
        LOG_INFO("Shutting down server...");
        
        // 停止网络服务器
        auto network_stop_result = network->stop();
        if (network_stop_result.hasError()) {
            LOG_ERROR("Failed to stop network server: " + network_stop_result.error().message);
        }
        
        // 关闭推理引擎
        auto engine_shutdown_result = inference_engine->shutdown();
        if (engine_shutdown_result.hasError()) {
            LOG_ERROR("Failed to shutdown inference engine: " + engine_shutdown_result.error().message);
        }
        
        // 等待监控线程退出
        if (monitor_thread.joinable()) {
            monitor_thread.join();
        }
        
        LOG_INFO("Server shutdown complete");
        
        return 0;
    } catch (const std::exception& e) {
        LOG_FATAL("Unhandled exception: " + std::string(e.what()));
        return 1;
    }
}