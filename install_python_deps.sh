#!/bin/bash
# 文件位置: [项目根目录]/install_python_deps.sh
# 用途: 安装生成模型所需的Python依赖

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 安装Python模型依赖 =====${NC}"

# 检查Python版本
PYTHON_CMD=""
if command -v python3 &>/dev/null; then
    PYTHON_CMD="python3"
elif command -v python &>/dev/null; then
    PYTHON_CMD="python"
else
    echo -e "${RED}错误: 未找到Python解释器${NC}"
    exit 1
fi

PYTHON_VERSION=$($PYTHON_CMD --version 2>&1 | awk '{print $2}')
echo -e "${BLUE}使用Python版本: ${PYTHON_VERSION}${NC}"

# 检查pip
PIP_CMD=""
if command -v pip3 &>/dev/null; then
    PIP_CMD="pip3"
elif command -v pip &>/dev/null; then
    PIP_CMD="pip"
else
    echo -e "${YELLOW}未找到pip，尝试安装...${NC}"
    $PYTHON_CMD -m ensurepip --upgrade || {
        echo -e "${RED}无法安装pip，请手动安装pip后重试${NC}"
        exit 1
    }
    PIP_CMD="$PYTHON_CMD -m pip"
fi

# 升级pip
echo -e "${BLUE}升级pip...${NC}"
$PIP_CMD install --upgrade pip

# 安装必要的依赖
echo -e "${BLUE}安装模型生成所需的依赖...${NC}"
$PIP_CMD install numpy onnx onnxruntime protobuf || {
    echo -e "${RED}安装依赖失败${NC}"
    exit 1
}

# 可选依赖，用于更高级的模型处理
echo -e "${BLUE}安装可选依赖...${NC}"
$PIP_CMD install opencv-python-headless pillow || {
    echo -e "${YELLOW}警告: 安装可选依赖失败，但不影响基本功能${NC}"
}

echo -e "${GREEN}依赖安装完成!${NC}"
echo -e "${BLUE}现在您可以运行模型生成脚本:${NC}"
echo -e "${YELLOW}  python3 scripts/generate_dummy_model.py --output models/yolo_nano_cs16.onnx${NC}"