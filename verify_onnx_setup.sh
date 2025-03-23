#!/bin/bash
# 文件: verify_onnx_setup.sh
# 用途: 验证ONNX Runtime环境设置是否正确

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}===== ONNX Runtime 环境验证脚本 =====${NC}"

# 检查环境变量
echo -e "${YELLOW}检查环境变量...${NC}"
if [ -z "$ONNXRUNTIME_ROOT_DIR" ]; then
    echo -e "${RED}错误: ONNXRUNTIME_ROOT_DIR 环境变量未设置${NC}"
    echo -e "${YELLOW}请运行: source ~/.bashrc 或 source ~/.bash_profile${NC}"
    exit 1
else
    echo -e "${GREEN}✓ ONNXRUNTIME_ROOT_DIR 已设置: $ONNXRUNTIME_ROOT_DIR${NC}"
fi

# 检查目录结构
echo -e "${YELLOW}检查目录结构...${NC}"
if [ ! -d "$ONNXRUNTIME_ROOT_DIR" ]; then
    echo -e "${RED}错误: $ONNXRUNTIME_ROOT_DIR 目录不存在${NC}"
    exit 1
else
    echo -e "${GREEN}✓ ONNXRUNTIME_ROOT_DIR 目录存在${NC}"
fi

if [ ! -d "$ONNXRUNTIME_ROOT_DIR/include" ]; then
    echo -e "${RED}错误: $ONNXRUNTIME_ROOT_DIR/include 目录不存在${NC}"
    exit 1
else
    echo -e "${GREEN}✓ 包含目录存在${NC}"
fi

if [ ! -d "$ONNXRUNTIME_ROOT_DIR/lib" ]; then
    echo -e "${RED}错误: $ONNXRUNTIME_ROOT_DIR/lib 目录不存在${NC}"
    exit 1
else
    echo -e "${GREEN}✓ 库目录存在${NC}"
fi

# 检查头文件
echo -e "${YELLOW}检查头文件...${NC}"
HEADER_FILES=(
    "onnxruntime_cxx_api.h"
    "onnxruntime_c_api.h"
)

for header in "${HEADER_FILES[@]}"; do
    if [ ! -f "$ONNXRUNTIME_ROOT_DIR/include/$header" ]; then
        echo -e "${RED}错误: 头文件 $header 不存在${NC}"
        exit 1
    else
        echo -e "${GREEN}✓ 头文件 $header 存在${NC}"
    fi
done

# 检查库文件
echo -e "${YELLOW}检查库文件...${NC}"
LIB_FILES=(
    "libonnxruntime.so"
    "libonnxruntime.dylib"
    "onnxruntime.lib"
    "onnxruntime.dll"
)

LIB_FOUND=false
for lib in "${LIB_FILES[@]}"; do
    if [ -f "$ONNXRUNTIME_ROOT_DIR/lib/$lib" ]; then
        echo -e "${GREEN}✓ 找到库文件: $lib${NC}"
        LIB_FOUND=true
    fi
done

if [ "$LIB_FOUND" = false ]; then
    echo -e "${RED}错误: 未找到ONNX Runtime库文件${NC}"
    exit 1
fi

# 检查LD_LIBRARY_PATH
echo -e "${YELLOW}检查 LD_LIBRARY_PATH...${NC}"
if [[ ":$LD_LIBRARY_PATH:" != *":$ONNXRUNTIME_ROOT_DIR/lib:"* ]]; then
    echo -e "${YELLOW}警告: $ONNXRUNTIME_ROOT_DIR/lib 不在 LD_LIBRARY_PATH 中${NC}"
    echo -e "${YELLOW}建议运行: export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:$ONNXRUNTIME_ROOT_DIR/lib${NC}"
else
    echo -e "${GREEN}✓ LD_LIBRARY_PATH 包含 ONNX Runtime 库路径${NC}"
fi

# 检查项目include目录
echo -e "${YELLOW}检查项目include目录...${NC}"
PROJECT_ROOT=$(pwd)
if [ ! -d "$PROJECT_ROOT/include" ]; then
    echo -e "${YELLOW}警告: 项目include目录不存在，创建中...${NC}"
    mkdir -p "$PROJECT_ROOT/include"
fi

# 创建符号链接
echo -e "${YELLOW}创建头文件符号链接...${NC}"
for header in "${HEADER_FILES[@]}"; do
    ln -sf "$ONNXRUNTIME_ROOT_DIR/include/$header" "$PROJECT_ROOT/include/$header"
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ 成功创建符号链接: $header${NC}"
    else
        echo -e "${RED}错误: 无法创建符号链接: $header${NC}"
    fi
done

echo -e "${GREEN}===== ONNX Runtime 环境验证完成! =====${NC}"
echo -e "${GREEN}您的环境看起来已正确配置${NC}"
echo -e "${BLUE}现在您可以运行: ./deploy_backend.sh${NC}"
