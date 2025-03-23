cmake_minimum_required(VERSION 3.10)
project(zero_latency_yolo VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3 -march=native")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)
find_package(ONNXRuntime QUIET)
if(NOT ONNXRuntime_FOUND)
  message(STATUS "ONNXRuntime not found via find_package, using manual setup")
  set(ONNXRUNTIME_ROOT_DIR "${CMAKE_SOURCE_DIR}/third_party/onnxruntime" CACHE PATH "ONNX Runtime root directory")
  set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_ROOT_DIR}/include")
  set(ONNXRUNTIME_LIBRARIES "${ONNXRUNTIME_ROOT_DIR}/lib/libonnxruntime.so")
endif()

# Common include directories
include_directories(${CMAKE_SOURCE_DIR}/src)

# Add server subdirectory
add_subdirectory(src/server)

# Add client subdirectory
add_subdirectory(src/client)

# Install targets
install(TARGETS yolo_server yolo_client
        RUNTIME DESTINATION bin)

# Install configuration files
install(DIRECTORY ${CMAKE_SOURCE_DIR}/configs/
        DESTINATION etc/zero_latency)

# Install models
install(DIRECTORY ${CMAKE_SOURCE_DIR}/models/
        DESTINATION share/zero_latency/models)