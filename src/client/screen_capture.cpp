#include "screen_capture.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wincodec.h>
#include "win32_utils.h"
#include "stb_image.h"
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace zero_latency {

ScreenCapture::ScreenCapture(HWND target_window, const CompressionSettings& compression)
    : target_window_(target_window),
      compression_settings_(compression),
      use_dxgi_capture_(true),
      frame_count_(0),
      previous_frame_width_(0),
      previous_frame_height_(0),
      has_previous_frame_(false),
      d3d_device_(nullptr),
      d3d_context_(nullptr),
      dxgi_output_duplication_(nullptr) {
    
    // 初始化ROI为空
    region_of_interest_.x = 0;
    region_of_interest_.y = 0;
    region_of_interest_.width = 0;
    region_of_interest_.height = 0;
    region_of_interest_.is_active = false;
}

ScreenCapture::~ScreenCapture() {
    shutdown();
}

bool ScreenCapture::initialize() {
    // 检查窗口是否有效
    if (!IsWindow(target_window_)) {
        std::cerr << "目标窗口无效" << std::endl;
        return false;
    }
    
    // 尝试初始化DirectX捕获
    if (!initializeDirectX()) {
        std::cerr << "初始化DirectX捕获失败，将使用GDI捕获" << std::endl;
        use_dxgi_capture_ = false;
    }
    
    // 预分配临时缓冲区
    temp_buffer_.reserve(1920 * 1080 * 4); // 足够大的缓冲区
    
    return true;
}

void ScreenCapture::shutdown() {
    // 释放DirectX资源
    releaseDirectX();
    
    // 清空缓冲区
    previous_frame_data_.clear();
    temp_buffer_.clear();
}

bool ScreenCapture::captureFrame(FrameData& frame) {
    std::lock_guard<std::mutex> lock(capture_mutex_);
    
    // 检查窗口是否有效
    if (!isWindowValid()) {
        std::cerr << "目标窗口无效，无法捕获" << std::endl;
        return false;
    }
    
    // 捕获画面到位图
    std::vector<uint8_t> bitmap_data;
    int width = 0, height = 0;
    if (!captureWindowToBitmap(bitmap_data, width, height)) {
        std::cerr << "捕获窗口失败" << std::endl;
        return false;
    }
    
    // 更新帧信息
    frame.frame_id = frame_count_++;
    frame.width = width;
    frame.height = height;
    
    // 计算是否为关键帧
    bool is_keyframe = (frame_count_ % compression_settings_.keyframe_interval == 0) || !has_previous_frame_;
    frame.keyframe = is_keyframe;
    
    // 区域编码或差分编码
    if (compression_settings_.use_roi_encoding && region_of_interest_.is_active) {
        // 使用ROI编码
        if (!encodeChangedRegion(bitmap_data, width, height, region_of_interest_, frame.data)) {
            std::cerr << "ROI编码失败" << std::endl;
            return false;
        }
    } else if (compression_settings_.use_difference_encoding && has_previous_frame_) {
        // 使用差分编码
        CaptureRegion diff_region;
        if (calculateFrameDifference(bitmap_data, previous_frame_data_, width, height, diff_region)) {
            if (diff_region.width > 0 && diff_region.height > 0) {
                if (!encodeChangedRegion(bitmap_data, width, height, diff_region, frame.data)) {
                    std::cerr << "差分编码失败" << std::endl;
                    return false;
                }
            } else {
                // 没有变化，发送空数据
                frame.data.clear();
            }
        } else {
            // 差分计算失败，使用完整帧
            if (!compressImage(bitmap_data, width, height, frame.data, true)) {
                std::cerr << "图像压缩失败" << std::endl;
                return false;
            }
        }
    } else {
        // 直接压缩整个图像
        if (!compressImage(bitmap_data, width, height, frame.data, is_keyframe)) {
            std::cerr << "图像压缩失败" << std::endl;
            return false;
        }
    }
    
    // 保存当前帧作为下一帧的参考
    if (is_keyframe || !has_previous_frame_) {
        previous_frame_data_ = bitmap_data;
        previous_frame_width_ = width;
        previous_frame_height_ = height;
        has_previous_frame_ = true;
    }
    
    return true;
}

void ScreenCapture::setTargetWindow(HWND window) {
    std::lock_guard<std::mutex> lock(capture_mutex_);
    
    if (target_window_ != window) {
        target_window_ = window;
        has_previous_frame_ = false; // 重置帧历史
    }
}

void ScreenCapture::setCompressionSettings(const CompressionSettings& settings) {
    std::lock_guard<std::mutex> lock(capture_mutex_);
    compression_settings_ = settings;
}

void ScreenCapture::setRegionOfInterest(const CaptureRegion& region) {
    std::lock_guard<std::mutex> lock(capture_mutex_);
    region_of_interest_ = region;
    region_of_interest_.is_active = true;
}

void ScreenCapture::resetRegionOfInterest() {
    std::lock_guard<std::mutex> lock(capture_mutex_);
    region_of_interest_.is_active = false;
}

bool ScreenCapture::isWindowValid() const {
    return IsWindow(target_window_) != 0;
}

bool ScreenCapture::initializeDirectX() {
    // 创建D3D设备
    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // 默认适配器
        D3D_DRIVER_TYPE_HARDWARE,   // 硬件设备
        nullptr,                    // 不使用软件驱动
        0,                          // 标志
        nullptr,                    // 不指定特征级别
        0,                          // 不指定特征级别数量
        D3D11_SDK_VERSION,          // SDK版本
        &d3d_device_,               // 输出设备
        &feature_level,             // 输出特征级别
        &d3d_context_               // 输出上下文
    );
    
    if (FAILED(hr)) {
        std::cerr << "创建D3D设备失败: " << hr << std::endl;
        return false;
    }
    
    // 获取DXGI设备
    IDXGIDevice* dxgi_device = nullptr;
    hr = d3d_device_->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgi_device);
    if (FAILED(hr)) {
        releaseDirectX();
        std::cerr << "获取DXGI设备失败: " << hr << std::endl;
        return false;
    }
    
    // 获取DXGI适配器
    IDXGIAdapter* dxgi_adapter = nullptr;
    hr = dxgi_device->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgi_adapter);
    dxgi_device->Release();
    if (FAILED(hr)) {
        releaseDirectX();
        std::cerr << "获取DXGI适配器失败: " << hr << std::endl;
        return false;
    }
    
    // 获取输出
    IDXGIOutput* dxgi_output = nullptr;
    hr = dxgi_adapter->EnumOutputs(0, &dxgi_output);
    dxgi_adapter->Release();
    if (FAILED(hr)) {
        releaseDirectX();
        std::cerr << "获取DXGI输出失败: " << hr << std::endl;
        return false;
    }
    
    // 获取Output1接口
    IDXGIOutput1* dxgi_output1 = nullptr;
    hr = dxgi_output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgi_output1);
    dxgi_output->Release();
    if (FAILED(hr)) {
        releaseDirectX();
        std::cerr << "获取DXGI Output1接口失败: " << hr << std::endl;
        return false;
    }
    
    // 创建桌面复制
    hr = dxgi_output1->DuplicateOutput(d3d_device_, &dxgi_output_duplication_);
    dxgi_output1->Release();
    if (FAILED(hr)) {
        releaseDirectX();
        std::cerr << "创建桌面复制失败: " << hr << std::endl;
        return false;
    }
    
    return true;
}

void ScreenCapture::releaseDirectX() {
    if (dxgi_output_duplication_) {
        dxgi_output_duplication_->Release();
        dxgi_output_duplication_ = nullptr;
    }
    
    if (d3d_context_) {
        d3d_context_->Release();
        d3d_context_ = nullptr;
    }
    
    if (d3d_device_) {
        d3d_device_->Release();
        d3d_device_ = nullptr;
    }
}

bool ScreenCapture::captureWindowToBitmap(std::vector<uint8_t>& bitmap_data, int& width, int& height) {
    // 获取客户区矩形
    RECT client_rect;
    if (!GetClientRect(target_window_, &client_rect)) {
        std::cerr << "获取客户区矩形失败: " << GetLastError() << std::endl;
        return false;
    }
    
    width = client_rect.right;
    height = client_rect.bottom;
    
    if (width <= 0 || height <= 0) {
        std::cerr << "无效的窗口尺寸: " << width << "x" << height << std::endl;
        return false;
    }
    
    // 分配像素缓冲区 (BGRA格式)
    bitmap_data.resize(width * height * 4);
    
    if (use_dxgi_capture_ && dxgi_output_duplication_) {
        // 使用DXGI捕获
        
        // 待实现: 使用DXGI捕获窗口内容
        // 此处省略DXGI实现代码，因为它比较复杂，需要处理桌面复制、获取帧等
        
        // 捕获DXGI复杂，此处使用GDI备选
        use_dxgi_capture_ = false;
    }
    
    // 使用GDI捕获
    HDC hdc_window = GetDC(target_window_);
    if (!hdc_window) {
        std::cerr << "获取窗口DC失败: " << GetLastError() << std::endl;
        return false;
    }
    
    HDC hdc_mem = CreateCompatibleDC(hdc_window);
    if (!hdc_mem) {
        ReleaseDC(target_window_, hdc_window);
        std::cerr << "创建内存DC失败: " << GetLastError() << std::endl;
        return false;
    }
    
    HBITMAP hbitmap = CreateCompatibleBitmap(hdc_window, width, height);
    if (!hbitmap) {
        DeleteDC(hdc_mem);
        ReleaseDC(target_window_, hdc_window);
        std::cerr << "创建位图失败: " << GetLastError() << std::endl;
        return false;
    }
    
    HGDIOBJ old_obj = SelectObject(hdc_mem, hbitmap);
    
    // 复制窗口内容到位图
    if (!BitBlt(hdc_mem, 0, 0, width, height, hdc_window, 0, 0, SRCCOPY)) {
        SelectObject(hdc_mem, old_obj);
        DeleteObject(hbitmap);
        DeleteDC(hdc_mem);
        ReleaseDC(target_window_, hdc_window);
        std::cerr << "位图传输失败: " << GetLastError() << std::endl;
        return false;
    }
    
    // 获取位图信息
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // 自上而下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;   // RGBA
    bmi.bmiHeader.biCompression = BI_RGB;
    
    // 获取位图数据
    if (!GetDIBits(hdc_mem, hbitmap, 0, height, bitmap_data.data(), &bmi, DIB_RGB_COLORS)) {
        SelectObject(hdc_mem, old_obj);
        DeleteObject(hbitmap);
        DeleteDC(hdc_mem);
        ReleaseDC(target_window_, hdc_window);
        std::cerr << "获取位图数据失败: " << GetLastError() << std::endl;
        return false;
    }
    
    // 清理资源
    SelectObject(hdc_mem, old_obj);
    DeleteObject(hbitmap);
    DeleteDC(hdc_mem);
    ReleaseDC(target_window_, hdc_window);
    
    return true;
}

bool ScreenCapture::compressImage(const std::vector<uint8_t>& bitmap_data, int width, int height, 
                                 std::vector<uint8_t>& compressed_data, bool force_keyframe) {
    // 转换BGRA到RGB
    temp_buffer_.resize(width * height * 3);
    
    for (int i = 0; i < width * height; i++) {
        temp_buffer_[i * 3 + 0] = bitmap_data[i * 4 + 2]; // R = B
        temp_buffer_[i * 3 + 1] = bitmap_data[i * 4 + 1]; // G = G
        temp_buffer_[i * 3 + 2] = bitmap_data[i * 4 + 0]; // B = R
    }
    
    // 使用stb_image_write进行JPEG压缩
    compressed_data.clear();
    stbi_write_jpg_to_func(
        [](void* context, void* data, int size) {
            std::vector<uint8_t>* output = static_cast<std::vector<uint8_t>*>(context);
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            output->insert(output->end(), bytes, bytes + size);
        },
        &compressed_data,
        width,
        height,
        3, // RGB格式，3通道
        temp_buffer_.data(),
        compression_settings_.quality
    );
    
    if (compressed_data.empty()) {
        std::cerr << "JPEG压缩失败" << std::endl;
        return false;
    }
    
    return true;
}

bool ScreenCapture::calculateFrameDifference(const std::vector<uint8_t>& current_frame, 
                                           const std::vector<uint8_t>& previous_frame,
                                           int width, int height,
                                           CaptureRegion& diff_region) {
    if (current_frame.size() != previous_frame.size() || 
        width != previous_frame_width_ || 
        height != previous_frame_height_) {
        return false;
    }
    
    // 初始化差异区域为无效值
    int min_x = width;
    int min_y = height;
    int max_x = 0;
    int max_y = 0;
    
    // 像素比较阈值
    const int threshold = 10;
    
    // 扫描图像找出差异区域
    bool found_diff = false;
    
    // 每隔几个像素采样以提高性能（网格采样）
    const int sample_step = 4;
    
    for (int y = 0; y < height; y += sample_step) {
        for (int x = 0; x < width; x += sample_step) {
            int pixel_index = (y * width + x) * 4;
            
            // 比较RGB差异
            int diff_b = std::abs(current_frame[pixel_index] - previous_frame[pixel_index]);
            int diff_g = std::abs(current_frame[pixel_index + 1] - previous_frame[pixel_index + 1]);
            int diff_r = std::abs(current_frame[pixel_index + 2] - previous_frame[pixel_index + 2]);
            
            // 如果差异超过阈值
            if (diff_r > threshold || diff_g > threshold || diff_b > threshold) {
                found_diff = true;
                
                // 更新差异区域范围
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
            }
        }
    }
    
    // 如果找到差异
    if (found_diff) {
        // 扩大差异区域，确保包含所有变化
        const int padding = compression_settings_.roi_padding;
        min_x = std::max(0, min_x - padding);
        min_y = std::max(0, min_y - padding);
        max_x = std::min(width - 1, max_x + padding);
        max_y = std::min(height - 1, max_y + padding);
        
        // 设置差异区域
        diff_region.x = min_x;
        diff_region.y = min_y;
        diff_region.width = max_x - min_x + 1;
        diff_region.height = max_y - min_y + 1;
        diff_region.is_active = true;
        
        // 确保区域对齐
        adjustRegionForAlignment(diff_region, 8);
        
        return true;
    } else {
        // 没有找到差异
        diff_region.x = 0;
        diff_region.y = 0;
        diff_region.width = 0;
        diff_region.height = 0;
        diff_region.is_active = false;
        
        return true;
    }
}

bool ScreenCapture::encodeChangedRegion(const std::vector<uint8_t>& bitmap_data, 
                                       int width, int height,
                                       const CaptureRegion& region,
                                       std::vector<uint8_t>& compressed_data) {
    // 验证区域有效性
    if (region.width <= 0 || region.height <= 0 ||
        region.x < 0 || region.y < 0 ||
        region.x + region.width > width ||
        region.y + region.height > height) {
        return false;
    }
    
    // 创建区域图像缓冲区
    std::vector<uint8_t> region_data(region.width * region.height * 3);
    
    // 从原始图像中提取区域数据
    for (int y = 0; y < region.height; y++) {
        for (int x = 0; x < region.width; x++) {
            int src_x = region.x + x;
            int src_y = region.y + y;
            
            int src_idx = (src_y * width + src_x) * 4;
            int dst_idx = (y * region.width + x) * 3;
            
            // BGRA到RGB转换
            region_data[dst_idx + 0] = bitmap_data[src_idx + 2]; // R = B
            region_data[dst_idx + 1] = bitmap_data[src_idx + 1]; // G = G
            region_data[dst_idx + 2] = bitmap_data[src_idx + 0]; // B = R
        }
    }
    
    // 编码为JPEG
    compressed_data.clear();
    
    // 创建元数据头部
    uint8_t header[16];
    memcpy(header, "ROIIMG", 6);
    *(uint16_t*)(header + 6) = region.x;
    *(uint16_t*)(header + 8) = region.y;
    *(uint16_t*)(header + 10) = region.width;
    *(uint16_t*)(header + 12) = region.height;
    *(uint16_t*)(header + 14) = width; // 全图宽度
    
    // 添加头部到压缩数据
    compressed_data.insert(compressed_data.begin(), header, header + 16);
    
    // 使用stb_image_write进行JPEG压缩
    size_t header_size = compressed_data.size();
    stbi_write_jpg_to_func(
        [](void* context, void* data, int size) {
            std::vector<uint8_t>* output = static_cast<std::vector<uint8_t>*>(context);
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            output->insert(output->end(), bytes, bytes + size);
        },
        &compressed_data,
        region.width,
        region.height,
        3, // RGB格式，3通道
        region_data.data(),
        compression_settings_.quality
    );
    
    if (compressed_data.size() <= header_size) {
        std::cerr << "区域JPEG压缩失败" << std::endl;
        return false;
    }
    
    return true;
}

void ScreenCapture::adjustRegionForAlignment(CaptureRegion& region, int alignment) {
    // 确保区域宽度和高度是alignment的倍数
    region.width = (region.width + alignment - 1) / alignment * alignment;
    region.height = (region.height + alignment - 1) / alignment * alignment;
    
    // 确保区域不超出边界
    region.width = std::min(region.width, previous_frame_width_ - region.x);
    region.height = std::min(region.height, previous_frame_height_ - region.y);
}

bool ScreenCapture::shouldSendKeyframe() {
    return (frame_count_ % compression_settings_.keyframe_interval == 0);
}

} // namespace zero_latency