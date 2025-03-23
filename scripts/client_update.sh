#!/bin/bash

# 零延迟YOLO FPS云辅助系统客户端更新脚本
# 此脚本用于构建最新客户端并将其部署到下载服务器

set -e

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统客户端更新脚本 =====${NC}"

# 变量定义
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
DOWNLOADS_DIR="$PROJECT_ROOT/downloads"
VERSION_FILE="$PROJECT_ROOT/src/client/version.h"

# 确保下载目录存在
mkdir -p "$DOWNLOADS_DIR"

# 获取版本信息
if [ -f "$VERSION_FILE" ]; then
    VERSION=$(grep "#define CLIENT_VERSION" "$VERSION_FILE" | awk '{print $3}' | tr -d '"')
else
    VERSION="1.0.0"
fi

echo -e "${YELLOW}当前客户端版本: $VERSION${NC}"

# 构建客户端
echo -e "${YELLOW}正在构建客户端...${NC}"
cd "$PROJECT_ROOT"
./build.sh

if [ $? -ne 0 ]; then
    echo -e "${RED}客户端构建失败!${NC}"
    exit 1
fi

# 创建发布目录
RELEASE_DIR="$BUILD_DIR/release"
CLIENT_BIN_DIR="$BUILD_DIR/src/client"
mkdir -p "$RELEASE_DIR/configs"

# 复制客户端文件
echo -e "${YELLOW}正在准备客户端文件...${NC}"
cp "$CLIENT_BIN_DIR/yolo_client.exe" "$RELEASE_DIR/"
cp "$PROJECT_ROOT/configs/client.json" "$RELEASE_DIR/configs/"

# 复制依赖DLL
echo -e "${YELLOW}正在复制依赖库...${NC}"
for DLL in "$CLIENT_BIN_DIR"/*.dll; do
    if [ -f "$DLL" ]; then
        cp "$DLL" "$RELEASE_DIR/"
    fi
done

# 创建README文件
cat > "$RELEASE_DIR/README.txt" << EOF
零延迟YOLO FPS云辅助系统客户端 v$VERSION

使用说明:
1. 编辑 configs/client.json 文件设置服务器IP
2. 运行 yolo_client.exe
3. 启动CS 1.6游戏
4. 按F2键启用/禁用ESP显示
5. 按F3键启用/禁用瞄准辅助

问题反馈与支持:
如有任何问题，请联系管理员或查看在线文档。

注意: 本软件仅用于教育和研究目的，请遵守相关游戏服务条款和法律法规。
EOF

# 创建ZIP包
echo -e "${YELLOW}正在创建客户端安装包...${NC}"
RELEASE_ZIP="$DOWNLOADS_DIR/zero-latency-client-v$VERSION.zip"
LATEST_ZIP="$DOWNLOADS_DIR/zero-latency-client.zip"

cd "$RELEASE_DIR"
zip -r "$RELEASE_ZIP" ./*

# 创建最新版本的副本
cp "$RELEASE_ZIP" "$LATEST_ZIP"

echo -e "${GREEN}客户端安装包已创建: $RELEASE_ZIP${NC}"
echo -e "${GREEN}最新版本链接: $LATEST_ZIP${NC}"

# 更新Web服务器上的版本信息
if [ -f "$PROJECT_ROOT/src/web/public/version.json" ]; then
    echo -e "${YELLOW}正在更新版本信息...${NC}"
    cat > "$PROJECT_ROOT/src/web/public/version.json" << EOF
{
    "version": "$VERSION",
    "buildDate": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
    "minRequired": "1.0.0",
    "changes": [
        "更新于 $(date +"%Y-%m-%d %H:%M")"
    ],
    "downloadUrl": "/download/client"
}
EOF
fi

# 检查web服务是否运行中，如果是则重启
if systemctl is-active --quiet zero-latency-web; then
    echo -e "${YELLOW}正在重启Web服务...${NC}"
    systemctl restart zero-latency-web
fi

echo -e "${GREEN}===== 客户端更新完成 =====${NC}"