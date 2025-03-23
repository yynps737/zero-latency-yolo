#pragma once

#include <Windows.h>
#include <vector>
#include <string>
#include <mutex>
#include <memory>
#include <d3d11.h>
#include <dxgi1_2.h>

#include "../common/types.h"

namespace zero_latency {

// 区域定义
struct CaptureRegion {
    int x;
    int y;
    int width;
    int height;
    bool is_active;
};

// 屏幕捕获类
class ScreenCapture {
public:
    ScreenCapture(HWND target_window, const CompressionSettings& compression);
    ~ScreenCapture();
    
    // 初始化捕获系统
    bool initialize();
    
    // 释放资源
    void shutdown();
    
    // 捕获帧并填充FrameData
    bool captureFrame(FrameData& frame);
    
    // 设置目标窗口
    void setTargetWindow(HWND window);
    
    // 设置压缩设置
    void setCompressionSettings(const CompressionSettings& settings);
    
    // 设置感兴趣区域
    void setRegionOfInterest(const CaptureRegion& region);
    
    // 重置感兴趣区域
    void resetRegionOfInterest();
    
    // 检查窗口是否仍然有效
    bool isWindowValid() const;
    
private:
    // 初始化DirectX捕获
    bool initializeDirectX();
    
    // 释放DirectX资源
    void releaseDirectX();
    
    // 捕获窗口内容到缓冲区
    bool captureWindowToBitmap(std::vector<uint8_t>& bitmap_data, int& width, int& height);
    
    // 压缩图像数据
    bool compressImage(const std::vector<uint8_t>& bitmap_data, int width, int height, 
                       std::vector<uint8_t>& compressed_data, bool force_keyframe = false);
    
    // 计算两帧之间的差异
    bool calculateFrameDifference(const std::vector<uint8_t>& current_frame, 
                                 const std::vector<uint8_t>& previous_frame,
                                 int width, int height,
                                 CaptureRegion& diff_region);
    
    // 只发送变化的区域
    bool encodeChangedRegion(const std::vector<uint8_t>& bitmap_data, 
                            int width, int height,
                            const CaptureRegion& region,
                            std::vector<uint8_t>& compressed_data);
    
    // 压缩区域之前，调整大小以确保对齐
    void adjustRegionForAlignment(CaptureRegion& region, int alignment);
    
    // 判断是否应该发送关键帧
    bool shouldSendKeyframe();
    
private:
    HWND target_window_;
    CompressionSettings compression_settings_;
    CaptureRegion region_of_interest_;
    bool use_dxgi_capture_;
    
    // 帧计数
    uint32_t frame_count_;
    
    // 上一帧信息
    std::vector<uint8_t> previous_frame_data_;
    int previous_frame_width_;
    int previous_frame_height_;
    bool has_previous_frame_;
    
    // 互斥锁
    mutable std::mutex capture_mutex_;
    
    // DirectX捕获相关
    ID3D11Device* d3d_device_;
    ID3D11DeviceContext* d3d_context_;
    IDXGIOutputDuplication* dxgi_output_duplication_;
    
    // 用于JPEG压缩的临时缓冲区
    std::vector<uint8_t> temp_buffer_;
};

} // namespace zero_latency