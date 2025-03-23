#!/bin/bash

# 零延迟YOLO FPS云辅助系统构建脚本
# 此脚本用于构建服务器端和客户端

# 严格错误检查
set -e

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 清理选项
if [ "$1" == "clean" ]; then
    echo -e "${YELLOW}清理构建目录...${NC}"
    rm -rf build
    echo -e "${GREEN}清理完成${NC}"
    exit 0
fi

# 检查关键依赖
echo -e "${YELLOW}检查构建依赖...${NC}"
command -v cmake >/dev/null 2>&1 || { echo -e "${RED}错误: 需要安装 cmake${NC}"; exit 1; }
command -v make >/dev/null 2>&1 || { echo -e "${RED}错误: 需要安装 make${NC}"; exit 1; }
command -v g++ >/dev/null 2>&1 || { echo -e "${RED}错误: 需要安装 g++${NC}"; exit 1; }

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
        wget -q https://github.com/microsoft/onnxruntime/releases/download/v1.15.1/onnxruntime-linux-x64-1.15.1.tgz
        if [ $? -ne 0 ]; then
            echo -e "${RED}下载ONNX运行时失败，请检查网络连接${NC}"
            exit 1
        fi
        
        tar -xzf onnxruntime-linux-x64-1.15.1.tgz
        if [ $? -ne 0 ]; then
            echo -e "${RED}解压ONNX运行时失败${NC}"
            exit 1
        fi
        
        mv onnxruntime-linux-x64-1.15.1 onnxruntime
        rm onnxruntime-linux-x64-1.15.1.tgz
    fi
    
    cd ../build
fi

# 检查ONNX安装
if [ ! -d "$ONNX_DIR" ]; then
    echo -e "${RED}错误: ONNX运行时目录不存在: $ONNX_DIR${NC}"
    echo -e "${YELLOW}请确保已下载并解压ONNX运行时到正确位置${NC}"
    exit 1
fi

# 确保配置目录存在
mkdir -p ../configs

# 检查配置文件是否存在，不存在则复制示例
if [ ! -f "../configs/server.json" ]; then
    if [ -f "../configs/server.json.example" ]; then
        echo -e "${YELLOW}复制服务器配置示例...${NC}"
        cp ../configs/server.json.example ../configs/server.json
    fi
fi

if [ ! -f "../configs/client.json" ]; then
    if [ -f "../configs/client.json.example" ]; then
        echo -e "${YELLOW}复制客户端配置示例...${NC}"
        cp ../configs/client.json.example ../configs/client.json
    fi
fi

# 构建项目
echo -e "${YELLOW}配置CMake...${NC}"
cmake .. $CMAKE_FLAGS
if [ $? -ne 0 ]; then
    echo -e "${RED}CMake配置失败${NC}"
    exit 1
fi

echo -e "${YELLOW}编译项目...${NC}"
NPROC=$(nproc 2>/dev/null || echo 2)
make -j$NPROC
if [ $? -ne 0 ]; then
    echo -e "${RED}构建失败!${NC}"
    exit 1
fi

# 创建目录结构
echo -e "${YELLOW}创建目录结构...${NC}"
mkdir -p bin logs downloads

# 复制二进制文件
echo -e "${YELLOW}复制二进制文件...${NC}"
if [ -f "src/server/yolo_server" ]; then
    cp src/server/yolo_server ../bin/
fi

if [ -f "src/client/yolo_client" ]; then
    cp src/client/yolo_client ../bin/
fi

# 构建Web服务器
echo -e "${YELLOW}设置Web服务器...${NC}"
cd ../src/web
npm install --no-audit --silent
if [ $? -ne 0 ]; then
    echo -e "${YELLOW}警告: npm安装失败，Web界面可能无法正常工作${NC}"
else
    echo -e "${GREEN}Web服务器设置完成${NC}"
fi

cd ../../

# 创建模型目录
mkdir -p models

echo -e "${GREEN}===== 构建完成 =====${NC}"
echo -e "${GREEN}服务器二进制文件: ${SCRIPT_DIR}/bin/yolo_server${NC}"

if [ "$SYSTEM" == "Linux" ]; then
    echo -e "${GREEN}客户端二进制文件: ${SCRIPT_DIR}/bin/yolo_client${NC}"
fi

echo -e "${GREEN}Web服务器: ${SCRIPT_DIR}/src/web (使用 npm start 启动)${NC}"
echo -e "${YELLOW}现在可以通过运行 ./bin/yolo_server 启动服务器${NC}"