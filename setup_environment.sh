#!/bin/bash
# 文件位置: [项目根目录]/setup_environment.sh
# 用途: 一次性安装所有开发环境，修复ONNX Runtime路径并准备编译环境

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${GREEN}===== 零延迟YOLO FPS云辅助系统环境安装脚本 =====${NC}"
echo -e "${BLUE}此脚本将安装所有开发和运行环境，包括交叉编译工具链${NC}"

# 检查root权限
if [[ $EUID -ne 0 ]]; then
   echo -e "${YELLOW}此脚本需要root权限来安装系统依赖.${NC}"
   echo -e "${YELLOW}请使用 sudo ./setup_environment.sh 运行${NC}"
   exit 1
fi

# 如果使用sudo运行，获取实际用户
REAL_USER=${SUDO_USER:-$USER}
REAL_HOME=$(eval echo ~$REAL_USER)
REAL_GROUP=$(id -gn $REAL_USER)

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT="$SCRIPT_DIR"

# 检查系统类型
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    VERSION=$VERSION_ID
    
    # 检查发行版分支
    if [ -f /etc/debian_version ]; then
        DEBIAN_BASED=1
    elif [ -f /etc/redhat-release ]; then
        RHEL_BASED=1
    fi
else
    echo -e "${RED}无法确定操作系统类型${NC}"
    exit 1
fi

echo -e "${BLUE}检测到操作系统: ${OS} ${VERSION}${NC}"

# 确保必要的目录存在
mkdir -p "$PROJECT_ROOT/logs"
LOG_FILE="$PROJECT_ROOT/logs/setup_environment_$(date +%Y%m%d_%H%M%S).log"
echo -e "${YELLOW}安装日志将保存到: ${LOG_FILE}${NC}"

# 确保log目录权限正确
chown -R $REAL_USER:$REAL_GROUP "$PROJECT_ROOT/logs"

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

# 安装基本依赖
log_section "安装基本依赖"

# Ubuntu/Debian系统
if [[ -n "$DEBIAN_BASED" || "$OS" == "ubuntu" || "$OS" == "debian" || "$OS" == "linuxmint" ]]; then
    log "在 Debian/Ubuntu 系统上安装依赖..."
    
    # 更新软件包列表
    apt-get update -qq || {
        echo -e "${RED}无法更新软件包列表${NC}"
        exit 1
    }
    
    # 安装编译工具和基本依赖
    apt-get install -y build-essential cmake git wget curl unzip \
                     libssl-dev zlib1g-dev libgomp1 \
                     ca-certificates python3 python3-pip || {
        echo -e "${RED}安装基本依赖失败${NC}"
        exit 1
    }
    
    # 检查和安装Node.js (如果不存在)
    if ! check_command node || ! check_command npm; then
        log "安装 Node.js 和 npm..."
        
        # 尝试使用NodeSource存储库安装Node.js 18.x
        if ! check_command curl; then
            apt-get install -y curl
        fi
        
        # 尝试使用NodeSource
        curl -fsSL https://deb.nodesource.com/setup_18.x -o nodesource_setup.sh
        if [ -f nodesource_setup.sh ]; then
            bash nodesource_setup.sh
            apt-get install -y nodejs
            rm nodesource_setup.sh
        else
            # 如果NodeSource不可用，使用Ubuntu默认存储库
            log "使用Ubuntu默认存储库安装Node.js..."
            apt-get install -y nodejs npm
        fi
        
        # 验证Node.js安装
        if ! check_command node || ! check_command npm; then
            echo -e "${RED}安装Node.js失败，脚本将继续但Web依赖安装可能会失败${NC}"
        else
            log "Node.js安装成功: $(node --version)"
            log "NPM安装成功: $(npm --version)"
        fi
    fi
    
    # 安装MinGW-w64交叉编译工具链
    log "安装MinGW-w64交叉编译工具链..."
    apt-get install -y mingw-w64 || {
        echo -e "${RED}安装交叉编译工具链失败${NC}"
        exit 1
    }
    
    # 安装Wine (可选，用于测试Windows程序)
    log "安装Wine (用于测试Windows程序)..."
    apt-get install -y wine64 || {
        log "Wine安装失败。这不会影响编译，但可能影响测试。"
    }

# CentOS/RHEL系统    
elif [[ -n "$RHEL_BASED" || "$OS" == "centos" || "$OS" == "rhel" || "$OS" == "fedora" ]]; then
    log "在 CentOS/RHEL 系统上安装依赖..."
    
    # 更新软件包
    yum update -y || {
        echo -e "${RED}无法更新软件包${NC}"
        exit 1
    }
    
    # 安装开发工具组
    yum groupinstall -y "Development Tools" || {
        echo -e "${RED}安装开发工具组失败${NC}"
        exit 1
    }
    
    # 安装其他必要依赖
    yum install -y cmake git wget curl unzip \
                 openssl-devel zlib-devel libgomp \
                 ca-certificates python3 python3-pip || {
        echo -e "${RED}安装基本依赖失败${NC}"
        exit 1
    }
    
    # 检查和安装Node.js (如果不存在)
    if ! check_command node || ! check_command npm; then
        log "安装 Node.js 和 npm..."
        
        # 尝试使用NodeSource存储库安装Node.js 18.x
        if ! check_command curl; then
            yum install -y curl
        fi
        
        # 使用NodeSource
        curl -fsSL https://rpm.nodesource.com/setup_18.x | bash -
        yum install -y nodejs
        
        # 验证Node.js安装
        if ! check_command node || ! check_command npm; then
            echo -e "${RED}安装Node.js失败，脚本将继续但Web依赖安装可能会失败${NC}"
        else
            log "Node.js安装成功: $(node --version)"
            log "NPM安装成功: $(npm --version)"
        fi
    fi
    
    # 交叉编译工具链（这可能需要EPEL和额外步骤）
    log "检查交叉编译工具链..."
    if ! check_command x86_64-w64-mingw32-gcc; then
        echo -e "${YELLOW}需要安装MinGW-w64工具链${NC}"
        
        # 检查EPEL是否已安装
        if ! rpm -q epel-release > /dev/null; then
            yum install -y epel-release || {
                echo -e "${RED}安装EPEL失败${NC}"
                exit 1
            }
        fi
        
        # 尝试安装mingw64-gcc
        yum install -y mingw64-gcc mingw64-gcc-c++ || {
            echo -e "${RED}安装交叉编译工具链失败${NC}"
            echo -e "${YELLOW}可能需要手动安装${NC}"
        }
    fi
    
# 其他系统不支持
else
    echo -e "${RED}不支持的操作系统: ${OS}${NC}"
    echo -e "${YELLOW}此脚本仅支持Ubuntu/Debian和CentOS/RHEL系统${NC}"
    echo -e "${YELLOW}请手动安装依赖和开发环境${NC}"
    exit 1
fi

# 验证安装的依赖
log_section "验证已安装的依赖"

# 检查必需工具
REQUIRED_COMMANDS=("gcc" "g++" "make" "cmake" "git" "wget" "curl" "python3")
MISSING_COMMANDS=()

for cmd in "${REQUIRED_COMMANDS[@]}"; do
    if ! check_command $cmd; then
        MISSING_COMMANDS+=($cmd)
    else
        # 获取版本信息
        version=$($cmd --version 2>&1 | head -n 1)
        log "$cmd: $version"
    fi
done

# 检查交叉编译工具（如果需要Windows客户端）
CROSS_COMMANDS=("x86_64-w64-mingw32-gcc" "x86_64-w64-mingw32-g++")
MISSING_CROSS=()

for cmd in "${CROSS_COMMANDS[@]}"; do
    if ! check_command $cmd; then
        MISSING_CROSS+=($cmd)
    else
        # 获取版本信息
        version=$($cmd --version 2>&1 | head -n 1)
        log "$cmd: $version"
    fi
done

# 检查Node.js和npm (Web依赖需要)
if check_command node; then
    node_version=$(node --version 2>&1)
    log "node: $node_version"
else
    log "警告: Node.js未安装。Web依赖安装可能会失败。"
fi

if check_command npm; then
    npm_version=$(npm --version 2>&1)
    log "npm: $npm_version"
else
    log "警告: npm未安装。Web依赖安装可能会失败。"
fi

if [ ${#MISSING_COMMANDS[@]} -ne 0 ]; then
    log_section "警告: 以下工具未安装:"
    for cmd in "${MISSING_COMMANDS[@]}"; do
        log " - $cmd"
    done
    echo -e "${RED}一些必需的依赖未能成功安装. 请手动安装它们.${NC}"
    exit 1
fi

if [ ${#MISSING_CROSS[@]} -ne 0 ]; then
    log "警告: 以下交叉编译工具未安装:"
    for cmd in "${MISSING_CROSS[@]}"; do
        log " - $cmd"
    done
    log "Windows客户端编译可能失败。"
fi

# 创建项目目录结构
log_section "创建项目目录结构"
mkdir -p "$PROJECT_ROOT/build" \
         "$PROJECT_ROOT/build_win" \
         "$PROJECT_ROOT/logs" \
         "$PROJECT_ROOT/downloads" \
         "$PROJECT_ROOT/configs" \
         "$PROJECT_ROOT/bin" \
         "$PROJECT_ROOT/bin/windows" \
         "$PROJECT_ROOT/models" \
         "$PROJECT_ROOT/third_party" \
         "$PROJECT_ROOT/cmake" \
         "$PROJECT_ROOT/include"

# 设置正确的权限
chown -R $REAL_USER:$REAL_GROUP "$PROJECT_ROOT/build" \
                              "$PROJECT_ROOT/build_win" \
                              "$PROJECT_ROOT/logs" \
                              "$PROJECT_ROOT/downloads" \
                              "$PROJECT_ROOT/configs" \
                              "$PROJECT_ROOT/bin" \
                              "$PROJECT_ROOT/bin/windows" \
                              "$PROJECT_ROOT/models" \
                              "$PROJECT_ROOT/third_party" \
                              "$PROJECT_ROOT/cmake" \
                              "$PROJECT_ROOT/include"

# 检查磁盘空间
log_section "检查磁盘空间"
REQUIRED_SPACE=500  # 需要至少500MB可用空间
AVAILABLE_SPACE=$(df -m "$PROJECT_ROOT" | awk 'NR==2 {print $4}')

if [ "$AVAILABLE_SPACE" -lt "$REQUIRED_SPACE" ]; then
    echo -e "${RED}警告: 可用磁盘空间不足! 需要至少${REQUIRED_SPACE}MB, 但只有${AVAILABLE_SPACE}MB可用.${NC}"
    echo -e "${YELLOW}安装可能会失败，请清理磁盘空间后重试.${NC}"
    # 不退出，只警告
fi

# 下载 Linux版ONNXRuntime
log_section "下载 Linux版ONNXRuntime"
ONNX_DIR="$PROJECT_ROOT/third_party/onnxruntime"

if [ ! -d "$ONNX_DIR" ]; then
    log "下载 ONNXRuntime..."
    
    # 创建临时目录用于下载
    TMP_DIR=$(mktemp -d)
    chmod 755 "$TMP_DIR"
    chown $REAL_USER:$REAL_GROUP "$TMP_DIR"
    
    # 下载ONNXRuntime
    ONNX_VERSION="1.15.1"
    ONNX_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/onnxruntime-linux-x64-${ONNX_VERSION}.tgz"
    
    log "从 $ONNX_URL 下载 ONNXRuntime"
    
    # 使用wget下载，最多尝试3次
    MAX_RETRY=3
    RETRY_COUNT=0
    DOWNLOAD_SUCCESS=false
    
    while [ $RETRY_COUNT -lt $MAX_RETRY ] && [ "$DOWNLOAD_SUCCESS" = "false" ]; do
        RETRY_COUNT=$((RETRY_COUNT+1))
        log "下载尝试 $RETRY_COUNT/$MAX_RETRY..."
        
        if wget -q --show-progress --timeout=30 --tries=3 "$ONNX_URL" -O "$TMP_DIR/onnxruntime.tgz"; then
            DOWNLOAD_SUCCESS=true
        else
            log "下载尝试 $RETRY_COUNT 失败，等待5秒后重试..."
            sleep 5
        fi
    done
    
    if [ "$DOWNLOAD_SUCCESS" = "false" ]; then
        echo -e "${RED}下载 ONNXRuntime 失败${NC}"
        rm -rf "$TMP_DIR"
        exit 1
    fi
    
    # 解压ONNXRuntime
    log "解压 ONNXRuntime..."
    mkdir -p "$PROJECT_ROOT/third_party"
    tar -xzf "$TMP_DIR/onnxruntime.tgz" -C "$PROJECT_ROOT/third_party" || {
        echo -e "${RED}解压 ONNXRuntime 失败${NC}"
        rm -rf "$TMP_DIR"
        exit 1
    }
    
    # 重命名目录
    if [ -d "$PROJECT_ROOT/third_party/onnxruntime-linux-x64-${ONNX_VERSION}" ]; then
        mv "$PROJECT_ROOT/third_party/onnxruntime-linux-x64-${ONNX_VERSION}" "$PROJECT_ROOT/third_party/onnxruntime" || {
            echo -e "${RED}重命名 ONNXRuntime 目录失败${NC}"
            rm -rf "$TMP_DIR"
            exit 1
        }
    fi
    
    # 清理临时文件
    rm -rf "$TMP_DIR"
    
    # 验证安装
    if [ ! -f "$ONNX_DIR/lib/libonnxruntime.so" ]; then
        echo -e "${RED}ONNXRuntime 安装不完整: 缺少 libonnxruntime.so${NC}"
        exit 1
    else
        log "ONNXRuntime 成功安装到 $ONNX_DIR"
    fi
    
    # 确保权限正确
    chown -R $REAL_USER:$REAL_GROUP "$ONNX_DIR"
else
    log "ONNXRuntime 已存在于 $ONNX_DIR"
fi

# 下载Windows版ONNXRuntime
log_section "下载Windows版ONNXRuntime"
ONNX_WIN_DIR="$PROJECT_ROOT/third_party/onnxruntime-windows"

if [ ! -d "$ONNX_WIN_DIR" ]; then
    log "下载Windows版ONNXRuntime..."
    
    # 创建临时目录用于下载
    TMP_DIR=$(mktemp -d)
    chmod 755 "$TMP_DIR"
    chown $REAL_USER:$REAL_GROUP "$TMP_DIR"
    
    # 下载Windows版ONNXRuntime
    ONNX_VERSION="1.15.1"
    ONNX_WIN_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/onnxruntime-win-x64-${ONNX_VERSION}.zip"
    
    log "从 $ONNX_WIN_URL 下载Windows版ONNXRuntime"
    
    # 使用wget下载，最多尝试3次
    MAX_RETRY=3
    RETRY_COUNT=0
    DOWNLOAD_SUCCESS=false
    
    while [ $RETRY_COUNT -lt $MAX_RETRY ] && [ "$DOWNLOAD_SUCCESS" = "false" ]; do
        RETRY_COUNT=$((RETRY_COUNT+1))
        log "下载尝试 $RETRY_COUNT/$MAX_RETRY..."
        
        if wget -q --show-progress --timeout=30 --tries=3 "$ONNX_WIN_URL" -O "$TMP_DIR/onnxruntime-windows.zip"; then
            DOWNLOAD_SUCCESS=true
        else
            log "下载尝试 $RETRY_COUNT 失败，等待5秒后重试..."
            sleep 5
        fi
    done
    
    if [ "$DOWNLOAD_SUCCESS" = "false" ]; then
        echo -e "${RED}下载Windows版ONNXRuntime失败${NC}"
        echo -e "${YELLOW}Windows客户端编译将无法进行${NC}"
        rm -rf "$TMP_DIR"
    else
        # 解压Windows版ONNXRuntime
        log "解压Windows版ONNXRuntime..."
        mkdir -p "$ONNX_WIN_DIR"
        
        # 检查是否安装了unzip
        if ! check_command unzip; then
            log "安装unzip工具..."
            if [[ -n "$DEBIAN_BASED" ]]; then
                apt-get install -y unzip
            elif [[ -n "$RHEL_BASED" ]]; then
                yum install -y unzip
            fi
        fi
        
        unzip -q "$TMP_DIR/onnxruntime-windows.zip" -d "$TMP_DIR/extract" || {
            echo -e "${RED}解压Windows版ONNXRuntime失败${NC}"
            rm -rf "$TMP_DIR"
            exit 1
        }
        
        # 移动文件到目标目录
        cp -r "$TMP_DIR/extract"/*/* "$ONNX_WIN_DIR/"
        
        # 清理临时文件
        rm -rf "$TMP_DIR"
        
        # 验证安装
        if [ ! -d "$ONNX_WIN_DIR/lib" ] || [ ! -f "$ONNX_WIN_DIR/lib/onnxruntime.lib" ]; then
            echo -e "${RED}解压后未找到Windows版ONNXRuntime库${NC}"
            echo -e "${YELLOW}Windows客户端编译可能会失败${NC}"
        else
            log "Windows版ONNXRuntime成功安装到 $ONNX_WIN_DIR"
            chown -R $REAL_USER:$REAL_GROUP "$ONNX_WIN_DIR"
        fi
    fi
else
    log "Windows版ONNXRuntime已存在于 $ONNX_WIN_DIR"
fi

# 创建CMake工具链文件
log_section "创建CMake工具链文件"
CMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/windows-toolchain.cmake"

cat > "$CMAKE_TOOLCHAIN_FILE" << 'EOF'
# 交叉编译到Windows的CMake工具链文件
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# 指定使用的编译器
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# 设置查找库和程序时，只在交叉编译环境中查找
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# 设置编译参数
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libgcc")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive")

# Windows特定定义
add_definitions(-DWIN32 -D_WINDOWS)
EOF

chown $REAL_USER:$REAL_GROUP "$CMAKE_TOOLCHAIN_FILE"
log "创建了CMake工具链文件: $CMAKE_TOOLCHAIN_FILE"

# 修复ONNX Runtime头文件路径问题
log_section "修复ONNX Runtime路径问题"

# 创建到ONNX Runtime头文件的符号链接
echo -e "${BLUE}创建ONNX Runtime头文件链接...${NC}"

if [ -d "$ONNX_DIR/include" ]; then
    # 链接所有ONNX Runtime头文件到项目的include目录
    for header in "$ONNX_DIR/include"/*.h; do
        if [ -f "$header" ]; then
            filename=$(basename "$header")
            ln -sf "$header" "$PROJECT_ROOT/include/$filename"
            echo -e "${BLUE}链接了 $filename${NC}"
        fi
    done
    
    # 创建ONNXRuntime目录结构
    mkdir -p "$PROJECT_ROOT/include/onnxruntime/core/session"
    if [ -f "$ONNX_DIR/include/onnxruntime_cxx_api.h" ]; then
        ln -sf "$ONNX_DIR/include/onnxruntime_cxx_api.h" "$PROJECT_ROOT/include/onnxruntime/core/session/"
        echo -e "${GREEN}创建了标准路径结构的 onnxruntime_cxx_api.h 链接${NC}"
    fi
else
    echo -e "${RED}错误: $ONNX_DIR/include 目录不存在${NC}"
    exit 1
fi

# 修复config.h中的引入问题
echo -e "${BLUE}检查 config.h 文件...${NC}"
CONFIG_H_FILE="$PROJECT_ROOT/src/server/config.h"
if [ -f "$CONFIG_H_FILE" ]; then
    # 确保包含了types.h
    if ! grep -q "#include \"../common/types.h\"" "$CONFIG_H_FILE"; then
        echo -e "${YELLOW}修复 config.h 中的包含路径...${NC}"
        # 备份原文件
        cp "$CONFIG_H_FILE" "${CONFIG_H_FILE}.bak"
        
        # 替换为修复后的版本
        cat > "$CONFIG_H_FILE" << 'EOF'
#pragma once

// 标准库头文件
#include <string>
#include <vector>
#include <unordered_map>

// 项目头文件 - 确保正确的包含顺序
#include "../common/constants.h"
#include "../common/types.h"

namespace zero_latency {

// 服务器配置
struct ServerConfig {
    std::string model_path;
    uint16_t port;
    uint16_t web_port;
    uint8_t max_clients;
    uint32_t target_fps;
    float confidence_threshold;
    float nms_threshold;
    size_t max_queue_size;
    bool use_cpu_affinity;
    int cpu_core_id;
    bool use_high_priority;
    
    // 默认构造函数使用默认值
    ServerConfig()
        : model_path(constants::paths::DEFAULT_MODEL_PATH),
          port(constants::DEFAULT_SERVER_PORT),
          web_port(constants::DEFAULT_WEB_PORT),
          max_clients(constants::MAX_CLIENTS),
          target_fps(constants::TARGET_SERVER_FPS),
          confidence_threshold(constants::DEFAULT_CONF_THRESHOLD),
          nms_threshold(constants::DEFAULT_NMS_THRESHOLD),
          max_queue_size(constants::INFERENCE_QUEUE_SIZE),
          use_cpu_affinity(true),
          cpu_core_id(0),
          use_high_priority(true) {
    }
};

// 客户端配置
struct ClientConfig {
    std::string server_ip;
    uint16_t server_port;
    uint8_t game_id;
    uint32_t target_fps;
    uint16_t screen_width;
    uint16_t screen_height;
    
    CompressionSettings compression;
    PredictionParams prediction;
    
    bool auto_connect;
    bool auto_start;
    
    bool enable_aim_assist;
    bool enable_esp;
    bool enable_recoil_control;
    
    bool use_high_priority;
    
    // 默认构造函数使用默认值
    ClientConfig()
        : server_ip("127.0.0.1"),
          server_port(constants::DEFAULT_SERVER_PORT),
          game_id(static_cast<uint8_t>(GameType::CS_1_6)),
          target_fps(constants::TARGET_CLIENT_FPS),
          screen_width(SCREEN_WIDTH),
          screen_height(SCREEN_HEIGHT),
          auto_connect(true),
          auto_start(false),
          enable_aim_assist(true),
          enable_esp(true),
          enable_recoil_control(true),
          use_high_priority(true) {
        
        // 设置默认压缩参数
        compression.quality = 75;
        compression.keyframe_interval = 30;
        compression.use_difference_encoding = true;
        compression.use_roi_encoding = true;
        compression.roi_padding = 20;
        
        // 设置默认预测参数
        prediction.max_prediction_time = 200.0f;
        prediction.position_uncertainty = 0.1f;
        prediction.velocity_uncertainty = 0.2f;
        prediction.acceleration_uncertainty = 0.3f;
        prediction.min_confidence_threshold = 0.5f;
    }
};

// 配置管理类
class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();
    
    // 加载服务器配置
    bool loadServerConfig(const std::string& path, ServerConfig& config);
    
    // 保存服务器配置
    bool saveServerConfig(const std::string& path, const ServerConfig& config);
    
    // 加载客户端配置
    bool loadClientConfig(const std::string& path, ClientConfig& config);
    
    // 保存客户端配置
    bool saveClientConfig(const std::string& path, const ClientConfig& config);
    
    // 导出配置为JSON
    std::string exportConfigToJson(const ClientConfig& config);
    
    // 从JSON导入配置
    bool importConfigFromJson(const std::string& json_str, ClientConfig& config);
    
private:
    // 创建默认配置
    void createDefaultServerConfig(const std::string& path);
    void createDefaultClientConfig(const std::string& path);
};

} // namespace zero_latency
EOF
        echo -e "${GREEN}已修复 config.h 文件${NC}"
    fi
else
    echo -e "${RED}错误: 找不到 $CONFIG_H_FILE${NC}"
fi

# 修复 yolo_engine.h 头文件包含问题
YOLO_ENGINE_H="$PROJECT_ROOT/src/server/yolo_engine.h"
if [ -f "$YOLO_ENGINE_H" ]; then
    echo -e "${BLUE}修复 yolo_engine.h 中的头文件包含...${NC}"
    # 备份原文件
    cp "$YOLO_ENGINE_H" "${YOLO_ENGINE_H}.bak"
    
    # 提取文件内容，并替换ONNX头文件包含部分
    sed -i 's/.*ONNX Runtime 头文件包含.*/#include "onnxruntime_cxx_api.h"/' "$YOLO_ENGINE_H"
    # 移除所有if, elif, else, endif与ONNX包含相关的行
    sed -i '/#if defined.*ONNXRUNTIME_ROOT_DIR/,/#error/d' "$YOLO_ENGINE_H"
    
    echo -e "${GREEN}已修复 yolo_engine.h 文件${NC}"
fi

# 生成一个测试模型 (如果需要)
if [ ! -d "$PROJECT_ROOT/models" ] || [ -z "$(find "$PROJECT_ROOT/models" -name "*.onnx" 2>/dev/null)" ]; then
    echo -e "${YELLOW}未检测到模型文件，尝试生成测试模型...${NC}"
    
    # 检查Python和相关依赖
    if command -v python3 &>/dev/null; then
        echo -e "${BLUE}安装必要的Python依赖...${NC}"
        python3 -m pip install numpy onnx
        
        # 运行模型生成脚本
        if [ -f "$PROJECT_ROOT/scripts/generate_dummy_model.py" ]; then
            mkdir -p "$PROJECT_ROOT/models"
            
            # 修复ModelProto.metadata_props问题
            SCRIPT_FILE="$PROJECT_ROOT/scripts/generate_dummy_model.py"
            if grep -q "helper.make_metadata_props" "$SCRIPT_FILE"; then
                echo -e "${YELLOW}修复模型生成脚本中的API调用...${NC}"
                sed -i 's/helper.make_metadata_props/helper.make_attribute/g' "$SCRIPT_FILE"
            fi
            
            python3 "$SCRIPT_FILE" --output "$PROJECT_ROOT/models/yolo_nano_cs16.onnx"
            if [ $? -eq 0 ]; then
                echo -e "${GREEN}成功生成测试模型${NC}"
            else
                echo -e "${RED}模型生成失败${NC}"
            fi
        else
            echo -e "${RED}找不到模型生成脚本${NC}"
        fi
    else
        echo -e "${RED}未安装Python3，无法生成测试模型${NC}"
    fi
fi

# 设置 ONNXRuntime 环境变量
log_section "设置环境变量"

ONNXRUNTIME_ROOT_DIR="$PROJECT_ROOT/third_party/onnxruntime"
log "ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}"
ONNXRUNTIME_WIN_DIR="$PROJECT_ROOT/third_party/onnxruntime-windows"
log "ONNXRUNTIME_WIN_DIR=${ONNXRUNTIME_WIN_DIR}"

# 为实际用户更新shell配置文件，而不是root
SHELL_CONFIG=""
if [ -f "$REAL_HOME/.bashrc" ]; then
    SHELL_CONFIG="$REAL_HOME/.bashrc"
elif [ -f "$REAL_HOME/.bash_profile" ]; then
    SHELL_CONFIG="$REAL_HOME/.bash_profile"
elif [ -f "$REAL_HOME/.zshrc" ]; then
    SHELL_CONFIG="$REAL_HOME/.zshrc"
fi

if [ -n "$SHELL_CONFIG" ]; then
    log "更新 shell 配置: $SHELL_CONFIG"
    
    # 临时存储环境变量配置
    ENV_CONFIG_FILE=$(mktemp)
    cat > "$ENV_CONFIG_FILE" << EOF

# 零延迟YOLO系统环境变量
export ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}
export ONNXRUNTIME_WIN_DIR=${ONNXRUNTIME_WIN_DIR}
export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:${ONNXRUNTIME_ROOT_DIR}/lib
EOF
    
    # 检查是否已存在环境变量
    if ! grep -q "ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}" "$SHELL_CONFIG"; then
        # 追加到shell配置
        cat "$ENV_CONFIG_FILE" >> "$SHELL_CONFIG"
        chown $REAL_USER:$REAL_GROUP "$SHELL_CONFIG"
    fi
    
    # 清理临时文件
    rm -f "$ENV_CONFIG_FILE"
else
    log "无法找到适当的shell配置文件."
    log "请手动添加以下行到您的shell配置文件:"
    log "export ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}"
    log "export ONNXRUNTIME_WIN_DIR=${ONNXRUNTIME_WIN_DIR}"
    log "export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:${ONNXRUNTIME_ROOT_DIR}/lib"
fi

# 复制配置文件
if [ -f "$PROJECT_ROOT/configs/server.json.example" ] && [ ! -f "$PROJECT_ROOT/configs/server.json" ]; then
    log "复制服务器配置示例..."
    cp "$PROJECT_ROOT/configs/server.json.example" "$PROJECT_ROOT/configs/server.json"
    chown $REAL_USER:$REAL_GROUP "$PROJECT_ROOT/configs/server.json"
fi

if [ -f "$PROJECT_ROOT/configs/client.json.example" ] && [ ! -f "$PROJECT_ROOT/configs/client.json" ]; then
    log "复制客户端配置示例..."
    cp "$PROJECT_ROOT/configs/client.json.example" "$PROJECT_ROOT/configs/client.json"
    chown $REAL_USER:$REAL_GROUP "$PROJECT_ROOT/configs/client.json"
fi

# 导出当前环境变量(用于后续脚本)
export ONNXRUNTIME_ROOT_DIR=${ONNXRUNTIME_ROOT_DIR}
export ONNXRUNTIME_WIN_DIR=${ONNXRUNTIME_WIN_DIR}
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${ONNXRUNTIME_ROOT_DIR}/lib

# 创建ONNX环境验证脚本
log_section "创建环境验证脚本"
VERIFY_SCRIPT="$PROJECT_ROOT/verify_onnx_setup.sh"

cat > "$VERIFY_SCRIPT" << 'EOF'
#!/bin/bash
# 文件: verify_onnx_setup.sh
# 用途: 验证ONNX Runtime环境设置是否正确

# 颜色设置
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}===== ONNX Runtime 环境验证脚本 =====${NC}"

# 检查环境变量
echo -e "${YELLOW}检查环境变量...${NC}"
if [ -z "$ONNXRUNTIME_ROOT_DIR" ]; then
    echo -e "${RED}错误: ONNXRUNTIME_ROOT_DIR 环境变量未设置${NC}"
    echo -e "${YELLOW}请运行: source ~/.bashrc 或 source ~/.bash_profile${NC}"
    exit 1
else
    echo -e "${GREEN}✓ ONNXRUNTIME_ROOT_DIR 已设置: $ONNXRUNTIME_ROOT_DIR${NC}"
fi

# 检查目录结构
echo -e "${YELLOW}检查目录结构...${NC}"
if [ ! -d "$ONNXRUNTIME_ROOT_DIR" ]; then
    echo -e "${RED}错误: $ONNXRUNTIME_ROOT_DIR 目录不存在${NC}"
    exit 1
else
    echo -e "${GREEN}✓ ONNXRUNTIME_ROOT_DIR 目录存在${NC}"
fi

if [ ! -d "$ONNXRUNTIME_ROOT_DIR/include" ]; then
    echo -e "${RED}错误: $ONNXRUNTIME_ROOT_DIR/include 目录不存在${NC}"
    exit 1
else
    echo -e "${GREEN}✓ 包含目录存在${NC}"
fi

if [ ! -d "$ONNXRUNTIME_ROOT_DIR/lib" ]; then
    echo -e "${RED}错误: $ONNXRUNTIME_ROOT_DIR/lib 目录不存在${NC}"
    exit 1
else
    echo -e "${GREEN}✓ 库目录存在${NC}"
fi

# 检查头文件
echo -e "${YELLOW}检查头文件...${NC}"
HEADER_FILES=(
    "onnxruntime_cxx_api.h"
    "onnxruntime_c_api.h"
)

for header in "${HEADER_FILES[@]}"; do
    if [ ! -f "$ONNXRUNTIME_ROOT_DIR/include/$header" ]; then
        echo -e "${RED}错误: 头文件 $header 不存在${NC}"
        exit 1
    else
        echo -e "${GREEN}✓ 头文件 $header 存在${NC}"
    fi
done

# 检查库文件
echo -e "${YELLOW}检查库文件...${NC}"
LIB_FILES=(
    "libonnxruntime.so"
    "libonnxruntime.dylib"
    "onnxruntime.lib"
    "onnxruntime.dll"
)

LIB_FOUND=false
for lib in "${LIB_FILES[@]}"; do
    if [ -f "$ONNXRUNTIME_ROOT_DIR/lib/$lib" ]; then
        echo -e "${GREEN}✓ 找到库文件: $lib${NC}"
        LIB_FOUND=true
    fi
done

if [ "$LIB_FOUND" = false ]; then
    echo -e "${RED}错误: 未找到ONNX Runtime库文件${NC}"
    exit 1
fi

# 检查LD_LIBRARY_PATH
echo -e "${YELLOW}检查 LD_LIBRARY_PATH...${NC}"
if [[ ":$LD_LIBRARY_PATH:" != *":$ONNXRUNTIME_ROOT_DIR/lib:"* ]]; then
    echo -e "${YELLOW}警告: $ONNXRUNTIME_ROOT_DIR/lib 不在 LD_LIBRARY_PATH 中${NC}"
    echo -e "${YELLOW}建议运行: export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:$ONNXRUNTIME_ROOT_DIR/lib${NC}"
else
    echo -e "${GREEN}✓ LD_LIBRARY_PATH 包含 ONNX Runtime 库路径${NC}"
fi

# 检查项目include目录
echo -e "${YELLOW}检查项目include目录...${NC}"
PROJECT_ROOT=$(pwd)
if [ ! -d "$PROJECT_ROOT/include" ]; then
    echo -e "${YELLOW}警告: 项目include目录不存在，创建中...${NC}"
    mkdir -p "$PROJECT_ROOT/include"
fi

# 创建符号链接
echo -e "${YELLOW}创建头文件符号链接...${NC}"
for header in "${HEADER_FILES[@]}"; do
    ln -sf "$ONNXRUNTIME_ROOT_DIR/include/$header" "$PROJECT_ROOT/include/$header"
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ 成功创建符号链接: $header${NC}"
    else
        echo -e "${RED}错误: 无法创建符号链接: $header${NC}"
    fi
done

echo -e "${GREEN}===== ONNX Runtime 环境验证完成! =====${NC}"
echo -e "${GREEN}您的环境看起来已正确配置${NC}"
echo -e "${BLUE}现在您可以运行: ./deploy_backend.sh${NC}"
EOF

chmod +x "$VERIFY_SCRIPT"
chown $REAL_USER:$REAL_GROUP "$VERIFY_SCRIPT"
log "创建了环境验证脚本: $VERIFY_SCRIPT"

# 安装完成
echo -e "${GREEN}===== 环境安装完成 =====${NC}"
echo -e "${BLUE}ONNXRuntime路径: ${ONNXRUNTIME_ROOT_DIR}${NC}"
echo -e "${BLUE}Windows版ONNXRuntime路径: ${ONNXRUNTIME_WIN_DIR}${NC}"
echo -e "${BLUE}CMake工具链文件: ${CMAKE_TOOLCHAIN_FILE}${NC}"
echo -e "${YELLOW}请运行以下命令更新环境变量:${NC}"
echo -e "${BLUE}source ${SHELL_CONFIG}${NC}"
echo -e "${YELLOW}然后验证ONNX环境:${NC}"
echo -e "${BLUE}./verify_onnx_setup.sh${NC}"
echo -e "${GREEN}现在您可以使用后端部署脚本和客户端编译脚本${NC}"