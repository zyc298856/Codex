#pragma once

#include <opencv2/opencv.hpp>
#include <rknn_api.h>

#include <cstddef>
#include <string>
#include <vector>

struct Detection {
  int class_id;
  float score;
  cv::Rect box;
};

class YoloRknnDetector {
 public:
  YoloRknnDetector();
  ~YoloRknnDetector();

  bool Load(const std::string& model_path);
  std::vector<Detection> Infer(const cv::Mat& frame, float score_threshold, float nms_threshold);
  void Release();

  int model_width() const { return model_width_; }
  int model_height() const { return model_height_; }
  bool loaded() const { return loaded_; }

 private:
  struct LetterBoxInfo {
    float scale = 1.0f;
    float pad_x = 0.0f;
    float pad_y = 0.0f;
    int src_width = 0;
    int src_height = 0;
  };

  bool PrepareInput(const cv::Mat& frame, std::vector<unsigned char>* input_u8,
                    LetterBoxInfo* letterbox) const;
  std::vector<Detection> DecodeOutput(const float* data, std::size_t element_count,
                                      const rknn_tensor_attr& output_attr,
                                      const LetterBoxInfo& letterbox, float score_threshold,
                                      float nms_threshold) const;

  rknn_context ctx_;
  rknn_input_output_num io_num_;
  std::vector<rknn_tensor_attr> input_attrs_;
  std::vector<rknn_tensor_attr> output_attrs_;

  int model_width_;
  int model_height_;
  int model_channels_;
  bool loaded_;
};
