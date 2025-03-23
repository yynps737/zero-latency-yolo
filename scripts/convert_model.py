#!/usr/bin/env python3
"""
零延迟YOLO FPS云辅助系统 - 模型转换脚本
此脚本用于将YOLO格式的权重文件转换为ONNX格式，并可选择进行INT8量化
"""

import os
import sys
import argparse
import numpy as np
import onnx
import onnxruntime as ort
from onnxruntime.quantization import quantize_dynamic, QuantType

def parse_args():
    """解析命令行参数"""
    parser = argparse.ArgumentParser(description='YOLO模型转换工具')
    parser.add_argument('--weights', type=str, required=True,
                        help='YOLO权重文件路径(.weights或.pt)')
    parser.add_argument('--cfg', type=str, 
                        help='YOLO配置文件路径(.cfg)')
    parser.add_argument('--output', type=str, required=True,
                        help='输出ONNX模型文件路径')
    parser.add_argument('--img-size', type=int, default=416,
                        help='输入图像尺寸(默认: 416)')
    parser.add_argument('--batch-size', type=int, default=1,
                        help='批处理大小(默认: 1)')
    parser.add_argument('--quantize', type=str, choices=['int8', 'none'], default='none',
                        help='是否进行INT8量化(默认: none)')
    parser.add_argument('--calibration-data', type=str, 
                        help='用于量化校准的数据目录')
    parser.add_argument('--opset', type=int, default=12,
                        help='ONNX opset版本(默认: 12)')
    parser.add_argument('--simplify', action='store_true',
                        help='使用ONNX-Simplifier简化模型')
    return parser.parse_args()

def convert_darknet_to_onnx(weights_path, cfg_path, output_path, img_size=416, batch_size=1, opset=12):
    """将Darknet格式的YOLO模型转换为ONNX格式"""
    try:
        import torch
        import torch.nn as nn
        from models.models import Darknet
    except ImportError:
        print("错误: 无法导入需要的依赖。请安装PyTorch和YOLOv3实现。")
        print("尝试: pip install torch>=1.7.0")
        sys.exit(1)
    
    print(f"正在加载Darknet模型: {weights_path}")
    model = Darknet(cfg_path, img_size)
    
    # 加载权重
    if weights_path.endswith('.weights'):
        model.load_darknet_weights(weights_path)
    else:
        model.load_state_dict(torch.load(weights_path, map_location='cpu')['model'])
    
    model.eval()
    
    # 创建输入
    x = torch.randn(batch_size, 3, img_size, img_size)
    
    # 导出到ONNX
    print(f"正在导出模型到ONNX: {output_path}")
    torch.onnx.export(model, x, output_path,
                      opset_version=opset,
                      input_names=['input'],
                      output_names=['output'],
                      dynamic_axes={'input': {0: 'batch_size'},
                                    'output': {0: 'batch_size'}})
    
    print("ONNX导出完成")
    return output_path

def convert_pytorch_to_onnx(weights_path, output_path, img_size=416, batch_size=1, opset=12):
    """将PyTorch格式的YOLO模型转换为ONNX格式"""
    try:
        # 尝试导入YOLOv5
        import torch
        from models.experimental import attempt_load
        from utils.general import check_img_size
    except ImportError:
        try:
            # 尝试导入YOLOv3
            import torch
            from models.models import Darknet
        except ImportError:
            print("错误: 无法导入需要的依赖。请安装YOLOv5或YOLOv3实现。")
            print("尝试: pip install torch>=1.7.0")
            sys.exit(1)
    
    print(f"正在加载PyTorch模型: {weights_path}")
    
    try:
        # 尝试使用YOLOv5加载
        model = attempt_load(weights_path)
        stride = int(model.stride.max())
        img_size = check_img_size(img_size, s=stride)
    except NameError:
        # 加载YOLOv3模型
        model = torch.load(weights_path, map_location='cpu')
        if isinstance(model, dict):
            model = model['model']
    
    model.eval()
    
    # 创建输入
    x = torch.randn(batch_size, 3, img_size, img_size)
    
    # 导出到ONNX
    print(f"正在导出模型到ONNX: {output_path}")
    torch.onnx.export(model, x, output_path,
                      opset_version=opset,
                      input_names=['input'],
                      output_names=['output'],
                      dynamic_axes={'input': {0: 'batch_size'},
                                    'output': {0: 'batch_size'}})
    
    print("ONNX导出完成")
    return output_path

def check_onnx_model(onnx_path):
    """检查ONNX模型是否有效"""
    try:
        onnx_model = onnx.load(onnx_path)
        onnx.checker.check_model(onnx_model)
        print("ONNX模型检查通过")
    except Exception as e:
        print(f"ONNX模型检查失败: {e}")
        sys.exit(1)
    return onnx_model

def simplify_onnx_model(onnx_path, output_path):
    """使用ONNX-Simplifier简化模型"""
    try:
        import onnxsim
    except ImportError:
        print("错误: 无法导入onnx-simplifier。请安装依赖:")
        print("pip install onnx-simplifier")
        return onnx_path
    
    print("正在简化ONNX模型...")
    model = onnx.load(onnx_path)
    model_simp, check = onnxsim.simplify(model)
    
    if check:
        onnx.save(model_simp, output_path)
        print(f"简化的模型已保存到: {output_path}")
        return output_path
    else:
        print("警告: 模型简化失败，使用原始模型")
        return onnx_path

def quantize_onnx_model(onnx_path, output_path, calibration_data_dir=None):
    """将ONNX模型量化为INT8"""
    print("正在进行INT8量化...")
    
    if calibration_data_dir and os.path.exists(calibration_data_dir):
        print(f"使用校准数据: {calibration_data_dir}")
        # 这里可以实现基于校准数据的量化
        # 本例中使用动态量化作为简化示例
    
    # 进行动态量化
    quantize_dynamic(onnx_path, output_path, weight_type=QuantType.QInt8)
    print(f"量化模型已保存到: {output_path}")
    
    return output_path

def test_onnx_model(onnx_path, img_size=416):
    """测试ONNX模型推理速度"""
    print(f"正在测试ONNX模型: {onnx_path}")
    
    try:
        # 创建推理会话
        session_options = ort.SessionOptions()
        session_options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
        session = ort.InferenceSession(onnx_path, session_options, providers=['CPUExecutionProvider'])
        
        # 获取输入名称
        input_name = session.get_inputs()[0].name
        
        # 创建随机输入
        dummy_input = np.random.rand(1, 3, img_size, img_size).astype(np.float32)
        
        # 预热
        for _ in range(5):
            session.run(None, {input_name: dummy_input})
        
        # 测量性能
        import time
        start_time = time.time()
        iterations = 20
        
        for _ in range(iterations):
            session.run(None, {input_name: dummy_input})
        
        end_time = time.time()
        inference_time = (end_time - start_time) / iterations * 1000  # 毫秒
        
        print(f"平均推理时间: {inference_time:.2f} ms")
        print(f"推理速度: {1000/inference_time:.2f} FPS")
    
    except Exception as e:
        print(f"测试模型时出错: {e}")

def main():
    args = parse_args()
    
    # 确保输出目录存在
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    
    # 中间文件路径
    temp_onnx = args.output
    if args.quantize != 'none' or args.simplify:
        temp_onnx = args.output.replace('.onnx', '_temp.onnx')
    
    # 根据输入权重格式选择转换方法
    if args.weights.endswith('.weights') and args.cfg:
        # Darknet格式
        onnx_path = convert_darknet_to_onnx(args.weights, args.cfg, temp_onnx, 
                                           args.img_size, args.batch_size, args.opset)
    else:
        # PyTorch格式
        onnx_path = convert_pytorch_to_onnx(args.weights, temp_onnx, 
                                           args.img_size, args.batch_size, args.opset)
    
    # 检查ONNX模型
    check_onnx_model(onnx_path)
    
    # 简化模型
    if args.simplify:
        onnx_path = simplify_onnx_model(onnx_path, temp_onnx.replace('_temp.onnx', '_simplified.onnx'))
    
    # 量化模型
    if args.quantize == 'int8':
        onnx_path = quantize_onnx_model(onnx_path, args.output, args.calibration_data)
    elif onnx_path != args.output:
        # 复制到最终输出路径
        import shutil
        shutil.copy(onnx_path, args.output)
        onnx_path = args.output
    
    # 清理中间文件
    if os.path.exists(temp_onnx) and temp_onnx != args.output:
        os.remove(temp_onnx)
    
    # 测试模型
    test_onnx_model(onnx_path, args.img_size)
    
    print(f"模型转换完成: {args.output}")
    print("模型信息:")
    print(f"  - 输入尺寸: {args.img_size}x{args.img_size}")
    print(f"  - 批处理大小: {args.batch_size}")
    print(f"  - 量化方式: {args.quantize}")
    print(f"  - ONNX opset: {args.opset}")

if __name__ == '__main__':
    main()