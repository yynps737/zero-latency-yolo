#include "input_control.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace zero_latency {

InputControl::InputControl()
    : input_locked_(false) {
    // 初始化鼠标位置
    current_mouse_position_.x = 0;
    current_mouse_position_.y = 0;
    
    // 初始化鼠标按钮状态
    for (int i = 0; i < 5; i++) {
        mouse_buttons_[i] = false;
    }
}

InputControl::~InputControl() {
    shutdown();
}

bool InputControl::initialize() {
    std::lock_guard<std::mutex> lock(input_mutex_);
    
    // 获取当前鼠标位置
    if (!GetCursorPos(&current_mouse_position_)) {
        std::cerr << "获取鼠标位置失败: " << GetLastError() << std::endl;
        return false;
    }
    
    // 初始化键盘状态
    key_states_.clear();
    
    return true;
}

void InputControl::shutdown() {
    std::lock_guard<std::mutex> lock(input_mutex_);
    
    // 清理资源
    key_states_.clear();
}

bool InputControl::moveMouseTo(HWND window, int x, int y) {
    // 如果输入被锁定，不进行操作
    if (input_locked_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(input_mutex_);
    
    // 转换客户端坐标到屏幕坐标
    POINT point = { x, y };
    if (!ClientToScreen(window, &point)) {
        std::cerr << "客户端坐标转换失败: " << GetLastError() << std::endl;
        return false;
    }
    
    // 保存新位置
    current_mouse_position_ = point;
    
    // 设置鼠标位置
    if (!SetCursorPos(point.x, point.y)) {
        std::cerr << "设置鼠标位置失败: " << GetLastError() << std::endl;
        return false;
    }
    
    return true;
}

bool InputControl::moveMouseBy(int dx, int dy) {
    // 如果输入被锁定，不进行操作
    if (input_locked_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(input_mutex_);
    
    // 获取当前位置
    POINT current_pos;
    if (!GetCursorPos(&current_pos)) {
        std::cerr << "获取鼠标位置失败: " << GetLastError() << std::endl;
        return false;
    }
    
    // 计算新位置
    POINT new_pos;
    new_pos.x = current_pos.x + dx;
    new_pos.y = current_pos.y + dy;
    
    // 保存新位置
    current_mouse_position_ = new_pos;
    
    // 设置鼠标位置
    if (!SetCursorPos(new_pos.x, new_pos.y)) {
        std::cerr << "设置鼠标位置失败: " << GetLastError() << std::endl;
        return false;
    }
    
    return true;
}

bool InputControl::simulateMouseClick(int button, bool is_down) {
    // 如果输入被锁定，不进行操作
    if (input_locked_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(input_mutex_);
    
    // 准备INPUT结构
    INPUT input = {};
    input.type = INPUT_MOUSE;
    
    // 设置按钮操作
    switch (button) {
        case 0: // 左键
            input.mi.dwFlags = is_down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            break;
        case 1: // 右键
            input.mi.dwFlags = is_down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            break;
        case 2: // 中键
            input.mi.dwFlags = is_down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            break;
        case 3: // X1键
            input.mi.dwFlags = is_down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData = XBUTTON1;
            break;
        case 4: // X2键
            input.mi.dwFlags = is_down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData = XBUTTON2;
            break;
        default:
            std::cerr << "无效的鼠标按钮: " << button << std::endl;
            return false;
    }
    
    // 更新鼠标状态
    if (button >= 0 && button < 5) {
        mouse_buttons_[button] = is_down;
    }
    
    // 发送输入
    return sendInput({ input });
}

bool InputControl::simulateKeyPress(int key_code, bool is_down) {
    // 如果输入被锁定，不进行操作
    if (input_locked_) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(input_mutex_);
    
    // 准备INPUT结构
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(key_code);
    input.ki.dwFlags = is_down ? 0 : KEYEVENTF_KEYUP;
    
    // 更新键盘状态
    updateKeyState(key_code, is_down);
    
    // 发送输入
    return sendInput({ input });
}

bool InputControl::applyRecoilCompensation(const Vector2D& compensation) {
    // 如果输入被锁定，不进行操作
    if (input_locked_) {
        return false;
    }
    
    // 获取前置条件 - 只有在鼠标左键按下时才应用后座力补偿
    if (!isMouseButtonPressed(0)) {
        return false;
    }
    
    // 应用后座力补偿 - 向下和稍微左右移动鼠标
    int dx = static_cast<int>(-compensation.x * 10.0f); // 放大效果
    int dy = static_cast<int>(-compensation.y * 10.0f); // 放大效果
    
    return moveMouseBy(dx, dy);
}

bool InputControl::getMousePosition(int& x, int& y) {
    std::lock_guard<std::mutex> lock(input_mutex_);
    
    x = current_mouse_position_.x;
    y = current_mouse_position_.y;
    
    return true;
}

bool InputControl::isKeyPressed(int key_code) {
    std::lock_guard<std::mutex> lock(input_mutex_);
    
    // 查找键状态
    for (const auto& key_state : key_states_) {
        if (key_state.key_code == key_code) {
            return key_state.is_pressed;
        }
    }
    
    // 如果没有找到记录，检查系统状态
    return (GetAsyncKeyState(key_code) & 0x8000) != 0;
}

bool InputControl::isMouseButtonPressed(int button) {
    if (button < 0 || button >= 5) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(input_mutex_);
    return mouse_buttons_[button];
}

void InputControl::setLocked(bool locked) {
    input_locked_ = locked;
}

bool InputControl::isLocked() const {
    return input_locked_;
}

bool InputControl::sendInput(const std::vector<INPUT>& inputs) {
    if (inputs.empty()) {
        return true;
    }
    
    // 发送输入
    UINT result = ::SendInput(static_cast<UINT>(inputs.size()), 
                            const_cast<INPUT*>(inputs.data()), 
                            sizeof(INPUT));
    
    if (result != inputs.size()) {
        std::cerr << "发送输入失败: " << GetLastError() << std::endl;
        return false;
    }
    
    return true;
}

bool InputControl::postInput(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (!window || !IsWindow(window)) {
        std::cerr << "无效的窗口句柄" << std::endl;
        return false;
    }
    
    // 发送Windows消息
    if (!PostMessage(window, message, wparam, lparam)) {
        std::cerr << "发送消息失败: " << GetLastError() << std::endl;
        return false;
    }
    
    return true;
}

void InputControl::updateKeyState(int key_code, bool is_pressed) {
    // 查找现有状态
    for (auto& key_state : key_states_) {
        if (key_state.key_code == key_code) {
            key_state.is_pressed = is_pressed;
            if (is_pressed) {
                key_state.press_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
            }
            return;
        }
    }
    
    // 如果是按下状态，添加新状态
    if (is_pressed) {
        KeyState new_state;
        new_state.key_code = key_code;
        new_state.is_pressed = true;
        new_state.press_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        key_states_.push_back(new_state);
    }
}

void InputControl::updateMouseState(int button, bool is_pressed) {
    if (button >= 0 && button < 5) {
        mouse_buttons_[button] = is_pressed;
    }
}

} // namespace zero_latency