#!/bin/bash

# 零延迟YOLO FPS云辅助系统构建脚本
# 此脚本用于构建服务器端和客户端

set -e

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统构建脚本 =====${NC}"

# 获取脚本所在目录路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "$SCRIPT_DIR"

# 创建构建目录
mkdir -p build
cd build

# 检查系统
SYSTEM=$(uname -s)
if [ "$SYSTEM" == "Linux" ]; then
    echo -e "${YELLOW}在Linux系统上构建...${NC}"
    CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release"
    
    # 检查是否有CUDA
    if [ -x "$(command -v nvidia-smi)" ]; then
        echo -e "${YELLOW}检测到NVIDIA GPU，添加CUDA支持...${NC}"
        CMAKE_FLAGS="$CMAKE_FLAGS -DUSE_CUDA=ON"
    else
        echo -e "${YELLOW}未检测到NVIDIA GPU，使用CPU模式...${NC}"
    fi
    
elif [ "$SYSTEM" == "Darwin" ]; then
    echo -e "${RED}macOS不受支持，仅可构建服务器组件${NC}"
    CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=Release -DBUILD_CLIENT=OFF"
else
    echo -e "${RED}不支持的操作系统: $SYSTEM${NC}"
    exit 1
fi

# 下载ONNX运行时（如果不存在）
ONNX_DIR="../third_party/onnxruntime"
if [ ! -d "$ONNX_DIR" ]; then
    echo -e "${YELLOW}下载ONNX运行时...${NC}"
    mkdir -p ../third_party
    cd ../third_party
    
    if [ "$SYSTEM" == "Linux" ]; then
        wget https://github.com/microsoft/onnxruntime/releases/download/v1.15.1/onnxruntime-linux-x64-1.15.1.tgz
        tar -xzf onnxruntime-linux-x64-1.15.1.tgz
        mv onnxruntime-linux-x64-1.15.1 onnxruntime
        rm onnxruntime-linux-x64-1.15.1.tgz
    fi
    
    cd ../build
fi

# 构建项目
echo -e "${YELLOW}配置CMake...${NC}"
cmake .. $CMAKE_FLAGS

echo -e "${YELLOW}编译项目...${NC}"
make -j$(nproc)

if [ $? -eq 0 ]; then
    echo -e "${GREEN}构建成功!${NC}"
    
    # 构建Web服务器
    echo -e "${YELLOW}设置Web服务器...${NC}"
    cd ../src/web
    npm install
    
    echo -e "${GREEN}===== 构建完成 =====${NC}"
    echo -e "${GREEN}服务器二进制文件: ${SCRIPT_DIR}/build/src/server/yolo_server${NC}"
    
    if [ "$SYSTEM" == "Linux" ]; then
        echo -e "${GREEN}客户端二进制文件: ${SCRIPT_DIR}/build/src/client/yolo_client${NC}"
    fi
    
    echo -e "${GREEN}Web服务器: ${SCRIPT_DIR}/src/web (使用 npm start 启动)${NC}"
else
    echo -e "${RED}构建失败!${NC}"
    exit 1
fi