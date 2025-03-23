#pragma once

#include <Windows.h>
#include <string>
#include <vector>

namespace zero_latency {

// Windows特定工具类
class Win32Utils {
public:
    // 设置进程优先级
    static bool setProcessPriority(bool high_priority);
    
    // 设置线程优先级
    static bool setThreadPriority(bool high_priority);
    
    // 注册热键
    static bool registerHotkey(HWND window, int id, UINT modifiers, UINT key);
    
    // 注销热键
    static bool unregisterHotkey(HWND window, int id);
    
    // 获取可执行文件路径
    static std::string getExecutablePath();
    
    // 获取应用程序目录
    static std::string getApplicationDirectory();
    
    // 检查管理员权限
    static bool isRunAsAdmin();
    
    // 以管理员身份重启
    static bool restartAsAdmin();
    
    // 隐藏控制台窗口
    static void hideConsoleWindow();
    
    // 显示控制台窗口
    static void showConsoleWindow();
    
    // 显示错误消息框
    static void showErrorMessage(const std::string& title, const std::string& message);
    
    // 获取所有窗口标题列表
    static std::vector<std::pair<HWND, std::string>> getWindowTitleList();
    
    // 获取窗口客户区大小
    static bool getClientSize(HWND window, int& width, int& height);
    
    // 将客户区坐标转换为屏幕坐标
    static bool clientToScreen(HWND window, int& x, int& y);
    
    // 将屏幕坐标转换为客户区坐标
    static bool screenToClient(HWND window, int& x, int& y);
    
    // 获取窗口进程ID
    static DWORD getWindowProcessId(HWND window);
    
    // 检查是否有管理员权限访问窗口
    static bool canAccessWindow(HWND window);
    
    // 获取系统DPI
    static int getSystemDPI();
    
    // 获取错误消息
    static std::string getLastErrorMessage();
};

} // namespace zero_latency