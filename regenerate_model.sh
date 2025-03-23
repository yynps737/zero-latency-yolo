#!/bin/bash
# 文件名: regenerate_model.sh
# 用途: 强制重新生成模型文件

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 重新生成YOLO模型文件 =====${NC}"

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$SCRIPT_DIR"
MODEL_PATH="$PROJECT_ROOT/models/yolo_nano_cs16.onnx"
MODEL_SCRIPT="$PROJECT_ROOT/scripts/generate_dummy_model.py"

# 检查Python3是否可用
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}错误: 未找到Python3${NC}"
    exit 1
fi

# 检查生成脚本是否存在
if [ ! -f "$MODEL_SCRIPT" ]; then
    echo -e "${RED}错误: 找不到模型生成脚本: $MODEL_SCRIPT${NC}"
    exit 1
fi

# 强制删除现有模型文件
echo -e "${YELLOW}删除现有模型文件...${NC}"
rm -f "$MODEL_PATH"

# 确保模型目录存在
mkdir -p "$PROJECT_ROOT/models"

# 安装必要的Python依赖
echo -e "${YELLOW}安装Python依赖...${NC}"
python3 -m pip install numpy onnx --quiet

# 生成新模型 - 明确指定opset版本为9
echo -e "${YELLOW}生成新模型文件，使用ONNX opset版本9...${NC}"
python3 "$MODEL_SCRIPT" --output "$MODEL_PATH" --opset 9

# 验证模型是否已创建
if [ -f "$MODEL_PATH" ]; then
    echo -e "${GREEN}成功生成模型文件: $MODEL_PATH${NC}"
    
    # 打印ONNX版本信息
    echo -e "${YELLOW}检查ONNX模型信息...${NC}"
    python3 -c "
import onnx
try:
    model = onnx.load('$MODEL_PATH')
    print(f'模型IR版本: {model.ir_version}')
    print(f'模型opset版本: {model.opset_import[0].version}')
    print(f'模型生产者: {model.producer_name}')
    print('模型验证: ', end='')
    onnx.checker.check_model(model)
    print('通过')
except Exception as e:
    print(f'错误: {e}')
"
    echo -e "${GREEN}模型重新生成完成！现在可以启动服务器了。${NC}"
else
    echo -e "${RED}模型生成失败！${NC}"
    exit 1
fi