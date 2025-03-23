#!/bin/bash

# 零延迟YOLO FPS云辅助系统依赖安装脚本
# 此脚本用于安装服务器需要的所有依赖

set -e

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统依赖安装脚本 =====${NC}"

# 检查系统类型
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    VERSION=$VERSION_ID
else
    echo -e "${RED}无法确定操作系统类型${NC}"
    exit 1
fi

echo -e "${YELLOW}检测到操作系统: ${OS} ${VERSION}${NC}"

# 安装基本依赖
echo -e "${YELLOW}正在安装基本依赖...${NC}"

# Ubuntu/Debian系统
if [[ "$OS" == "ubuntu" || "$OS" == "debian" ]]; then
    apt-get update
    apt-get install -y build-essential cmake git wget curl unzip \
                       libssl-dev zlib1g-dev \
                       nodejs npm
    
    # 安装 ONNXRuntime 依赖
    apt-get install -y libgomp1
# CentOS/RHEL系统
elif [[ "$OS" == "centos" || "$OS" == "rhel" ]]; then
    yum update -y
    yum groupinstall -y "Development Tools"
    yum install -y cmake git wget curl unzip \
                   openssl-devel zlib-devel \
                   nodejs npm
    
    # 安装 ONNXRuntime 依赖
    yum install -y libgomp
# Alpine系统
elif [[ "$OS" == "alpine" ]]; then
    apk update
    apk add build-base cmake git wget curl unzip \
            openssl-dev zlib-dev \
            nodejs npm
    
    # 安装 ONNXRuntime 依赖
    apk add libgomp
else
    echo -e "${RED}不支持的操作系统: ${OS}${NC}"
    exit 1
fi

# 下载 ONNXRuntime
echo -e "${YELLOW}正在下载 ONNXRuntime...${NC}"
mkdir -p third_party
cd third_party

ONNX_VERSION="1.15.1"
ONNX_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/onnxruntime-linux-x64-${ONNX_VERSION}.tgz"

if [ ! -d "onnxruntime" ]; then
    wget -q ${ONNX_URL} -O onnxruntime.tgz
    tar -xzf onnxruntime.tgz
    mv onnxruntime-linux-x64-${ONNX_VERSION} onnxruntime
    rm onnxruntime.tgz
fi

cd ..

# 设置 ONNXRuntime 环境变量
export ONNXRUNTIME_ROOT_DIR="$(pwd)/third_party/onnxruntime"
echo "export ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}" >> ~/.bashrc
echo "export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:${ONNXRUNTIME_ROOT_DIR}/lib" >> ~/.bashrc

# 安装 Web 服务依赖
echo -e "${YELLOW}正在安装 Web 服务依赖...${NC}"
cd src/web
npm install
cd ../..

# 创建必要的目录
echo -e "${YELLOW}正在创建必要的目录...${NC}"
mkdir -p build
mkdir -p logs
mkdir -p downloads
mkdir -p configs

# 复制默认配置文件
echo -e "${YELLOW}正在复制默认配置文件...${NC}"
cp -n configs/server.json.example configs/server.json 2>/dev/null || true
cp -n configs/client.json.example configs/client.json 2>/dev/null || true

echo -e "${GREEN}===== 依赖安装完成 =====${NC}"
echo -e "${YELLOW}请运行 'source ~/.bashrc' 更新环境变量${NC}"