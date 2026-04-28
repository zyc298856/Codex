#include "yolo_rknn.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>

#ifdef HAVE_RGA
#include <cstddef>
#include <rga/im2d.h>
#include <rga/rga.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

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

bool LayoutDebugEnabled() {
  const char* env_value = std::getenv("RK_YOLO_DEBUG_LAYOUT");
  return env_value != nullptr && env_value[0] != '\0' && env_value[0] != '0';
}

bool ZeroCopyInputEnabled() {
  const char* env_value = std::getenv("RK_YOLO_ZERO_COPY_INPUT");
  return env_value != nullptr && env_value[0] != '\0' && env_value[0] != '0';
}

bool RgaPreprocessRequested() {
  const char* env_value = std::getenv("RK_YOLO_PREPROCESS");
  if (env_value != nullptr) {
    const std::string value(env_value);
    return value == "rga" || value == "RGA";
  }

  env_value = std::getenv("RK_YOLO_USE_RGA");
  return env_value != nullptr && env_value[0] != '\0' && env_value[0] != '0';
}

bool LooksLikeFeatureAxis(int dim) {
  // Current deployments use compact per-box feature layouts such as:
  // - 84  : generic COCO-style raw head
  // - 5   : single-class raw head (cx, cy, w, h, score)
  // - 6   : end2end direct boxes (x1, y1, x2, y2, score, class)
  return dim >= 5 && dim <= 256;
}

struct OutputLayout {
  int feature_count = 0;
  int box_count = 0;
  bool direct_boxes = false;
};

OutputLayout ResolveOutputLayout(const rknn_tensor_attr& output_attr) {
  OutputLayout layout;
  if (output_attr.n_dims < 3) {
    return layout;
  }

  const int last = output_attr.n_dims - 1;
  const int dim_a = output_attr.dims[last - 1];
  const int dim_b = output_attr.dims[last];

  if (dim_a == 6 && dim_b > dim_a) {
    layout.feature_count = dim_a;
    layout.box_count = dim_b;
    layout.direct_boxes = true;
  } else if (dim_b == 6 && dim_a > dim_b) {
    layout.feature_count = dim_b;
    layout.box_count = dim_a;
    layout.direct_boxes = true;
  } else if (LooksLikeFeatureAxis(dim_a) && dim_b > dim_a) {
    layout.feature_count = dim_a;
    layout.box_count = dim_b;
  } else if (LooksLikeFeatureAxis(dim_b) && dim_a > dim_b) {
    layout.feature_count = dim_b;
    layout.box_count = dim_a;
  }

  return layout;
}

void PrintLayoutStats(const float* data, const OutputLayout& layout) {
  if (data == nullptr || layout.feature_count <= 0 || layout.box_count <= 0) {
    return;
  }

  auto print_stats = [&](bool channel_first, const char* label) {
    std::cout << "layout-debug " << label << std::endl;
    for (int feature_idx = 0; feature_idx < layout.feature_count; ++feature_idx) {
      float min_value = std::numeric_limits<float>::infinity();
      float max_value = -std::numeric_limits<float>::infinity();
      double sum = 0.0;
      for (int box_idx = 0; box_idx < layout.box_count; ++box_idx) {
        const float value = channel_first
                                ? data[static_cast<std::size_t>(feature_idx) * layout.box_count + box_idx]
                                : data[static_cast<std::size_t>(box_idx) * layout.feature_count + feature_idx];
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
        sum += value;
      }
      std::cout << "  feature[" << feature_idx << "] min=" << min_value << " max=" << max_value
                << " mean=" << (sum / layout.box_count) << std::endl;
    }
  };

  print_stats(true, "channel_first");
  print_stats(false, "box_first");
}

}  // namespace

YoloRknnDetector::YoloRknnDetector()
    : ctx_(0),
      io_num_{},
      model_width_(0),
      model_height_(0),
      model_channels_(0),
      class_count_(0),
      zero_copy_input_enabled_(false),
      rga_preprocess_enabled_(false),
      zero_copy_input_mem_(nullptr),
      zero_copy_input_attr_{},
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

  const OutputLayout output_layout = ResolveOutputLayout(output_attrs_.front());
  class_count_ = output_layout.direct_boxes ? 0 : std::max(0, output_layout.feature_count - 4);
  std::cout << "resolved model classes: " << class_count_ << std::endl;

  rga_preprocess_enabled_ = RgaPreprocessRequested();
#ifdef HAVE_RGA
  std::cout << "preprocess=" << (rga_preprocess_enabled_ ? "rga_resize" : "opencv")
            << std::endl;
#else
  if (rga_preprocess_enabled_) {
    std::cout << "preprocess=rga_requested_but_unavailable_fallback_opencv" << std::endl;
    rga_preprocess_enabled_ = false;
  } else {
    std::cout << "preprocess=opencv" << std::endl;
  }
#endif

  if (ZeroCopyInputEnabled()) {
    zero_copy_input_enabled_ = InitZeroCopyInput();
    std::cout << "zero_copy_input=" << (zero_copy_input_enabled_ ? "on" : "failed_fallback")
              << std::endl;
  } else {
    std::cout << "zero_copy_input=off" << std::endl;
  }

  loaded_ = true;
  return true;
}

void YoloRknnDetector::Release() {
  if (ctx_ != 0 && zero_copy_input_mem_ != nullptr) {
    rknn_destroy_mem(ctx_, zero_copy_input_mem_);
    zero_copy_input_mem_ = nullptr;
  }
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
  class_count_ = 0;
  zero_copy_input_enabled_ = false;
  rga_preprocess_enabled_ = false;
  zero_copy_input_attr_ = {};
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
  bool resized_with_rga = false;
  if (rga_preprocess_enabled_) {
    resized_with_rga = ResizeRgbWithRga(rgb, &resized, resized_w, resized_h);
  }
  if (!resized_with_rga) {
    cv::resize(rgb, resized, cv::Size(resized_w, resized_h), 0.0, 0.0, cv::INTER_LINEAR);
  }

  cv::Mat padded;
  cv::copyMakeBorder(resized, padded, pad_top, pad_bottom, pad_left, pad_right, cv::BORDER_CONSTANT,
                     cv::Scalar(114, 114, 114));

  input_u8->assign(padded.data, padded.data + padded.total() * padded.elemSize());
  return true;
}

bool YoloRknnDetector::ResizeRgbWithRga(const cv::Mat& rgb, cv::Mat* resized, int width,
                                        int height) const {
#ifndef HAVE_RGA
  (void)rgb;
  (void)resized;
  (void)width;
  (void)height;
  return false;
#else
  static bool warned = false;
  if (rgb.empty() || resized == nullptr || width <= 0 || height <= 0 || rgb.type() != CV_8UC3) {
    if (!warned) {
      std::cerr << "RGA resize skipped: invalid RGB input" << std::endl;
      warned = true;
    }
    return false;
  }

  const cv::Mat rgb_contiguous = rgb.isContinuous() ? rgb : rgb.clone();
  resized->create(height, width, CV_8UC3);
  if (!resized->isContinuous()) {
    *resized = resized->clone();
  }

  rga_buffer_t src = wrapbuffer_virtualaddr(const_cast<unsigned char*>(rgb_contiguous.data),
                                            rgb_contiguous.cols, rgb_contiguous.rows,
                                            RK_FORMAT_RGB_888);
  rga_buffer_t dst = wrapbuffer_virtualaddr(resized->data, width, height, RK_FORMAT_RGB_888);

  IM_STATUS status = imcheck(src, dst, {}, {});
  if (status != IM_STATUS_NOERROR) {
    if (!warned) {
      std::cerr << "RGA imcheck failed: " << imStrError(status)
                << "; fallback to OpenCV resize" << std::endl;
      warned = true;
    }
    return false;
  }

  status = imresize(src, dst);
  if (status != IM_STATUS_SUCCESS) {
    if (!warned) {
      std::cerr << "RGA imresize failed: " << imStrError(status)
                << "; fallback to OpenCV resize" << std::endl;
      warned = true;
    }
    return false;
  }
  return true;
#endif
}

bool YoloRknnDetector::InitZeroCopyInput() {
  if (ctx_ == 0 || input_attrs_.empty() || model_width_ <= 0 || model_height_ <= 0 ||
      model_channels_ <= 0) {
    return false;
  }

  zero_copy_input_attr_ = input_attrs_.front();
  zero_copy_input_attr_.index = 0;
  zero_copy_input_attr_.type = RKNN_TENSOR_UINT8;
  zero_copy_input_attr_.fmt = RKNN_TENSOR_NHWC;
  zero_copy_input_attr_.pass_through = 0;
  zero_copy_input_attr_.size =
      static_cast<uint32_t>(model_width_ * model_height_ * model_channels_);
  if (zero_copy_input_attr_.size_with_stride < zero_copy_input_attr_.size) {
    zero_copy_input_attr_.size_with_stride = zero_copy_input_attr_.size;
  }

  zero_copy_input_mem_ = rknn_create_mem(ctx_, zero_copy_input_attr_.size_with_stride);
  if (zero_copy_input_mem_ == nullptr) {
    std::cerr << "rknn_create_mem for zero-copy input failed" << std::endl;
    return false;
  }
  if (zero_copy_input_mem_->virt_addr == nullptr) {
    std::cerr << "rknn_create_mem returned null zero-copy virtual address" << std::endl;
    rknn_destroy_mem(ctx_, zero_copy_input_mem_);
    zero_copy_input_mem_ = nullptr;
    return false;
  }

  std::memset(zero_copy_input_mem_->virt_addr, 0, zero_copy_input_mem_->size);
  const int ret = rknn_set_io_mem(ctx_, zero_copy_input_mem_, &zero_copy_input_attr_);
  if (ret != RKNN_SUCC) {
    std::cerr << "rknn_set_io_mem for zero-copy input failed: " << ret << std::endl;
    rknn_destroy_mem(ctx_, zero_copy_input_mem_);
    zero_copy_input_mem_ = nullptr;
    return false;
  }

  std::cout << "zero-copy input mem size=" << zero_copy_input_mem_->size
            << " attr_size=" << zero_copy_input_attr_.size
            << " stride_size=" << zero_copy_input_attr_.size_with_stride << std::endl;
  return true;
}

bool YoloRknnDetector::UseZeroCopyInput(const std::vector<unsigned char>& input_u8) {
  if (!zero_copy_input_enabled_ || zero_copy_input_mem_ == nullptr ||
      zero_copy_input_mem_->virt_addr == nullptr) {
    return false;
  }
  if (input_u8.size() > zero_copy_input_mem_->size) {
    std::cerr << "zero-copy input buffer is too small: need " << input_u8.size()
              << " bytes, have " << zero_copy_input_mem_->size << std::endl;
    return false;
  }

  std::memcpy(zero_copy_input_mem_->virt_addr, input_u8.data(), input_u8.size());
  if (input_u8.size() < zero_copy_input_mem_->size) {
    std::memset(static_cast<unsigned char*>(zero_copy_input_mem_->virt_addr) + input_u8.size(), 0,
                zero_copy_input_mem_->size - input_u8.size());
  }
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

  const OutputLayout output_layout = ResolveOutputLayout(output_attr);
  const int feature_count = output_layout.feature_count;
  const int box_count = output_layout.box_count;
  const bool direct_boxes = output_layout.direct_boxes;

  if (feature_count <= 4 || box_count <= 0) {
    std::cerr << "unexpected output shape, cannot decode detections: "
              << DimsToString(output_attr) << std::endl;
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
  return InferProfiled(frame, score_threshold, nms_threshold, nullptr);
}

std::vector<Detection> YoloRknnDetector::InferProfiled(const cv::Mat& frame,
                                                       float score_threshold,
                                                       float nms_threshold,
                                                       InferProfile* profile) {
  if (!loaded_) {
    return {};
  }

  InferProfile local_profile;
  InferProfile& p = (profile != nullptr) ? *profile : local_profile;
  p = InferProfile{};
  p.zero_copy_input = zero_copy_input_enabled_;
  const auto total_start = Clock::now();

  std::vector<unsigned char> input_u8;
  LetterBoxInfo letterbox;
  const auto prepare_start = Clock::now();
  if (!PrepareInput(frame, &input_u8, &letterbox)) {
    std::cerr << "failed to prepare input frame" << std::endl;
    return {};
  }
  const auto prepare_end = Clock::now();
  p.prepare_ms = ElapsedMs(prepare_start, prepare_end);

  rknn_input input = {};
  input.index = 0;
  input.pass_through = 0;
  input.type = RKNN_TENSOR_UINT8;
  input.fmt = RKNN_TENSOR_NHWC;
  input.size = static_cast<uint32_t>(input_u8.size());
  input.buf = input_u8.data();

  const auto inputs_set_start = Clock::now();
  int ret = RKNN_SUCC;
  if (zero_copy_input_enabled_) {
    ret = UseZeroCopyInput(input_u8) ? RKNN_SUCC : -1;
  } else {
    ret = rknn_inputs_set(ctx_, 1, &input);
  }
  const auto inputs_set_end = Clock::now();
  p.inputs_set_ms = ElapsedMs(inputs_set_start, inputs_set_end);
  if (ret != RKNN_SUCC) {
    std::cerr << (zero_copy_input_enabled_ ? "zero-copy input update failed: "
                                           : "rknn_inputs_set failed: ")
              << ret << std::endl;
    return {};
  }

  const auto run_start = Clock::now();
  ret = rknn_run(ctx_, nullptr);
  const auto run_end = Clock::now();
  p.run_ms = ElapsedMs(run_start, run_end);
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

  const auto outputs_get_start = Clock::now();
  ret = rknn_outputs_get(ctx_, io_num_.n_output, outputs.data(), nullptr);
  const auto outputs_get_end = Clock::now();
  p.outputs_get_ms = ElapsedMs(outputs_get_start, outputs_get_end);
  if (ret != RKNN_SUCC) {
    std::cerr << "rknn_outputs_get failed: " << ret << std::endl;
    return {};
  }

  const float* output_data = static_cast<const float*>(outputs[0].buf);
  const std::size_t output_count = output_attrs_[0].n_elems;
  const OutputLayout output_layout = ResolveOutputLayout(output_attrs_[0]);
  if (LayoutDebugEnabled()) {
    PrintLayoutStats(output_data, output_layout);
  }
  if (RawScoreDebugEnabled() && output_data != nullptr) {
    float max_score = 0.0f;
    for (std::size_t i = 4; i < output_count; ++i) {
      max_score = std::max(max_score, output_data[i]);
    }
    std::cout << "frame max raw score: " << max_score << std::endl;
  }

  const auto decode_start = Clock::now();
  std::vector<Detection> detections =
      DecodeOutput(output_data, output_count, output_attrs_[0], letterbox, score_threshold, nms_threshold);
  const auto decode_end = Clock::now();
  p.decode_ms = ElapsedMs(decode_start, decode_end);

  const auto release_start = Clock::now();
  rknn_outputs_release(ctx_, io_num_.n_output, outputs.data());
  const auto release_end = Clock::now();
  p.outputs_release_ms = ElapsedMs(release_start, release_end);
  p.detections = detections.size();
  p.total_ms = ElapsedMs(total_start, release_end);
  return detections;
}
