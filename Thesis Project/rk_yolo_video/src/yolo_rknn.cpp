#include "yolo_rknn.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>
#include <sstream>

namespace {

std::string TensorTypeToString(rknn_tensor_type type) {
  switch (type) {
    case RKNN_TENSOR_FLOAT32:
      return "float32";
    case RKNN_TENSOR_FLOAT16:
      return "float16";
    case RKNN_TENSOR_INT8:
      return "int8";
    case RKNN_TENSOR_UINT8:
      return "uint8";
    default:
      return "other";
  }
}

std::string TensorFmtToString(rknn_tensor_format fmt) {
  switch (fmt) {
    case RKNN_TENSOR_NCHW:
      return "nchw";
    case RKNN_TENSOR_NHWC:
      return "nhwc";
    default:
      return "other";
  }
}

std::string DimsToString(const rknn_tensor_attr& attr) {
  std::ostringstream oss;
  oss << "[";
  for (unsigned int i = 0; i < attr.n_dims; ++i) {
    if (i > 0) {
      oss << ", ";
    }
    oss << attr.dims[i];
  }
  oss << "]";
  return oss.str();
}

void LogTensorAttr(const char* prefix, const rknn_tensor_attr& attr) {
  std::cout << prefix << " index=" << attr.index << " name=" << attr.name
            << " dims=" << DimsToString(attr) << " elems=" << attr.n_elems
            << " size=" << attr.size << " fmt=" << TensorFmtToString(attr.fmt)
            << " type=" << TensorTypeToString(attr.type) << std::endl;
}

float IoU(const cv::Rect2f& a, const cv::Rect2f& b) {
  const float area_a = a.area();
  const float area_b = b.area();
  if (area_a <= 0.0f || area_b <= 0.0f) {
    return 0.0f;
  }

  const float inter_x1 = std::max(a.x, b.x);
  const float inter_y1 = std::max(a.y, b.y);
  const float inter_x2 = std::min(a.x + a.width, b.x + b.width);
  const float inter_y2 = std::min(a.y + a.height, b.y + b.height);

  const float inter_w = std::max(0.0f, inter_x2 - inter_x1);
  const float inter_h = std::max(0.0f, inter_y2 - inter_y1);
  const float inter_area = inter_w * inter_h;
  return inter_area / (area_a + area_b - inter_area + 1e-6f);
}

bool RawScoreDebugEnabled() {
  const char* env_value = std::getenv("RK_YOLO_DEBUG_SCORES");
  return env_value != nullptr && env_value[0] != '\0' && env_value[0] != '0';
}

}  // namespace

YoloRknnDetector::YoloRknnDetector()
    : ctx_(0),
      io_num_{},
      model_width_(0),
      model_height_(0),
      model_channels_(0),
      loaded_(false) {}

YoloRknnDetector::~YoloRknnDetector() { Release(); }

bool YoloRknnDetector::Load(const std::string& model_path) {
  Release();

  int ret = rknn_init(&ctx_, const_cast<char*>(model_path.c_str()), 0, 0, nullptr);
  if (ret != RKNN_SUCC) {
    std::cerr << "rknn_init failed: " << ret << std::endl;
    return false;
  }

  ret = rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num_, sizeof(io_num_));
  if (ret != RKNN_SUCC) {
    std::cerr << "RKNN_QUERY_IN_OUT_NUM failed: " << ret << std::endl;
    Release();
    return false;
  }

  input_attrs_.assign(io_num_.n_input, rknn_tensor_attr{});
  output_attrs_.assign(io_num_.n_output, rknn_tensor_attr{});

  std::cout << "model input num: " << io_num_.n_input
            << ", output num: " << io_num_.n_output << std::endl;

  for (uint32_t i = 0; i < io_num_.n_input; ++i) {
    input_attrs_[i].index = i;
    ret = rknn_query(ctx_, RKNN_QUERY_INPUT_ATTR, &input_attrs_[i], sizeof(rknn_tensor_attr));
    if (ret != RKNN_SUCC) {
      std::cerr << "RKNN_QUERY_INPUT_ATTR failed at index " << i << ": " << ret << std::endl;
      Release();
      return false;
    }
    LogTensorAttr("input", input_attrs_[i]);
  }

  for (uint32_t i = 0; i < io_num_.n_output; ++i) {
    output_attrs_[i].index = i;
    ret = rknn_query(ctx_, RKNN_QUERY_OUTPUT_ATTR, &output_attrs_[i], sizeof(rknn_tensor_attr));
    if (ret != RKNN_SUCC) {
      std::cerr << "RKNN_QUERY_OUTPUT_ATTR failed at index " << i << ": " << ret << std::endl;
      Release();
      return false;
    }
    LogTensorAttr("output", output_attrs_[i]);
  }

  const rknn_tensor_attr& input = input_attrs_.front();
  if (input.fmt == RKNN_TENSOR_NCHW) {
    model_channels_ = input.dims[1];
    model_height_ = input.dims[2];
    model_width_ = input.dims[3];
  } else {
    model_height_ = input.dims[1];
    model_width_ = input.dims[2];
    model_channels_ = input.dims[3];
  }

  std::cout << "resolved model input: " << model_width_ << "x" << model_height_
            << " channels=" << model_channels_ << std::endl;

  loaded_ = true;
  return true;
}

void YoloRknnDetector::Release() {
  if (ctx_ != 0) {
    rknn_destroy(ctx_);
    ctx_ = 0;
  }
  input_attrs_.clear();
  output_attrs_.clear();
  io_num_ = {};
  model_width_ = 0;
  model_height_ = 0;
  model_channels_ = 0;
  loaded_ = false;
}

bool YoloRknnDetector::PrepareInput(const cv::Mat& frame, std::vector<unsigned char>* input_u8,
                                    LetterBoxInfo* letterbox) const {
  if (frame.empty() || model_width_ <= 0 || model_height_ <= 0) {
    return false;
  }

  letterbox->src_width = frame.cols;
  letterbox->src_height = frame.rows;

  const float scale =
      std::min(static_cast<float>(model_width_) / frame.cols, static_cast<float>(model_height_) / frame.rows);
  const int resized_w = std::max(1, static_cast<int>(std::round(frame.cols * scale)));
  const int resized_h = std::max(1, static_cast<int>(std::round(frame.rows * scale)));
  const int pad_left = (model_width_ - resized_w) / 2;
  const int pad_top = (model_height_ - resized_h) / 2;
  const int pad_right = model_width_ - resized_w - pad_left;
  const int pad_bottom = model_height_ - resized_h - pad_top;

  letterbox->scale = scale;
  letterbox->pad_x = static_cast<float>(pad_left);
  letterbox->pad_y = static_cast<float>(pad_top);

  cv::Mat rgb;
  cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);

  cv::Mat resized;
  cv::resize(rgb, resized, cv::Size(resized_w, resized_h), 0.0, 0.0, cv::INTER_LINEAR);

  cv::Mat padded;
  cv::copyMakeBorder(resized, padded, pad_top, pad_bottom, pad_left, pad_right, cv::BORDER_CONSTANT,
                     cv::Scalar(114, 114, 114));

  input_u8->assign(padded.data, padded.data + padded.total() * padded.elemSize());
  return true;
}

std::vector<Detection> YoloRknnDetector::DecodeOutput(const float* data, std::size_t element_count,
                                                      const rknn_tensor_attr& output_attr,
                                                      const LetterBoxInfo& letterbox,
                                                      float score_threshold,
                                                      float nms_threshold) const {
  std::vector<Detection> candidates;

  if (output_attr.n_dims < 2 || data == nullptr || element_count == 0) {
    return candidates;
  }

  int feature_count = 0;
  int box_count = 0;
  bool direct_boxes = false;
  if (output_attr.n_dims >= 3) {
    const int last = output_attr.n_dims - 1;
    if (output_attr.dims[last - 1] == 84) {
      feature_count = output_attr.dims[last - 1];
      box_count = output_attr.dims[last];
    } else if (output_attr.dims[last] == 84) {
      box_count = output_attr.dims[last - 1];
      feature_count = output_attr.dims[last];
    } else if (output_attr.dims[last] == 6) {
      box_count = output_attr.dims[last - 1];
      feature_count = output_attr.dims[last];
      direct_boxes = true;
    } else if (output_attr.dims[last - 1] == 6) {
      feature_count = output_attr.dims[last - 1];
      box_count = output_attr.dims[last];
      direct_boxes = true;
    }
  }

  if (feature_count <= 4 || box_count <= 0) {
    std::cerr << "unexpected output shape, cannot decode detections" << std::endl;
    return candidates;
  }

  const bool channel_first = (output_attr.dims[output_attr.n_dims - 2] == feature_count);
  const int class_count = direct_boxes ? 0 : (feature_count - 4);

  for (int box_idx = 0; box_idx < box_count; ++box_idx) {
    auto value_at = [&](int feature_idx) -> float {
      if (channel_first) {
        return data[static_cast<std::size_t>(feature_idx) * box_count + box_idx];
      }
      return data[static_cast<std::size_t>(box_idx) * feature_count + feature_idx];
    };

    int best_class = -1;
    float best_score = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;

    if (direct_boxes) {
      x1 = value_at(0);
      y1 = value_at(1);
      x2 = value_at(2);
      y2 = value_at(3);
      best_score = value_at(4);
      best_class = static_cast<int>(std::round(value_at(5)));
    } else {
      for (int class_idx = 0; class_idx < class_count; ++class_idx) {
        const float score = value_at(4 + class_idx);
        if (score > best_score) {
          best_score = score;
          best_class = class_idx;
        }
      }
      const float cx = value_at(0);
      const float cy = value_at(1);
      const float w = value_at(2);
      const float h = value_at(3);
      x1 = cx - w * 0.5f;
      y1 = cy - h * 0.5f;
      x2 = cx + w * 0.5f;
      y2 = cy + h * 0.5f;
    }

    if (best_score < score_threshold || best_class < 0) {
      continue;
    }

    x1 = (x1 - letterbox.pad_x) / letterbox.scale;
    y1 = (y1 - letterbox.pad_y) / letterbox.scale;
    x2 = (x2 - letterbox.pad_x) / letterbox.scale;
    y2 = (y2 - letterbox.pad_y) / letterbox.scale;

    x1 = std::clamp(x1, 0.0f, static_cast<float>(letterbox.src_width - 1));
    y1 = std::clamp(y1, 0.0f, static_cast<float>(letterbox.src_height - 1));
    x2 = std::clamp(x2, 0.0f, static_cast<float>(letterbox.src_width - 1));
    y2 = std::clamp(y2, 0.0f, static_cast<float>(letterbox.src_height - 1));

    const int left = static_cast<int>(std::round(x1));
    const int top = static_cast<int>(std::round(y1));
    const int right = static_cast<int>(std::round(x2));
    const int bottom = static_cast<int>(std::round(y2));

    if (right <= left || bottom <= top) {
      continue;
    }

    candidates.push_back({best_class, best_score, cv::Rect(left, top, right - left, bottom - top)});
  }

  if (candidates.empty()) {
    return candidates;
  }

  std::vector<int> order(candidates.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    return candidates[a].score > candidates[b].score;
  });

  std::vector<Detection> selected;
  std::vector<bool> removed(candidates.size(), false);
  for (std::size_t i = 0; i < order.size(); ++i) {
    const int current = order[i];
    if (removed[current]) {
      continue;
    }
    selected.push_back(candidates[current]);

    const cv::Rect2f box_a = candidates[current].box;
    for (std::size_t j = i + 1; j < order.size(); ++j) {
      const int other = order[j];
      if (removed[other] || candidates[other].class_id != candidates[current].class_id) {
        continue;
      }
      const cv::Rect2f box_b = candidates[other].box;
      if (IoU(box_a, box_b) > nms_threshold) {
        removed[other] = true;
      }
    }
  }

  return selected;
}

std::vector<Detection> YoloRknnDetector::Infer(const cv::Mat& frame, float score_threshold,
                                               float nms_threshold) {
  if (!loaded_) {
    return {};
  }

  std::vector<unsigned char> input_u8;
  LetterBoxInfo letterbox;
  if (!PrepareInput(frame, &input_u8, &letterbox)) {
    std::cerr << "failed to prepare input frame" << std::endl;
    return {};
  }

  rknn_input input = {};
  input.index = 0;
  input.pass_through = 0;
  input.type = RKNN_TENSOR_UINT8;
  input.fmt = RKNN_TENSOR_NHWC;
  input.size = static_cast<uint32_t>(input_u8.size());
  input.buf = input_u8.data();

  int ret = rknn_inputs_set(ctx_, 1, &input);
  if (ret != RKNN_SUCC) {
    std::cerr << "rknn_inputs_set failed: " << ret << std::endl;
    return {};
  }

  ret = rknn_run(ctx_, nullptr);
  if (ret != RKNN_SUCC) {
    std::cerr << "rknn_run failed: " << ret << std::endl;
    return {};
  }

  std::vector<rknn_output> outputs(io_num_.n_output);
  for (uint32_t i = 0; i < io_num_.n_output; ++i) {
    outputs[i].index = i;
    outputs[i].want_float = 1;
    outputs[i].is_prealloc = 0;
  }

  ret = rknn_outputs_get(ctx_, io_num_.n_output, outputs.data(), nullptr);
  if (ret != RKNN_SUCC) {
    std::cerr << "rknn_outputs_get failed: " << ret << std::endl;
    return {};
  }

  const float* output_data = static_cast<const float*>(outputs[0].buf);
  const std::size_t output_count = output_attrs_[0].n_elems;
  if (RawScoreDebugEnabled() && output_data != nullptr) {
    float max_score = 0.0f;
    for (std::size_t i = 4; i < output_count; ++i) {
      max_score = std::max(max_score, output_data[i]);
    }
    std::cout << "frame max raw score: " << max_score << std::endl;
  }

  std::vector<Detection> detections =
      DecodeOutput(output_data, output_count, output_attrs_[0], letterbox, score_threshold, nms_threshold);

  rknn_outputs_release(ctx_, io_num_.n_output, outputs.data());
  return detections;
}
