#!/bin/bash
# 文件: quick_fix.sh
# 用途: 快速修复模型文件

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 快速修复模型文件 =====${NC}"
PROJECT_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# 创建修复后的模型生成脚本
MODEL_SCRIPT="$PROJECT_ROOT/scripts/generate_dummy_model.py"
echo -e "${YELLOW}创建修复后的模型生成脚本...${NC}"

# 备份原始脚本
if [ -f "$MODEL_SCRIPT" ]; then
    cp "$MODEL_SCRIPT" "${MODEL_SCRIPT}.bak"
fi

# 创建新脚本
cat > "$MODEL_SCRIPT" << 'EOF'
#!/usr/bin/env python3
# 文件位置: [项目根目录]/scripts/generate_dummy_model.py
# 用途: 生成一个简单的YOLO模型用于测试

import os
import sys
import argparse
import numpy as np
import onnx
from onnx import helper, TensorProto, numpy_helper

def parse_args():
    parser = argparse.ArgumentParser(description='生成测试用YOLO ONNX模型')
    parser.add_argument('--output', type=str, default='models/yolo_nano_cs16.onnx',
                        help='输出模型文件路径')
    parser.add_argument('--input_shape', type=str, default='1,3,416,416',
                        help='输入形状，格式为 batch,channels,height,width')
    parser.add_argument('--num_classes', type=int, default=80,
                        help='类别数量')
    parser.add_argument('--opset', type=int, default=9,
                        help='ONNX opset版本，最大支持9')
    return parser.parse_args()

def create_dummy_yolo_model(output_path, input_shape, num_classes, opset_version=9):
    # 确保opset版本不超过9
    if opset_version > 9:
        print(f"警告: 指定的opset版本 {opset_version} 超过最大支持的9版本，将使用版本9")
        opset_version = 9
        
    # 解析输入形状
    input_shape = [int(dim) for dim in input_shape.split(',')]
    batch, channels, height, width = input_shape
    
    # 创建模型输入
    input_tensor = helper.make_tensor_value_info(
        'input', TensorProto.FLOAT, input_shape)
    
    # 创建卷积层权重
    conv_weight_shape = [16, channels, 3, 3]  # 16个3x3过滤器
    conv_weight_data = np.random.randn(*conv_weight_shape).astype(np.float32)
    conv_weight = numpy_helper.from_array(
        conv_weight_data, name='conv_weight')
    
    # 创建卷积层偏置
    conv_bias_shape = [16]
    conv_bias_data = np.random.randn(*conv_bias_shape).astype(np.float32)
    conv_bias = numpy_helper.from_array(
        conv_bias_data, name='conv_bias')
    
    # 创建最终预测层
    # YOLO 输出 [(5 + num_classes) * 3] 通道 - 3个锚框，每个有5个边界框参数+类别数
    output_channels = (5 + num_classes) * 3
    output_weight_shape = [output_channels, 16, 1, 1]
    output_weight_data = np.random.randn(*output_weight_shape).astype(np.float32) * 0.1
    output_weight = numpy_helper.from_array(
        output_weight_data, name='output_weight')
    
    output_bias_shape = [output_channels]
    output_bias_data = np.random.randn(*output_bias_shape).astype(np.float32) * 0.1
    output_bias = numpy_helper.from_array(
        output_bias_data, name='output_bias')
    
    # 计算输出尺寸 (简化版，假设下采样到输入的1/8)
    output_height, output_width = height // 8, width // 8
    
    # 创建模型输出
    output_shape = [batch, output_channels, output_height, output_width]
    output_tensor = helper.make_tensor_value_info(
        'output', TensorProto.FLOAT, output_shape)
    
    # 创建节点
    nodes = [
        # 第一个卷积层
        helper.make_node(
            'Conv',
            inputs=['input', 'conv_weight', 'conv_bias'],
            outputs=['conv1'],
            kernel_shape=[3, 3],
            pads=[1, 1, 1, 1],
            strides=[2, 2],
            name='conv1'
        ),
        # ReLU激活
        helper.make_node(
            'Relu',
            inputs=['conv1'],
            outputs=['relu1'],
            name='relu1'
        ),
        # 输出卷积层
        helper.make_node(
            'Conv',
            inputs=['relu1', 'output_weight', 'output_bias'],
            outputs=['output'],
            kernel_shape=[1, 1],
            pads=[0, 0, 0, 0],
            strides=[1, 1],
            name='output_conv'
        )
    ]
    
    # 创建计算图
    graph = helper.make_graph(
        nodes=nodes,
        name='YOLOTest',
        inputs=[input_tensor],
        outputs=[output_tensor],
        initializer=[conv_weight, conv_bias, output_weight, output_bias]
    )
    
    # 创建模型 - 使用指定的opset版本
    model = helper.make_model(
        graph,
        producer_name='ZeroLatencyYOLO',
        opset_imports=[helper.make_opsetid('', opset_version)]
    )
    
    # 添加模型元数据 - 直接使用StringStringEntryProto
    model.metadata_props.append(onnx.StringStringEntryProto(key="framework", value="onnx"))
    model.metadata_props.append(onnx.StringStringEntryProto(key="model_type", value="yolo"))
    model.metadata_props.append(onnx.StringStringEntryProto(key="num_classes", value=str(num_classes)))

    # 检查模型正确性
    onnx.checker.check_model(model)
    
    # 确保输出目录存在
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    
    # 保存模型
    onnx.save(model, output_path)
    print(f"模型已保存至: {output_path}")
    print(f"输入形状: {input_shape}")
    print(f"输出形状: {output_shape}")
    print(f"ONNX opset版本: {opset_version}")
    print(f"IR版本: {model.ir_version}")

if __name__ == '__main__':
    args = parse_args()
    create_dummy_yolo_model(args.output, args.input_shape, args.num_classes, args.opset)
    print("测试模型生成成功!")
EOF

chmod +x "$MODEL_SCRIPT"
echo -e "${GREEN}已创建修复后的模型生成脚本${NC}"

# 安装Python依赖
echo -e "${BLUE}安装Python依赖...${NC}"
python3 -m pip install numpy onnx --quiet

# 删除现有模型
MODEL_PATH="$PROJECT_ROOT/models/yolo_nano_cs16.onnx"
if [ -f "$MODEL_PATH" ]; then
    echo -e "${YELLOW}删除现有模型文件...${NC}"
    rm -f "$MODEL_PATH"
fi

# 确保模型目录存在
mkdir -p "$PROJECT_ROOT/models"

# 生成新模型
echo -e "${BLUE}生成兼容的模型文件...${NC}"
python3 "$MODEL_SCRIPT" --output "$MODEL_PATH" --opset 9

# 验证生成的模型
if [ -f "$MODEL_PATH" ]; then
    echo -e "${GREEN}模型生成成功！${NC}"
    echo -e "${BLUE}验证模型...${NC}"
    
    python3 -c "
import onnx
try:
    model = onnx.load('$MODEL_PATH')
    print(f'模型IR版本: {model.ir_version}')
    print(f'模型opset版本: {model.opset_import[0].version}')
    if model.opset_import[0].version <= 9:
        print('模型版本正确 ✓')
    else:
        print('错误: 模型版本仍然 > 9')
except Exception as e:
    print(f'错误: {e}')
"
    
    echo -e "${GREEN}修复完成！${NC}"
    echo -e "${YELLOW}现在可以启动服务器:${NC}"
    echo -e "${BLUE}./start.sh${NC}"
else
    echo -e "${RED}模型生成失败${NC}"
    exit 1
fi