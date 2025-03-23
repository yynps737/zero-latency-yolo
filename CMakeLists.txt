cmake_minimum_required(VERSION 3.10)
project(zero_latency_yolo VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
option(BUILD_WINDOWS "构建Windows客户端" OFF)
option(BUILD_WEB_SERVER "构建Web服务器" ON)
option(USE_CUDA "使用CUDA加速推理" OFF)
option(USE_SIMD "使用SIMD指令加速" ON)
option(BUILD_TESTS "构建单元测试" OFF)

# 版本信息
set(ZL_VERSION_MAJOR 1)
set(ZL_VERSION_MINOR 0)
set(ZL_VERSION_PATCH 0)
set(ZL_VERSION "${ZL_VERSION_MAJOR}.${ZL_VERSION_MINOR}.${ZL_VERSION_PATCH}")

# 定义宏
add_definitions(-DZL_VERSION="${ZL_VERSION}")
add_definitions(-DZL_VERSION_MAJOR=${ZL_VERSION_MAJOR})
add_definitions(-DZL_VERSION_MINOR=${ZL_VERSION_MINOR})
add_definitions(-DZL_VERSION_PATCH=${ZL_VERSION_PATCH})

# 添加第三方库路径
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 查找线程库
find_package(Threads REQUIRED)

# 设置ONNXRuntime路径
if(NOT DEFINED ENV{ONNXRUNTIME_ROOT_DIR})
    message(STATUS "ONNXRUNTIME_ROOT_DIR environment variable not found, trying to use find_package")
    find_package(ONNXRuntime)
    if(NOT ONNXRuntime_FOUND)
        message(FATAL_ERROR "ONNXRuntime not found, please install or set ONNXRUNTIME_ROOT_DIR environment variable")
    endif()
else()
    message(STATUS "Using ONNXRUNTIME_ROOT_DIR from environment")
    set(ONNXRUNTIME_ROOT_DIR $ENV{ONNXRUNTIME_ROOT_DIR})
    
    # 确保包含目录存在
    if(NOT EXISTS ${ONNXRUNTIME_ROOT_DIR}/include)
        message(FATAL_ERROR "ONNXRuntime include directory not found: ${ONNXRUNTIME_ROOT_DIR}/include")
    endif()
    
    # 检查库文件
    if(NOT EXISTS ${ONNXRUNTIME_ROOT_DIR}/lib)
        message(FATAL_ERROR "ONNXRuntime library directory not found: ${ONNXRUNTIME_ROOT_DIR}/lib")
    endif()
    
    # 添加包含目录
    include_directories(${ONNXRUNTIME_ROOT_DIR}/include)
endif()

# 检查CUDA支持
if(USE_CUDA)
    find_package(CUDA)
    if(CUDA_FOUND)
        message(STATUS "CUDA found, enabling CUDA support")
        add_definitions(-DUSE_CUDA)
        include_directories(${CUDA_INCLUDE_DIRS})
    else()
        message(WARNING "CUDA requested but not found, disabling CUDA support")
        set(USE_CUDA OFF)
    endif()
endif()

# 检查SIMD支持
if(USE_SIMD)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-mavx" COMPILER_SUPPORTS_AVX)
    check_cxx_compiler_flag("-msse4.2" COMPILER_SUPPORTS_SSE42)
    
    if(COMPILER_SUPPORTS_AVX)
        message(STATUS "AVX support enabled")
        add_definitions(-DUSE_AVX)
        set(SIMD_FLAGS "${SIMD_FLAGS} -mavx")
    endif()
    
    if(COMPILER_SUPPORTS_SSE42)
        message(STATUS "SSE4.2 support enabled")
        add_definitions(-DUSE_SSE42)
        set(SIMD_FLAGS "${SIMD_FLAGS} -msse4.2")
    endif()
endif()

# 设置JSON库路径
set(JSON_BuildTests OFF CACHE INTERNAL "")
add_subdirectory(${THIRD_PARTY_DIR}/json)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# 添加包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${THIRD_PARTY_DIR}/json/include
)

# 收集源文件
file(GLOB_RECURSE COMMON_SOURCES 
    "src/common/*.cpp"
)

file(GLOB_RECURSE INFERENCE_SOURCES 
    "src/inference/*.cpp"
)

file(GLOB_RECURSE GAME_SOURCES 
    "src/game/*.cpp"
)

file(GLOB_RECURSE NETWORK_SOURCES 
    "src/network/*.cpp"
)

file(GLOB_RECURSE SERVER_SOURCES 
    "src/server/*.cpp"
)

# 定义服务器目标
add_executable(zero_latency_server
    ${COMMON_SOURCES}
    ${INFERENCE_SOURCES}
    ${GAME_SOURCES}
    ${NETWORK_SOURCES}
    ${SERVER_SOURCES}
)

# 添加定义
target_compile_definitions(zero_latency_server PRIVATE
    -DBUILD_SERVER
)

# 设置服务器链接库
target_link_libraries(zero_latency_server PRIVATE
    Threads::Threads
    stdc++fs
    dl
    nlohmann_json::nlohmann_json
)

# 设置ONNXRuntime库路径
if(EXISTS ${ONNXRUNTIME_ROOT_DIR}/lib/libonnxruntime.so)
    target_link_libraries(zero_latency_server PRIVATE ${ONNXRUNTIME_ROOT_DIR}/lib/libonnxruntime.so)
elseif(EXISTS ${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.dll)
    target_link_libraries(zero_latency_server PRIVATE ${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.dll)
elseif(EXISTS ${ONNXRUNTIME_ROOT_DIR}/lib/libonnxruntime.dylib)
    target_link_libraries(zero_latency_server PRIVATE ${ONNXRUNTIME_ROOT_DIR}/lib/libonnxruntime.dylib)
else()
    find_library(ONNXRUNTIME_LIBRARIES 
        NAMES onnxruntime libonnxruntime
        PATHS ${ONNXRUNTIME_ROOT_DIR}/lib
        NO_DEFAULT_PATH)
    
    if(NOT ONNXRUNTIME_LIBRARIES)
        message(FATAL_ERROR "ONNXRuntime library not found in ${ONNXRUNTIME_ROOT_DIR}/lib")
    endif()
    
    target_link_libraries(zero_latency_server PRIVATE ${ONNXRUNTIME_LIBRARIES})
endif()

# 添加CUDA库
if(USE_CUDA AND CUDA_FOUND)
    target_link_libraries(zero_latency_server PRIVATE ${CUDA_LIBRARIES})
endif()

# 设置Windows客户端条件和特定代码
if(BUILD_WINDOWS)
    message(STATUS "Building Windows client")
    
    # 检查Windows版ONNXRuntime
    if(NOT DEFINED ENV{ONNXRUNTIME_WIN_DIR})
        message(FATAL_ERROR "ONNXRUNTIME_WIN_DIR environment variable not found, cannot build Windows client")
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
    add_executable(zero_latency_client ${CLIENT_SOURCES})
    
    # 设置Windows客户端包含目录
    target_include_directories(zero_latency_client PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${ONNXRUNTIME_WIN_DIR}/include
        ${THIRD_PARTY_DIR}/json/include
    )
    
    # 设置Windows客户端链接库
    target_link_directories(zero_latency_client PRIVATE
        ${ONNXRUNTIME_WIN_DIR}/lib
    )
    
    target_link_libraries(zero_latency_client PRIVATE
        onnxruntime
        ws2_32
        winmm
        nlohmann_json::nlohmann_json
    )
    
    # 如果存在资源文件，添加到编译
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/client.rc")
        target_sources(zero_latency_client PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources/client.rc")
    endif()
    
    # 设置输出名称
    set_target_properties(zero_latency_client PROPERTIES
        OUTPUT_NAME "zero_latency_client"
        SUFFIX ".exe"
    )
    
    # 安装客户端
    install(TARGETS zero_latency_client
        RUNTIME DESTINATION bin
    )
else()
    message(STATUS "Client build is only supported on Windows platform")
endif()

# 构建Web服务器
if(BUILD_WEB_SERVER)
    message(STATUS "Building Web server")
    
    # 添加Web服务器源文件
    file(GLOB WEB_SERVER_SOURCES 
        "src/web/server.cpp"
    )
    
    # 创建Web服务器可执行文件
    add_executable(zero_latency_web_server ${WEB_SERVER_SOURCES})
    
    # 设置Web服务器包含目录
    target_include_directories(zero_latency_web_server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${THIRD_PARTY_DIR}/json/include
    )
    
    # 设置Web服务器链接库
    target_link_libraries(zero_latency_web_server PRIVATE
        Threads::Threads
        stdc++fs
        nlohmann_json::nlohmann_json
    )
    
    # 设置输出名称
    set_target_properties(zero_latency_web_server PROPERTIES
        OUTPUT_NAME "zero_latency_web_server"
    )
    
    # 安装Web服务器
    install(TARGETS zero_latency_web_server
        RUNTIME DESTINATION bin
    )
endif()

# 优化标志
if(CMAKE_BUILD_TYPE MATCHES "Release")
    target_compile_options(zero_latency_server PRIVATE 
        -O3 -march=native -mtune=native -ffast-math -ftree-vectorize ${SIMD_FLAGS}
    )
    
    if(BUILD_WINDOWS)
        target_compile_options(zero_latency_client PRIVATE 
            -O3 -march=native -mtune=native -ffast-math -ftree-vectorize ${SIMD_FLAGS}
        )
    endif()
    
    if(BUILD_WEB_SERVER)
        target_compile_options(zero_latency_web_server PRIVATE 
            -O3 -march=native -mtune=native -ffast-math -ftree-vectorize ${SIMD_FLAGS}
        )
    endif()
endif()

# 单元测试
if(BUILD_TESTS)
    message(STATUS "Building unit tests")
    
    # 查找GoogleTest
    find_package(GTest)
    if(NOT GTest_FOUND)
        # 如果没有安装，使用FetchContent下载
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG release-1.11.0
        )
        FetchContent_MakeAvailable(googletest)
    endif()
    
    # 添加测试源文件
    file(GLOB_RECURSE TEST_SOURCES 
        "tests/*.cpp"
    )
    
    # 创建测试可执行文件
    add_executable(zero_latency_tests ${TEST_SOURCES})
    
    # 设置测试包含目录
    target_include_directories(zero_latency_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${THIRD_PARTY_DIR}/json/include
    )
    
    # 设置测试链接库
    target_link_libraries(zero_latency_tests PRIVATE
        Threads::Threads
        stdc++fs
        nlohmann_json::nlohmann_json
        gtest
        gtest_main
    )
    
    # 启用测试
    enable_testing()
    add_test(NAME zero_latency_tests COMMAND zero_latency_tests)
endif()

# 安装目标
install(TARGETS zero_latency_server
    RUNTIME DESTINATION bin
)

# 安装配置文件
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/configs/
    DESTINATION configs
    FILES_MATCHING PATTERN "*.json"
)

# 安装模型文件
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/models/
    DESTINATION models
    FILES_MATCHING PATTERN "*.onnx"
)

# 输出配置信息
message(STATUS "Build configuration:")
message(STATUS "  CMake version: ${CMAKE_VERSION}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  ONNXRuntime dir: ${ONNXRUNTIME_ROOT_DIR}")
message(STATUS "  Use CUDA: ${USE_CUDA}")
message(STATUS "  Use SIMD: ${USE_SIMD}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Build Windows client: ${BUILD_WINDOWS}")
message(STATUS "  Build Web server: ${BUILD_WEB_SERVER}")
message(STATUS "  Version: ${ZL_VERSION}")