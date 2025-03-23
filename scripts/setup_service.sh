#!/bin/bash

# 零延迟YOLO FPS云辅助系统服务设置脚本
# 此脚本用于创建并启动系统服务

set -e

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统服务设置脚本 =====${NC}"

# 检查权限
if [ "$EUID" -ne 0 ]; then
  echo -e "${RED}错误: 请使用root权限运行此脚本${NC}"
  exit 1
fi

# 设置变量
INSTALL_DIR="/opt/zero-latency-yolo"
SERVICE_NAME="zero-latency-yolo"
WEB_SERVICE_NAME="zero-latency-web"
USER="$SUDO_USER"

if [ -z "$USER" ]; then
    USER=$(whoami)
    if [ "$USER" == "root" ]; then
        USER="zero-latency"
        # 创建用户（如果不存在）
        if ! id -u $USER &>/dev/null; then
            useradd -m -s /bin/bash -d /home/$USER $USER
            echo -e "${YELLOW}已创建用户: $USER${NC}"
        fi
    fi
fi

echo -e "${YELLOW}将使用用户 $USER 运行服务${NC}"

# 创建系统服务文件
echo -e "${YELLOW}正在创建系统服务...${NC}"

# 主服务
cat > /etc/systemd/system/$SERVICE_NAME.service << EOF
[Unit]
Description=Zero Latency YOLO FPS Cloud Assistant Server
After=network.target

[Service]
Type=simple
User=$USER
ExecStart=/usr/local/bin/start-zero-latency.sh
WorkingDirectory=$INSTALL_DIR
Restart=always
RestartSec=5
LimitNOFILE=1048576
LimitMEMLOCK=infinity
LimitCORE=infinity
StandardOutput=append:$INSTALL_DIR/logs/service.log
StandardError=append:$INSTALL_DIR/logs/error.log

# CPU和内存限制
CPUSchedulingPolicy=fifo
CPUSchedulingPriority=99
CPUAffinity=0

[Install]
WantedBy=multi-user.target
EOF

# Web服务
cat > /etc/systemd/system/$WEB_SERVICE_NAME.service << EOF
[Unit]
Description=Zero Latency YOLO FPS Cloud Assistant Web Server
After=network.target

[Service]
Type=simple
User=$USER
ExecStart=/usr/bin/node $INSTALL_DIR/web/server.js
WorkingDirectory=$INSTALL_DIR/web
Environment=PORT=3000
Restart=always
RestartSec=10
StandardOutput=append:$INSTALL_DIR/logs/web.log
StandardError=append:$INSTALL_DIR/logs/web_error.log

[Install]
WantedBy=multi-user.target
EOF

# 更新systemd配置
systemctl daemon-reload

# 设置服务自启动
echo -e "${YELLOW}正在设置服务自启动...${NC}"
systemctl enable $SERVICE_NAME
systemctl enable $WEB_SERVICE_NAME

# 设置文件所有权
echo -e "${YELLOW}正在设置文件权限...${NC}"
chown -R $USER:$USER $INSTALL_DIR

# 确保启动脚本存在并可执行
if [ ! -f /usr/local/bin/start-zero-latency.sh ]; then
    echo -e "${YELLOW}创建服务启动脚本...${NC}"
    cat > /usr/local/bin/start-zero-latency.sh << 'EOF'
#!/bin/bash
SERVICE_DIR="/opt/zero-latency-yolo"
SERVER_BIN="$SERVICE_DIR/bin/yolo_server"

# 确保日志目录存在
mkdir -p "$SERVICE_DIR/logs"

# 设置CPU亲和性和实时优先级
taskset -c 0 chrt -f 99 "$SERVER_BIN" --config="$SERVICE_DIR/configs/server.json" > "$SERVICE_DIR/logs/server.log" 2>&1
EOF
    chmod +x /usr/local/bin/start-zero-latency.sh
fi

# 启动服务
echo -e "${YELLOW}正在启动服务...${NC}"
systemctl start $SERVICE_NAME
systemctl start $WEB_SERVICE_NAME

# 检查服务状态
echo -e "${YELLOW}检查服务状态...${NC}"
if systemctl is-active --quiet $SERVICE_NAME; then
    echo -e "${GREEN}主服务已成功启动!${NC}"
else
    echo -e "${RED}主服务启动失败!${NC}"
    systemctl status $SERVICE_NAME
fi

if systemctl is-active --quiet $WEB_SERVICE_NAME; then
    echo -e "${GREEN}Web服务已成功启动!${NC}"
else
    echo -e "${RED}Web服务启动失败!${NC}"
    systemctl status $WEB_SERVICE_NAME
fi

# 显示服务URL
SERVER_IP=$(hostname -I | awk '{print $1}')
echo -e "${GREEN}===== 服务设置完成 =====${NC}"
echo -e "${GREEN}服务器API地址: ${SERVER_IP}:7788${NC}"
echo -e "${GREEN}Web界面地址: http://${SERVER_IP}:3000${NC}"
echo -e "${GREEN}客户端下载: http://${SERVER_IP}:3000/download/client${NC}"
echo -e "\n${YELLOW}使用以下命令管理服务:${NC}"
echo -e "  ${YELLOW}启动: systemctl start $SERVICE_NAME${NC}"
echo -e "  ${YELLOW}停止: systemctl stop $SERVICE_NAME${NC}"
echo -e "  ${YELLOW}重启: systemctl restart $SERVICE_NAME${NC}"
echo -e "  ${YELLOW}状态: systemctl status $SERVICE_NAME${NC}"
echo -e "  ${YELLOW}日志: journalctl -u $SERVICE_NAME -f${NC}"