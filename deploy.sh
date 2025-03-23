#!/bin/bash

# 零延迟YOLO FPS云辅助系统部署脚本
# 此脚本用于在服务器上部署系统

# 严格错误处理
set -e

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统部署脚本 =====${NC}"

# 创建日志目录和日志文件
mkdir -p logs
LOGFILE="logs/deploy_$(date +%Y%m%d_%H%M%S).log"
echo "部署开始: $(date)" > $LOGFILE

# 日志函数
log() {
    echo "$1" | tee -a $LOGFILE
}

log_error() {
    echo -e "${RED}错误: $1${NC}" | tee -a $LOGFILE
}

log_warning() {
    echo -e "${YELLOW}警告: $1${NC}" | tee -a $LOGFILE
}

log_success() {
    echo -e "${GREEN}$1${NC}" | tee -a $LOGFILE
}

# 检查权限
if [ "$EUID" -ne 0 ]; then
  log_error "请使用root权限运行此脚本"
  exit 1
fi

# 检查必要的二进制文件是否存在
if [ ! -f "./build/src/server/yolo_server" ]; then
    log_error "服务器二进制文件不存在: ./build/src/server/yolo_server"
    log "请先运行 ./build.sh 构建项目"
    exit 1
fi

# 创建备份目录
BACKUP_DIR="backup_$(date +%Y%m%d_%H%M%S)"
mkdir -p $BACKUP_DIR
log "创建备份目录: $BACKUP_DIR"

# 备份现有安装（如果存在）
INSTALL_DIR="/opt/zero-latency-yolo"
if [ -d "$INSTALL_DIR" ]; then
    log "备份现有安装..."
    cp -r $INSTALL_DIR/* $BACKUP_DIR/ 2>/dev/null || true
fi

# 清理现有安装目录
log "清理安装目录: $INSTALL_DIR"
mkdir -p $INSTALL_DIR
mkdir -p $INSTALL_DIR/bin
mkdir -p $INSTALL_DIR/models
mkdir -p $INSTALL_DIR/configs
mkdir -p $INSTALL_DIR/logs
mkdir -p $INSTALL_DIR/web

# 系统环境优化
log "正在优化系统环境..."
if [ -f "./scripts/optimize_server.sh" ]; then
    bash ./scripts/optimize_server.sh >> $LOGFILE 2>&1 || {
        log_warning "系统优化过程遇到错误，继续部署..."
    }
else
    log_warning "未找到优化脚本: ./scripts/optimize_server.sh"
fi

# 安装依赖
log "正在安装系统依赖..."
if [ -f "./scripts/install_deps.sh" ]; then
    bash ./scripts/install_deps.sh >> $LOGFILE 2>&1 || {
        log_error "安装依赖失败"
        exit 1
    }
else
    log_warning "未找到依赖安装脚本，跳过依赖安装"
fi

# 构建项目
log "正在构建项目..."
if [ ! -f "./build/src/server/yolo_server" ]; then
    bash ./build.sh >> $LOGFILE 2>&1 || {
        log_error "构建项目失败"
        exit 1
    }
fi

# 检查服务器二进制文件
if [ ! -f "./build/src/server/yolo_server" ]; then
    log_error "服务器二进制文件不存在，构建可能失败"
    exit 1
fi

# 检查客户端二进制文件（可选）
if [ ! -f "./build/src/client/yolo_client" ]; then
    log_warning "客户端二进制文件不存在，只部署服务器组件"
fi

# 检查模型文件
if [ ! -d "./models" ] || [ -z "$(ls -A ./models 2>/dev/null)" ]; then
    log_warning "模型目录为空，尝试创建虚拟模型..."
    
    mkdir -p ./models
    if [ -f "./scripts/generate_dummy_model.py" ]; then
        python3 ./scripts/generate_dummy_model.py --output ./models/yolo_nano_cs16.onnx >> $LOGFILE 2>&1 || {
            log_warning "无法生成虚拟模型，系统可能无法正常工作"
        }
    else
        log_warning "未找到模型生成脚本，系统可能无法正常工作"
    fi
fi

# 复制文件
log "正在复制文件到安装目录..."
cp ./build/src/server/yolo_server $INSTALL_DIR/bin/ || {
    log_error "复制服务器二进制文件失败"
    exit 1
}

# 复制客户端（如果存在）
if [ -f "./build/src/client/yolo_client" ]; then
    cp ./build/src/client/yolo_client $INSTALL_DIR/bin/ || {
        log_warning "复制客户端二进制文件失败"
    }
fi

# 复制模型文件
if [ -d "./models" ]; then
    cp -r ./models/* $INSTALL_DIR/models/ 2>/dev/null || {
        log_warning "复制模型文件失败或模型目录为空"
    }
fi

# 复制配置文件
if [ -d "./configs" ]; then
    cp -r ./configs/* $INSTALL_DIR/configs/ 2>/dev/null || {
        log_warning "复制配置文件失败或配置目录为空"
    }
fi

# 复制Web文件
if [ -d "./src/web" ]; then
    cp -r ./src/web/* $INSTALL_DIR/web/ 2>/dev/null || {
        log_warning "复制Web文件失败"
    }
fi

# 设置权限
chmod +x $INSTALL_DIR/bin/yolo_server
if [ -f "$INSTALL_DIR/bin/yolo_client" ]; then
    chmod +x $INSTALL_DIR/bin/yolo_client
fi

# 验证配置文件
if [ ! -f "$INSTALL_DIR/configs/server.json" ]; then
    log_warning "服务器配置文件不存在，创建默认配置..."
    if [ -f "$INSTALL_DIR/configs/server.json.example" ]; then
        cp $INSTALL_DIR/configs/server.json.example $INSTALL_DIR/configs/server.json || {
            log_error "创建默认配置失败"
            exit 1
        }
    else
        log_error "无法创建默认配置，示例文件不存在"
        exit 1
    fi
fi

# 创建服务启动脚本
log "创建服务启动脚本..."
cat > /usr/local/bin/start-zero-latency.sh << 'EOF'
#!/bin/bash
SERVICE_DIR="/opt/zero-latency-yolo"
SERVER_BIN="$SERVICE_DIR/bin/yolo_server"
LOG_FILE="$SERVICE_DIR/logs/server.log"

# 确保日志目录存在
mkdir -p "$SERVICE_DIR/logs"

# 启动时记录时间戳
echo "=== 服务启动于 $(date) ===" >> "$LOG_FILE"

# 使用配置文件启动服务
if [ -f "$SERVICE_DIR/configs/server.json" ]; then
    echo "使用配置: $SERVICE_DIR/configs/server.json" >> "$LOG_FILE"
    # 设置CPU亲和性和实时优先级
    taskset -c 0 chrt -f 99 "$SERVER_BIN" --config="$SERVICE_DIR/configs/server.json" >> "$LOG_FILE" 2>&1
else
    echo "警告: 找不到配置文件，使用默认参数启动" >> "$LOG_FILE"
    taskset -c 0 chrt -f 99 "$SERVER_BIN" >> "$LOG_FILE" 2>&1
fi

# 记录退出状态
echo "=== 服务退出，状态码: $? 于 $(date) ===" >> "$LOG_FILE"
EOF

chmod +x /usr/local/bin/start-zero-latency.sh

# 设置系统服务
log "正在设置系统服务..."
if [ -f "./scripts/setup_service.sh" ]; then
    bash ./scripts/setup_service.sh >> $LOGFILE 2>&1 || {
        log_warning "设置系统服务失败，尝试手动创建..."
        
        # 创建系统服务文件
        cat > /etc/systemd/system/zero-latency-yolo.service << EOF
[Unit]
Description=Zero Latency YOLO FPS Cloud Assistant Server
After=network.target

[Service]
Type=simple
User=root
ExecStart=/usr/local/bin/start-zero-latency.sh
WorkingDirectory=/opt/zero-latency-yolo
Restart=always
RestartSec=5
LimitNOFILE=1048576
LimitMEMLOCK=infinity
LimitCORE=infinity
StandardOutput=append:/opt/zero-latency-yolo/logs/service.log
StandardError=append:/opt/zero-latency-yolo/logs/error.log

# CPU和内存限制
CPUSchedulingPolicy=fifo
CPUSchedulingPriority=99
CPUAffinity=0

[Install]
WantedBy=multi-user.target
EOF

        # 创建Web服务系统服务文件
        cat > /etc/systemd/system/zero-latency-web.service << EOF
[Unit]
Description=Zero Latency YOLO FPS Cloud Assistant Web Server
After=network.target

[Service]
Type=simple
User=root
ExecStart=/usr/bin/node /opt/zero-latency-yolo/web/server.js
WorkingDirectory=/opt/zero-latency-yolo/web
Environment=PORT=3000
Restart=always
RestartSec=10
StandardOutput=append:/opt/zero-latency-yolo/logs/web.log
StandardError=append:/opt/zero-latency-yolo/logs/web_error.log

[Install]
WantedBy=multi-user.target
EOF

        # 重新加载systemd配置
        systemctl daemon-reload
        
        # 启用服务自启动
        systemctl enable zero-latency-yolo
        systemctl enable zero-latency-web
    }
else
    log_warning "未找到服务设置脚本，创建默认服务..."
    
    # 创建系统服务文件
    cat > /etc/systemd/system/zero-latency-yolo.service << EOF
[Unit]
Description=Zero Latency YOLO FPS Cloud Assistant Server
After=network.target

[Service]
Type=simple
User=root
ExecStart=/usr/local/bin/start-zero-latency.sh
WorkingDirectory=/opt/zero-latency-yolo
Restart=always
RestartSec=5
LimitNOFILE=1048576
LimitMEMLOCK=infinity
LimitCORE=infinity
StandardOutput=append:/opt/zero-latency-yolo/logs/service.log
StandardError=append:/opt/zero-latency-yolo/logs/error.log

# CPU和内存限制
CPUSchedulingPolicy=fifo
CPUSchedulingPriority=99
CPUAffinity=0

[Install]
WantedBy=multi-user.target
EOF

    # 创建Web服务系统服务文件
    cat > /etc/systemd/system/zero-latency-web.service << EOF
[Unit]
Description=Zero Latency YOLO FPS Cloud Assistant Web Server
After=network.target

[Service]
Type=simple
User=root
ExecStart=/usr/bin/node /opt/zero-latency-yolo/web/server.js
WorkingDirectory=/opt/zero-latency-yolo/web
Environment=PORT=3000
Restart=always
RestartSec=10
StandardOutput=append:/opt/zero-latency-yolo/logs/web.log
StandardError=append:/opt/zero-latency-yolo/logs/web_error.log

[Install]
WantedBy=multi-user.target
EOF

    # 重新加载systemd配置
    systemctl daemon-reload
    
    # 启用服务自启动
    systemctl enable zero-latency-yolo
    systemctl enable zero-latency-web
fi

# 启动Web服务
log "正在启动Web服务..."
cd $INSTALL_DIR/web
npm install >> $LOGFILE 2>&1 || {
    log_warning "Web服务依赖安装失败，Web界面可能无法正常工作"
}

# 启动服务
log "正在启动服务..."
systemctl restart zero-latency-yolo
systemctl restart zero-latency-web

# 检查服务状态
sleep 3
if systemctl is-active --quiet zero-latency-yolo; then
    log_success "服务已成功启动!"
    
    # 获取服务器IP地址
    SERVER_IP=$(hostname -I | awk '{print $1}')
    
    log_success "服务器已部署在: ${SERVER_IP}:7788"
    log_success "下载页面: http://${SERVER_IP}:3000"
else
    log_error "服务启动失败，请检查日志文件"
    log "查看日志: journalctl -u zero-latency-yolo"
    
    # 尝试从日志获取错误信息
    ERROR_LOG=$(journalctl -u zero-latency-yolo -n 20 --no-pager)
    log "最近的错误日志:\n$ERROR_LOG"
    
    # 尝试恢复备份
    log_warning "尝试恢复备份..."
    if [ -d "$BACKUP_DIR" ] && [ "$(ls -A $BACKUP_DIR)" ]; then
        rm -rf $INSTALL_DIR/*
        cp -r $BACKUP_DIR/* $INSTALL_DIR/
        log "备份已恢复"
        
        # 尝试启动旧版本
        systemctl restart zero-latency-yolo
        if systemctl is-active --quiet zero-latency-yolo; then
            log_success "已成功回滚到先前版本!"
        else
            log_error "回滚失败，请手动恢复系统"
        fi
    else
        log_error "没有可用的备份，无法恢复"
    fi
    
    exit 1
fi

# 创建示例客户端更新脚本
if [ ! -f "$INSTALL_DIR/update_client.sh" ]; then
    log "创建客户端更新脚本..."
    cat > $INSTALL_DIR/update_client.sh << 'EOF'
#!/bin/bash
# 客户端更新脚本
./scripts/client_update.sh
EOF
    chmod +x $INSTALL_DIR/update_client.sh
fi

echo -e "${GREEN}===== 部署完成 =====${NC}"
echo -e "${BLUE}服务器IP: ${SERVER_IP}${NC}"
echo -e "${BLUE}服务端口: 7788${NC}"
echo -e "${BLUE}Web界面: http://${SERVER_IP}:3000${NC}"
echo -e "\n${YELLOW}管理命令:${NC}"
echo -e "  ${YELLOW}启动服务: systemctl start zero-latency-yolo${NC}"
echo -e "  ${YELLOW}停止服务: systemctl stop zero-latency-yolo${NC}"
echo -e "  ${YELLOW}重启服务: systemctl restart zero-latency-yolo${NC}"
echo -e "  ${YELLOW}查看状态: systemctl status zero-latency-yolo${NC}"
echo -e "  ${YELLOW}查看日志: journalctl -u zero-latency-yolo -f${NC}"
echo -e "  ${YELLOW}Web服务: systemctl [start|stop|restart] zero-latency-web${NC}"

# 记录部署成功
log_success "部署成功完成! 日志保存在: $LOGFILE"