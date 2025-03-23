set(SERVER_SOURCES
    main.cpp
    yolo_engine.cpp
    game_adapter.cpp
    network.cpp
    config.cpp
)

set(SERVER_HEADERS
    yolo_engine.h
    game_adapter.h
    network.h
    config.h
)

# 确保包含common目录的文件
file(GLOB COMMON_SOURCES
    ${PROJECT_SOURCE_DIR}/src/common/*.cpp
)

file(GLOB COMMON_HEADERS
    ${PROJECT_SOURCE_DIR}/src/common/*.h
)

add_executable(yolo_server 
    ${SERVER_SOURCES} 
    ${SERVER_HEADERS}
    ${COMMON_SOURCES}
    ${COMMON_HEADERS}
)

target_include_directories(yolo_server PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/include
    ${ONNXRUNTIME_ROOT_DIR}/include
)

# 设置ONNXRuntime库路径
if(EXISTS ${ONNXRUNTIME_ROOT_DIR}/lib/libonnxruntime.so)
    set(ONNXRUNTIME_LIBRARIES ${ONNXRUNTIME_ROOT_DIR}/lib/libonnxruntime.so)
elseif(EXISTS ${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.dll)
    set(ONNXRUNTIME_LIBRARIES ${ONNXRUNTIME_ROOT_DIR}/lib/onnxruntime.dll)
elseif(EXISTS ${ONNXRUNTIME_ROOT_DIR}/lib/libonnxruntime.dylib)
    set(ONNXRUNTIME_LIBRARIES ${ONNXRUNTIME_ROOT_DIR}/lib/libonnxruntime.dylib)
else()
    find_library(ONNXRUNTIME_LIBRARIES 
        NAMES onnxruntime libonnxruntime
        PATHS ${ONNXRUNTIME_ROOT_DIR}/lib
        NO_DEFAULT_PATH)
endif()

if(NOT ONNXRUNTIME_LIBRARIES)
    message(FATAL_ERROR "找不到ONNXRuntime库文件在 ${ONNXRUNTIME_ROOT_DIR}/lib")
endif()

message(STATUS "Using ONNXRuntime libraries: ${ONNXRUNTIME_LIBRARIES}")

target_link_libraries(yolo_server PRIVATE
    ${ONNXRUNTIME_LIBRARIES}
    Threads::Threads
    dl
)

# 添加必要的宏定义
target_compile_definitions(yolo_server PRIVATE
    -DBUILD_SERVER
)

# 优化标志
if(CMAKE_BUILD_TYPE MATCHES "Release")
    target_compile_options(yolo_server PRIVATE 
        -O3 -march=native -mtune=native -ffast-math -ftree-vectorize
    )
    
    # 检查AVX/SSE支持
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-mavx HAS_AVX)
    check_cxx_compiler_flag(-msse4.2 HAS_SSE42)
    
    if(HAS_AVX)
        target_compile_options(yolo_server PRIVATE -mavx)
    endif()
    
    if(HAS_SSE42)
        target_compile_options(yolo_server PRIVATE -msse4.2)
    endif()
endif()

# 安装目标
install(TARGETS yolo_server DESTINATION bin)