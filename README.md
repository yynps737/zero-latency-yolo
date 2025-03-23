# 零延迟YOLO FPS云辅助系统

![版本](https://img.shields.io/badge/版本-1.0.0-blue)
![架构](https://img.shields.io/badge/架构-双引擎混合-green)
![模型](https://img.shields.io/badge/模型-YOLOv8_优化-orange)
![性能](https://img.shields.io/badge/服务器要求-2核4G-red)

## 项目概述

零延迟YOLO FPS云辅助系统是一个基于高度优化的计算机视觉技术的云辅助解决方案，采用创新的"双引擎混合架构"技术，通过本地预测引擎与云端YOLO检测系统的协同工作，实现接近零感知延迟的游戏体验。本系统具有高精度目标检测、超低感知延迟和资源占用少等特点，特别针对CS 1.6等经典FPS游戏进行了优化。

**主要特点:**
- **双引擎混合架构** - 客户端预测与服务器校正结合，消除网络延迟影响
- **极低感知延迟** - 感知延迟降低至20ms以下，提供近乎本地的游戏体验
- **高精度目标检测** - 基于高度优化的YOLO模型，针对FPS游戏精心调校
- **资源高效性** - 针对2核4G服务器环境优化，保持60FPS稳定检测速率
- **完整解决方案** - 包含服务器部署、客户端应用和Web管理界面

## 系统架构

### 双引擎混合架构

零延迟YOLO FPS云辅助系统采用创新的"双引擎混合架构"，这是一种独特的设计，结合了传统云辅助系统的高精度与本地系统的低延迟优势：

```
┌─────────────────┐              ┌──────────────────┐
│                 │              │                  │
│    客户端       │◀─── UDP ────▶│     服务器       │
│                 │   数据包     │                  │
└─────┬───────────┘              └──────┬───────────┘
      │                                 │
┌─────▼───────────┐              ┌──────▼───────────┐
│  屏幕捕获模块   │              │  YOLO 检测引擎   │
└─────┬───────────┘              └──────┬───────────┘
      │                                 │
┌─────▼───────────┐              ┌──────▼───────────┐
│  本地预测引擎   │              │   游戏适配器     │
└─────┬───────────┘              └──────┬───────────┘
      │                                 │
┌─────▼───────────┐              ┌──────▼───────────┐
│   双引擎协调器  │◀───────────▶│   结果处理器     │
└─────┬───────────┘              └──────────────────┘
      │
┌─────▼───────────┐
│  渲染/辅助模块  │
└─────────────────┘
```

### 关键组件

1. **服务器端组件**
   - **YOLO检测引擎** - 基于ONNX运行时的高度优化YOLO模型，专为FPS游戏检测优化
   - **游戏适配器** - 针对不同游戏提供特定处理逻辑和元数据
   - **网络服务器** - 高性能UDP通信层，支持高吞吐量与低延迟

2. **客户端组件**
   - **屏幕捕获** - 高效低延迟的屏幕图像获取和预处理
   - **本地预测引擎** - 基于卡尔曼滤波器的运动预测系统
   - **双引擎协调器** - 融合服务器检测结果与本地预测
   - **渲染器** - 低延迟的Direct3D覆盖渲染
   - **网络客户端** - 高性能UDP通信与数据压缩

3. **Web管理界面**
   - 系统状态监控和配置
   - 客户端下载和更新
   - 服务器性能监控

## 技术栈

### 服务器端
- **C++17** - 核心语言
- **ONNX Runtime** - 神经网络推理引擎
- **CMake** - 构建系统
- **Boost** - 网络和工具库
- **Node.js** - Web服务器

### 客户端
- **C++17** - 核心语言
- **DirectX 11** - 图形渲染
- **Win32 API** - 系统接口
- **CMake** - 构建系统

### 模型
- **YOLOv8-nano** - 基础检测模型，经过量化和剪枝优化

## 安装与部署

### 系统要求

#### 服务器端
- Linux 系统 (Ubuntu 20.04+ 或 CentOS 7+ 推荐)
- 至少2核CPU (4核以上推荐)
- 至少4GB RAM
- 100Mbps以上带宽
- Ubuntu, CentOS, Debian或其他主流Linux发行版

#### 客户端
- Windows 7/10/11 64位
- 2GB以上内存
- 任何支持DirectX 11的显卡
- 5Mbps以上带宽

### 服务器部署

1. **克隆仓库**
   ```bash
   git clone https://github.com/your-repo/zero-latency-yolo.git
   cd zero-latency-yolo
   ```

2. **安装依赖**
   ```bash
   # 运行依赖安装脚本（需要root权限）
   sudo ./scripts/install_deps.sh
   ```

3. **构建项目**
   ```bash
   ./build.sh
   ```

4. **配置服务器**
   ```bash
   # 编辑服务器配置文件
   nano configs/server.json
   ```

5. **部署服务**
   ```bash
   # 使用部署脚本（需要root权限）
   sudo ./deploy.sh
   ```

6. **检查服务状态**
   ```bash
   systemctl status zero-latency-yolo
   systemctl status zero-latency-web
   ```

### 客户端安装

1. **通过Web界面下载客户端**
   - 访问 `http://[服务器IP]:3000`
   - 下载客户端安装包

2. **解压安装包**
   - 将ZIP包解压到任意目录

3. **配置客户端**
   - 编辑 `configs/client.json`
   - 设置服务器IP和端口

4. **运行客户端**
   - 运行 `yolo_client.exe`

## 配置指南

### 服务器配置

服务器配置文件位于 `configs/server.json`，主要配置项如下：

```json
{
  "model_path": "models/yolo_nano_cs16.onnx",
  "port": 7788,
  "web_port": 3000,
  "max_clients": 10,
  "target_fps": 60,
  "confidence_threshold": 0.5,
  "nms_threshold": 0.45,
  "max_queue_size": 8,
  "use_cpu_affinity": true,
  "cpu_core_id": 0,
  "use_high_priority": true,
  
  "logging": {
    "enable_logging": true,
    "log_level": "info",
    "log_file": "logs/server.log",
    "max_log_size_mb": 10,
    "max_log_files": 5
  },
  
  "network": {
    "recv_buffer_size": 1048576,
    "send_buffer_size": 1048576,
    "timeout_ms": 5000,
    "heartbeat_interval_ms": 1000
  },
  
  "detection": {
    "model_width": 416,
    "model_height": 416,
    "enable_tracking": true,
    "max_tracking_age_ms": 500,
    "class_weights": {
      "player_t": 1.0,
      "player_ct": 1.0,
      "head": 1.2,
      "weapon": 0.8
    }
  }
}
```

**重要配置项说明：**

- `model_path` - YOLO模型路径
- `port` - 服务器监听端口
- `target_fps` - 目标推理帧率
- `confidence_threshold` - 检测置信度阈值
- `use_cpu_affinity` - 启用CPU亲和性（提高性能）
- `cpu_core_id` - 分配的CPU核心ID

### 客户端配置

客户端配置文件位于 `configs/client.json`，主要配置项如下：

```json
{
  "server_ip": "127.0.0.1",
  "server_port": 7788,
  "game_id": 1,
  "target_fps": 60,
  "screen_width": 800,
  "screen_height": 600,
  "auto_connect": true,
  "auto_start": false,
  "enable_aim_assist": true,
  "enable_esp": true,
  "enable_recoil_control": true,
  "use_high_priority": true,
  
  "hotkeys": {
    "toggle_esp": "F2",
    "toggle_aim": "F3",
    "toggle_recoil": "F4",
    "toggle_all": "F6"
  },
  
  "compression": {
    "quality": 75,
    "keyframe_interval": 30,
    "use_difference_encoding": true,
    "use_roi_encoding": true,
    "roi_padding": 20
  },
  
  "prediction": {
    "max_prediction_time": 200.0,
    "position_uncertainty": 0.1,
    "velocity_uncertainty": 0.2,
    "acceleration_uncertainty": 0.3,
    "min_confidence_threshold": 0.5
  }
}
```

**重要配置项说明：**

- `server_ip` 和 `server_port` - 服务器连接信息
- `game_id` - 游戏类型（1=CS 1.6）
- `enable_aim_assist` - 启用瞄准辅助
- `enable_esp` - 启用ESP功能
- `enable_recoil_control` - 启用后座力控制
- `hotkeys` - 热键配置
- `prediction` - 预测引擎参数（影响客户端预测准确性）

## 使用说明

### 客户端操作

1. **启动客户端**
   - 启动 `yolo_client.exe` 
   - 客户端将自动连接到配置文件中指定的服务器

2. **启动游戏**
   - 启动CS 1.6或其他支持的游戏
   - 客户端会自动检测并捕获游戏窗口

3. **功能热键**
   - `F2` - 切换ESP显示
   - `F3` - 切换瞄准辅助
   - `F4` - 切换后座力控制
   - `F6` - 切换所有功能

4. **瞄准辅助使用**
   - 默认情况下，使用`左Shift`激活瞄准辅助
   - 瞄准辅助会自动选择最近的目标

5. **关闭客户端**
   - 按 `Ctrl+C` 关闭或直接关闭窗口

### 服务器管理

1. **服务操作**
   ```bash
   # 重启服务
   systemctl restart zero-latency-yolo
   
   # 停止服务
   systemctl stop zero-latency-yolo
   
   # 查看服务日志
   journalctl -u zero-latency-yolo -f
   ```

2. **Web管理界面**
   - 访问 `http://[服务器IP]:3000`
   - 监控服务器状态和性能
   - 下载客户端更新

## 故障排除

### 常见问题

#### 服务器问题

1. **服务无法启动**
   - 检查`logs/server.log`文件
   - 确认ONNX Runtime路径正确
   - 确认服务器配置文件语法正确
   - 确认端口未被占用：`netstat -tuln | grep 7788`

2. **检测性能低下**
   - 检查CPU使用率：`top`
   - 调整`confidence_threshold`和`nms_threshold`
   - 减少`max_clients`数量
   - 启用CPU亲和性并调整`cpu_core_id`

3. **内存泄漏**
   - 检查内存使用：`free -m`
   - 重启服务：`systemctl restart zero-latency-yolo`
   - 检查`journalctl -u zero-latency-yolo`中的崩溃日志

#### 客户端问题

1. **连接失败**
   - 确认服务器IP和端口正确
   - 检查防火墙设置
   - 确认服务器正在运行
   - 尝试ping服务器

2. **游戏窗口未捕获**
   - 确认游戏以窗口模式运行
   - 尝试使用管理员权限运行客户端
   - 检查`configs/client.json`中的游戏ID

3. **性能问题**
   - 调整压缩设置
   - 减小捕获分辨率
   - 降低目标FPS
   - 关闭其他占用资源的应用

## 性能优化指南

### 服务器优化

1. **系统级优化**
   ```bash
   # 运行服务器优化脚本
   sudo ./scripts/optimize_server.sh
   ```

2. **YOLO模型优化**
   - 使用INT8量化模型减少内存使用
   - 减小模型输入尺寸（如降至320x320）
   - 启用CPU亲和性并分配独立核心

3. **网络优化**
   - 增加接收缓冲区大小
   - 减少心跳包频率
   - 优化UDP参数

### 客户端优化

1. **捕获优化**
   - 降低捕获分辨率
   - 启用区域编码
   - 增加关键帧间隔

2. **预测引擎优化**
   - 调整不确定性参数
   - 减少最大预测时间

## 扩展开发

### 添加新游戏支持

1. **修改游戏适配器**
   - 在`src/server/game_adapter.cpp`中添加新游戏处理逻辑
   - 添加适当的枚举值到`GameType`

2. **训练专用模型**
   - 收集并标注游戏截图
   - 训练自定义YOLO模型
   - 使用`scripts/convert_model.py`转换为ONNX格式

3. **更新客户端配置**
   - 在`config.h`中添加新游戏类型常量
   - 在客户端代码中添加特定游戏处理

### 自定义功能开发

1. **修改检测逻辑**
   - 编辑`src/server/yolo_engine.cpp`
   - 调整后处理步骤和NMS算法

2. **自定义ESP渲染**
   - 修改`src/client/renderer.cpp`
   - 添加新的渲染函数和视觉效果

3. **扩展预测引擎**
   - 编辑`src/client/prediction_engine.cpp`
   - 改进预测算法和运动模型

## 安全考虑

本系统包含以下安全考虑：

1. **配置安全**
   - 配置文件使用合理默认值
   - 端口和绑定地址可自定义
   - 日志敏感信息脱敏

2. **网络安全**
   - 支持心跳和会话超时
   - 实现简单的握手协议
   - UDP帧序列化和验证

3. **客户端安全**
   - 最小化系统权限需求
   - 本地配置文件存储
   - 防止进程注入

## 法律和免责声明

零延迟YOLO FPS云辅助系统是一个仅用于教育和研究目的的软件项目。此系统的设计旨在展示计算机视觉、网络通信和实时预测算法的协同工作原理。

### 免责声明

1. **教育目的**: 本系统仅供学习和研究用途，未经授权不得用于任何商业环境。

2. **使用限制**: 用户应遵守所有适用的法律法规，并遵守游戏服务条款。不得将本系统用于违反游戏规则、服务条款或任何适用法律的目的。

3. **无保证**: 本软件按"原样"提供，不提供任何明示或暗示的保证，包括但不限于对特定用途的适用性、无侵权或不中断运行的保证。

4. **责任限制**: 在任何情况下，本软件的作者或版权持有人均不对因使用本软件而导致的任何损害负责。

### 许可协议

本项目采用MIT许可证。请参阅LICENSE文件了解详情。

## 贡献指南

欢迎对本项目提出改进建议和贡献代码。请遵循以下步骤：

1. Fork项目仓库
2. 创建功能分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m 'Add amazing feature'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 创建Pull Request

## 联系方式

如有任何问题或建议，请通过以下方式联系我们：

- 项目议题: [GitHub Issues](https://github.com/your-repo/zero-latency-yolo/issues)
- 电子邮件: your-email@example.com

## 致谢

- ONNX Runtime团队提供高性能推理引擎
- YOLOv8开发团队提供先进的目标检测模型
- 所有为本项目做出贡献的开发者

---

*零延迟YOLO FPS云辅助系统 - 版本1.0.0*
*最后更新: 2023年12月*