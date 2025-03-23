#!/bin/bash
# 文件位置: [项目根目录]/setup_environment.sh
# 用途: 一次性安装所有开发环境，包括交叉编译工具链和依赖项

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统环境安装脚本 =====${NC}"
echo -e "${BLUE}此脚本将安装所有开发和运行环境，包括交叉编译工具链${NC}"

# 检查root权限
if [[ $EUID -ne 0 ]]; then
   echo -e "${YELLOW}此脚本需要root权限来安装系统依赖.${NC}"
   echo -e "${YELLOW}请使用 sudo ./setup_environment.sh 运行${NC}"
   exit 1
fi

# 如果使用sudo运行，获取实际用户
REAL_USER=${SUDO_USER:-$USER}
REAL_HOME=$(eval echo ~$REAL_USER)
REAL_GROUP=$(id -gn $REAL_USER)

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$SCRIPT_DIR"

# 检查系统类型
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    VERSION=$VERSION_ID
    
    # 检查发行版分支
    if [ -f /etc/debian_version ]; then
        DEBIAN_BASED=1
    elif [ -f /etc/redhat-release ]; then
        RHEL_BASED=1
    fi
else
    echo -e "${RED}无法确定操作系统类型${NC}"
    exit 1
fi

echo -e "${BLUE}检测到操作系统: ${OS} ${VERSION}${NC}"

# 确保必要的目录存在
mkdir -p "$PROJECT_ROOT/logs"
LOG_FILE="$PROJECT_ROOT/logs/setup_environment_$(date +%Y%m%d_%H%M%S).log"
echo -e "${YELLOW}安装日志将保存到: ${LOG_FILE}${NC}"

# 确保log目录权限正确
chown -R $REAL_USER:$REAL_GROUP "$PROJECT_ROOT/logs"

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

# 安装基本依赖
log_section "安装基本依赖"

# Ubuntu/Debian系统
if [[ -n "$DEBIAN_BASED" || "$OS" == "ubuntu" || "$OS" == "debian" || "$OS" == "linuxmint" ]]; then
    log "在 Debian/Ubuntu 系统上安装依赖..."
    
    # 更新软件包列表
    apt-get update -qq || {
        echo -e "${RED}无法更新软件包列表${NC}"
        exit 1
    }
    
    # 安装编译工具和基本依赖
    apt-get install -y build-essential cmake git wget curl unzip \
                     libssl-dev zlib1g-dev libgomp1 \
                     ca-certificates python3 python3-pip || {
        echo -e "${RED}安装基本依赖失败${NC}"
        exit 1
    }
    
    # 检查和安装Node.js (如果不存在)
    if ! check_command node || ! check_command npm; then
        log "安装 Node.js 和 npm..."
        
        # 尝试使用NodeSource存储库安装Node.js 18.x
        if ! check_command curl; then
            apt-get install -y curl
        fi
        
        # 尝试使用NodeSource
        curl -fsSL https://deb.nodesource.com/setup_18.x -o nodesource_setup.sh
        if [ -f nodesource_setup.sh ]; then
            bash nodesource_setup.sh
            apt-get install -y nodejs
            rm nodesource_setup.sh
        else
            # 如果NodeSource不可用，使用Ubuntu默认存储库
            log "使用Ubuntu默认存储库安装Node.js..."
            apt-get install -y nodejs npm
        fi
        
        # 验证Node.js安装
        if ! check_command node || ! check_command npm; then
            echo -e "${RED}安装Node.js失败，脚本将继续但Web依赖安装可能会失败${NC}"
        else
            log "Node.js安装成功: $(node --version)"
            log "NPM安装成功: $(npm --version)"
        fi
    fi
    
    # 安装MinGW-w64交叉编译工具链
    log "安装MinGW-w64交叉编译工具链..."
    apt-get install -y mingw-w64 || {
        echo -e "${RED}安装交叉编译工具链失败${NC}"
        exit 1
    }
    
    # 安装Wine (可选，用于测试Windows程序)
    log "安装Wine (用于测试Windows程序)..."
    apt-get install -y wine64 || {
        log "Wine安装失败。这不会影响编译，但可能影响测试。"
    }
    
# 其他系统不支持
else
    echo -e "${RED}不支持的操作系统: ${OS}${NC}"
    echo -e "${YELLOW}此脚本仅支持Ubuntu/Debian系统${NC}"
    exit 1
fi

# 验证安装的依赖
log_section "验证已安装的依赖"

# 检查必需工具
REQUIRED_COMMANDS=("gcc" "g++" "make" "cmake" "git" "wget" "curl" "python3" "x86_64-w64-mingw32-gcc" "x86_64-w64-mingw32-g++")
MISSING_COMMANDS=()

for cmd in "${REQUIRED_COMMANDS[@]}"; do
    if ! check_command $cmd; then
        MISSING_COMMANDS+=($cmd)
    else
        # 获取版本信息
        version=$($cmd --version 2>&1 | head -n 1)
        log "$cmd: $version"
    fi
done

# 检查Node.js和npm (Web依赖需要)
if check_command node; then
    node_version=$(node --version 2>&1)
    log "node: $node_version"
else
    log "警告: Node.js未安装。Web依赖安装可能会失败。"
fi

if check_command npm; then
    npm_version=$(npm --version 2>&1)
    log "npm: $npm_version"
else
    log "警告: npm未安装。Web依赖安装可能会失败。"
fi

if [ ${#MISSING_COMMANDS[@]} -ne 0 ]; then
    log_section "警告: 以下工具未安装:"
    for cmd in "${MISSING_COMMANDS[@]}"; do
        log " - $cmd"
    done
    echo -e "${RED}一些必需的依赖未能成功安装. 请手动安装它们.${NC}"
    exit 1
fi

# 创建项目目录结构
log_section "创建项目目录结构"
mkdir -p "$PROJECT_ROOT/build" \
         "$PROJECT_ROOT/build_win" \
         "$PROJECT_ROOT/logs" \
         "$PROJECT_ROOT/downloads" \
         "$PROJECT_ROOT/configs" \
         "$PROJECT_ROOT/bin" \
         "$PROJECT_ROOT/bin/windows" \
         "$PROJECT_ROOT/models" \
         "$PROJECT_ROOT/third_party" \
         "$PROJECT_ROOT/cmake"

# 设置正确的权限
chown -R $REAL_USER:$REAL_GROUP "$PROJECT_ROOT/build" \
                              "$PROJECT_ROOT/build_win" \
                              "$PROJECT_ROOT/logs" \
                              "$PROJECT_ROOT/downloads" \
                              "$PROJECT_ROOT/configs" \
                              "$PROJECT_ROOT/bin" \
                              "$PROJECT_ROOT/bin/windows" \
                              "$PROJECT_ROOT/models" \
                              "$PROJECT_ROOT/third_party" \
                              "$PROJECT_ROOT/cmake"

# 检查磁盘空间
log_section "检查磁盘空间"
REQUIRED_SPACE=500  # 需要至少500MB可用空间
AVAILABLE_SPACE=$(df -m "$PROJECT_ROOT" | awk 'NR==2 {print $4}')

if [ "$AVAILABLE_SPACE" -lt "$REQUIRED_SPACE" ]; then
    echo -e "${RED}警告: 可用磁盘空间不足! 需要至少${REQUIRED_SPACE}MB, 但只有${AVAILABLE_SPACE}MB可用.${NC}"
    echo -e "${YELLOW}安装可能会失败，请清理磁盘空间后重试.${NC}"
    # 不退出，只警告
fi

# 下载 Linux版ONNXRuntime
log_section "下载 Linux版ONNXRuntime"
ONNX_DIR="$PROJECT_ROOT/third_party/onnxruntime"

if [ ! -d "$ONNX_DIR" ]; then
    log "下载 ONNXRuntime..."
    
    # 创建临时目录用于下载
    TMP_DIR=$(mktemp -d)
    chmod 755 "$TMP_DIR"
    chown $REAL_USER:$REAL_GROUP "$TMP_DIR"
    
    # 下载ONNXRuntime
    ONNX_VERSION="1.15.1"
    ONNX_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/onnxruntime-linux-x64-${ONNX_VERSION}.tgz"
    
    log "从 $ONNX_URL 下载 ONNXRuntime"
    
    # 使用wget下载，最多尝试3次
    MAX_RETRY=3
    RETRY_COUNT=0
    DOWNLOAD_SUCCESS=false
    
    while [ $RETRY_COUNT -lt $MAX_RETRY ] && [ "$DOWNLOAD_SUCCESS" = "false" ]; do
        RETRY_COUNT=$((RETRY_COUNT+1))
        log "下载尝试 $RETRY_COUNT/$MAX_RETRY..."
        
        if wget -q --show-progress --timeout=30 --tries=3 "$ONNX_URL" -O "$TMP_DIR/onnxruntime.tgz"; then
            DOWNLOAD_SUCCESS=true
        else
            log "下载尝试 $RETRY_COUNT 失败，等待5秒后重试..."
            sleep 5
        fi
    done
    
    if [ "$DOWNLOAD_SUCCESS" = "false" ]; then
        echo -e "${RED}下载 ONNXRuntime 失败${NC}"
        rm -rf "$TMP_DIR"
        exit 1
    fi
    
    # 解压ONNXRuntime
    log "解压 ONNXRuntime..."
    mkdir -p "$PROJECT_ROOT/third_party"
    tar -xzf "$TMP_DIR/onnxruntime.tgz" -C "$PROJECT_ROOT/third_party" || {
        echo -e "${RED}解压 ONNXRuntime 失败${NC}"
        rm -rf "$TMP_DIR"
        exit 1
    }
    
    # 重命名目录
    if [ -d "$PROJECT_ROOT/third_party/onnxruntime-linux-x64-${ONNX_VERSION}" ]; then
        mv "$PROJECT_ROOT/third_party/onnxruntime-linux-x64-${ONNX_VERSION}" "$PROJECT_ROOT/third_party/onnxruntime" || {
            echo -e "${RED}重命名 ONNXRuntime 目录失败${NC}"
            rm -rf "$TMP_DIR"
            exit 1
        }
    fi
    
    # 清理临时文件
    rm -rf "$TMP_DIR"
    
    # 验证安装
    if [ ! -f "$ONNX_DIR/lib/libonnxruntime.so" ]; then
        echo -e "${RED}ONNXRuntime 安装不完整: 缺少 libonnxruntime.so${NC}"
        exit 1
    else
        log "ONNXRuntime 成功安装到 $ONNX_DIR"
    fi
    
    # 确保权限正确
    chown -R $REAL_USER:$REAL_GROUP "$ONNX_DIR"
else
    log "ONNXRuntime 已存在于 $ONNX_DIR"
fi

# 下载Windows版ONNXRuntime
log_section "下载Windows版ONNXRuntime"
ONNX_WIN_DIR="$PROJECT_ROOT/third_party/onnxruntime-windows"

if [ ! -d "$ONNX_WIN_DIR" ]; then
    log "下载Windows版ONNXRuntime..."
    
    # 创建临时目录用于下载
    TMP_DIR=$(mktemp -d)
    chmod 755 "$TMP_DIR"
    chown $REAL_USER:$REAL_GROUP "$TMP_DIR"
    
    # 下载Windows版ONNXRuntime
    ONNX_VERSION="1.15.1"
    ONNX_WIN_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/onnxruntime-win-x64-${ONNX_VERSION}.zip"
    
    log "从 $ONNX_WIN_URL 下载Windows版ONNXRuntime"
    
    # 使用wget下载，最多尝试3次
    MAX_RETRY=3
    RETRY_COUNT=0
    DOWNLOAD_SUCCESS=false
    
    while [ $RETRY_COUNT -lt $MAX_RETRY ] && [ "$DOWNLOAD_SUCCESS" = "false" ]; do
        RETRY_COUNT=$((RETRY_COUNT+1))
        log "下载尝试 $RETRY_COUNT/$MAX_RETRY..."
        
        if wget -q --show-progress --timeout=30 --tries=3 "$ONNX_WIN_URL" -O "$TMP_DIR/onnxruntime-windows.zip"; then
            DOWNLOAD_SUCCESS=true
        else
            log "下载尝试 $RETRY_COUNT 失败，等待5秒后重试..."
            sleep 5
        fi
    done
    
    if [ "$DOWNLOAD_SUCCESS" = "false" ]; then
        echo -e "${RED}下载Windows版ONNXRuntime失败${NC}"
        echo -e "${YELLOW}Windows客户端编译将无法进行${NC}"
        rm -rf "$TMP_DIR"
    else
        # 解压Windows版ONNXRuntime
        log "解压Windows版ONNXRuntime..."
        mkdir -p "$ONNX_WIN_DIR"
        
        # 检查是否安装了unzip
        if ! check_command unzip; then
            log "安装unzip工具..."
            apt-get install -y unzip
        fi
        
        unzip -q "$TMP_DIR/onnxruntime-windows.zip" -d "$TMP_DIR/extract" || {
            echo -e "${RED}解压Windows版ONNXRuntime失败${NC}"
            rm -rf "$TMP_DIR"
            exit 1
        }
        
        # 移动文件到目标目录
        cp -r "$TMP_DIR/extract"/*/* "$ONNX_WIN_DIR/"
        
        # 清理临时文件
        rm -rf "$TMP_DIR"
        
        # 验证安装
        if [ ! -d "$ONNX_WIN_DIR/lib" ] || [ ! -f "$ONNX_WIN_DIR/lib/onnxruntime.lib" ]; then
            echo -e "${RED}解压后未找到Windows版ONNXRuntime库${NC}"
            echo -e "${YELLOW}Windows客户端编译可能会失败${NC}"
        else
            log "Windows版ONNXRuntime成功安装到 $ONNX_WIN_DIR"
            chown -R $REAL_USER:$REAL_GROUP "$ONNX_WIN_DIR"
        fi
    fi
else
    log "Windows版ONNXRuntime已存在于 $ONNX_WIN_DIR"
fi

# 创建CMake工具链文件
log_section "创建CMake工具链文件"
CMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/windows-toolchain.cmake"

cat > "$CMAKE_TOOLCHAIN_FILE" << 'EOF'
# 交叉编译到Windows的CMake工具链文件
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# 指定使用的编译器
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# 设置查找库和程序时，只在交叉编译环境中查找
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# 设置编译参数
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libgcc")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")

# Windows特定定义
add_definitions(-DWIN32 -D_WINDOWS)
EOF

chown $REAL_USER:$REAL_GROUP "$CMAKE_TOOLCHAIN_FILE"
log "创建了CMake工具链文件: $CMAKE_TOOLCHAIN_FILE"

# 设置 ONNXRuntime 环境变量
log_section "设置环境变量"

ONNXRUNTIME_ROOT_DIR="$PROJECT_ROOT/third_party/onnxruntime"
log "ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}"
ONNXRUNTIME_WIN_DIR="$PROJECT_ROOT/third_party/onnxruntime-windows"
log "ONNXRUNTIME_WIN_DIR=${ONNXRUNTIME_WIN_DIR}"

# 为实际用户更新shell配置文件，而不是root
SHELL_CONFIG=""
if [ -f "$REAL_HOME/.bashrc" ]; then
    SHELL_CONFIG="$REAL_HOME/.bashrc"
elif [ -f "$REAL_HOME/.bash_profile" ]; then
    SHELL_CONFIG="$REAL_HOME/.bash_profile"
elif [ -f "$REAL_HOME/.zshrc" ]; then
    SHELL_CONFIG="$REAL_HOME/.zshrc"
fi

if [ -n "$SHELL_CONFIG" ]; then
    log "更新 shell 配置: $SHELL_CONFIG"
    
    # 临时存储环境变量配置
    ENV_CONFIG_FILE=$(mktemp)
    cat > "$ENV_CONFIG_FILE" << EOF

# 零延迟YOLO系统环境变量
export ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}
export ONNXRUNTIME_WIN_DIR=${ONNXRUNTIME_WIN_DIR}
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:${ONNXRUNTIME_ROOT_DIR}/lib
EOF
    
    # 检查是否已存在环境变量
    if ! grep -q "ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}" "$SHELL_CONFIG"; then
        # 追加到shell配置
        cat "$ENV_CONFIG_FILE" >> "$SHELL_CONFIG"
        chown $REAL_USER:$REAL_GROUP "$SHELL_CONFIG"
    fi
    
    # 清理临时文件
    rm -f "$ENV_CONFIG_FILE"
else
    log "无法找到适当的shell配置文件."
    log "请手动添加以下行到您的shell配置文件:"
    log "export ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}"
    log "export ONNXRUNTIME_WIN_DIR=${ONNXRUNTIME_WIN_DIR}"
    log "export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:${ONNXRUNTIME_ROOT_DIR}/lib"
fi

# 复制配置文件
if [ -f "$PROJECT_ROOT/configs/server.json.example" ] && [ ! -f "$PROJECT_ROOT/configs/server.json" ]; then
    log "复制服务器配置示例..."
    cp "$PROJECT_ROOT/configs/server.json.example" "$PROJECT_ROOT/configs/server.json"
    chown $REAL_USER:$REAL_GROUP "$PROJECT_ROOT/configs/server.json"
fi

if [ -f "$PROJECT_ROOT/configs/client.json.example" ] && [ ! -f "$PROJECT_ROOT/configs/client.json" ]; then
    log "复制客户端配置示例..."
    cp "$PROJECT_ROOT/configs/client.json.example" "$PROJECT_ROOT/configs/client.json"
    chown $REAL_USER:$REAL_GROUP "$PROJECT_ROOT/configs/client.json"
fi

# 导出当前环境变量(用于后续脚本)
export ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}
export ONNXRUNTIME_WIN_DIR=${ONNXRUNTIME_WIN_DIR}
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${ONNXRUNTIME_ROOT_DIR}/lib

# 创建模型测试脚本
MODEL_TEST_SCRIPT="$PROJECT_ROOT/test_model.sh"
cat > "$MODEL_TEST_SCRIPT" << 'EOF'
#!/bin/bash
# 模型测试脚本

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== YOLO模型测试脚本 =====${NC}"

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$SCRIPT_DIR"

# 检查模型文件
MODEL_DIR="$PROJECT_ROOT/models"
if [ ! -d "$MODEL_DIR" ]; then
    echo -e "${RED}模型目录不存在: $MODEL_DIR${NC}"
    exit 1
fi

# 列出所有模型文件
echo -e "${BLUE}模型目录中的文件:${NC}"
find "$MODEL_DIR" -name "*.onnx" | while read -r model_file; do
    echo "  - $(basename "$model_file")"
done

# 检查是否有模型文件
if [ -z "$(find "$MODEL_DIR" -name "*.onnx")" ]; then
    echo -e "${YELLOW}未找到模型文件，尝试生成测试模型...${NC}"
    
    # 尝试运行生成脚本
    if [ -f "$PROJECT_ROOT/scripts/generate_dummy_model.py" ]; then
        echo -e "${BLUE}运行模型生成脚本...${NC}"
        python3 "$PROJECT_ROOT/scripts/generate_dummy_model.py" --output "$MODEL_DIR/yolo_nano_cs16.onnx"
        
        if [ $? -eq 0 ]; then
            echo -e "${GREEN}成功生成测试模型${NC}"
        else
            echo -e "${RED}模型生成失败${NC}"
            exit 1
        fi
    else
        echo -e "${RED}找不到模型生成脚本${NC}"
        exit 1
    fi
fi

echo -e "${GREEN}模型文件检查完成${NC}"
exit 0
EOF

chmod +x "$MODEL_TEST_SCRIPT"
chown $REAL_USER:$REAL_GROUP "$MODEL_TEST_SCRIPT"
log "创建了模型测试脚本: $MODEL_TEST_SCRIPT"

# 安装完成
echo -e "${GREEN}===== 环境安装完成 =====${NC}"
echo -e "${BLUE}ONNXRuntime路径: ${ONNXRUNTIME_ROOT_DIR}${NC}"
echo -e "${BLUE}Windows版ONNXRuntime路径: ${ONNXRUNTIME_WIN_DIR}${NC}"
echo -e "${BLUE}CMake工具链文件: ${CMAKE_TOOLCHAIN_FILE}${NC}"
echo -e "${YELLOW}请运行以下命令更新环境变量:${NC}"
echo -e "${BLUE}source ${SHELL_CONFIG}${NC}"
echo -e "${GREEN}现在您可以使用后端部署脚本和客户端编译脚本${NC}"