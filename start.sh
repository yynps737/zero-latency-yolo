#!/bin/bash
# 文件: start.sh
# 用途: 启动零延迟YOLO FPS云辅助系统服务器

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统启动脚本 =====${NC}"
PROJECT_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# 加载环境变量
if [ -f "$PROJECT_ROOT/.env" ]; then
    source "$PROJECT_ROOT/.env"
fi

# 检查环境变量是否设置
if [ -z "$ONNXRUNTIME_ROOT_DIR" ]; then
    export ONNXRUNTIME_ROOT_DIR="$PROJECT_ROOT/third_party/onnxruntime"
    export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$ONNXRUNTIME_ROOT_DIR/lib"
    echo -e "${YELLOW}自动设置 ONNXRUNTIME_ROOT_DIR=$ONNXRUNTIME_ROOT_DIR${NC}"
fi

# 检查模型文件是否存在
MODEL_PATH="$PROJECT_ROOT/models/yolo_nano_cs16.onnx"
if [ ! -f "$MODEL_PATH" ]; then
    echo -e "${YELLOW}警告: 模型文件不存在: $MODEL_PATH${NC}"
    
    # 查找模型生成脚本
    if [ -f "$PROJECT_ROOT/quick_fix.sh" ]; then
        echo -e "${YELLOW}尝试生成兼容的模型文件...${NC}"
        bash "$PROJECT_ROOT/quick_fix.sh"
    elif [ -f "$PROJECT_ROOT/generate_model.sh" ]; then
        echo -e "${YELLOW}尝试生成模型文件...${NC}"
        bash "$PROJECT_ROOT/generate_model.sh"
    else
        echo -e "${RED}错误: 找不到模型生成脚本，无法继续${NC}"
        exit 1
    fi
    
    # 再次检查模型是否生成
    if [ ! -f "$MODEL_PATH" ]; then
        echo -e "${RED}错误: 模型生成失败，无法启动服务器${NC}"
        exit 1
    fi
fi

# 查找服务器可执行文件
SERVER_BIN=""
POSSIBLE_LOCATIONS=(
    "$PROJECT_ROOT/bin/yolo_server"
    "$PROJECT_ROOT/bin/server"
    "$PROJECT_ROOT/build/bin/yolo_server"
    "$PROJECT_ROOT/build/yolo_server"
)

for location in "${POSSIBLE_LOCATIONS[@]}"; do
    if [ -f "$location" ] && [ -x "$location" ]; then
        SERVER_BIN="$location"
        echo -e "${GREEN}找到服务器可执行文件: $SERVER_BIN${NC}"
        break
    fi
done

# 如果没找到，尝试查找任何可执行文件
if [ -z "$SERVER_BIN" ]; then
    echo -e "${YELLOW}在bin目录中搜索可执行文件...${NC}"
    SERVER_BIN=$(find "$PROJECT_ROOT/bin" -type f -executable 2>/dev/null | head -n 1)
    
    if [ -z "$SERVER_BIN" ]; then
        echo -e "${YELLOW}在build目录中搜索可执行文件...${NC}"
        SERVER_BIN=$(find "$PROJECT_ROOT/build" -type f -executable -not -path "*/CMakeFiles/*" 2>/dev/null | grep -v "\.sh" | head -n 1)
    fi
    
    if [ -n "$SERVER_BIN" ]; then
        echo -e "${GREEN}找到服务器可执行文件: $SERVER_BIN${NC}"
    else
        echo -e "${RED}错误: 找不到服务器可执行文件${NC}"
        echo -e "${YELLOW}请先运行 ./build_server.sh 编译服务器${NC}"
        exit 1
    fi
fi

# 启动服务器
echo -e "${BLUE}正在启动服务器...${NC}"

# 将工作目录切换到项目根目录，确保相对路径正确
cd "$PROJECT_ROOT"

# 启动服务器
"$SERVER_BIN"

# 检查启动状态
EXIT_CODE=$?
if [ $EXIT_CODE -ne 0 ]; then
    echo -e "${RED}服务器启动失败，错误码: $EXIT_CODE${NC}"
    echo -e "${YELLOW}可能的原因:${NC}"
    echo -e "1. 模型版本不兼容 - 请运行 ./quick_fix.sh 生成兼容的模型"
    echo -e "2. 环境变量设置错误 - 请确保正确设置 ONNXRUNTIME_ROOT_DIR"
    echo -e "3. 端口被占用 - 尝试修改配置文件中的端口"
    echo -e "4. 权限问题 - 检查可执行文件权限"
    echo -e "\n${BLUE}查看日志获取更多信息${NC}"
else
    echo -e "${GREEN}服务器正在运行${NC}"
fi