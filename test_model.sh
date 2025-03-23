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
