#!/bin/bash
# 文件: generate_model.sh
# 用途: 强制生成兼容的YOLO模型(opset版本9)

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 强制生成兼容的YOLO模型文件 =====${NC}"
PROJECT_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
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
    echo -e "${YELLOW}检查文件是否存在:${NC}"
    ls -la "$PROJECT_ROOT/scripts/"
    exit 1
fi

# 确保模型目录存在
mkdir -p "$PROJECT_ROOT/models"

# 安装必要的Python依赖
echo -e "${YELLOW}安装Python依赖...${NC}"
python3 -m pip install numpy onnx --quiet

# 强制删除现有模型文件
echo -e "${YELLOW}强制删除现有模型文件...${NC}"
rm -f "$MODEL_PATH"
if [ -f "$MODEL_PATH" ]; then
    echo -e "${RED}无法删除现有模型文件，尝试使用sudo${NC}"
    sudo rm -f "$MODEL_PATH"
fi

# 生成新模型 - 明确指定opset版本为9
echo -e "${YELLOW}生成新模型文件，使用ONNX opset版本9...${NC}"

# 显示generate_dummy_model.py的内容
echo -e "${BLUE}检查模型生成脚本内容:${NC}"
head -n 20 "$MODEL_SCRIPT"

# 运行模型生成命令并详细输出
echo -e "${YELLOW}执行模型生成命令...${NC}"
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
    
    # 确保opset版本为9或更低
    if model.opset_import[0].version > 9:
        print('警告: 模型opset版本仍然大于9!')
        exit(1)
except Exception as e:
    print(f'错误: {e}')
    exit(1)
"
    
    # 检查Python脚本的退出状态
    if [ $? -ne 0 ]; then
        echo -e "${RED}模型验证失败!${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}模型生成完成！现在可以启动服务器了。${NC}"
else
    echo -e "${RED}模型生成失败！${NC}"
    exit 1
fi

# 检查模型文件权限
chmod 644 "$MODEL_PATH"
echo -e "${GREEN}已设置模型文件权限${NC}"