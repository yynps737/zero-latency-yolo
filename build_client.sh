#!/bin/bash
# 文件位置: [项目根目录]/build_client.sh
# 用途: 使用交叉编译工具链编译Windows客户端程序

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统Windows客户端编译脚本 =====${NC}"

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$SCRIPT_DIR"

# 日志设置
mkdir -p "$PROJECT_ROOT/logs"
LOG_FILE="$PROJECT_ROOT/logs/build_client_$(date +%Y%m%d_%H%M%S).log"
echo -e "${YELLOW}编译日志将保存到: ${LOG_FILE}${NC}"

# 日志函数
log() {
    echo "$1" | tee -a "$LOG_FILE"
}

log_section() {
    echo -e "\n=== $1 ===" | tee -a "$LOG_FILE"
}

# 检查命令是否存在
check_command() {
    command -v $1 >/dev/null 2>&1
}

# 检查交叉编译工具链
log_section "检查交叉编译工具链"

if ! check_command x86_64-w64-mingw32-gcc || ! check_command x86_64-w64-mingw32-g++; then
    echo -e "${RED}错误: 找不到MinGW-w64交叉编译工具链${NC}"
    echo -e "${YELLOW}请先运行 sudo ./setup_environment.sh 安装环境${NC}"
    exit 1
fi

log "MinGW-w64工具链检查通过"
log "gcc版本: $(x86_64-w64-mingw32-gcc --version | head -n 1)"
log "g++版本: $(x86_64-w64-mingw32-g++ --version | head -n 1)"

# 检查CMake工具链文件
CMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/windows-toolchain.cmake"
if [ ! -f "$CMAKE_TOOLCHAIN_FILE" ]; then
    echo -e "${RED}错误: 找不到CMake工具链文件${NC}"
    echo -e "${YELLOW}请先运行 sudo ./setup_environment.sh 安装环境${NC}"
    exit 1
fi

log "CMake工具链文件检查通过: $CMAKE_TOOLCHAIN_FILE"

# 检查Windows版ONNXRuntime
log_section "检查Windows版ONNXRuntime"

if [ -z "$ONNXRUNTIME_WIN_DIR" ]; then
    # 尝试从预期位置获取
    if [ -d "$PROJECT_ROOT/third_party/onnxruntime-windows" ]; then
        export ONNXRUNTIME_WIN_DIR="$PROJECT_ROOT/third_party/onnxruntime-windows"
        log "自动设置ONNXRUNTIME_WIN_DIR=$ONNXRUNTIME_WIN_DIR"
    else
        echo -e "${RED}错误: ONNXRUNTIME_WIN_DIR环境变量未设置并且找不到Windows版ONNXRuntime${NC}"
        echo -e "${YELLOW}请先运行 ./setup_environment.sh 安装环境${NC}"
        exit 1
    fi
fi

if [ ! -d "$ONNXRUNTIME_WIN_DIR" ] || [ ! -f "$ONNXRUNTIME_WIN_DIR/lib/onnxruntime.lib" ]; then
    echo -e "${RED}错误: Windows版ONNXRuntime库文件缺失${NC}"
    echo -e "${YELLOW}请先运行 ./setup_environment.sh 安装环境${NC}"
    exit 1
fi

log "Windows版ONNXRuntime检查通过: $ONNXRUNTIME_WIN_DIR"

# 编译Windows客户端
log_section "编译Windows客户端"

# 创建并进入构建目录
BUILD_WIN_DIR="$PROJECT_ROOT/build_win"
mkdir -p "$BUILD_WIN_DIR"
cd "$BUILD_WIN_DIR" || {
    echo -e "${RED}无法进入构建目录${NC}"
    exit 1
}

# 运行CMake配置
log "运行CMake配置..."
cmake .. -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" -DCMAKE_BUILD_TYPE=Release -DBUILD_WINDOWS=ON \
      -DONNXRUNTIME_WIN_DIR="$ONNXRUNTIME_WIN_DIR" || {
    echo -e "${RED}CMake配置失败${NC}"
    log "检查CMake错误..."
    
    # 检查常见问题
    if [ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]; then
        echo -e "${RED}错误: 找不到CMakeLists.txt文件${NC}"
        exit 1
    fi
    
    log "您的CMakeLists.txt可能需要修改以支持Windows交叉编译"
    log "请确保CMakeLists.txt中包含以下内容:"
    log "option(BUILD_WINDOWS \"Build for Windows platform\" OFF)"
    log "if(BUILD_WINDOWS)"
    log "    # Windows特定配置"
    log "    add_definitions(-DWINDOWS_BUILD)"
    log "    # 链接Windows版ONNXRuntime"
    log "    set(ONNXRUNTIME_DIR \${ONNXRUNTIME_WIN_DIR})"
    log "endif()"
    
    exit 1
}

# 编译
log "编译项目..."
cpu_cores=$(nproc)
log "使用 $cpu_cores 个CPU核心进行并行编译"

make -j$cpu_cores || {
    echo -e "${RED}编译失败${NC}"
    exit 1
}

log "编译成功"

# 准备发布包
log_section "准备Windows客户端发布包"

# 创建输出目录
BIN_WIN_DIR="$PROJECT_ROOT/bin/windows"
mkdir -p "$BIN_WIN_DIR"
OUTPUT_DIR="$PROJECT_ROOT/release/client"
mkdir -p "$OUTPUT_DIR"

# 查找编译后的可执行文件
EXE_FILES=($(find "$BUILD_WIN_DIR" -name "*.exe"))

if [ ${#EXE_FILES[@]} -eq 0 ]; then
    echo -e "${RED}错误: 编译后未找到.exe文件${NC}"
    exit 1
fi

# 复制所有.exe文件
for exe_file in "${EXE_FILES[@]}"; do
    exe_name=$(basename "$exe_file")
    log "复制 $exe_name 到输出目录"
    cp "$exe_file" "$BIN_WIN_DIR/"
    cp "$exe_file" "$OUTPUT_DIR/"
done

# 假设第一个.exe是主程序
MAIN_EXE=$(basename "${EXE_FILES[0]}")
log "主程序: $MAIN_EXE"

# 复制必要的DLL
log "复制必要的DLL文件..."
cp "$ONNXRUNTIME_WIN_DIR/lib/"*.dll "$OUTPUT_DIR/" || {
    echo -e "${YELLOW}警告: 复制ONNXRuntime DLL失败${NC}"
}

# 复制配置文件
if [ -d "$PROJECT_ROOT/configs" ]; then
    log "复制配置文件..."
    mkdir -p "$OUTPUT_DIR/configs"
    
    # 尝试先找客户端特定配置
    if [ -f "$PROJECT_ROOT/configs/client.json" ]; then
        cp "$PROJECT_ROOT/configs/client.json" "$OUTPUT_DIR/configs/"
    elif [ -f "$PROJECT_ROOT/configs/client.json.example" ]; then
        cp "$PROJECT_ROOT/configs/client.json.example" "$OUTPUT_DIR/configs/client.json"
    fi
    
    # 复制任何其他配置文件
    find "$PROJECT_ROOT/configs" -name "*.json" | while read -r config_file; do
        config_name=$(basename "$config_file")
        if [[ "$config_name" != "server.json"* && "$config_name" != "client.json"* ]]; then
            cp "$config_file" "$OUTPUT_DIR/configs/"
        fi
    done
fi

# 复制模型文件(如果客户端需要)
if [ -d "$PROJECT_ROOT/models" ]; then
    log "复制模型文件..."
    mkdir -p "$OUTPUT_DIR/models"
    find "$PROJECT_ROOT/models" -name "*.onnx" | while read -r model_file; do
        cp "$model_file" "$OUTPUT_DIR/models/"
    done
fi

# 创建启动脚本
log "创建Windows启动脚本..."
START_BAT="$OUTPUT_DIR/start.bat"

cat > "$START_BAT" << EOF
@echo off
echo 正在启动零延迟YOLO FPS云辅助系统客户端...
start "" "$MAIN_EXE"
EOF

# 创建README文件
README_FILE="$OUTPUT_DIR/README.txt"
cat > "$README_FILE" << EOF
零延迟YOLO FPS云辅助系统 - Windows客户端

使用方法:
1. 双击start.bat启动程序
2. 配置文件位于configs目录

注意事项:
- 如果程序无法启动，请确保已安装Visual C++ Redistributable 2019
  下载地址: https://aka.ms/vs/16/release/vc_redist.x64.exe
EOF

# 创建ZIP包
log_section "创建ZIP发布包"
RELEASE_ZIP="$PROJECT_ROOT/release/yolo_fps_client_windows.zip"

# 确保release目录存在
mkdir -p "$(dirname "$RELEASE_ZIP")"

# 检查zip命令
if check_command zip; then
    log "创建ZIP包: $RELEASE_ZIP"
    (cd "$OUTPUT_DIR" && zip -r "$RELEASE_ZIP" .)
    
    if [ $? -eq 0 ]; then
        log "ZIP包创建成功: $RELEASE_ZIP"
    else
        echo -e "${RED}创建ZIP包失败${NC}"
    fi
else
    echo -e "${YELLOW}警告: 未找到zip命令，跳过创建ZIP包${NC}"
    echo -e "${YELLOW}客户端文件位于: $OUTPUT_DIR${NC}"
fi

# 完成
echo -e "${GREEN}===== Windows客户端编译完成 =====${NC}"
echo -e "${BLUE}Windows可执行文件: $BIN_WIN_DIR/$MAIN_EXE${NC}"
echo -e "${BLUE}发布目录: $OUTPUT_DIR${NC}"

if [ -f "$RELEASE_ZIP" ]; then
    echo -e "${BLUE}发布包: $RELEASE_ZIP${NC}"
fi

echo -e "${GREEN}Windows客户端编译和打包成功${NC}"