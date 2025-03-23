cmake_minimum_required(VERSION 3.10)
project(zero_latency_yolo VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
option(BUILD_WINDOWS "构建Windows客户端" OFF)

# 添加包含目录
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR})  # 确保能找到src目录

# 设置ONNXRuntime路径
if(NOT DEFINED ENV{ONNXRUNTIME_ROOT_DIR})
    message(STATUS "未找到ONNXRUNTIME_ROOT_DIR环境变量，尝试使用find_package")
    find_package(ONNXRuntime)
    if(NOT ONNXRuntime_FOUND)
        message(FATAL_ERROR "未找到ONNXRuntime，请安装或设置ONNXRUNTIME_ROOT_DIR环境变量")
    endif()
else()
    message(STATUS "ONNXRuntime not found via find_package, using manual setup")
    set(ONNXRUNTIME_ROOT_DIR $ENV{ONNXRUNTIME_ROOT_DIR})
    
    # 确保包含目录存在
    if(NOT EXISTS ${ONNXRUNTIME_ROOT_DIR}/include)
        message(FATAL_ERROR "ONNXRuntime包含目录不存在: ${ONNXRUNTIME_ROOT_DIR}/include")
    endif()
    
    # 检查库文件
    if(NOT EXISTS ${ONNXRUNTIME_ROOT_DIR}/lib)
        message(FATAL_ERROR "ONNXRuntime库目录不存在: ${ONNXRUNTIME_ROOT_DIR}/lib")
    endif()
    
    # 添加包含目录
    include_directories(${ONNXRUNTIME_ROOT_DIR}/include)
endif()

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# 设置构建条件和特定平台代码
if(BUILD_WINDOWS)
    message(STATUS "构建Windows客户端")
    
    # 检查Windows版ONNXRuntime
    if(NOT DEFINED ENV{ONNXRUNTIME_WIN_DIR})
        message(FATAL_ERROR "未找到ONNXRUNTIME_WIN_DIR环境变量，无法构建Windows客户端")
    endif()
    
    set(ONNXRUNTIME_WIN_DIR $ENV{ONNXRUNTIME_WIN_DIR})
    
    # Windows特定定义
    add_definitions(-DWIN32 -D_WINDOWS -DBUILD_CLIENT)
    
    # 添加Windows客户端源文件
    file(GLOB CLIENT_SOURCES 
        "src/client/*.cpp"
        "src/common/*.cpp"
        "src/platform/windows/*.cpp"
    )
    
    # 创建Windows客户端可执行文件
    add_executable(yolo_client ${CLIENT_SOURCES})
    
    # 设置Windows客户端包含目录
    target_include_directories(yolo_client PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${ONNXRUNTIME_WIN_DIR}/include
    )
    
    # 设置Windows客户端链接库
    target_link_directories(yolo_client PRIVATE
        ${ONNXRUNTIME_WIN_DIR}/lib
    )
    
    target_link_libraries(yolo_client
        onnxruntime
        ws2_32
        winmm
    )
    
    # 如果存在资源文件，添加到编译
    if(EXISTS "${PROJECT_SOURCE_DIR}/resources/client.rc")
        target_sources(yolo_client PRIVATE "${PROJECT_SOURCE_DIR}/resources/client.rc")
    endif()
    
    # 设置输出名称
    set_target_properties(yolo_client PROPERTIES
        OUTPUT_NAME "yolo_client"
        SUFFIX ".exe"
    )
    
    # 安装客户端
    install(TARGETS yolo_client
        RUNTIME DESTINATION bin
    )
else()
    message(STATUS "客户端编译仅支持Windows平台")
endif()

# 始终构建服务端
message(STATUS "构建Linux服务端")

# 添加Linux服务端特定定义
add_definitions(-DLINUX -DBUILD_SERVER)

# 添加服务端源文件
file(GLOB SERVER_SOURCES 
    "src/server/*.cpp"
    "src/common/*.cpp"
    "src/platform/linux/*.cpp"
)

# 创建服务端可执行文件
add_executable(yolo_server ${SERVER_SOURCES})

# 设置服务端包含目录
target_include_directories(yolo_server PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${ONNXRUNTIME_ROOT_DIR}/include
)

# 设置服务端链接库
target_link_directories(yolo_server PRIVATE
    ${ONNXRUNTIME_ROOT_DIR}/lib
)

target_link_libraries(yolo_server
    onnxruntime
    pthread
    dl
)

# 找到线程库并链接
find_package(Threads REQUIRED)
target_link_libraries(yolo_server
    Threads::Threads
)

# 安装服务端
install(TARGETS yolo_server
    RUNTIME DESTINATION bin
)

# 安装配置文件
install(DIRECTORY ${PROJECT_SOURCE_DIR}/configs/
    DESTINATION configs
    FILES_MATCHING PATTERN "*.json"
)

# 安装模型文件
install(DIRECTORY ${PROJECT_SOURCE_DIR}/models/
    DESTINATION models
    FILES_MATCHING PATTERN "*.onnx"
)

# 输出配置信息
message(STATUS "项目配置完成")
message(STATUS "CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "ONNXRUNTIME_ROOT_DIR: ${ONNXRUNTIME_ROOT_DIR}")
if(BUILD_WINDOWS)
    message(STATUS "ONNXRUNTIME_WIN_DIR: ${ONNXRUNTIME_WIN_DIR}")
endif()