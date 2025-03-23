#pragma once

#include <Windows.h>
#include <vector>
#include <string>
#include <mutex>
#include <memory>
#include <atomic>
#include "../common/types.h"

namespace zero_latency {

// 按键状态
struct KeyState {
    int key_code;       // 虚拟键码(VK_*)
    bool is_pressed;    // 是否按下
    uint64_t press_time; // 按下时间
};

// 输入控制类
class InputControl {
public:
    InputControl();
    ~InputControl();
    
    // 初始化输入控制系统
    bool initialize();
    
    // 释放资源
    void shutdown();
    
    // 鼠标移动到指定位置(绝对坐标)
    bool moveMouseTo(HWND window, int x, int y);
    
    // 鼠标移动指定偏移量(相对坐标)
    bool moveMouseBy(int dx, int dy);
    
    // 模拟鼠标点击
    bool simulateMouseClick(int button, bool is_down);
    
    // 模拟键盘按键
    bool simulateKeyPress(int key_code, bool is_down);
    
    // 应用后座力补偿
    bool applyRecoilCompensation(const Vector2D& compensation);
    
    // 获取鼠标位置
    bool getMousePosition(int& x, int& y);
    
    // 检查键盘按键状态
    bool isKeyPressed(int key_code);
    
    // 检查鼠标按键状态
    bool isMouseButtonPressed(int button);
    
    // 锁定/解锁输入控制
    void setLocked(bool locked);
    
    // 检查输入是否锁定
    bool isLocked() const;
    
private:
    // 直接在Input API级别发送输入
    bool sendInput(const std::vector<INPUT>& inputs);
    
    // 使用Windows消息发送输入
    bool postInput(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    
    // 更新键盘状态
    void updateKeyState(int key_code, bool is_pressed);
    
    // 更新鼠标状态
    void updateMouseState(int button, bool is_pressed);
    
private:
    // 当前状态
    POINT current_mouse_position_;
    std::vector<KeyState> key_states_;
    bool mouse_buttons_[5]; // 左、右、中、X1、X2
    
    // 锁定状态
    std::atomic<bool> input_locked_;
    
    // 互斥锁
    mutable std::mutex input_mutex_;
};

} // namespace zero_latency