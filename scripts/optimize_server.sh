#!/bin/bash

# 零延迟YOLO FPS云辅助系统服务器优化脚本
# 此脚本用于优化服务器系统设置以获得最佳性能

set -e

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统服务器优化脚本 =====${NC}"

# 检查权限
if [ "$EUID" -ne 0 ]; then
  echo -e "${RED}错误: 请使用root权限运行此脚本${NC}"
  exit 1
fi

# 检查系统类型
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    VERSION=$VERSION_ID
else
    echo -e "${RED}无法确定操作系统类型${NC}"
    exit 1
fi

echo -e "${YELLOW}检测到操作系统: ${OS} ${VERSION}${NC}"

# 优化内核参数
echo -e "${YELLOW}正在优化内核参数...${NC}"

# 创建系统优化配置文件
cat > /etc/sysctl.d/99-zero-latency.conf << EOF
# 网络优化
net.core.rmem_max = 16777216
net.core.wmem_max = 16777216
net.core.rmem_default = 1048576
net.core.wmem_default = 1048576
net.core.netdev_max_backlog = 5000
net.ipv4.tcp_rmem = 4096 1048576 16777216
net.ipv4.tcp_wmem = 4096 1048576 16777216
net.ipv4.tcp_max_syn_backlog = 8192
net.ipv4.tcp_slow_start_after_idle = 0
net.ipv4.tcp_tw_reuse = 1
net.ipv4.ip_local_port_range = 1024 65535
net.core.somaxconn = 65535

# 内存管理优化
vm.swappiness = 10
vm.dirty_ratio = 60
vm.dirty_background_ratio = 2

# 文件系统优化
fs.file-max = 2097152
fs.inotify.max_user_watches = 524288

# UDP缓冲区优化
net.ipv4.udp_rmem_min = 8192
net.ipv4.udp_wmem_min = 8192
EOF

# 应用系统优化
sysctl -p /etc/sysctl.d/99-zero-latency.conf

# 设置最大打开文件数
echo -e "${YELLOW}正在设置最大打开文件数...${NC}"
cat > /etc/security/limits.d/99-zero-latency.conf << EOF
# 增加最大打开文件数限制
*       soft    nofile    1048576
*       hard    nofile    1048576
*       soft    nproc     65535
*       hard    nproc     65535
EOF

# 优化 CPU 调度策略
echo -e "${YELLOW}正在优化 CPU 调度...${NC}"
if [[ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]]; then
    for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        echo "performance" > "$cpu"
    done
    echo -e "${GREEN}已设置 CPU 调度器为 performance 模式${NC}"
else
    echo -e "${YELLOW}无法设置 CPU 调度器，可能不受支持${NC}"
fi

# 停用不必要的服务
echo -e "${YELLOW}正在停用不必要的服务...${NC}"
UNNECESSARY_SERVICES=(
    "bluetooth"
    "cups"
    "avahi-daemon"
    "ModemManager"
    "snapd"
    "ufw"
    "firewalld"
)

for service in "${UNNECESSARY_SERVICES[@]}"; do
    if systemctl list-unit-files | grep -q "$service"; then
        systemctl stop "$service" 2>/dev/null || true
        systemctl disable "$service" 2>/dev/null || true
        echo -e "${GREEN}已停用 $service 服务${NC}"
    fi
done

# 实时性优化
echo -e "${YELLOW}正在优化实时性能...${NC}"
if [[ "$OS" == "ubuntu" || "$OS" == "debian" ]]; then
    if ! dpkg -l | grep -q linux-image-lowlatency; then
        echo -e "${YELLOW}推荐安装低延迟内核以获得更好性能${NC}"
        echo -e "${YELLOW}可以通过运行以下命令安装:${NC}"
        echo -e "${YELLOW}apt-get install -y linux-image-lowlatency linux-headers-lowlatency${NC}"
    fi
fi

# 创建服务启动脚本，设置实时优先级和 CPU 亲和性
echo -e "${YELLOW}正在创建优化的服务启动脚本...${NC}"
cat > /usr/local/bin/start-zero-latency.sh << 'EOF'
#!/bin/bash
SERVICE_DIR="/opt/zero-latency-yolo"
SERVER_BIN="$SERVICE_DIR/bin/yolo_server"

# 确保日志目录存在
mkdir -p "$SERVICE_DIR/logs"

# 确保内存锁定
ulimit -l unlimited

# 设置CPU亲和性
taskset -c 0 chrt -f 99 "$SERVER_BIN" --config="$SERVICE_DIR/configs/server.json" > "$SERVICE_DIR/logs/server.log" 2>&1
EOF

chmod +x /usr/local/bin/start-zero-latency.sh

# 网络优化
echo -e "${YELLOW}正在优化网络...${NC}"
if command -v ethtool &> /dev/null; then
    # 获取主网络接口
    MAIN_INTERFACE=$(ip route | grep default | sed -e 's/^.*dev.//' -e 's/.proto.*$//' | awk '{print $1}')
    if [[ -n "$MAIN_INTERFACE" ]]; then
        # 禁用网卡中断合并
        ethtool -C "$MAIN_INTERFACE" adaptive-rx off rx-usecs 0 rx-frames 0 2>/dev/null || true
        # 启用网卡硬件卸载功能
        ethtool -K "$MAIN_INTERFACE" tso on gso on gro off lro off 2>/dev/null || true
        echo -e "${GREEN}已优化网络接口 $MAIN_INTERFACE${NC}"
    fi
fi

# 设置 I/O 调度器
echo -e "${YELLOW}正在优化 I/O 调度...${NC}"
for disk in /sys/block/sd*; do
    if [[ -f "$disk/queue/scheduler" ]]; then
        echo "deadline" > "$disk/queue/scheduler"
        echo 1 > "$disk/queue/iosched/fifo_batch"
        echo -e "${GREEN}已为 $(basename "$disk") 设置 deadline 调度器${NC}"
    fi
done

# 禁用透明大页
echo -e "${YELLOW}正在禁用透明大页...${NC}"
if [[ -d /sys/kernel/mm/transparent_hugepage ]]; then
    echo never > /sys/kernel/mm/transparent_hugepage/enabled
    echo never > /sys/kernel/mm/transparent_hugepage/defrag
    echo -e "${GREEN}已禁用透明大页${NC}"
fi

echo -e "${GREEN}===== 服务器优化完成 =====${NC}"
echo -e "${YELLOW}请重启服务器以应用所有更改${NC}"