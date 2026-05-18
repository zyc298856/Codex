cmake_minimum_required(VERSION 3.16)
project(rk_yolo_video LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
  pkg_check_modules(RGA QUIET librga)
  pkg_check_modules(MPP QUIET rockchip_mpp)
  pkg_check_modules(GST_RTSP QUIET gstreamer-1.0 gstreamer-app-1.0 gstreamer-rtsp-server-1.0)
  pkg_check_modules(GST_ALLOCATORS QUIET gstreamer-allocators-1.0)
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

find_path(
  MPP_INCLUDE_DIR
  NAMES rk_mpi.h
  HINTS ${MPP_INCLUDE_DIRS}
  PATHS
    /usr/include/rockchip
    /usr/local/include/rockchip
    /usr/include
    /usr/local/include
)

find_library(
  MPP_LIBRARY
  NAMES rockchip_mpp
  HINTS ${MPP_LIBRARY_DIRS}
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

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
  add_executable(
    rk_yolo_video
    src/main.cpp
    src/yolo_encoder_adapter.cpp
    src/yolo_legacy_roi.cpp
    src/yolo_rknn.cpp
  )
endif()

add_executable(
  rk_yolo_dma_demo
  src/dma_demo.cpp
  src/yolo_rknn.cpp
)

if(GST_RTSP_FOUND)
  add_executable(
    rk_yolo_dma_rtsp_demo
    src/dma_rtsp_demo.cpp
    src/yolo_rknn.cpp
  )
else()
  message(WARNING "GStreamer RTSP development packages not found: rk_yolo_dma_rtsp_demo target disabled")
endif()

if(MPP_INCLUDE_DIR AND MPP_LIBRARY)
  add_executable(
    rk_yolo_mpp_dma_demo
    src/mpp_dma_demo.cpp
    src/yolo_rknn.cpp
  )
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/mpp_file_demo.cpp")
    add_executable(
      rk_yolo_mpp_file_demo
      src/mpp_file_demo.cpp
      src/yolo_rknn.cpp
    )
  endif()
  if(GST_RTSP_FOUND)
    add_executable(
      rk_yolo_mpp_dma_rtsp_demo
      src/mpp_dma_demo.cpp
      src/yolo_rknn.cpp
    )
  else()
    message(WARNING "GStreamer RTSP development packages not found: rk_yolo_mpp_dma_rtsp_demo target disabled")
  endif()
else()
  message(WARNING "Rockchip MPP development files not found: MPP route-B targets disabled")
endif()

if(TARGET rk_yolo_video)
  target_include_directories(
    rk_yolo_video
    PRIVATE
      ${OpenCV_INCLUDE_DIRS}
      ${RKNN_INCLUDE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/../encoder/include
  )
endif()

target_include_directories(
  rk_yolo_dma_demo
  PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${RKNN_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../encoder/include
)

if(TARGET rk_yolo_dma_rtsp_demo)
  target_include_directories(
    rk_yolo_dma_rtsp_demo
    PRIVATE
      ${OpenCV_INCLUDE_DIRS}
      ${RKNN_INCLUDE_DIR}
      ${GST_RTSP_INCLUDE_DIRS}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/../encoder/include
  )
endif()

if(TARGET rk_yolo_mpp_dma_demo)
  target_include_directories(
    rk_yolo_mpp_dma_demo
    PRIVATE
      ${OpenCV_INCLUDE_DIRS}
      ${RKNN_INCLUDE_DIR}
      ${MPP_INCLUDE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/../encoder/include
  )
endif()

if(TARGET rk_yolo_mpp_file_demo)
  target_include_directories(
    rk_yolo_mpp_file_demo
    PRIVATE
      ${OpenCV_INCLUDE_DIRS}
      ${RKNN_INCLUDE_DIR}
      ${MPP_INCLUDE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/../encoder/include
  )
endif()

if(TARGET rk_yolo_mpp_dma_rtsp_demo)
  target_include_directories(
    rk_yolo_mpp_dma_rtsp_demo
    PRIVATE
      ${OpenCV_INCLUDE_DIRS}
      ${RKNN_INCLUDE_DIR}
      ${MPP_INCLUDE_DIR}
      ${GST_RTSP_INCLUDE_DIRS}
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/../encoder/include
  )
endif()

if(TARGET rk_yolo_video)
  target_link_libraries(
    rk_yolo_video
    PRIVATE
      ${OpenCV_LIBS}
      ${RKNN_LIBRARY}
      Threads::Threads
  )
endif()

target_link_libraries(
  rk_yolo_dma_demo
  PRIVATE
    ${OpenCV_LIBS}
    ${RKNN_LIBRARY}
    Threads::Threads
)

if(TARGET rk_yolo_dma_rtsp_demo)
  target_link_libraries(
    rk_yolo_dma_rtsp_demo
    PRIVATE
      ${OpenCV_LIBS}
      ${RKNN_LIBRARY}
      ${GST_RTSP_LIBRARIES}
      Threads::Threads
  )
  target_link_directories(rk_yolo_dma_rtsp_demo PRIVATE ${GST_RTSP_LIBRARY_DIRS})
  target_compile_options(rk_yolo_dma_rtsp_demo PRIVATE ${GST_RTSP_CFLAGS_OTHER})
endif()

if(TARGET rk_yolo_mpp_dma_demo)
  target_compile_definitions(rk_yolo_mpp_dma_demo PRIVATE HAVE_MPP=1)
  target_link_libraries(
    rk_yolo_mpp_dma_demo
    PRIVATE
      ${OpenCV_LIBS}
      ${RKNN_LIBRARY}
      ${MPP_LIBRARY}
      Threads::Threads
  )
endif()

if(TARGET rk_yolo_mpp_file_demo)
  target_compile_definitions(rk_yolo_mpp_file_demo PRIVATE HAVE_MPP=1)
  target_link_libraries(
    rk_yolo_mpp_file_demo
    PRIVATE
      ${OpenCV_LIBS}
      ${RKNN_LIBRARY}
      ${MPP_LIBRARY}
      Threads::Threads
  )
endif()

if(TARGET rk_yolo_mpp_dma_rtsp_demo)
  target_compile_definitions(
    rk_yolo_mpp_dma_rtsp_demo
    PRIVATE
      HAVE_MPP=1
      MPP_DMA_RTSP_DEMO=1
  )
  if(GST_ALLOCATORS_FOUND)
    target_compile_definitions(rk_yolo_mpp_dma_rtsp_demo PRIVATE HAVE_GST_ALLOCATORS=1)
    target_include_directories(rk_yolo_mpp_dma_rtsp_demo PRIVATE ${GST_ALLOCATORS_INCLUDE_DIRS})
    target_link_directories(rk_yolo_mpp_dma_rtsp_demo PRIVATE ${GST_ALLOCATORS_LIBRARY_DIRS})
    target_link_libraries(rk_yolo_mpp_dma_rtsp_demo PRIVATE ${GST_ALLOCATORS_LIBRARIES})
    target_compile_options(rk_yolo_mpp_dma_rtsp_demo PRIVATE ${GST_ALLOCATORS_CFLAGS_OTHER})
  else()
    message(WARNING "GStreamer allocators package not found: RTSP DMABUF output mode disabled")
  endif()
  target_link_libraries(
    rk_yolo_mpp_dma_rtsp_demo
    PRIVATE
      ${OpenCV_LIBS}
      ${RKNN_LIBRARY}
      ${MPP_LIBRARY}
      ${GST_RTSP_LIBRARIES}
      Threads::Threads
  )
  target_link_directories(rk_yolo_mpp_dma_rtsp_demo PRIVATE ${GST_RTSP_LIBRARY_DIRS})
  target_compile_options(rk_yolo_mpp_dma_rtsp_demo PRIVATE ${GST_RTSP_CFLAGS_OTHER})
endif()

if(RGA_FOUND)
  if(TARGET rk_yolo_video)
    target_compile_definitions(rk_yolo_video PRIVATE HAVE_RGA=1)
  endif()
  target_compile_definitions(rk_yolo_dma_demo PRIVATE HAVE_RGA=1)
  if(TARGET rk_yolo_dma_rtsp_demo)
    target_compile_definitions(rk_yolo_dma_rtsp_demo PRIVATE HAVE_RGA=1)
  endif()
  if(TARGET rk_yolo_mpp_dma_demo)
    target_compile_definitions(rk_yolo_mpp_dma_demo PRIVATE HAVE_RGA=1)
  endif()
  if(TARGET rk_yolo_mpp_file_demo)
    target_compile_definitions(rk_yolo_mpp_file_demo PRIVATE HAVE_RGA=1)
  endif()
  if(TARGET rk_yolo_mpp_dma_rtsp_demo)
    target_compile_definitions(rk_yolo_mpp_dma_rtsp_demo PRIVATE HAVE_RGA=1)
  endif()
  if(TARGET rk_yolo_video)
    target_include_directories(rk_yolo_video PRIVATE ${RGA_INCLUDE_DIRS})
  endif()
  target_include_directories(rk_yolo_dma_demo PRIVATE ${RGA_INCLUDE_DIRS})
  if(TARGET rk_yolo_dma_rtsp_demo)
    target_include_directories(rk_yolo_dma_rtsp_demo PRIVATE ${RGA_INCLUDE_DIRS})
  endif()
  if(TARGET rk_yolo_mpp_dma_demo)
    target_include_directories(rk_yolo_mpp_dma_demo PRIVATE ${RGA_INCLUDE_DIRS})
  endif()
  if(TARGET rk_yolo_mpp_file_demo)
    target_include_directories(rk_yolo_mpp_file_demo PRIVATE ${RGA_INCLUDE_DIRS})
  endif()
  if(TARGET rk_yolo_mpp_dma_rtsp_demo)
    target_include_directories(rk_yolo_mpp_dma_rtsp_demo PRIVATE ${RGA_INCLUDE_DIRS})
  endif()
  if(TARGET rk_yolo_video)
    target_link_directories(rk_yolo_video PRIVATE ${RGA_LIBRARY_DIRS})
  endif()
  target_link_directories(rk_yolo_dma_demo PRIVATE ${RGA_LIBRARY_DIRS})
  if(TARGET rk_yolo_dma_rtsp_demo)
    target_link_directories(rk_yolo_dma_rtsp_demo PRIVATE ${RGA_LIBRARY_DIRS})
  endif()
  if(TARGET rk_yolo_mpp_dma_demo)
    target_link_directories(rk_yolo_mpp_dma_demo PRIVATE ${RGA_LIBRARY_DIRS})
  endif()
  if(TARGET rk_yolo_mpp_file_demo)
    target_link_directories(rk_yolo_mpp_file_demo PRIVATE ${RGA_LIBRARY_DIRS})
  endif()
  if(TARGET rk_yolo_mpp_dma_rtsp_demo)
    target_link_directories(rk_yolo_mpp_dma_rtsp_demo PRIVATE ${RGA_LIBRARY_DIRS})
  endif()
  if(TARGET rk_yolo_video)
    target_link_libraries(rk_yolo_video PRIVATE ${RGA_LIBRARIES})
  endif()
  target_link_libraries(rk_yolo_dma_demo PRIVATE ${RGA_LIBRARIES})
  if(TARGET rk_yolo_dma_rtsp_demo)
    target_link_libraries(rk_yolo_dma_rtsp_demo PRIVATE ${RGA_LIBRARIES})
  endif()
  if(TARGET rk_yolo_mpp_dma_demo)
    target_link_libraries(rk_yolo_mpp_dma_demo PRIVATE ${RGA_LIBRARIES})
  endif()
  if(TARGET rk_yolo_mpp_file_demo)
    target_link_libraries(rk_yolo_mpp_file_demo PRIVATE ${RGA_LIBRARIES})
  endif()
  if(TARGET rk_yolo_mpp_dma_rtsp_demo)
    target_link_libraries(rk_yolo_mpp_dma_rtsp_demo PRIVATE ${RGA_LIBRARIES})
  endif()
  message(STATUS "librga found: optional RK_YOLO_PREPROCESS=rga path enabled")
else()
  message(WARNING "librga not found: RK_YOLO_PREPROCESS=rga will fall back to OpenCV")
endif()
