#!/bin/bash
# 文件位置: [项目根目录]/deploy_backend.sh
# 用途: 编译和部署后端服务，包括Web前端

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统后端部署脚本 =====${NC}"

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$SCRIPT_DIR"

# 检查是否root，如果是，提示不要用root运行
if [[ $EUID -eq 0 ]]; then
   echo -e "${YELLOW}警告: 此脚本不需要root权限运行，推荐使用普通用户身份运行${NC}"
   echo -e "${YELLOW}是否继续? [y/N] ${NC}"
   read -r confirm
   if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
      echo -e "${BLUE}请使用普通用户身份重新运行此脚本${NC}"
      exit 1
   fi
fi

# 日志设置
mkdir -p "$PROJECT_ROOT/logs"
LOG_FILE="$PROJECT_ROOT/logs/deploy_backend_$(date +%Y%m%d_%H%M%S).log"
echo -e "${YELLOW}部署日志将保存到: ${LOG_FILE}${NC}"

# 日志函数
log() {
    echo "$1" | tee -a "$LOG_FILE"
}

log_section() {
    echo -e "\n=== $1 ===" | tee -a "$LOG_FILE"
}

# 检查命令是否存在
check_command() {
    command -v $1 >/dev/null 2>&1
}

# 检查环境变量
log_section "检查环境变量"

if [ -z "$ONNXRUNTIME_ROOT_DIR" ]; then
    # 尝试从预期位置获取
    if [ -d "$PROJECT_ROOT/third_party/onnxruntime" ]; then
        export ONNXRUNTIME_ROOT_DIR="$PROJECT_ROOT/third_party/onnxruntime"
        export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$ONNXRUNTIME_ROOT_DIR/lib
        log "自动设置ONNXRUNTIME_ROOT_DIR=$ONNXRUNTIME_ROOT_DIR"
    else
        echo -e "${RED}错误: ONNXRUNTIME_ROOT_DIR环境变量未设置${NC}"
        echo -e "${YELLOW}请先运行 ./setup_environment.sh 安装环境${NC}"
        exit 1
    fi
fi

log "ONNXRUNTIME_ROOT_DIR=$ONNXRUNTIME_ROOT_DIR"

# 检查模型文件
log_section "检查模型文件"
MODEL_DIR="$PROJECT_ROOT/models"

if [ ! -d "$MODEL_DIR" ] || [ -z "$(find "$MODEL_DIR" -name "*.onnx" 2>/dev/null)" ]; then
    echo -e "${YELLOW}警告: 未找到模型文件${NC}"
    
    # 尝试生成测试模型
    if [ -f "$PROJECT_ROOT/scripts/generate_dummy_model.py" ]; then
        echo -e "${BLUE}尝试生成测试模型...${NC}"
        mkdir -p "$MODEL_DIR"
        python3 "$PROJECT_ROOT/scripts/generate_dummy_model.py" --output "$MODEL_DIR/yolo_nano_cs16.onnx"
        
        if [ $? -ne 0 ]; then
            echo -e "${RED}模型生成失败${NC}"
            echo -e "${YELLOW}请手动添加模型文件到 $MODEL_DIR 目录${NC}"
        else
            echo -e "${GREEN}成功生成测试模型${NC}"
        fi
    else
        echo -e "${YELLOW}警告: 找不到模型生成脚本，请手动添加模型文件${NC}"
    fi
else
    log "模型文件检查通过"
    # 显示找到的模型文件
    find "$MODEL_DIR" -name "*.onnx" | while read -r model_file; do
        log "找到模型: $(basename "$model_file")"
    done
fi

# 编译后端
log_section "编译后端服务"

# 创建并进入构建目录
BUILD_DIR="$PROJECT_ROOT/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR" || {
    echo -e "${RED}无法进入构建目录${NC}"
    exit 1
}

# 运行CMake
log "运行CMake配置..."
cmake .. -DCMAKE_BUILD_TYPE=Release || {
    echo -e "${RED}CMake配置失败${NC}"
    exit 1
}

# 编译
log "编译项目..."
cpu_cores=$(nproc)
log "使用 $cpu_cores 个CPU核心进行并行编译"

make -j$cpu_cores || {
    echo -e "${RED}编译失败${NC}"
    exit 1
}

log "编译成功"

# 复制可执行文件到bin目录
log_section "安装可执行文件"
BIN_DIR="$PROJECT_ROOT/bin"
mkdir -p "$BIN_DIR"

# 查找编译后的可执行文件
EXECUTABLE=""
POSSIBLE_NAMES=("yolo_fps_assist" "yolo_server" "server")

for name in "${POSSIBLE_NAMES[@]}"; do
    if [ -f "$BUILD_DIR/$name" ]; then
        EXECUTABLE="$BUILD_DIR/$name"
        break
    fi
done

if [ -z "$EXECUTABLE" ]; then
    log "在构建目录中查找任何可执行文件..."
    EXECUTABLE=$(find "$BUILD_DIR" -type f -executable -not -path "*/CMakeFiles/*" | head -n 1)
fi

if [ -z "$EXECUTABLE" ]; then
    echo -e "${RED}错误: 未找到可执行文件${NC}"
    exit 1
fi

log "找到可执行文件: $EXECUTABLE"
cp "$EXECUTABLE" "$BIN_DIR/" || {
    echo -e "${RED}复制可执行文件失败${NC}"
    exit 1
}

EXECUTABLE_NAME=$(basename "$EXECUTABLE")
log "复制 $EXECUTABLE_NAME 到 $BIN_DIR 成功"

# 检查配置文件
log_section "检查配置文件"
CONFIG_DIR="$PROJECT_ROOT/configs"

if [ ! -d "$CONFIG_DIR" ]; then
    mkdir -p "$CONFIG_DIR"
fi

# 检查服务器配置
if [ ! -f "$CONFIG_DIR/server.json" ]; then
    if [ -f "$CONFIG_DIR/server.json.example" ]; then
        log "复制服务器配置示例..."
        cp "$CONFIG_DIR/server.json.example" "$CONFIG_DIR/server.json"
    else
        log "创建默认服务器配置..."
        cat > "$CONFIG_DIR/server.json" << EOF
{
    "server": {
        "port": 6789,
        "host": "0.0.0.0",
        "max_connections": 10
    },
    "model": {
        "path": "models/yolo_nano_cs16.onnx",
        "confidence_threshold": 0.45,
        "nms_threshold": 0.35
    },
    "logging": {
        "level": "info",
        "file": "logs/server.log"
    }
}
EOF
    fi
    log "服务器配置文件已创建: $CONFIG_DIR/server.json"
fi

# 安装Web依赖
log_section "安装Web依赖"
WEB_DIR="$PROJECT_ROOT/src/web"

if [ -d "$WEB_DIR" ]; then
    if [ -f "$WEB_DIR/package.json" ]; then
        log "安装Web依赖..."
        
        # 检查Node.js
        if ! check_command node || ! check_command npm; then
            echo -e "${YELLOW}警告: 未安装Node.js或npm，跳过Web依赖安装${NC}"
        else
            cd "$WEB_DIR" || {
                echo -e "${RED}无法进入Web目录${NC}"
                exit 1
            }
            
            # 设置npm配置提高可靠性
            npm config set registry https://registry.npmjs.org/
            npm config set fetch-retries 5
            
            log "开始安装npm依赖 (可能需要几分钟)..."
            npm install --no-audit --no-fund --loglevel=error
            
            if [ $? -ne 0 ]; then
                log "尝试使用--legacy-peer-deps选项..."
                npm install --no-audit --no-fund --legacy-peer-deps --loglevel=error
                
                if [ $? -ne 0 ]; then
                    echo -e "${RED}Web依赖安装失败${NC}"
                    echo -e "${YELLOW}您可能需要手动运行: cd $WEB_DIR && npm install${NC}"
                else
                    log "Web依赖安装成功 (使用legacy-peer-deps)"
                    
                    # 构建Web前端
                    log "构建Web前端..."
                    npm run build
                    
                    if [ $? -ne 0 ]; then
                        echo -e "${RED}Web前端构建失败${NC}"
                    else
                        log "Web前端构建成功"
                    fi
                fi
            else
                log "Web依赖安装成功"
                
                # 构建Web前端
                log "构建Web前端..."
                npm run build
                
                if [ $? -ne 0 ]; then
                    echo -e "${RED}Web前端构建失败${NC}"
                else
                    log "Web前端构建成功"
                fi
            fi
            
            # 返回项目根目录
            cd "$PROJECT_ROOT" || exit 1
        fi
    else
        log "Web目录中没有找到package.json文件，跳过Web依赖安装"
    fi
else
    log "Web目录不存在，跳过Web依赖安装"
fi

# 创建启动脚本
log_section "创建启动脚本"
START_SCRIPT="$PROJECT_ROOT/start_server.sh"

cat > "$START_SCRIPT" << EOF
#!/bin/bash
# 零延迟YOLO FPS云辅助系统服务器启动脚本

# 设置环境变量
export ONNXRUNTIME_ROOT_DIR="$ONNXRUNTIME_ROOT_DIR"
export LD_LIBRARY_PATH="\$LD_LIBRARY_PATH:$ONNXRUNTIME_ROOT_DIR/lib"

# 获取脚本所在目录
SCRIPT_DIR="\$( cd "\$( dirname "\${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd "\$SCRIPT_DIR"

# 创建日志目录
mkdir -p logs

# 启动服务器
echo "启动零延迟YOLO FPS云辅助系统服务器..."
./bin/$EXECUTABLE_NAME "\$@"
EOF

chmod +x "$START_SCRIPT"
log "创建了启动脚本: $START_SCRIPT"

# 创建服务文件(用于systemd)
log_section "创建systemd服务文件"
SERVICE_FILE="$PROJECT_ROOT/yolo_fps_assist.service"

cat > "$SERVICE_FILE" << EOF
[Unit]
Description=零延迟YOLO FPS云辅助系统服务
After=network.target

[Service]
Type=simple
User=$(whoami)
WorkingDirectory=$PROJECT_ROOT
ExecStart=$PROJECT_ROOT/start_server.sh
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal
Environment="ONNXRUNTIME_ROOT_DIR=$ONNXRUNTIME_ROOT_DIR"
Environment="LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$ONNXRUNTIME_ROOT_DIR/lib"

[Install]
WantedBy=multi-user.target
EOF

log "创建了systemd服务文件: $SERVICE_FILE"
log "如需安装为系统服务，请运行:"
log "  sudo cp $SERVICE_FILE /etc/systemd/system/"
log "  sudo systemctl daemon-reload"
log "  sudo systemctl enable yolo_fps_assist.service"
log "  sudo systemctl start yolo_fps_assist.service"

# 完成
echo -e "${GREEN}===== 后端部署完成 =====${NC}"
echo -e "${BLUE}服务可执行文件: $BIN_DIR/$EXECUTABLE_NAME${NC}"
echo -e "${BLUE}配置文件: $CONFIG_DIR/server.json${NC}"
echo -e "${BLUE}启动脚本: $START_SCRIPT${NC}"
echo -e "${GREEN}使用以下命令启动服务:${NC}"
echo -e "${YELLOW}$START_SCRIPT${NC}"