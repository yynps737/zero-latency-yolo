#include "win32_utils.h"
#include <iostream>
#include <sstream>
#include <Psapi.h>
#include <Shlwapi.h>
#include <Shlobj.h>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

namespace zero_latency {

// 窗口枚举回调函数
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    
    // 获取窗口标题
    char title[256];
    if (GetWindowTextA(hwnd, title, sizeof(title)) == 0) {
        return TRUE; // 没有标题的窗口跳过
    }
    
    // 跳过系统窗口
    if (strcmp(title, "Program Manager") == 0 || 
        strcmp(title, "Windows Shell Experience Host") == 0) {
        return TRUE;
    }
    
    // 添加到窗口列表
    std::vector<std::pair<HWND, std::string>>* window_list = 
        reinterpret_cast<std::vector<std::pair<HWND, std::string>>*>(lParam);
    
    window_list->push_back(std::make_pair(hwnd, title));
    
    return TRUE;
}

bool Win32Utils::setProcessPriority(bool high_priority) {
    DWORD priority_class = high_priority ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS;
    
    if (!SetPriorityClass(GetCurrentProcess(), priority_class)) {
        std::cerr << "设置进程优先级失败: " << GetLastError() << std::endl;
        return false;
    }
    
    return true;
}

bool Win32Utils::setThreadPriority(bool high_priority) {
    int priority = high_priority ? THREAD_PRIORITY_HIGHEST : THREAD_PRIORITY_NORMAL;
    
    if (!SetThreadPriority(GetCurrentThread(), priority)) {
        std::cerr << "设置线程优先级失败: " << GetLastError() << std::endl;
        return false;
    }
    
    return true;
}

bool Win32Utils::registerHotkey(HWND window, int id, UINT modifiers, UINT key) {
    if (!RegisterHotKey(window, id, modifiers, key)) {
        std::cerr << "注册热键失败: " << GetLastError() << std::endl;
        return false;
    }
    
    return true;
}

bool Win32Utils::unregisterHotkey(HWND window, int id) {
    if (!UnregisterHotKey(window, id)) {
        std::cerr << "注销热键失败: " << GetLastError() << std::endl;
        return false;
    }
    
    return true;
}

std::string Win32Utils::getExecutablePath() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    return std::string(path);
}

std::string Win32Utils::getApplicationDirectory() {
    std::string exe_path = getExecutablePath();
    size_t pos = exe_path.find_last_of("\\/");
    
    if (pos != std::string::npos) {
        return exe_path.substr(0, pos + 1);
    }
    
    return exe_path;
}

bool Win32Utils::isRunAsAdmin() {
    BOOL is_admin = FALSE;
    SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
    PSID admin_group = NULL;
    
    // 创建管理员组SID
    if (!AllocateAndInitializeSid(
        &nt_authority,
        2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &admin_group)) {
        return false;
    }
    
    // 检查当前用户是否在管理员组中
    if (!CheckTokenMembership(NULL, admin_group, &is_admin)) {
        is_admin = FALSE;
    }
    
    // 释放SID
    FreeSid(admin_group);
    
    return is_admin != FALSE;
}

bool Win32Utils::restartAsAdmin() {
    // 获取当前可执行文件路径
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    
    // 使用ShellExecute以管理员身份启动程序
    SHELLEXECUTEINFOA sei = {0};
    sei.cbSize = sizeof(SHELLEXECUTEINFOA);
    sei.lpVerb = "runas";  // 请求管理员权限
    sei.lpFile = path;
    sei.nShow = SW_NORMAL;
    
    if (!ShellExecuteExA(&sei)) {
        DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) {
            // 用户取消了UAC提示
            return false;
        }
        
        std::cerr << "以管理员身份重启失败: " << error << std::endl;
        return false;
    }
    
    // 关闭当前进程
    ExitProcess(0);
    
    return true;
}

void Win32Utils::hideConsoleWindow() {
    ShowWindow(GetConsoleWindow(), SW_HIDE);
}

void Win32Utils::showConsoleWindow() {
    ShowWindow(GetConsoleWindow(), SW_SHOW);
}

void Win32Utils::showErrorMessage(const std::string& title, const std::string& message) {
    MessageBoxA(NULL, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
}

std::vector<std::pair<HWND, std::string>> Win32Utils::getWindowTitleList() {
    std::vector<std::pair<HWND, std::string>> window_list;
    
    // 枚举所有可见窗口
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&window_list));
    
    return window_list;
}

bool Win32Utils::getClientSize(HWND window, int& width, int& height) {
    if (!IsWindow(window)) {
        return false;
    }
    
    RECT rect;
    if (!GetClientRect(window, &rect)) {
        return false;
    }
    
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    
    return true;
}

bool Win32Utils::clientToScreen(HWND window, int& x, int& y) {
    if (!IsWindow(window)) {
        return false;
    }
    
    POINT point = {x, y};
    if (!ClientToScreen(window, &point)) {
        return false;
    }
    
    x = point.x;
    y = point.y;
    
    return true;
}

bool Win32Utils::screenToClient(HWND window, int& x, int& y) {
    if (!IsWindow(window)) {
        return false;
    }
    
    POINT point = {x, y};
    if (!ScreenToClient(window, &point)) {
        return false;
    }
    
    x = point.x;
    y = point.y;
    
    return true;
}

DWORD Win32Utils::getWindowProcessId(HWND window) {
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    return process_id;
}

bool Win32Utils::canAccessWindow(HWND window) {
    // 检查窗口是否有效
    if (!IsWindow(window)) {
        return false;
    }
    
    // 获取窗口进程ID
    DWORD process_id = getWindowProcessId(window);
    if (process_id == 0) {
        return false;
    }
    
    // 获取进程句柄
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process_id);
    if (process == NULL) {
        return false;
    }
    
    // 如果能打开进程，则可以访问该窗口
    CloseHandle(process);
    return true;
}

int Win32Utils::getSystemDPI() {
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    return dpi;
}

std::string Win32Utils::getLastErrorMessage() {
    DWORD error_code = GetLastError();
    if (error_code == 0) {
        return "没有错误";
    }
    
    LPSTR message_buffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&message_buffer,
        0,
        NULL
    );
    
    std::string message;
    if (message_buffer) {
        message = std::string(message_buffer, size);
        LocalFree(message_buffer);
    } else {
        std::stringstream ss;
        ss << "错误码: " << error_code;
        message = ss.str();
    }
    
    return message;
}

} // namespace zero_latency