#!/bin/bash
# 零延迟YOLO FPS云辅助系统Web服务启动脚本

# 获取脚本所在目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

# 创建日志目录
mkdir -p logs

# 启动Web服务
echo "启动零延迟YOLO FPS云辅助系统Web服务..."
cd src/web
PORT=3000 node server.js > ../../logs/web_server.log 2>&1
