#!/bin/bash

# 零延迟YOLO FPS云辅助系统部署脚本
# 此脚本用于在服务器上部署系统

set -e

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统部署脚本 =====${NC}"

# 检查权限
if [ "$EUID" -ne 0 ]; then
  echo -e "${RED}错误: 请使用root权限运行此脚本${NC}"
  exit 1
fi

# 系统环境优化
echo -e "${YELLOW}正在优化系统环境...${NC}"
bash ./scripts/optimize_server.sh

# 安装依赖
echo -e "${YELLOW}正在安装系统依赖...${NC}"
bash ./scripts/install_deps.sh

# 创建安装目录
INSTALL_DIR="/opt/zero-latency-yolo"
mkdir -p $INSTALL_DIR
mkdir -p $INSTALL_DIR/bin
mkdir -p $INSTALL_DIR/models
mkdir -p $INSTALL_DIR/configs
mkdir -p $INSTALL_DIR/logs
mkdir -p $INSTALL_DIR/web

# 构建项目
echo -e "${YELLOW}正在构建项目...${NC}"
bash ./build.sh

# 复制文件
echo -e "${YELLOW}正在复制文件到安装目录...${NC}"
cp ./build/src/server/yolo_server $INSTALL_DIR/bin/
cp ./build/src/client/yolo_client $INSTALL_DIR/bin/
cp -r ./models/* $INSTALL_DIR/models/
cp -r ./configs/* $INSTALL_DIR/configs/
cp -r ./src/web/* $INSTALL_DIR/web/

# 设置权限
chmod +x $INSTALL_DIR/bin/yolo_server
chmod +x $INSTALL_DIR/bin/yolo_client

# 设置自启动服务
echo -e "${YELLOW}正在设置系统服务...${NC}"
bash ./scripts/setup_service.sh

# 启动Web服务
echo -e "${YELLOW}正在启动Web服务...${NC}"
cd $INSTALL_DIR/web
npm install
npm start &

# 启动服务
echo -e "${YELLOW}正在启动服务...${NC}"
systemctl start zero-latency-yolo

# 检查服务状态
sleep 2
if systemctl is-active --quiet zero-latency-yolo; then
  echo -e "${GREEN}服务已成功启动!${NC}"
  echo -e "${GREEN}服务器已部署在: $(hostname -I | awk '{print $1}'):7788${NC}"
  echo -e "${GREEN}下载页面: http://$(hostname -I | awk '{print $1}'):3000${NC}"
else
  echo -e "${RED}服务启动失败，请检查日志文件${NC}"
  echo -e "${YELLOW}查看日志: journalctl -u zero-latency-yolo${NC}"
fi

echo -e "${GREEN}===== 部署完成 =====${NC}"