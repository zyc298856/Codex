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

struct InferProfile {
  double prepare_ms = 0.0;
  double inputs_set_ms = 0.0;
  double run_ms = 0.0;
  double outputs_get_ms = 0.0;
  double decode_ms = 0.0;
  double outputs_release_ms = 0.0;
  double total_ms = 0.0;
  std::size_t detections = 0;
  bool zero_copy_input = false;
};

class YoloRknnDetector {
 public:
  YoloRknnDetector();
  ~YoloRknnDetector();

  bool Load(const std::string& model_path);
  std::vector<Detection> Infer(const cv::Mat& frame, float score_threshold, float nms_threshold);
  std::vector<Detection> InferProfiled(const cv::Mat& frame, float score_threshold,
                                       float nms_threshold, InferProfile* profile);
  void Release();

  int model_width() const { return model_width_; }
  int model_height() const { return model_height_; }
  int class_count() const { return class_count_; }
  bool zero_copy_input_enabled() const { return zero_copy_input_enabled_; }
  bool rga_preprocess_enabled() const { return rga_preprocess_enabled_; }
  bool rga_cvt_resize_enabled() const { return rga_cvt_resize_enabled_; }
  bool rga_letterbox_enabled() const { return rga_letterbox_enabled_; }
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
  bool ResizeRgbWithRga(const cv::Mat& rgb, cv::Mat* resized, int width, int height) const;
  bool ResizeBgrToRgbWithRga(const cv::Mat& bgr, cv::Mat* resized_rgb, int width,
                             int height) const;
  bool PrepareLetterboxWithRga(const cv::Mat& bgr, std::vector<unsigned char>* input_u8,
                               int resized_w, int resized_h, int pad_left,
                               int pad_top) const;
  bool InitZeroCopyInput();
  bool UseZeroCopyInput(const std::vector<unsigned char>& input_u8);
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
  int class_count_;
  bool zero_copy_input_enabled_;
  bool rga_preprocess_enabled_;
  bool rga_cvt_resize_enabled_;
  bool rga_letterbox_enabled_;
  rknn_tensor_mem* zero_copy_input_mem_;
  rknn_tensor_attr zero_copy_input_attr_;
  bool loaded_;
};
