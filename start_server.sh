#!/bin/bash
# 零延迟YOLO FPS云辅助系统服务器启动脚本

# 设置环境变量
export ONNXRUNTIME_ROOT_DIR="/workspaces/zero-latency-yolo/third_party/onnxruntime"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/workspaces/zero-latency-yolo/third_party/onnxruntime/lib"

# 获取脚本所在目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

# 创建日志目录
mkdir -p logs

# 启动服务器
echo "启动零延迟YOLO FPS云辅助系统服务器..."
./bin/yolo_server "$@"
