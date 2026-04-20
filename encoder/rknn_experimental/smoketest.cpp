#include "../yolo/yolov10_rknn.h"

#include <opencv2/opencv.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool ConvertBgrToI420(const cv::Mat& bgr, std::vector<unsigned char>* i420, int* stride_y) {
  if (bgr.empty() || i420 == nullptr || stride_y == nullptr) {
    return false;
  }

  cv::Mat yuv_i420;
  cv::cvtColor(bgr, yuv_i420, cv::COLOR_BGR2YUV_I420);
  i420->assign(yuv_i420.data, yuv_i420.data + yuv_i420.total() * yuv_i420.elemSize());
  *stride_y = bgr.cols;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <input_video> <frame_count> [score_threshold]\n";
    return 1;
  }

  const std::string input_video = argv[1];
  const int frame_limit = std::max(1, std::atoi(argv[2]));
  const float score_threshold = (argc >= 4) ? std::strtof(argv[3], nullptr) : 0.30f;

  cv::VideoCapture cap(input_video);
  if (!cap.isOpened()) {
    std::cerr << "failed to open input video: " << input_video << std::endl;
    return 2;
  }

  AVElement_t video_state;
  std::memset(&video_state, 0, sizeof(video_state));
  video_state.i_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
  video_state.i_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
  video_state.f_prob = score_threshold;
  BlockBufferInit(&video_state.video_object, 32, 1 << 20, 1);

  void* handle = open_yolo_rknn(video_state.i_width, video_state.i_height);
  if (handle == nullptr) {
    std::cerr << "open_yolo_rknn failed" << std::endl;
    return 3;
  }

  int processed = 0;
  int roi_messages = 0;
  cv::Mat frame;
  while (processed < frame_limit && cap.read(frame)) {
    std::vector<unsigned char> i420;
    int stride_y = 0;
    if (!ConvertBgrToI420(frame, &i420, &stride_y)) {
      std::cerr << "failed to convert frame " << processed + 1 << " to I420" << std::endl;
      close_yolo_rknn(handle);
      return 4;
    }

    const int detections =
        yolo_infer_rknn(&video_state, handle, i420.data(), stride_y, 0);
    const int queued = BlockBufferGetNum(&video_state.video_object);
    if (queued > 0) {
      block_t* block = BlockBufferGet(&video_state.video_object);
      if (block != nullptr && block->p_buffer != nullptr) {
        std::cout << "frame=" << processed + 1 << " detections=" << detections
                  << " roi=" << reinterpret_cast<const char*>(block->p_buffer) << std::endl;
        ++roi_messages;
      }
      BlockBufferGetOff(&video_state.video_object);
    } else {
      std::cout << "frame=" << processed + 1 << " detections=" << detections << " roi=<none>"
                << std::endl;
    }

    ++processed;
  }

  close_yolo_rknn(handle);
  std::cout << "processed=" << processed << " roi_messages=" << roi_messages << std::endl;
  return 0;
}
