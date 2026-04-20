cmake_minimum_required(VERSION 3.16)
project(encoder_yolo_rknn_experimental LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

find_path(
  RKNN_INCLUDE_DIR
  NAMES rknn_api.h
  HINTS ENV RKNN_API_PATH
  PATH_SUFFIXES include
  PATHS
    ../include
    /usr/include
    /usr/local/include
)

find_library(
  RKNN_LIBRARY
  NAMES rknnrt
  HINTS ENV RKNN_API_PATH
  PATH_SUFFIXES lib lib64 lib/Linux/aarch64 Linux/aarch64
  PATHS
    /lib
    /usr/lib
    /usr/lib64
    /usr/lib/aarch64-linux-gnu
    /usr/local/lib
)

if(NOT RKNN_INCLUDE_DIR OR NOT RKNN_LIBRARY)
  message(FATAL_ERROR "RKNN runtime not found. Set RKNN_API_PATH to the runtime package root or install rknn_api.h and librknnrt.")
endif()

add_library(
  encoder_yolo_rknn
  STATIC
    ../block.c
    ../cJSON.c
    ../yolo/yolov10_rknn.cpp
    ../../rk_yolo_video/src/yolo_encoder_adapter.cpp
    ../../rk_yolo_video/src/yolo_legacy_roi.cpp
    ../../rk_yolo_video/src/yolo_rknn.cpp
)

target_include_directories(
  encoder_yolo_rknn
  PUBLIC
    ${OpenCV_INCLUDE_DIRS}
    ${RKNN_INCLUDE_DIR}
    ../
    ../../rk_yolo_video/include
)

target_link_libraries(
  encoder_yolo_rknn
  PUBLIC
    ${OpenCV_LIBS}
    ${RKNN_LIBRARY}
    Threads::Threads
)

target_compile_definitions(
  encoder_yolo_rknn
  PUBLIC
    ENABLE_RKNN_YOLO_BRIDGE
)

add_executable(
  encoder_yolo_rknn_smoketest
  smoketest.cpp
)

target_include_directories(
  encoder_yolo_rknn_smoketest
  PRIVATE
    ../
)

target_link_libraries(
  encoder_yolo_rknn_smoketest
  PRIVATE
    encoder_yolo_rknn
)
