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

#include "../common/logger.h"
#include "../common/event_bus.h"
#include "../common/result.h"
#include "../server/config.h"

#include "../inference/inference_engine.h"
#include "../inference/onnx_engine.h"
#include "../game/base/game_adapter_base.h"
#include "../game/base/game_adapter_manager.h"
#include "../network/reliable_udp.h"
#include "../network/network_server.h"

using namespace zero_latency;
namespace fs = std::filesystem;

std::atomic<bool> g_running(true);

void signalHandler(int signal) {
    LOG_INFO("Received signal: " + std::to_string(signal) + ", shutting down server...");
    g_running = false;
}

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

void printSystemInfo(const ServerConfig& config) {
    LOG_INFO("===== Zero Latency YOLO FPS Cloud Assist System =====");
    LOG_INFO("Version: 1.0.0");
    LOG_INFO("System information:");
    
    LOG_INFO("  - CPU cores: " + std::to_string(std::thread::hardware_concurrency()));
    
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    long memory_mb = (pages * page_size) / (1024 * 1024);
    LOG_INFO("  - System memory: " + std::to_string(memory_mb) + " MB");
    
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

void monitorThread(std::shared_ptr<IInferenceEngine> engine, 
                  std::shared_ptr<GameAdapterBase> adapter,
                  std::shared_ptr<ReliableUdpServer> network,
                  const ServerConfig& config) {
    LOG_INFO("Status monitor thread started");
    
    uint64_t prev_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        uint64_t current_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        uint64_t elapsed = current_timestamp - prev_timestamp;
        
        if (elapsed > 0) {
            auto engine_status = engine->getStatus();
            auto queue_size = engine->getQueueSize();
            
            auto network_status = network->getStatus();
            auto client_count = network->getClientCount();
            
            auto adapter_status = adapter->getStatus();
            
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
            
            if (config.analytics.enable_analytics && config.analytics.save_stats_to_file) {
                fs::path stats_path(config.analytics.stats_file);
                fs::path stats_dir = stats_path.parent_path();
                if (!stats_dir.empty() && !fs::exists(stats_dir)) {
                    try {
                        fs::create_directories(stats_dir);
                    } catch (const std::exception& e) {
                        LOG_ERROR("Failed to create stats directory: " + std::string(e.what()));
                    }
                }
            }
            
            prev_timestamp = current_timestamp;
        }
    }
    
    LOG_INFO("Status monitor thread stopped");
}

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

int main(int argc, char* argv[]) {
    try {
        initLogger("logs/server.log", LogLevel::INFO, LogLevel::INFO);
        
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
        
        LOG_INFO("Zero Latency YOLO FPS Cloud Assist System starting up...");
        
        auto dirs_result = ensureDirectoriesExist();
        if (dirs_result.hasError()) {
            LOG_ERROR(dirs_result.error().toString());
            return 1;
        }
        
        auto onnx_result = checkOnnxRuntimeDependencies();
        if (onnx_result.hasError()) {
            LOG_ERROR(onnx_result.error().toString());
            LOG_ERROR("Hint: When setting environment variables, make sure to use the correct shell path syntax.");
            LOG_ERROR("Example: export ONNXRUNTIME_ROOT_DIR=/path/to/onnxruntime");
            return 1;
        }
        
        ConfigManager& config_manager = ConfigManager::getInstance();
        auto config_result = config_manager.loadServerConfig("configs/server.json");
        
        if (config_result.hasError()) {
            LOG_ERROR("Failed to load configuration: " + config_result.error().message);
            LOG_WARN("Using default configuration");
            config_result = Result<ServerConfig>::ok(ServerConfig());
        }
        
        ServerConfig config = config_result.value();
        
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
        
        printSystemInfo(config);
        
        std::shared_ptr<IInferenceEngine> inference_engine;
        
        if (config.inference_engine == "onnx") {
            inference_engine = std::make_shared<OnnxInferenceEngine>(config);
        } else {
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
        
        auto engine_init_result = inference_engine->initialize();
        if (engine_init_result.hasError()) {
            LOG_ERROR("Failed to initialize inference engine: " + engine_init_result.error().message);
            return 1;
        }
        
        std::shared_ptr<GameAdapterBase> game_adapter;
        
        game_adapter = GameAdapterManager::getInstance().createAdapter("cs16");
        if (!game_adapter) {
            LOG_ERROR("Failed to create game adapter");
            LOG_ERROR("Available adapters: ");
            
            for (const auto& name : GameAdapterManager::getInstance().getAvailableAdapters()) {
                LOG_ERROR("  - " + name);
            }
            
            return 1;
        }
        
        auto adapter_init_result = game_adapter->initialize(config.game_adapters);
        if (adapter_init_result.hasError()) {
            LOG_ERROR("Failed to initialize game adapter: " + adapter_init_result.error().message);
            return 1;
        }
        
        ReliableUdpConfig udp_config;
        udp_config.port = config.network.port;
        udp_config.recv_buffer_size = config.network.recv_buffer_size;
        udp_config.send_buffer_size = config.network.send_buffer_size;
        udp_config.timeout_ms = config.network.timeout_ms;
        udp_config.heartbeat_interval_ms = config.network.heartbeat_interval_ms;
        udp_config.max_retries = config.network.max_retries;
        
        auto network = std::make_shared<ReliableUdpServer>(udp_config);
        
        auto network_init_result = network->initialize();
        if (network_init_result.hasError()) {
            LOG_ERROR("Failed to initialize network server: " + network_init_result.error().message);
            return 1;
        }
        
        auto server = std::make_shared<NetworkServer>(network, inference_engine, game_adapter);
        
        network->setPacketHandler([server](const std::vector<uint8_t>& data, const struct sockaddr_in& addr) {
            server->handlePacket(data, addr);
        });
        
        auto network_start_result = network->start();
        if (network_start_result.hasError()) {
            LOG_ERROR("Failed to start network server: " + network_start_result.error().message);
            return 1;
        }
        
        std::thread monitor_thread(monitorThread, inference_engine, game_adapter, network, config);
        
        LOG_INFO("Server started successfully on port " + std::to_string(config.network.port));
        LOG_INFO("Press Ctrl+C to stop the server");
        
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        LOG_INFO("Shutting down server...");
        
        auto network_stop_result = network->stop();
        if (network_stop_result.hasError()) {
            LOG_ERROR("Failed to stop network server: " + network_stop_result.error().message);
        }
        
        auto engine_shutdown_result = inference_engine->shutdown();
        if (engine_shutdown_result.hasError()) {
            LOG_ERROR("Failed to shutdown inference engine: " + engine_shutdown_result.error().message);
        }
        
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