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

add_executable(yolo_server ${SERVER_SOURCES} ${SERVER_HEADERS})

target_include_directories(yolo_server PRIVATE
    ${ONNXRUNTIME_INCLUDE_DIRS}
)

target_link_libraries(yolo_server PRIVATE
    ${ONNXRUNTIME_LIBRARIES}
    Threads::Threads
    dl
    rt
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