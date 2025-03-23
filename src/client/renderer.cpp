#include "renderer.h"
#include <iostream>
#include <cmath>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <dwrite.h>
#include <d2d1.h>
#include <wrl/client.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d2d1.lib")

namespace zero_latency {

// 顶点结构
struct Vertex {
    XMFLOAT3 position;
    XMFLOAT4 color;
};

// 常量缓冲区结构
struct ConstantBuffer {
    XMMATRIX vp;
};

// 着色器代码 - 顶点着色器
static const char* vertexShaderCode = R"(
cbuffer ConstantBuffer : register(b0)
{
    matrix VP;
};

struct VS_INPUT
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    output.position = mul(float4(input.position, 1.0f), VP);
    output.color = input.color;
    return output;
})";

// 着色器代码 - 像素着色器
static const char* pixelShaderCode = R"(
struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

float4 main(PS_INPUT input) : SV_TARGET
{
    return input.color;
})";

Renderer::Renderer()
    : d3d_device_(nullptr),
      d3d_context_(nullptr),
      swap_chain_(nullptr),
      render_target_view_(nullptr),
      vertex_shader_(nullptr),
      pixel_shader_(nullptr),
      input_layout_(nullptr),
      constant_buffer_(nullptr),
      blend_state_(nullptr),
      dwrite_factory_(nullptr),
      d2d_factory_(nullptr),
      d2d_render_target_(nullptr),
      d2d_brush_(nullptr),
      text_format_(nullptr),
      target_window_(nullptr),
      window_width_(0),
      window_height_(0),
      is_initialized_(false),
      is_frame_started_(false) {
}

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::initialize() {
    if (is_initialized_) {
        return true;
    }
    
    // 初始化DirectX资源
    if (!initializeDirectX()) {
        std::cerr << "初始化DirectX资源失败" << std::endl;
        return false;
    }
    
    // 创建顶点着色器
    if (!createVertexShader()) {
        std::cerr << "创建顶点着色器失败" << std::endl;
        releaseDirectX();
        return false;
    }
    
    // 创建像素着色器
    if (!createPixelShader()) {
        std::cerr << "创建像素着色器失败" << std::endl;
        releaseDirectX();
        return false;
    }
    
    // 创建输入布局
    if (!createInputLayout()) {
        std::cerr << "创建输入布局失败" << std::endl;
        releaseDirectX();
        return false;
    }
    
    // 创建常量缓冲区
    if (!createConstantBuffer()) {
        std::cerr << "创建常量缓冲区失败" << std::endl;
        releaseDirectX();
        return false;
    }
    
    // 创建混合状态
    if (!createBlendState()) {
        std::cerr << "创建混合状态失败" << std::endl;
        releaseDirectX();
        return false;
    }
    
    // 创建字体资源
    if (!createFontResources()) {
        std::cerr << "创建字体资源失败" << std::endl;
        releaseDirectX();
        return false;
    }
    
    is_initialized_ = true;
    return true;
}

void Renderer::shutdown() {
    if (!is_initialized_) {
        return;
    }
    
    // 确保没有正在进行的帧
    if (is_frame_started_) {
        endFrame();
    }
    
    // 释放字体资源
    if (text_format_) {
        text_format_->Release();
        text_format_ = nullptr;
    }
    
    if (d2d_brush_) {
        d2d_brush_->Release();
        d2d_brush_ = nullptr;
    }
    
    if (d2d_render_target_) {
        d2d_render_target_->Release();
        d2d_render_target_ = nullptr;
    }
    
    if (d2d_factory_) {
        d2d_factory_->Release();
        d2d_factory_ = nullptr;
    }
    
    if (dwrite_factory_) {
        dwrite_factory_->Release();
        dwrite_factory_ = nullptr;
    }
    
    // 释放DirectX资源
    releaseDirectX();
    
    is_initialized_ = false;
}

bool Renderer::beginFrame(HWND target_window) {
    if (!is_initialized_) {
        return false;
    }
    
    // 确保没有正在进行的帧
    if (is_frame_started_) {
        endFrame();
    }
    
    target_window_ = target_window;
    
    // 获取窗口尺寸
    RECT client_rect;
    GetClientRect(target_window, &client_rect);
    window_width_ = client_rect.right - client_rect.left;
    window_height_ = client_rect.bottom - client_rect.top;
    
    // 如果窗口尺寸无效，则返回失败
    if (window_width_ <= 0 || window_height_ <= 0) {
        return false;
    }
    
    // 创建渲染目标
    if (!createRenderTarget(target_window)) {
        return false;
    }
    
    // 更新视图和投影矩阵
    updateViewProjection(window_width_, window_height_);
    
    // 设置视口
    D3D11_VIEWPORT viewport;
    viewport.Width = static_cast<float>(window_width_);
    viewport.Height = static_cast<float>(window_height_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    d3d_context_->RSSetViewports(1, &viewport);
    
    // 设置渲染目标
    d3d_context_->OMSetRenderTargets(1, &render_target_view_, nullptr);
    
    // 设置混合状态
    float blend_factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    d3d_context_->OMSetBlendState(blend_state_, blend_factor, 0xFFFFFFFF);
    
    // 设置着色器和布局
    d3d_context_->VSSetShader(vertex_shader_, nullptr, 0);
    d3d_context_->PSSetShader(pixel_shader_, nullptr, 0);
    d3d_context_->IASetInputLayout(input_layout_);
    d3d_context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    
    // 设置常量缓冲区
    d3d_context_->VSSetConstantBuffers(0, 1, &constant_buffer_);
    
    // 清空渲染队列
    lines_.clear();
    rectangles_.clear();
    texts_.clear();
    
    is_frame_started_ = true;
    return true;
}

void Renderer::endFrame() {
    if (!is_frame_started_) {
        return;
    }
    
    // 渲染所有几何体
    renderLines();
    renderRectangles();
    renderText();
    
    // 呈现帧
    swap_chain_->Present(1, 0);
    
    // 释放资源
    if (render_target_view_) {
        render_target_view_->Release();
        render_target_view_ = nullptr;
    }
    
    if (d2d_render_target_) {
        d2d_render_target_->Release();
        d2d_render_target_ = nullptr;
    }
    
    is_frame_started_ = false;
}

void Renderer::drawLine(float x1, float y1, float x2, float y2, DWORD color, float thickness) {
    if (!is_frame_started_) {
        return;
    }
    
    LineSegment line;
    line.x1 = x1;
    line.y1 = y1;
    line.x2 = x2;
    line.y2 = y2;
    line.color = color;
    line.thickness = thickness;
    lines_.push_back(line);
}

void Renderer::drawRect(float x, float y, float width, float height, DWORD color, float thickness) {
    if (!is_frame_started_) {
        return;
    }
    
    Rectangle rect;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    rect.color = color;
    rect.thickness = thickness;
    rect.filled = false;
    rectangles_.push_back(rect);
}

void Renderer::drawFilledRect(float x, float y, float width, float height, DWORD color) {
    if (!is_frame_started_) {
        return;
    }
    
    Rectangle rect;
    rect.x = x;
    rect.y = y;
    rect.width = width;
    rect.height = height;
    rect.color = color;
    rect.thickness = 1.0f;
    rect.filled = true;
    rectangles_.push_back(rect);
}

void Renderer::drawBox(float x, float y, float width, float height, DWORD color, float thickness) {
    drawRect(x, y, width, height, color, thickness);
}

void Renderer::drawText(float x, float y, const std::string& text, DWORD color, float size, bool centered) {
    if (!is_frame_started_) {
        return;
    }
    
    TextElement text_element;
    text_element.x = x;
    text_element.y = y;
    text_element.text = text;
    text_element.color = color;
    text_element.size = size;
    text_element.centered = centered;
    texts_.push_back(text_element);
}

void Renderer::drawCircle(float x, float y, float radius, DWORD color, float thickness) {
    if (!is_frame_started_) {
        return;
    }
    
    const int segments = 24;
    const float step = XM_2PI / segments;
    
    for (int i = 0; i < segments; i++) {
        float angle1 = i * step;
        float angle2 = (i + 1) * step;
        
        float x1 = x + radius * std::cos(angle1);
        float y1 = y + radius * std::sin(angle1);
        float x2 = x + radius * std::cos(angle2);
        float y2 = y + radius * std::sin(angle2);
        
        drawLine(x1, y1, x2, y2, color, thickness);
    }
}

void Renderer::drawFilledCircle(float x, float y, float radius, DWORD color) {
    if (!is_frame_started_) {
        return;
    }
    
    // 绘制多个同心圆来填充
    for (float r = 0.5f; r <= radius; r += 1.0f) {
        drawCircle(x, y, r, color, 1.0f);
    }
}

void Renderer::getWindowSize(int& width, int& height) const {
    width = window_width_;
    height = window_height_;
}

bool Renderer::initializeDirectX() {
    // 创建D3D11设备和上下文
    D3D_FEATURE_LEVEL feature_level;
    UINT flags = 0;
    
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    
    HRESULT hr = D3D11CreateDevice(
        nullptr, // 默认适配器
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &d3d_device_,
        &feature_level,
        &d3d_context_
    );
    
    if (FAILED(hr)) {
        std::cerr << "创建D3D11设备失败: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }
    
    return true;
}

void Renderer::releaseDirectX() {
    // 释放渲染状态
    if (blend_state_) {
        blend_state_->Release();
        blend_state_ = nullptr;
    }
    
    // 释放缓冲区
    if (constant_buffer_) {
        constant_buffer_->Release();
        constant_buffer_ = nullptr;
    }
    
    // 释放输入布局
    if (input_layout_) {
        input_layout_->Release();
        input_layout_ = nullptr;
    }
    
    // 释放着色器
    if (pixel_shader_) {
        pixel_shader_->Release();
        pixel_shader_ = nullptr;
    }
    
    if (vertex_shader_) {
        vertex_shader_->Release();
        vertex_shader_ = nullptr;
    }
    
    // 释放交换链和渲染目标
    if (render_target_view_) {
        render_target_view_->Release();
        render_target_view_ = nullptr;
    }
    
    if (swap_chain_) {
        swap_chain_->Release();
        swap_chain_ = nullptr;
    }
    
    // 释放设备和上下文
    if (d3d_context_) {
        d3d_context_->Release();
        d3d_context_ = nullptr;
    }
    
    if (d3d_device_) {
        d3d_device_->Release();
        d3d_device_ = nullptr;
    }
}

bool Renderer::createVertexShader() {
    ID3DBlob* shader_blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    
    // 编译顶点着色器
    HRESULT hr = D3DCompile(
        vertexShaderCode,
        strlen(vertexShaderCode),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_4_0",
        0,
        0,
        &shader_blob,
        &error_blob
    );
    
    if (FAILED(hr)) {
        if (error_blob) {
            std::cerr << "编译顶点着色器失败: " << static_cast<char*>(error_blob->GetBufferPointer()) << std::endl;
            error_blob->Release();
        }
        return false;
    }
    
    // 创建顶点着色器
    hr = d3d_device_->CreateVertexShader(
        shader_blob->GetBufferPointer(),
        shader_blob->GetBufferSize(),
        nullptr,
        &vertex_shader_
    );
    
    if (FAILED(hr)) {
        std::cerr << "创建顶点着色器失败: 0x" << std::hex << hr << std::dec << std::endl;
        shader_blob->Release();
        return false;
    }
    
    // 保留blob用于创建输入布局
    // 在createInputLayout中会释放
    return true;
}

bool Renderer::createPixelShader() {
    ID3DBlob* shader_blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    
    // 编译像素着色器
    HRESULT hr = D3DCompile(
        pixelShaderCode,
        strlen(pixelShaderCode),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "ps_4_0",
        0,
        0,
        &shader_blob,
        &error_blob
    );
    
    if (FAILED(hr)) {
        if (error_blob) {
            std::cerr << "编译像素着色器失败: " << static_cast<char*>(error_blob->GetBufferPointer()) << std::endl;
            error_blob->Release();
        }
        return false;
    }
    
    // 创建像素着色器
    hr = d3d_device_->CreatePixelShader(
        shader_blob->GetBufferPointer(),
        shader_blob->GetBufferSize(),
        nullptr,
        &pixel_shader_
    );
    
    if (FAILED(hr)) {
        std::cerr << "创建像素着色器失败: 0x" << std::hex << hr << std::dec << std::endl;
        shader_blob->Release();
        return false;
    }
    
    shader_blob->Release();
    return true;
}

bool Renderer::createInputLayout() {
    // 重新编译顶点着色器获取Blob
    ID3DBlob* shader_blob = nullptr;
    ID3DBlob* error_blob = nullptr;
    
    HRESULT hr = D3DCompile(
        vertexShaderCode,
        strlen(vertexShaderCode),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_4_0",
        0,
        0,
        &shader_blob,
        &error_blob
    );
    
    if (FAILED(hr)) {
        if (error_blob) {
            error_blob->Release();
        }
        return false;
    }
    
    // 定义输入布局
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    
    // 创建输入布局
    hr = d3d_device_->CreateInputLayout(
        layout,
        ARRAYSIZE(layout),
        shader_blob->GetBufferPointer(),
        shader_blob->GetBufferSize(),
        &input_layout_
    );
    
    shader_blob->Release();
    
    if (FAILED(hr)) {
        std::cerr << "创建输入布局失败: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }
    
    return true;
}

bool Renderer::createConstantBuffer() {
    // 创建常量缓冲区
    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.ByteWidth = sizeof(ConstantBuffer);
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    HRESULT hr = d3d_device_->CreateBuffer(&buffer_desc, nullptr, &constant_buffer_);
    if (FAILED(hr)) {
        std::cerr << "创建常量缓冲区失败: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }
    
    return true;
}

bool Renderer::createBlendState() {
    // 创建混合状态
    D3D11_BLEND_DESC blend_desc = {};
    blend_desc.AlphaToCoverageEnable = FALSE;
    blend_desc.IndependentBlendEnable = FALSE;
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    
    HRESULT hr = d3d_device_->CreateBlendState(&blend_desc, &blend_state_);
    if (FAILED(hr)) {
        std::cerr << "创建混合状态失败: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }
    
    return true;
}

bool Renderer::createFontResources() {
    // 创建DirectWrite工厂
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&dwrite_factory_)
    );
    
    if (FAILED(hr)) {
        std::cerr << "创建DirectWrite工厂失败: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }
    
    // 创建Direct2D工厂
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_);
    if (FAILED(hr)) {
        std::cerr << "创建Direct2D工厂失败: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }
    
    // 创建文本格式
    hr = dwrite_factory_->CreateTextFormat(
        L"Consolas", // 字体名称
        NULL,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        14.0f, // 字体大小
        L"en-us",
        &text_format_
    );
    
    if (FAILED(hr)) {
        std::cerr << "创建文本格式失败: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }
    
    // 设置文本对齐方式
    text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    
    return true;
}

bool Renderer::createRenderTarget(HWND window) {
    // 获取窗口DC
    HDC window_dc = GetDC(window);
    if (!window_dc) {
        std::cerr << "获取窗口DC失败: " << GetLastError() << std::endl;
        return false;
    }
    
    // 获取DXGI设备和适配器
    IDXGIDevice* dxgi_device = nullptr;
    HRESULT hr = d3d_device_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device));
    if (FAILED(hr)) {
        std::cerr << "获取DXGI设备失败: 0x" << std::hex << hr << std::dec << std::endl;
        ReleaseDC(window, window_dc);
        return false;
    }
    
    IDXGIAdapter* dxgi_adapter = nullptr;
    hr = dxgi_device->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&dxgi_adapter));
    dxgi_device->Release();
    
    if (FAILED(hr)) {
        std::cerr << "获取DXGI适配器失败: 0x" << std::hex << hr << std::dec << std::endl;
        ReleaseDC(window, window_dc);
        return false;
    }
    
    IDXGIFactory2* dxgi_factory = nullptr;
    hr = dxgi_adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgi_factory));
    dxgi_adapter->Release();
    
    if (FAILED(hr)) {
        std::cerr << "获取DXGI工厂失败: 0x" << std::hex << hr << std::dec << std::endl;
        ReleaseDC(window, window_dc);
        return false;
    }
    
    // 创建交换链
    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
    swap_chain_desc.Width = window_width_;
    swap_chain_desc.Height = window_height_;
    swap_chain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_chain_desc.Stereo = FALSE;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.SampleDesc.Quality = 0;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.Scaling = DXGI_SCALING_NONE;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    swap_chain_desc.Flags = 0;
    
    hr = dxgi_factory->CreateSwapChainForHwnd(
        d3d_device_,
        window,
        &swap_chain_desc,
        nullptr,
        nullptr,
        &swap_chain_
    );
    
    dxgi_factory->Release();
    ReleaseDC(window, window_dc);
    
    if (FAILED(hr)) {
        std::cerr << "创建交换链失败: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }
    
    // 获取后缓冲区
    ID3D11Texture2D* back_buffer = nullptr;
    hr = swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back_buffer));
    if (FAILED(hr)) {
        std::cerr << "获取后缓冲区失败: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }
    
    // 创建渲染目标视图
    hr = d3d_device_->CreateRenderTargetView(back_buffer, nullptr, &render_target_view_);
    back_buffer->Release();
    
    if (FAILED(hr)) {
        std::cerr << "创建渲染目标视图失败: 0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }
    
    // 创建Direct2D渲染目标
    IDXGISurface* dxgi_surface = nullptr;
    hr = swap_chain_->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(&dxgi_surface));
    if (SUCCEEDED(hr)) {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );
        
        hr = d2d_factory_->CreateDxgiSurfaceRenderTarget(dxgi_surface, &props, &d2d_render_target_);
        dxgi_surface->Release();
        
        if (SUCCEEDED(hr)) {
            // 创建画刷
            hr = d2d_render_target_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &d2d_brush_);
            if (FAILED(hr)) {
                std::cerr << "创建Direct2D画刷失败: 0x" << std::hex << hr << std::dec << std::endl;
            }
        } else {
            std::cerr << "创建Direct2D渲染目标失败: 0x" << std::hex << hr << std::dec << std::endl;
        }
    } else {
        std::cerr << "获取DXGI表面失败: 0x" << std::hex << hr << std::dec << std::endl;
    }
    
    return true;
}

void Renderer::updateViewProjection(int width, int height) {
    // 创建正交投影矩阵
    XMMATRIX projection = XMMatrixOrthographicOffCenterLH(
        0.0f, static_cast<float>(width),
        static_cast<float>(height), 0.0f,
        0.1f, 100.0f
    );
    
    // 创建视图矩阵
    XMMATRIX view = XMMatrixLookAtLH(
        XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f),
        XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f),
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)
    );
    
    // 更新常量缓冲区
    D3D11_MAPPED_SUBRESOURCE mapped_resource;
    HRESULT hr = d3d_context_->Map(constant_buffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
    if (SUCCEEDED(hr)) {
        ConstantBuffer* buffer = reinterpret_cast<ConstantBuffer*>(mapped_resource.pData);
        buffer->vp = XMMatrixTranspose(view * projection);
        d3d_context_->Unmap(constant_buffer_, 0);
    }
}

void Renderer::renderLines() {
    if (lines_.empty()) {
        return;
    }
    
    // 准备顶点数据
    std::vector<Vertex> vertices;
    vertices.reserve(lines_.size() * 2);
    
    for (const auto& line : lines_) {
        // 转换颜色从ARGB到RGBA
        XMFLOAT4 color;
        color.x = static_cast<float>((line.color >> 16) & 0xFF) / 255.0f; // R
        color.y = static_cast<float>((line.color >> 8) & 0xFF) / 255.0f;  // G
        color.z = static_cast<float>(line.color & 0xFF) / 255.0f;         // B
        color.w = static_cast<float>((line.color >> 24) & 0xFF) / 255.0f; // A
        
        // 添加线段顶点
        vertices.push_back({ XMFLOAT3(line.x1, line.y1, 0.0f), color });
        vertices.push_back({ XMFLOAT3(line.x2, line.y2, 0.0f), color });
    }
    
    // 创建顶点缓冲区
    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * vertices.size());
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    
    D3D11_SUBRESOURCE_DATA init_data = {};
    init_data.pSysMem = vertices.data();
    
    ID3D11Buffer* vertex_buffer = nullptr;
    HRESULT hr = d3d_device_->CreateBuffer(&buffer_desc, &init_data, &vertex_buffer);
    if (FAILED(hr)) {
        std::cerr << "创建顶点缓冲区失败: 0x" << std::hex << hr << std::dec << std::endl;
        return;
    }
    
    // 设置顶点缓冲区
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    d3d_context_->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
    
    // 绘制线段
    d3d_context_->Draw(static_cast<UINT>(vertices.size()), 0);
    
    // 释放顶点缓冲区
    vertex_buffer->Release();
}

void Renderer::renderRectangles() {
    if (rectangles_.empty()) {
        return;
    }
    
    // 处理填充矩形和轮廓矩形
    for (const auto& rect : rectangles_) {
        if (rect.filled) {
            // 绘制填充矩形(两个三角形)
            drawLine(rect.x, rect.y, rect.x + rect.width, rect.y, rect.color);
            drawLine(rect.x + rect.width, rect.y, rect.x + rect.width, rect.y + rect.height, rect.color);
            drawLine(rect.x + rect.width, rect.y + rect.height, rect.x, rect.y + rect.height, rect.color);
            drawLine(rect.x, rect.y + rect.height, rect.x, rect.y, rect.color);
            
            // 填充内部
            for (float y = rect.y + 1; y < rect.y + rect.height; y += 1.0f) {
                drawLine(rect.x, y, rect.x + rect.width, y, rect.color);
            }
        } else {
            // 绘制矩形轮廓
            drawLine(rect.x, rect.y, rect.x + rect.width, rect.y, rect.color, rect.thickness);
            drawLine(rect.x + rect.width, rect.y, rect.x + rect.width, rect.y + rect.height, rect.color, rect.thickness);
            drawLine(rect.x + rect.width, rect.y + rect.height, rect.x, rect.y + rect.height, rect.color, rect.thickness);
            drawLine(rect.x, rect.y + rect.height, rect.x, rect.y, rect.color, rect.thickness);
        }
    }
}

void Renderer::renderText() {
    if (texts_.empty() || !d2d_render_target_ || !d2d_brush_) {
        return;
    }
    
    // 开始Direct2D绘制
    d2d_render_target_->BeginDraw();
    
    for (const auto& text_element : texts_) {
        // 设置文本对齐方式
        if (text_element.centered) {
            text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        } else {
            text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }
        
        // 转换颜色
        D2D1_COLOR_F color = D2D1::ColorF(
            static_cast<float>((text_element.color >> 16) & 0xFF) / 255.0f, // R
            static_cast<float>((text_element.color >> 8) & 0xFF) / 255.0f,  // G
            static_cast<float>(text_element.color & 0xFF) / 255.0f,         // B
            static_cast<float>((text_element.color >> 24) & 0xFF) / 255.0f  // A
        );
        
        // 设置画刷颜色
        d2d_brush_->SetColor(color);
        
        // 创建文本布局
        IDWriteTextLayout* text_layout = nullptr;
        std::wstring wide_text(text_element.text.begin(), text_element.text.end());
        
        HRESULT hr = dwrite_factory_->CreateTextLayout(
            wide_text.c_str(),
            static_cast<UINT32>(wide_text.length()),
            text_format_,
            1000.0f, // 最大宽度
            100.0f,  // 最大高度
            &text_layout
        );
        
        if (SUCCEEDED(hr)) {
            // 设置字体大小
            DWRITE_TEXT_RANGE range = { 0, static_cast<UINT32>(wide_text.length()) };
            text_layout->SetFontSize(text_element.size, range);
            
            // 绘制文本
            d2d_render_target_->DrawTextLayout(
                D2D1::Point2F(text_element.x, text_element.y),
                text_layout,
                d2d_brush_,
                D2D1_DRAW_TEXT_OPTIONS_NONE
            );
            
            text_layout->Release();
        }
    }
    
    // 结束Direct2D绘制
    d2d_render_target_->EndDraw();
}

} // namespace zero_latency