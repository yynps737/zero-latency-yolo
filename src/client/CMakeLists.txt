set(CLIENT_SOURCES
    main.cpp
    prediction_engine.cpp
    screen_capture.cpp
    dual_engine.cpp
    renderer.cpp
    input_control.cpp
    network.cpp
    config.cpp
)

set(CLIENT_HEADERS
    prediction_engine.h
    screen_capture.h
    dual_engine.h
    renderer.h
    input_control.h
    network.h
    config.h
)

# 检查系统是否为Windows（客户端需要Windows API）
if(WIN32)
    # Windows平台特定的源文件和头文件
    list(APPEND CLIENT_SOURCES 
        win32_utils.cpp
    )
    
    list(APPEND CLIENT_HEADERS 
        win32_utils.h
    )
    
    # 添加Windows库
    set(PLATFORM_LIBS
        ws2_32    # Winsock
        user32    # User interface
        gdi32     # Graphics
        d3d11     # DirectX 11
        dxgi      # DirectX Graphics Infrastructure
        dwmapi    # Desktop Window Manager
    )
    
    # 设置Windows定义
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX)
    
    # 添加可执行文件
    add_executable(yolo_client ${CLIENT_SOURCES} ${CLIENT_HEADERS})
    
    # 链接Windows库
    target_link_libraries(yolo_client PRIVATE ${PLATFORM_LIBS} Threads::Threads)
    
    # 如果使用MSVC编译器，设置特定选项
    if(MSVC)
        target_compile_options(yolo_client PRIVATE /W4 /O2)
        # 静态链接C/C++运行时以减少依赖
        set_target_properties(yolo_client PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    endif()
    
    # 安装目标
    install(TARGETS yolo_client DESTINATION bin)
else()
    message(STATUS "客户端编译仅支持Windows平台")
    # 在非Windows平台上不创建客户端目标
endif()