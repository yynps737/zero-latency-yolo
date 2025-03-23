#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <string>
#include <vector>
#include <memory>

namespace zero_latency {

// 线段定义
struct LineSegment {
    float x1, y1;
    float x2, y2;
    DWORD color;
    float thickness;
};

// 矩形定义
struct Rectangle {
    float x, y;
    float width, height;
    DWORD color;
    float thickness;
    bool filled;
};

// 文本定义
struct TextElement {
    float x, y;
    std::string text;
    DWORD color;
    float size;
    bool centered;
};

// 渲染系统类
class Renderer {
public:
    Renderer();
    ~Renderer();
    
    // 初始化渲染系统
    bool initialize();
    
    // 释放资源
    void shutdown();
    
    // 开始新帧绘制
    bool beginFrame(HWND target_window);
    
    // 结束帧绘制
    void endFrame();
    
    // 绘制线段
    void drawLine(float x1, float y1, float x2, float y2, DWORD color, float thickness = 1.0f);
    
    // 绘制矩形
    void drawRect(float x, float y, float width, float height, DWORD color, float thickness = 1.0f);
    
    // 绘制填充矩形
    void drawFilledRect(float x, float y, float width, float height, DWORD color);
    
    // 绘制边界框
    void drawBox(float x, float y, float width, float height, DWORD color, float thickness = 1.0f);
    
    // 绘制文本
    void drawText(float x, float y, const std::string& text, DWORD color, float size = 14.0f, bool centered = false);
    
    // 绘制圆形
    void drawCircle(float x, float y, float radius, DWORD color, float thickness = 1.0f);
    
    // 绘制填充圆形
    void drawFilledCircle(float x, float y, float radius, DWORD color);
    
    // 获取窗口尺寸
    void getWindowSize(int& width, int& height) const;
    
private:
    // 初始化DirectX资源
    bool initializeDirectX();
    
    // 释放DirectX资源
    void releaseDirectX();
    
    // 创建顶点着色器
    bool createVertexShader();
    
    // 创建像素着色器
    bool createPixelShader();
    
    // 创建输入布局
    bool createInputLayout();
    
    // 创建常量缓冲区
    bool createConstantBuffer();
    
    // 创建混合状态
    bool createBlendState();
    
    // 创建字体资源
    bool createFontResources();
    
    // 在窗口上创建渲染目标
    bool createRenderTarget(HWND window);
    
    // 更新VP矩阵
    void updateViewProjection(int width, int height);
    
    // 绘制线段几何体
    void renderLines();
    
    // 绘制矩形几何体
    void renderRectangles();
    
    // 绘制文本元素
    void renderText();
    
private:
    // DirectX资源
    ID3D11Device* d3d_device_;
    ID3D11DeviceContext* d3d_context_;
    IDXGISwapChain1* swap_chain_;
    ID3D11RenderTargetView* render_target_view_;
    
    // 着色器资源
    ID3D11VertexShader* vertex_shader_;
    ID3D11PixelShader* pixel_shader_;
    ID3D11InputLayout* input_layout_;
    ID3D11Buffer* constant_buffer_;
    ID3D11BlendState* blend_state_;
    
    // 文本渲染资源
    IDWriteFactory* dwrite_factory_;
    ID2D1Factory* d2d_factory_;
    ID2D1RenderTarget* d2d_render_target_;
    ID2D1SolidColorBrush* d2d_brush_;
    IDWriteTextFormat* text_format_;
    
    // 当前窗口和尺寸
    HWND target_window_;
    int window_width_;
    int window_height_;
    
    // 渲染队列
    std::vector<LineSegment> lines_;
    std::vector<Rectangle> rectangles_;
    std::vector<TextElement> texts_;
    
    // 状态标志
    bool is_initialized_;
    bool is_frame_started_;
};

} // namespace zero_latency