#!/usr/bin/env python3
"""
零延迟YOLO FPS云辅助系统 - 测试用模型生成脚本
此脚本用于生成一个简单的测试用ONNX模型，用于系统调试
注意：此脚本生成的模型仅用于测试，不具备实际检测能力
"""

import os
import argparse
import numpy as np
import onnx
from onnx import helper, TensorProto, numpy_helper

def parse_args():
    """解析命令行参数"""
    parser = argparse.ArgumentParser(description='生成测试用ONNX模型')
    parser.add_argument('--output', type=str, default='models/yolo_nano_cs16.onnx',
                        help='输出ONNX模型文件路径')
    parser.add_argument('--img-size', type=int, default=416,
                        help='输入图像尺寸(默认: 416)')
    parser.add_argument('--quantize', action='store_true',
                        help='是否生成INT8量化模型')
    return parser.parse_args()

def create_dummy_yolo_model(output_path, img_size=416, quantize=False):
    """创建一个简单的测试用ONNX模型，模拟YOLO的输入输出结构"""
    # 定义输入
    input_shape = [1, 3, img_size, img_size]  # [batch, channels, height, width]
    output_shape = [1, 100, 85]  # [batch, num_detections, (x,y,w,h,conf,class...)]
    
    # 创建输入节点
    input_tensor = helper.make_tensor_value_info(
        name='input',
        elem_type=TensorProto.FLOAT,
        shape=input_shape
    )
    
    # 创建输出节点
    output_tensor = helper.make_tensor_value_info(
        name='output',
        elem_type=TensorProto.FLOAT,
        shape=output_shape
    )
    
    # 创建权重，用于Conv操作
    # 生成简单的卷积核，3x3，输入通道数=3，输出通道数=16
    conv1_weight_data = np.random.randn(16, 3, 3, 3).astype(np.float32) * 0.1
    if quantize:
        # 将权重量化到INT8范围
        conv1_weight_data = np.clip(conv1_weight_data * 127, -127, 127).astype(np.int8)
        conv1_weight_data = conv1_weight_data.astype(np.float32) / 127.0
    
    conv1_weight = helper.make_tensor(
        name='conv1_weight',
        data_type=TensorProto.FLOAT,
        dims=[16, 3, 3, 3],
        vals=conv1_weight_data.flatten().tolist()
    )
    
    # 创建Constant节点提供卷积权重
    constant_node = helper.make_node(
        'Constant',
        inputs=[],
        outputs=['conv1_weight_tensor'],
        value=conv1_weight
    )
    
    # 创建Conv节点 - 将图像转换为特征图
    conv_node = helper.make_node(
        'Conv',
        inputs=['input', 'conv1_weight_tensor'],
        outputs=['features'],
        kernel_shape=[3, 3],
        pads=[1, 1, 1, 1],
        strides=[2, 2]
    )
    
    # 创建GlobalAveragePool节点 - 将特征图转为全局特征
    gap_node = helper.make_node(
        'GlobalAveragePool',
        inputs=['features'],
        outputs=['global_features']
    )
    
    # 创建Flatten节点 - 将全局特征展平
    flatten_node = helper.make_node(
        'Flatten',
        inputs=['global_features'],
        outputs=['flattened_features'],
        axis=1
    )
    
    # 创建Shape节点 - 获取输入形状
    shape_node = helper.make_node(
        'Shape',
        inputs=['input'],
        outputs=['input_shape']
    )
    
    # 创建Gather节点 - 获取批次大小
    gather_node = helper.make_node(
        'Gather',
        inputs=['input_shape', 'zero_index'],
        outputs=['batch_size'],
        axis=0
    )
    
    # 创建Constant节点提供索引0
    zero_tensor = helper.make_tensor(
        name='zero_tensor',
        data_type=TensorProto.INT64,
        dims=[1],
        vals=[0]
    )
    
    zero_index_node = helper.make_node(
        'Constant',
        inputs=[],
        outputs=['zero_index'],
        value=zero_tensor
    )
    
    # 创建Reshape节点 - 将特征整形为YOLO输出格式
    reshape_node = helper.make_node(
        'Reshape',
        inputs=['flattened_features', 'output_shape_tensor'],
        outputs=['output']
    )
    
    # 创建Constant节点提供输出形状
    output_shape_tensor = helper.make_tensor(
        name='output_shape_tensor_value',
        data_type=TensorProto.INT64,
        dims=[3],
        vals=[-1, 100, 85]  # -1表示动态批次大小
    )
    
    output_shape_node = helper.make_node(
        'Constant',
        inputs=[],
        outputs=['output_shape_tensor'],
        value=output_shape_tensor
    )
    
    # 创建图
    graph = helper.make_graph(
        nodes=[
            zero_index_node, constant_node, conv_node, gap_node, flatten_node,
            shape_node, gather_node, output_shape_node, reshape_node
        ],
        name='dummy_yolo',
        inputs=[input_tensor],
        outputs=[output_tensor],
        initializer=[]
    )
    
    # 创建模型
    model = helper.make_model(
        graph,
        producer_name='ZeroLatencyYOLO',
        opset_imports=[helper.make_opsetid("", 12)]
    )
    
    # 检查模型
    onnx.checker.check_model(model)
    
    # 保存模型
    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    onnx.save(model, output_path)
    print(f"测试模型已保存到: {output_path}")

def main():
    args = parse_args()
    create_dummy_yolo_model(args.output, args.img_size, args.quantize)
    print("模型信息:")
    print(f"  - 输入尺寸: {args.img_size}x{args.img_size}")
    print(f"  - 量化: {'INT8' if args.quantize else '浮点数'}")
    print(f"  - 注意: 此模型仅用于测试，不具备实际检测能力")
    print("如需使用实际模型，请通过scripts/convert_model.py转换YOLO模型")

if __name__ == '__main__':
    main()