#!/bin/bash
# 文件位置: [项目根目录]/scripts/setup_environment.sh
# 用途: 修复ONNX Runtime路径并准备编译环境

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 修复编译环境脚本 =====${NC}"
echo -e "${BLUE}此脚本将修复ONNX Runtime路径和编译环境${NC}"

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# 确保必要的目录存在
mkdir -p "$PROJECT_ROOT/include"

# 检查ONNX Runtime环境变量
if [ -z "$ONNXRUNTIME_ROOT_DIR" ]; then
    echo -e "${YELLOW}警告: ONNXRUNTIME_ROOT_DIR 环境变量未设置${NC}"
    if [ -d "$PROJECT_ROOT/third_party/onnxruntime" ]; then
        export ONNXRUNTIME_ROOT_DIR="$PROJECT_ROOT/third_party/onnxruntime"
        echo -e "${GREEN}自动设置 ONNXRUNTIME_ROOT_DIR=$ONNXRUNTIME_ROOT_DIR${NC}"
    else
        echo -e "${RED}错误: 未找到ONNX Runtime安装目录${NC}"
        exit 1
    fi
fi

# 创建到ONNX Runtime头文件的符号链接
echo -e "${BLUE}创建ONNX Runtime头文件链接...${NC}"
if [ -d "$ONNXRUNTIME_ROOT_DIR/include" ]; then
    # 链接所有ONNX Runtime头文件到项目的include目录
    if [ -f "$ONNXRUNTIME_ROOT_DIR/include/onnxruntime_cxx_api.h" ]; then
        ln -sf "$ONNXRUNTIME_ROOT_DIR/include/onnxruntime_cxx_api.h" "$PROJECT_ROOT/include/"
        echo -e "${GREEN}链接了 onnxruntime_cxx_api.h${NC}"
    else
        echo -e "${RED}错误: 在 $ONNXRUNTIME_ROOT_DIR/include 中找不到 onnxruntime_cxx_api.h${NC}"
    fi
    
    # 链接其他可能需要的头文件
    for header in "$ONNXRUNTIME_ROOT_DIR/include"/*.h; do
        if [ -f "$header" ]; then
            filename=$(basename "$header")
            ln -sf "$header" "$PROJECT_ROOT/include/$filename"
            echo -e "${BLUE}链接了 $filename${NC}"
        fi
    done
    
    # 创建ONNXRuntime目录结构
    mkdir -p "$PROJECT_ROOT/include/onnxruntime/core/session"
    if [ -f "$ONNXRUNTIME_ROOT_DIR/include/onnxruntime_cxx_api.h" ]; then
        ln -sf "$ONNXRUNTIME_ROOT_DIR/include/onnxruntime_cxx_api.h" "$PROJECT_ROOT/include/onnxruntime/core/session/"
        echo -e "${GREEN}创建了标准路径结构的 onnxruntime_cxx_api.h 链接${NC}"
    fi
else
    echo -e "${RED}错误: $ONNXRUNTIME_ROOT_DIR/include 目录不存在${NC}"
    exit 1
fi

# 修复config.h中的引入问题
echo -e "${BLUE}检查 config.h 文件...${NC}"
CONFIG_H_FILE="$PROJECT_ROOT/src/server/config.h"
if [ -f "$CONFIG_H_FILE" ]; then
    # 确保包含了types.h
    if ! grep -q "#include \"../common/types.h\"" "$CONFIG_H_FILE"; then
        echo -e "${YELLOW}修复 config.h 中的包含路径...${NC}"
        # 备份原文件
        cp "$CONFIG_H_FILE" "${CONFIG_H_FILE}.bak"
        
        # 在第一个include下添加types.h
        sed -i '/#include/a #include "../common/types.h"' "$CONFIG_H_FILE"
    fi
else
    echo -e "${RED}错误: 找不到 $CONFIG_H_FILE${NC}"
fi

# 生成一个测试模型 (如果需要)
if [ ! -d "$PROJECT_ROOT/models" ] || [ -z "$(find "$PROJECT_ROOT/models" -name "*.onnx" 2>/dev/null)" ]; then
    echo -e "${YELLOW}未检测到模型文件，尝试生成测试模型...${NC}"
    
    # 检查Python和相关依赖
    if command -v python3 &>/dev/null; then
        echo -e "${BLUE}安装必要的Python依赖...${NC}"
        python3 -m pip install numpy onnx
        
        # 运行模型生成脚本
        if [ -f "$PROJECT_ROOT/scripts/generate_dummy_model.py" ]; then
            mkdir -p "$PROJECT_ROOT/models"
            
            # 修复ModelProto.metadata_props问题
            SCRIPT_FILE="$PROJECT_ROOT/scripts/generate_dummy_model.py"
            if grep -q "helper.make_metadata_props" "$SCRIPT_FILE"; then
                echo -e "${YELLOW}修复模型生成脚本中的API调用...${NC}"
                sed -i 's/helper.make_metadata_props/helper.make_attribute/g' "$SCRIPT_FILE"
            fi
            
            python3 "$SCRIPT_FILE" --output "$PROJECT_ROOT/models/yolo_nano_cs16.onnx"
            if [ $? -eq 0 ]; then
                echo -e "${GREEN}成功生成测试模型${NC}"
            else
                echo -e "${RED}模型生成失败${NC}"
            fi
        else
            echo -e "${RED}找不到模型生成脚本${NC}"
        fi
    else
        echo -e "${RED}未安装Python3，无法生成测试模型${NC}"
    fi
fi

echo -e "${GREEN}环境修复完成!${NC}"
echo -e "${YELLOW}您现在可以尝试重新编译项目:${NC}"
echo -e "${BLUE}cd $PROJECT_ROOT && mkdir -p build && cd build && cmake .. && make${NC}"