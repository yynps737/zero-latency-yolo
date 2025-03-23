#!/bin/bash
# 零延迟YOLO FPS云辅助系统Web服务启动脚本

# 获取脚本所在目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

# 创建必要的目录
mkdir -p logs
mkdir -p downloads
mkdir -p models
mkdir -p configs

# 检查Node.js
if ! command -v node &> /dev/null; then
    echo "错误: 找不到Node.js"
    echo "请安装Node.js (https://nodejs.org)"
    exit 1
fi

# 检查Web服务目录
if [ ! -d "src/web" ]; then
    echo "错误: 找不到Web服务目录 (src/web)"
    exit 1
fi

# 检查Web服务脚本
if [ ! -f "src/web/server.js" ]; then
    echo "错误: 找不到Web服务脚本 (src/web/server.js)"
    exit 1
fi

# 检查依赖项
cd "src/web"
if [ ! -d "node_modules" ]; then
    echo "警告: 找不到Node.js依赖，尝试安装..."
    
    if ! command -v npm &> /dev/null; then
        echo "错误: 找不到npm"
        echo "请安装Node.js和npm后重试"
        exit 1
    fi
    
    # 安装依赖
    npm install --no-audit --no-fund
    
    if [ $? -ne 0 ]; then
        echo "尝试使用--legacy-peer-deps选项..."
        npm install --no-audit --no-fund --legacy-peer-deps
        
        if [ $? -ne 0 ]; then
            echo "错误: 安装依赖失败"
            exit 1
        fi
    fi
fi

# 返回项目根目录
cd "$SCRIPT_DIR"

# 保存启动时间到日志
echo "===== Web服务启动于 $(date) =====" >> "logs/web_server.log"

# 启动Web服务
echo "启动零延迟YOLO FPS云辅助系统Web服务..."
echo "Web服务将运行在端口 3000"
echo "日志将保存到 logs/web_server.log"

# 设置端口并启动服务
cd "src/web"
PORT=3000 node server.js >> "../../logs/web_server.log" 2>&1

# 保存退出代码
EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
    echo "Web服务异常退出，退出代码: $EXIT_CODE"
    echo "请检查日志文件 logs/web_server.log 获取详细信息"
else
    echo "Web服务正常退出"
fi

exit $EXIT_CODE