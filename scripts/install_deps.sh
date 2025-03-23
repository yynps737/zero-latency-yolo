#!/bin/bash

# 零延迟YOLO FPS云辅助系统依赖安装脚本
# 此脚本用于安装服务器需要的所有依赖

# 严格错误检查
set -e

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统依赖安装脚本 =====${NC}"

# 检查root权限
if [[ $EUID -ne 0 ]]; then
   echo -e "${YELLOW}此脚本需要root权限来安装系统依赖.${NC}"
   echo -e "${YELLOW}请使用 sudo ./install_deps.sh 运行${NC}"
   exit 1
fi

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

# 创建日志目录
mkdir -p logs
LOG_FILE="logs/install_deps_$(date +%Y%m%d_%H%M%S).log"
echo -e "${YELLOW}安装日志将保存到: ${LOG_FILE}${NC}"

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
    apt-get update -qq
    
    # 安装编译工具和基本依赖
    apt-get install -y build-essential cmake git wget curl unzip \
                     libssl-dev zlib1g-dev libgomp1 \
                     ca-certificates
    
    # 检查和安装Node.js (如果不存在)
    if ! check_command node || ! check_command npm; then
        log "安装 Node.js 和 npm..."
        # 添加Node.js存储库
        curl -sL https://deb.nodesource.com/setup_16.x | bash -
        apt-get install -y nodejs
    fi
    
# CentOS/RHEL/Fedora系统
elif [[ -n "$RHEL_BASED" || "$OS" == "centos" || "$OS" == "rhel" || "$OS" == "fedora" ]]; then
    log "在 RHEL/CentOS 系统上安装依赖..."
    
    # 更新软件包
    yum update -y
    
    # 安装开发工具
    yum groupinstall -y "Development Tools"
    yum install -y cmake git wget curl unzip openssl-devel zlib-devel libgomp
    
    # 检查和安装Node.js (如果不存在)
    if ! check_command node || ! check_command npm; then
        log "安装 Node.js 和 npm..."
        curl -sL https://rpm.nodesource.com/setup_16.x | bash -
        yum install -y nodejs
    fi
    
# Alpine系统
elif [[ "$OS" == "alpine" ]]; then
    log "在 Alpine 系统上安装依赖..."
    
    # 更新软件包
    apk update
    
    # 安装依赖
    apk add build-base cmake git wget curl unzip openssl-dev zlib-dev libgomp
    
    # 检查和安装Node.js (如果不存在)
    if ! check_command node || ! check_command npm; then
        log "安装 Node.js 和 npm..."
        apk add nodejs npm
    fi
    
else
    echo -e "${RED}不支持的操作系统: ${OS}${NC}"
    exit 1
fi

# 验证安装的依赖
log_section "验证已安装的依赖"

# 检查必需工具
REQUIRED_COMMANDS=("gcc" "g++" "make" "cmake" "git" "wget" "curl" "node" "npm")
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

if [ ${#MISSING_COMMANDS[@]} -ne 0 ]; then
    log_section "警告: 以下工具未安装:"
    for cmd in "${MISSING_COMMANDS[@]}"; do
        log " - $cmd"
    done
    echo -e "${RED}一些必需的依赖未能成功安装. 请手动安装它们.${NC}"
    exit 1
fi

# 下载 ONNXRuntime
log_section "下载ONNXRuntime"

ONNX_DIR="../third_party/onnxruntime"
if [ ! -d "$ONNX_DIR" ]; then
    log "下载 ONNXRuntime..."
    mkdir -p ../third_party
    cd ../third_party
    
    ONNX_VERSION="1.15.1"
    ONNX_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/onnxruntime-linux-x64-${ONNX_VERSION}.tgz"
    
    log "从 $ONNX_URL 下载 ONNXRuntime"
    wget -q --show-progress $ONNX_URL -O onnxruntime.tgz
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}下载 ONNXRuntime 失败${NC}"
        exit 1
    fi
    
    log "解压 ONNXRuntime..."
    tar -xzf onnxruntime.tgz
    
    if [ $? -ne 0 ]; then
        echo -e "${RED}解压 ONNXRuntime 失败${NC}"
        exit 1
    fi
    
    mv onnxruntime-linux-x64-${ONNX_VERSION} onnxruntime
    rm onnxruntime.tgz
    
    # 验证安装
    if [ ! -f "$ONNX_DIR/lib/libonnxruntime.so" ]; then
        echo -e "${RED}ONNXRuntime 安装不完整: 缺少 libonnxruntime.so${NC}"
        exit 1
    else
        log "ONNXRuntime 成功安装到 $ONNX_DIR"
    fi
    
    cd - > /dev/null
else
    log "ONNXRuntime 已存在于 $ONNX_DIR"
fi

# 设置 ONNXRuntime 环境变量
ONNXRUNTIME_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../third_party/onnxruntime" && pwd)"
log_section "设置环境变量"
log "ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}"

# 更新shell配置文件
SHELL_CONFIG=""
if [ -f "$HOME/.bashrc" ]; then
    SHELL_CONFIG="$HOME/.bashrc"
elif [ -f "$HOME/.bash_profile" ]; then
    SHELL_CONFIG="$HOME/.bash_profile"
elif [ -f "$HOME/.zshrc" ]; then
    SHELL_CONFIG="$HOME/.zshrc"
fi

if [ -n "$SHELL_CONFIG" ]; then
    log "更新 shell 配置: $SHELL_CONFIG"
    
    # 检查是否已存在环境变量
    grep -q "ONNXRUNTIME_ROOT_DIR" "$SHELL_CONFIG" || {
        echo "# 零延迟YOLO系统环境变量" >> "$SHELL_CONFIG"
        echo "export ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}" >> "$SHELL_CONFIG"
        echo "export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:${ONNXRUNTIME_ROOT_DIR}/lib" >> "$SHELL_CONFIG"
    }
else
    log "无法找到适当的shell配置文件."
    log "请手动添加以下行到您的shell配置文件:"
    log "export ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}"
    log "export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:${ONNXRUNTIME_ROOT_DIR}/lib"
fi

# 安装 Web 服务依赖
log_section "安装Web服务依赖"
cd ../src/web
npm install --no-audit
if [ $? -ne 0 ]; then
    log "警告: npm 安装可能不完整"
else
    log "Web依赖已成功安装"
fi
cd - > /dev/null

# 创建必要的目录
log_section "创建项目目录结构"
mkdir -p ../build ../logs ../downloads ../configs ../bin ../models

# 复制默认配置文件
if [ ! -f "../configs/server.json" ] && [ -f "../configs/server.json.example" ]; then
    log "复制服务器配置示例..."
    cp ../configs/server.json.example ../configs/server.json
fi

if [ ! -f "../configs/client.json" ] && [ -f "../configs/client.json.example" ]; then
    log "复制客户端配置示例..."
    cp ../configs/client.json.example ../configs/client.json
fi

log_section "生成虚拟YOLO模型"
if [ -f "../scripts/generate_dummy_model.py" ]; then
    log "生成测试用模型..."
    if check_command python3; then
        python3 ../scripts/generate_dummy_model.py --output ../models/yolo_nano_cs16.onnx
        if [ $? -eq 0 ]; then
            log "成功生成测试用模型"
        else
            log "警告: 模型生成失败. 您需要手动生成或下载模型."
        fi
    else
        log "警告: 未找到Python3, 跳过模型生成."
    fi
fi

echo -e "${GREEN}===== 依赖安装完成 =====${NC}"
echo -e "${YELLOW}请运行以下命令更新环境变量:${NC}"
echo -e "${BLUE}source ${SHELL_CONFIG}${NC}"
echo -e "${YELLOW}或重新打开终端窗口${NC}"
echo -e "${GREEN}现在您可以运行 ./build.sh 来构建项目${NC}"