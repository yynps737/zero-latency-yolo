#!/bin/bash
# 零延迟YOLO FPS云辅助系统服务器启动脚本

# 获取脚本所在目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

# 创建目录(如果不存在)
mkdir -p logs
mkdir -p configs
mkdir -p models

# 检查配置文件
if [ ! -f "configs/server.json" ]; then
    echo "警告: 未找到服务器配置文件，使用默认配置"
    if [ ! -f "configs/server.json.example" ]; then
        # 创建默认配置
        cat > "configs/server.json" << EOF
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
    "use_high_priority": true
}
EOF
    else
        # 复制示例配置
        cp "configs/server.json.example" "configs/server.json"
    fi
fi

# 检查 ONNXRuntime 环境变量
if [ -z "$ONNXRUNTIME_ROOT_DIR" ]; then
    # 尝试查找 ONNXRuntime 路径
    if [ -d "./third_party/onnxruntime" ]; then
        export ONNXRUNTIME_ROOT_DIR="$SCRIPT_DIR/third_party/onnxruntime"
        echo "自动设置 ONNXRUNTIME_ROOT_DIR=$ONNXRUNTIME_ROOT_DIR"
    elif [ -d "/usr/local/onnxruntime" ]; then
        export ONNXRUNTIME_ROOT_DIR="/usr/local/onnxruntime"
        echo "自动设置 ONNXRUNTIME_ROOT_DIR=$ONNXRUNTIME_ROOT_DIR"
    else
        echo "错误: 未设置 ONNXRUNTIME_ROOT_DIR 环境变量，且无法自动找到 ONNXRuntime 路径"
        echo "请设置环境变量: export ONNXRUNTIME_ROOT_DIR=/path/to/onnxruntime"
        exit 1
    fi
fi

# 设置 LD_LIBRARY_PATH 以包含 ONNXRuntime 库
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$ONNXRUNTIME_ROOT_DIR/lib"

# 检查 ONNXRuntime 库文件
if [ ! -f "$ONNXRUNTIME_ROOT_DIR/lib/libonnxruntime.so" ] && [ ! -f "$ONNXRUNTIME_ROOT_DIR/lib/libonnxruntime.dylib" ]; then
    echo "警告: 在 $ONNXRUNTIME_ROOT_DIR/lib 中找不到 libonnxruntime.so 或 libonnxruntime.dylib"
    echo "服务器可能无法正常启动"
fi

# 检查可执行文件
SERVER_EXECUTABLE=""
for exe in "bin/yolo_server" "bin/server" "bin/yolo_fps_assist"; do
    if [ -f "$exe" ]; then
        SERVER_EXECUTABLE="$exe"
        break
    fi
done

if [ -z "$SERVER_EXECUTABLE" ]; then
    echo "错误: 找不到服务器可执行文件"
    echo "请确保已经编译服务器并将可执行文件放在 bin 目录中"
    echo "您可以运行 ./deploy_backend.sh 重新编译服务器"
    exit 1
fi

# 如果没有模型文件，显示警告
if [ ! -f "models/yolo_nano_cs16.onnx" ] && [ -z "$(find models -name "*.onnx" 2>/dev/null)" ]; then
    echo "警告: 未找到模型文件"
    echo "服务器将尝试使用配置文件中指定的模型路径"
    echo "如果启动失败，请确保放置至少一个 ONNX 模型文件到 models 目录中"
fi

# 启动服务器
echo "启动零延迟YOLO FPS云辅助系统服务器..."
echo "使用 $SERVER_EXECUTABLE"
echo "日志将保存到 logs/server.log"

# 保存启动时间到日志
echo "===== 服务器启动于 $(date) =====" >> "logs/server.log"

# 启动服务器，将输出重定向到日志文件
./$SERVER_EXECUTABLE "$@" >> "logs/server.log" 2>&1

# 保存退出代码
EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
    echo "服务器异常退出，退出代码: $EXIT_CODE"
    echo "请检查日志文件 logs/server.log 获取详细信息"
else
    echo "服务器正常退出"
fi

exit $EXIT_CODE