#include "yolo_encoder_adapter.h"

#include <opencv2/imgproc.hpp>

#include <cstring>
#include <vector>

bool YoloEncoderAdapter::LoadModel(const std::string& model_path) {
  return detector_.Load(model_path);
}

void YoloEncoderAdapter::Release() { detector_.Release(); }

bool YoloEncoderAdapter::InferI420(const uint8_t* i420_data, int width, int height, int stride_y,
                                   float score_threshold, float nms_threshold,
                                   LegacyYoloOutput* output) {
  if (i420_data == nullptr || width <= 0 || height <= 0 || stride_y < width || output == nullptr) {
    return false;
  }

  std::vector<uint8_t> packed_i420 = PackI420Frame(i420_data, width, height, stride_y);
  if (packed_i420.empty()) {
    return false;
  }

  cv::Mat i420_mat(height * 3 / 2, width, CV_8UC1, packed_i420.data());
  cv::Mat bgr;
  cv::cvtColor(i420_mat, bgr, cv::COLOR_YUV2BGR_I420);

  output->detections = detector_.Infer(bgr, score_threshold, nms_threshold);
  output->prob = BuildLegacyYoloProbBuffer(output->detections);
  output->roi_json = BuildLegacyRoiJson(output->detections);
  return true;
}

std::vector<uint8_t> YoloEncoderAdapter::PackI420Frame(const uint8_t* i420_data, int width,
                                                       int height, int stride_y) const {
  const int stride_uv = stride_y / 2;
  const int chroma_width = width / 2;
  const int chroma_height = height / 2;

  std::vector<uint8_t> packed(static_cast<std::size_t>(width * height * 3 / 2), 0);
  uint8_t* dst_y = packed.data();
  uint8_t* dst_u = dst_y + width * height;
  uint8_t* dst_v = dst_u + chroma_width * chroma_height;

  const uint8_t* src_y = i420_data;
  const uint8_t* src_u = src_y + stride_y * height;
  const uint8_t* src_v = src_u + stride_uv * chroma_height;

  for (int row = 0; row < height; ++row) {
    std::memcpy(dst_y + row * width, src_y + row * stride_y, static_cast<std::size_t>(width));
  }

  for (int row = 0; row < chroma_height; ++row) {
    std::memcpy(dst_u + row * chroma_width, src_u + row * stride_uv,
                static_cast<std::size_t>(chroma_width));
    std::memcpy(dst_v + row * chroma_width, src_v + row * stride_uv,
                static_cast<std::size_t>(chroma_width));
  }

  return packed;
}
