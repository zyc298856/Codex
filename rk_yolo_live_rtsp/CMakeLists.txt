cmake_minimum_required(VERSION 3.16)
project(rk_yolo_live_rtsp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0)

find_path(
  RKNN_INCLUDE_DIR
  NAMES rknn_api.h
  HINTS ENV RKNN_API_PATH
  PATH_SUFFIXES include
  PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/../encoder/include
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

add_executable(
  rk_yolo_live_rtsp
  src/main.cpp
  ../rk_yolo_video/src/yolo_rknn.cpp
)

target_include_directories(
  rk_yolo_live_rtsp
  PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${GST_INCLUDE_DIRS}
    ${RKNN_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../rk_yolo_video/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../encoder/include
)

target_link_libraries(
  rk_yolo_live_rtsp
  PRIVATE
    ${OpenCV_LIBS}
    ${GST_LIBRARIES}
    ${RKNN_LIBRARY}
    Threads::Threads
)

target_compile_options(
  rk_yolo_live_rtsp
  PRIVATE
    ${GST_CFLAGS_OTHER}
)
