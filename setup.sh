#!/bin/bash
# 文件: setup.sh
# 用途: 设置ONNX Runtime环境

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统环境配置脚本 =====${NC}"
PROJECT_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# 创建日志目录
mkdir -p "$PROJECT_ROOT/logs"
LOG_FILE="$PROJECT_ROOT/logs/setup_$(date +%Y%m%d_%H%M%S).log"
echo -e "${YELLOW}安装日志将保存到: ${LOG_FILE}${NC}"

# 创建所需目录结构
echo -e "${BLUE}创建目录结构...${NC}" | tee -a "$LOG_FILE"
mkdir -p "$PROJECT_ROOT/build" \
         "$PROJECT_ROOT/bin" \
         "$PROJECT_ROOT/models" \
         "$PROJECT_ROOT/configs" \
         "$PROJECT_ROOT/downloads" \
         "$PROJECT_ROOT/include" \
         "$PROJECT_ROOT/third_party"

# 检查Python依赖
echo -e "${BLUE}检查Python依赖...${NC}" | tee -a "$LOG_FILE"
if ! command -v python3 &> /dev/null; then
    echo -e "${YELLOW}安装Python3...${NC}" | tee -a "$LOG_FILE"
    if [[ -f /etc/debian_version ]]; then
        sudo apt-get update
        sudo apt-get install -y python3 python3-pip
    elif [[ -f /etc/redhat-release ]]; then
        sudo yum install -y python3 python3-pip
    else
        echo -e "${RED}无法确定操作系统，请手动安装Python3${NC}" | tee -a "$LOG_FILE"
        exit 1
    fi
fi

# 安装Python模块
echo -e "${BLUE}安装Python模块...${NC}" | tee -a "$LOG_FILE"
python3 -m pip install numpy onnx --quiet

# 检查ONNX Runtime
ONNX_DIR="$PROJECT_ROOT/third_party/onnxruntime"
if [ ! -d "$ONNX_DIR" ]; then
    echo -e "${BLUE}下载并安装ONNX Runtime...${NC}" | tee -a "$LOG_FILE"
    
    TEMP_DIR=$(mktemp -d)
    ONNX_VERSION="1.15.1"
    ONNX_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/onnxruntime-linux-x64-${ONNX_VERSION}.tgz"
    
    echo -e "${YELLOW}从 $ONNX_URL 下载 ONNX Runtime...${NC}" | tee -a "$LOG_FILE"
    
    if ! command -v wget &> /dev/null; then
        if [[ -f /etc/debian_version ]]; then
            sudo apt-get install -y wget
        elif [[ -f /etc/redhat-release ]]; then
            sudo yum install -y wget
        fi
    fi
    
    wget -q --show-progress "$ONNX_URL" -O "$TEMP_DIR/onnxruntime.tgz" || {
        echo -e "${RED}下载ONNX Runtime失败${NC}" | tee -a "$LOG_FILE"
        rm -rf "$TEMP_DIR"
        exit 1
    }
    
    mkdir -p "$PROJECT_ROOT/third_party"
    tar -xzf "$TEMP_DIR/onnxruntime.tgz" -C "$PROJECT_ROOT/third_party" || {
        echo -e "${RED}解压ONNX Runtime失败${NC}" | tee -a "$LOG_FILE"
        rm -rf "$TEMP_DIR"
        exit 1
    }
    
    # 重命名目录
    mv "$PROJECT_ROOT/third_party/onnxruntime-linux-x64-${ONNX_VERSION}" "$ONNX_DIR" || {
        echo -e "${RED}重命名ONNX Runtime目录失败${NC}" | tee -a "$LOG_FILE"
        rm -rf "$TEMP_DIR"
        exit 1
    }
    
    # 清理临时文件
    rm -rf "$TEMP_DIR"
    
    echo -e "${GREEN}ONNX Runtime安装完成${NC}" | tee -a "$LOG_FILE"
else
    echo -e "${GREEN}ONNX Runtime已安装${NC}" | tee -a "$LOG_FILE"
fi

# 创建环境变量文件
ONNXRUNTIME_ROOT_DIR="$PROJECT_ROOT/third_party/onnxruntime"
echo -e "${BLUE}创建环境变量文件...${NC}" | tee -a "$LOG_FILE"
cat > "$PROJECT_ROOT/.env" << EOF
# 零延迟YOLO FPS云辅助系统环境变量
export ONNXRUNTIME_ROOT_DIR="$ONNXRUNTIME_ROOT_DIR"
export LD_LIBRARY_PATH="\$LD_LIBRARY_PATH:$ONNXRUNTIME_ROOT_DIR/lib"
EOF

# 添加执行权限
chmod +x "$PROJECT_ROOT/.env"

# 创建头文件符号链接
echo -e "${BLUE}创建头文件符号链接...${NC}" | tee -a "$LOG_FILE"
for header in "$ONNXRUNTIME_ROOT_DIR/include/"*.h; do
    if [ -f "$header" ]; then
        ln -sf "$header" "$PROJECT_ROOT/include/$(basename "$header")"
    fi
done

# 创建onnxruntime/core/session目录结构
mkdir -p "$PROJECT_ROOT/include/onnxruntime/core/session"
if [ -f "$ONNXRUNTIME_ROOT_DIR/include/onnxruntime_cxx_api.h" ]; then
    ln -sf "$ONNXRUNTIME_ROOT_DIR/include/onnxruntime_cxx_api.h" "$PROJECT_ROOT/include/onnxruntime/core/session/"
fi

# 加载环境变量
source "$PROJECT_ROOT/.env"

# 检查编译工具
echo -e "${BLUE}检查编译工具...${NC}" | tee -a "$LOG_FILE"
if ! command -v cmake &> /dev/null || ! command -v g++ &> /dev/null; then
    echo -e "${YELLOW}安装编译工具...${NC}" | tee -a "$LOG_FILE"
    if [[ -f /etc/debian_version ]]; then
        sudo apt-get install -y build-essential cmake
    elif [[ -f /etc/redhat-release ]]; then
        sudo yum groupinstall -y "Development Tools"
        sudo yum install -y cmake
    else
        echo -e "${RED}无法确定操作系统，请手动安装build-essential和cmake${NC}" | tee -a "$LOG_FILE"
        exit 1
    fi
fi

# 为其他脚本添加执行权限
echo -e "${BLUE}为脚本添加执行权限...${NC}" | tee -a "$LOG_FILE"
chmod +x "$PROJECT_ROOT/generate_model.sh"
chmod +x "$PROJECT_ROOT/build_server.sh"
chmod +x "$PROJECT_ROOT/start.sh"

echo -e "${GREEN}环境配置完成!${NC}"
echo -e "${YELLOW}请运行以下命令加载环境变量:${NC}"
echo -e "${BLUE}source .env${NC}"
echo -e "${YELLOW}接下来，请生成兼容的模型:${NC}"
echo -e "${BLUE}./generate_model.sh${NC}"