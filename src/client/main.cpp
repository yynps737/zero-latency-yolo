#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <Windows.h>

#include "prediction_engine.h"
#include "screen_capture.h"
#include "dual_engine.h"
#include "renderer.h"
#include "input_control.h"
#include "network.h"
#include "config.h"
#include "win32_utils.h"
#include "../common/constants.h"

using namespace zero_latency;

// 全局变量
std::atomic<bool> g_running(true);
std::atomic<uint64_t> g_frame_count(0);

// 控制台处理函数
BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
        std::cout << "接收到退出信号，正在关闭客户端..." << std::endl;
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

// 设置进程优先级
bool setProcessPriority(bool high_priority) {
    DWORD priority_class = high_priority ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS;
    if (!SetPriorityClass(GetCurrentProcess(), priority_class)) {
        std::cerr << "设置进程优先级失败: " << GetLastError() << std::endl;
        return false;
    }
    return true;
}

// 状态监控线程函数
void monitorThread(NetworkClient* network, ScreenCapture* capture, DualEngine* dual_engine) {
    uint64_t last_frame_count = 0;
    auto last_time = std::chrono::steady_clock::now();
    
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - last_time).count();
        
        if (elapsed > 0) {
            uint64_t current_frame_count = g_frame_count.load();
            
            float fps = static_cast<float>(current_frame_count - last_frame_count) / elapsed;
            
            SystemStatus status = network->getStatus();
            
            std::cout << "状态: FPS=" << fps
                      << ", 网络延迟=" << status.ping << "ms"
                      << ", 检测数=" << dual_engine->getDetectionCount()
                      << ", 预测数=" << dual_engine->getPredictionCount()
                      << std::endl;
            
            last_frame_count = current_frame_count;
            last_time = current_time;
        }
    }
}

// 查找游戏窗口
HWND findGameWindow(DWORD game_id) {
    HWND hwnd = NULL;
    
    switch (game_id) {
        case static_cast<DWORD>(GameType::CS_1_6):
            hwnd = FindWindowA("Valve001", NULL);  // CS 1.6 窗口类名
            break;
            
        case static_cast<DWORD>(GameType::CSGO):
            hwnd = FindWindowA("Valve001", NULL);  // CS:GO 窗口类名相同
            break;
            
        default:
            std::cerr << "不支持的游戏ID: " << game_id << std::endl;
            break;
    }
    
    return hwnd;
}

// 主函数
int main(int argc, char* argv[]) {
    // 设置控制台处理函数
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    
    std::cout << "===== 零延迟YOLO FPS云辅助系统客户端 =====" << std::endl;
    std::cout << "版本: 1.0.0" << std::endl;
    
    // 加载配置
    ClientConfig config;
    ConfigManager configManager;
    
    if (!configManager.loadClientConfig("configs/client.json", config)) {
        std::cerr << "加载配置失败，使用默认配置" << std::endl;
        config = ClientConfig(); // 使用默认值
    }
    
    // 设置进程优先级
    if (config.use_high_priority) {
        if (!setProcessPriority(true)) {
            std::cerr << "警告: 无法设置高优先级" << std::endl;
        }
    }
    
    // 创建组件实例
    PredictionEngine prediction_engine(config.prediction);
    NetworkClient network_client(config.server_ip, config.server_port);
    DualEngine dual_engine(&prediction_engine);
    Renderer renderer;
    InputControl input_control;
    
    // 初始化网络客户端
    if (!network_client.initialize()) {
        std::cerr << "初始化网络客户端失败" << std::endl;
        return 1;
    }
    
    // 设置网络回调
    network_client.setResultCallback([&dual_engine](const GameState& state) {
        dual_engine.addServerDetections(state);
    });
    
    // 初始化渲染器
    if (!renderer.initialize()) {
        std::cerr << "初始化渲染器失败" << std::endl;
        return 1;
    }
    
    // 初始化输入控制
    if (!input_control.initialize()) {
        std::cerr << "初始化输入控制失败" << std::endl;
        return 1;
    }
    
    // 查找游戏窗口
    HWND game_window = findGameWindow(config.game_id);
    if (!game_window && config.auto_start) {
        std::cerr << "找不到游戏窗口，等待游戏启动..." << std::endl;
        
        // 等待游戏启动
        int retry_count = 0;
        while (!game_window && retry_count < 30 && g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            game_window = findGameWindow(config.game_id);
            retry_count++;
        }
    }
    
    if (!game_window) {
        std::cerr << "找不到游戏窗口，请先启动游戏" << std::endl;
        return 1;
    }
    
    std::cout << "找到游戏窗口: " << std::hex << game_window << std::dec << std::endl;
    
    // 初始化屏幕捕获
    ScreenCapture screen_capture(game_window, config.compression);
    if (!screen_capture.initialize()) {
        std::cerr << "初始化屏幕捕获失败" << std::endl;
        return 1;
    }
    
    // 获取客户端信息
    RECT window_rect;
    GetClientRect(game_window, &window_rect);
    int width = window_rect.right - window_rect.left;
    int height = window_rect.bottom - window_rect.top;
    
    ClientInfo client_info;
    client_info.client_id = 0;  // 将由服务器分配
    client_info.protocol_version = PROTOCOL_VERSION;
    client_info.screen_width = width;
    client_info.screen_height = height;
    client_info.game_id = config.game_id;
    
    // 设置客户端信息
    network_client.setClientInfo(client_info);
    
    // 连接到服务器
    if (config.auto_connect) {
        if (!network_client.connect()) {
            std::cerr << "连接服务器失败: " << config.server_ip << ":" << config.server_port << std::endl;
            return 1;
        }
        std::cout << "已连接到服务器: " << config.server_ip << ":" << config.server_port << std::endl;
    }
    
    // 启动监控线程
    std::thread monitor_thread(monitorThread, &network_client, &screen_capture, &dual_engine);
    
    // 主循环
    std::cout << "客户端运行中，按Ctrl+C退出..." << std::endl;
    
    uint32_t frame_id = 0;
    auto last_capture_time = std::chrono::steady_clock::now();
    
    while (g_running) {
        auto start_time = std::chrono::steady_clock::now();
        
        // 检查游戏窗口是否仍然有效
        if (!IsWindow(game_window)) {
            std::cerr << "游戏窗口已关闭" << std::endl;
            break;
        }
        
        // 计算帧间隔
        auto now = std::chrono::steady_clock::now();
        auto capture_interval = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_capture_time).count();
        
        // 按目标FPS进行捕获
        if (capture_interval >= 1000 / config.target_fps) {
            // 捕获屏幕
            FrameData frame;
            frame.frame_id = frame_id++;
            frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
            
            if (screen_capture.captureFrame(frame)) {
                // 发送帧数据
                network_client.sendFrame(frame);
                
                // 更新统计信息
                g_frame_count++;
                last_capture_time = now;
            }
        }
        
        // 更新双引擎
        dual_engine.update();
        
        // 获取最新的游戏状态
        GameState current_state = dual_engine.getCurrentState();
        
        // 处理目标锁定和瞄准辅助
        if (config.enable_aim_assist && !current_state.detections.empty()) {
            int target_index = -1;
            
            // 查找最佳目标
            float best_score = std::numeric_limits<float>::max();
            for (size_t i = 0; i < current_state.detections.size(); i++) {
                const auto& det = current_state.detections[i];
                
                // 只考虑敌人或头部
                if (det.class_id != constants::cs16::CLASS_T && 
                    det.class_id != constants::cs16::CLASS_HEAD) {
                    continue;
                }
                
                // 计算到屏幕中心的距离
                float center_x = 0.5f;
                float center_y = 0.5f;
                float dx = det.box.x - center_x;
                float dy = det.box.y - center_y;
                float distance = std::sqrt(dx * dx + dy * dy);
                
                // 给头部更高优先级
                if (det.class_id == constants::cs16::CLASS_HEAD) {
                    distance *= 0.5f;
                }
                
                if (distance < best_score) {
                    best_score = distance;
                    target_index = i;
                }
            }
            
            // 如果找到目标，执行瞄准
            if (target_index >= 0) {
                const auto& target = current_state.detections[target_index];
                
                // 计算瞄准点
                float aim_x = target.box.x;
                float aim_y = target.box.y;
                
                // 根据目标类型调整瞄准点
                if (target.class_id != constants::cs16::CLASS_HEAD) {
                    // 如果不是头部，稍微向上瞄准
                    aim_y -= target.box.height * 0.2f;
                }
                
                // 获取游戏窗口客户端区域
                RECT client_rect;
                GetClientRect(game_window, &client_rect);
                int client_width = client_rect.right - client_rect.left;
                int client_height = client_rect.bottom - client_rect.top;
                
                // 转换归一化坐标到屏幕坐标
                int screen_x = static_cast<int>(aim_x * client_width);
                int screen_y = static_cast<int>(aim_y * client_height);
                
                // 检测键盘输入以激活辅助
                if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
                    // 鼠标左键按下时执行辅助
                    input_control.moveMouseTo(game_window, screen_x, screen_y);
                }
            }
        }
        
        // 渲染ESP
        if (config.enable_esp && !current_state.detections.empty()) {
            renderer.beginFrame(game_window);
            
            for (const auto& detection : current_state.detections) {
                // 获取游戏窗口客户端区域
                RECT client_rect;
                GetClientRect(game_window, &client_rect);
                int client_width = client_rect.right - client_rect.left;
                int client_height = client_rect.bottom - client_rect.top;
                
                // 转换归一化坐标到屏幕坐标
                int x = static_cast<int>(detection.box.x * client_width);
                int y = static_cast<int>(detection.box.y * client_height);
                int w = static_cast<int>(detection.box.width * client_width);
                int h = static_cast<int>(detection.box.height * client_height);
                
                // 根据目标类型选择颜色
                DWORD color;
                switch (detection.class_id) {
                    case constants::cs16::CLASS_T:
                        color = constants::ui::colors::T_COLOR;
                        break;
                    case constants::cs16::CLASS_CT:
                        color = constants::ui::colors::CT_COLOR;
                        break;
                    case constants::cs16::CLASS_HEAD:
                        color = constants::ui::colors::HEAD_COLOR;
                        break;
                    default:
                        color = constants::ui::colors::TEXT_COLOR;
                        break;
                }
                
                // 绘制边界框
                renderer.drawBox(x - w/2, y - h/2, w, h, color, constants::ui::ESP_LINE_THICKNESS);
                
                // 显示置信度
                char text[32];
                sprintf_s(text, "%.0f%%", detection.confidence * 100);
                renderer.drawText(x, y - h/2 - 15, text, constants::ui::colors::TEXT_COLOR, constants::ui::TEXT_SIZE);
            }
            
            renderer.endFrame();
        }
        
        // 计算帧耗时
        auto end_time = std::chrono::steady_clock::now();
        auto frame_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        // 控制帧率
        int target_frame_time = 1000 / config.target_fps;
        if (frame_time < target_frame_time) {
            std::this_thread::sleep_for(std::chrono::milliseconds(target_frame_time - frame_time));
        }
    }
    
    // 等待监控线程结束
    if (monitor_thread.joinable()) {
        monitor_thread.join();
    }
    
    // 断开连接
    network_client.disconnect();
    
    // 清理资源
    renderer.shutdown();
    input_control.shutdown();
    
    std::cout << "客户端已关闭" << std::endl;
    std::cout << "总共发送帧数: " << g_frame_count.load() << std::endl;
    
    return 0;
}