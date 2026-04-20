#pragma once

#include "yolo_legacy_roi.h"
#include "yolo_rknn.h"

#include <cstdint>
#include <string>
#include <vector>

struct LegacyYoloOutput {
  std::vector<Detection> detections;
  std::vector<float> prob;
  std::string roi_json;
};

class YoloEncoderAdapter {
 public:
  bool LoadModel(const std::string& model_path);
  void Release();

  bool InferI420(const uint8_t* i420_data, int width, int height, int stride_y,
                 float score_threshold, float nms_threshold, LegacyYoloOutput* output);

 private:
  std::vector<uint8_t> PackI420Frame(const uint8_t* i420_data, int width, int height,
                                     int stride_y) const;

  YoloRknnDetector detector_;
};
