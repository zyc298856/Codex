cmake_minimum_required(VERSION 3.16)
project(rk_yolo_video LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)
find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
  pkg_check_modules(RGA QUIET librga)
endif()

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
  rk_yolo_video
  src/main.cpp
  src/yolo_encoder_adapter.cpp
  src/yolo_legacy_roi.cpp
  src/yolo_rknn.cpp
)

target_include_directories(
  rk_yolo_video
  PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${RKNN_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../encoder/include
)

target_link_libraries(
  rk_yolo_video
  PRIVATE
    ${OpenCV_LIBS}
    ${RKNN_LIBRARY}
)

if(RGA_FOUND)
  target_compile_definitions(rk_yolo_video PRIVATE HAVE_RGA=1)
  target_include_directories(rk_yolo_video PRIVATE ${RGA_INCLUDE_DIRS})
  target_link_directories(rk_yolo_video PRIVATE ${RGA_LIBRARY_DIRS})
  target_link_libraries(rk_yolo_video PRIVATE ${RGA_LIBRARIES})
  message(STATUS "librga found: optional RK_YOLO_PREPROCESS=rga path enabled")
else()
  message(WARNING "librga not found: RK_YOLO_PREPROCESS=rga will fall back to OpenCV")
endif()
