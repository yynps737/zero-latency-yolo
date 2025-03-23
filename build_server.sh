#!/bin/bash
# 文件: build_server.sh
# 用途: 编译服务器程序

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统服务器编译脚本 =====${NC}"
PROJECT_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# 加载环境变量
if [ -f "$PROJECT_ROOT/.env" ]; then
    source "$PROJECT_ROOT/.env"
fi

# 检查环境变量是否设置
if [ -z "$ONNXRUNTIME_ROOT_DIR" ]; then
    echo -e "${RED}错误: ONNXRUNTIME_ROOT_DIR环境变量未设置${NC}"
    echo -e "${YELLOW}请先运行: source .env${NC}"
    exit 1
fi

# 检查ONNX Runtime
if [ ! -d "$ONNXRUNTIME_ROOT_DIR" ]; then
    echo -e "${RED}错误: ONNX Runtime目录不存在: $ONNXRUNTIME_ROOT_DIR${NC}"
    echo -e "${YELLOW}请先运行 ./setup.sh 安装环境${NC}"
    exit 1
fi

# 创建日志目录
mkdir -p "$PROJECT_ROOT/logs"
LOG_FILE="$PROJECT_ROOT/logs/build_server_$(date +%Y%m%d_%H%M%S).log"
echo -e "${YELLOW}编译日志将保存到: ${LOG_FILE}${NC}"

# 确保目录存在
mkdir -p "$PROJECT_ROOT/build" "$PROJECT_ROOT/bin"

# 编译服务器
echo -e "${BLUE}编译服务器...${NC}" | tee -a "$LOG_FILE"
cd "$PROJECT_ROOT/build"

# 运行CMake
echo -e "${YELLOW}配置CMake...${NC}" | tee -a "$LOG_FILE"
cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1 | tee -a "$LOG_FILE" || {
    echo -e "${RED}CMake配置失败${NC}" | tee -a "$LOG_FILE"
    echo -e "${YELLOW}检查错误信息:${NC}"
    tail -n 20 "$LOG_FILE"
    exit 1
}

# 编译
echo -e "${YELLOW}开始编译...${NC}" | tee -a "$LOG_FILE"
cpu_cores=$(nproc 2>/dev/null || echo 2)
echo -e "${BLUE}使用 $cpu_cores 个CPU核心进行编译${NC}" | tee -a "$LOG_FILE"

make -j$cpu_cores 2>&1 | tee -a "$LOG_FILE" || {
    echo -e "${RED}并行编译失败，尝试单线程编译...${NC}" | tee -a "$LOG_FILE"
    make 2>&1 | tee -a "$LOG_FILE"
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}编译失败，请检查错误信息${NC}" | tee -a "$LOG_FILE"
        echo -e "${YELLOW}检查错误信息:${NC}"
        tail -n 20 "$LOG_FILE"
        exit 1
    fi
}

# 查找编译后的可执行文件
echo -e "${BLUE}查找编译后的可执行文件...${NC}" | tee -a "$LOG_FILE"

# 详细列出build目录中编译的可执行文件
echo -e "${YELLOW}编译输出目录内容:${NC}" | tee -a "$LOG_FILE"
find "$PROJECT_ROOT/build" -type f -executable -not -path "*/CMakeFiles/*" | tee -a "$LOG_FILE"

# 查找可能的文件位置
EXECUTABLE=""
POSSIBLE_LOCATIONS=(
    "$PROJECT_ROOT/build/yolo_server"
    "$PROJECT_ROOT/build/bin/yolo_server"
    "$PROJECT_ROOT/build/src/server/yolo_server"
    "$PROJECT_ROOT/bin/yolo_server"
)

# 检查CMakeLists.txt中设置的输出目录
CMAKE_OUTPUT_DIR=$(grep "CMAKE_RUNTIME_OUTPUT_DIRECTORY" "$PROJECT_ROOT/CMakeLists.txt" | awk -F'"' '{print $2}')
if [ -n "$CMAKE_OUTPUT_DIR" ]; then
    POSSIBLE_LOCATIONS+=("$CMAKE_OUTPUT_DIR/yolo_server")
fi

# 检查所有可能的位置
for location in "${POSSIBLE_LOCATIONS[@]}"; do
    if [ -f "$location" ]; then
        EXECUTABLE="$location"
        echo -e "${GREEN}找到可执行文件: $EXECUTABLE${NC}" | tee -a "$LOG_FILE"
        break
    fi
done

# 如果还是没找到，尝试更广泛的搜索
if [ -z "$EXECUTABLE" ]; then
    echo -e "${YELLOW}尝试在build目录中查找yolo_server...${NC}" | tee -a "$LOG_FILE"
    EXECUTABLE=$(find "$PROJECT_ROOT/build" -type f -name "yolo_server" 2>/dev/null | head -n 1)
fi

# 如果仍未找到，尝试查找任何可执行文件
if [ -z "$EXECUTABLE" ]; then
    echo -e "${YELLOW}尝试查找任何可执行文件...${NC}" | tee -a "$LOG_FILE"
    EXECUTABLE=$(find "$PROJECT_ROOT/build" -type f -executable -not -path "*/CMakeFiles/*" 2>/dev/null | head -n 1)
fi

if [ -z "$EXECUTABLE" ]; then
    echo -e "${RED}找不到编译后的可执行文件${NC}" | tee -a "$LOG_FILE"
    
    # 检查CMakeCache.txt中的输出目录设置
    CACHED_OUTPUT_DIR=$(grep "CMAKE_RUNTIME_OUTPUT_DIRECTORY" "$PROJECT_ROOT/build/CMakeCache.txt" | awk -F'=' '{print $2}')
    if [ -n "$CACHED_OUTPUT_DIR" ]; then
        echo -e "${YELLOW}根据CMakeCache.txt, 可执行文件应该在: $CACHED_OUTPUT_DIR${NC}" | tee -a "$LOG_FILE"
        if [ -d "$CACHED_OUTPUT_DIR" ]; then
            echo -e "${YELLOW}检查该目录内容:${NC}" | tee -a "$LOG_FILE"
            ls -la "$CACHED_OUTPUT_DIR" | tee -a "$LOG_FILE"
        fi
    fi
    
    echo -e "${YELLOW}查看CMakeLists.txt中的设置...${NC}" | tee -a "$LOG_FILE"
    grep -n "add_executable" "$PROJECT_ROOT/CMakeLists.txt" | tee -a "$LOG_FILE"
    grep -n "CMAKE_RUNTIME_OUTPUT_DIRECTORY" "$PROJECT_ROOT/CMakeLists.txt" | tee -a "$LOG_FILE"
    
    echo -e "${RED}请手动查找编译后的可执行文件并复制到bin目录${NC}" | tee -a "$LOG_FILE"
    exit 1
fi

# 复制到bin目录
echo -e "${BLUE}复制可执行文件到bin目录...${NC}" | tee -a "$LOG_FILE"
cp "$EXECUTABLE" "$PROJECT_ROOT/bin/"
chmod +x "$PROJECT_ROOT/bin/$(basename "$EXECUTABLE")"

echo -e "${GREEN}服务器编译完成!${NC}"
echo -e "${BLUE}可执行文件: $PROJECT_ROOT/bin/$(basename "$EXECUTABLE")${NC}"
echo -e "${YELLOW}接下来，确保您已生成兼容的模型:${NC}"
echo -e "${BLUE}./generate_model.sh${NC}"
echo -e "${YELLOW}然后启动服务器:${NC}"
echo -e "${BLUE}./start.sh${NC}"