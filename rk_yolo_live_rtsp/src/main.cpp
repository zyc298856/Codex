#include "yolo_rknn.h"

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <opencv2/opencv.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

const char* const kCocoClassNames[80] = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
    "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
    "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra",
    "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
    "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
    "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
    "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier",
    "toothbrush"};

constexpr int kDefaultWidth = 640;
constexpr int kDefaultHeight = 480;
constexpr int kDefaultFps = 15;
constexpr float kDefaultScoreThreshold = 0.30f;
constexpr float kDefaultNmsThreshold = 0.45f;
constexpr int kDefaultDetectEveryN = 1;
constexpr float kDefaultRoiMarginRatio = 0.35f;
constexpr float kDefaultRoiMinCoverageRatio = 0.55f;
constexpr int kDefaultRoiFullFrameRefresh = 5;
constexpr std::size_t kCaptureQueueCapacity = 2;
constexpr std::size_t kPublishQueueCapacity = 2;

std::atomic<bool> g_stop{false};

void HandleSignal(int) { g_stop.store(true); }

template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity)
      : capacity_(capacity), stopped_(false), dropped_items_(0) {}

  void Push(T item) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
      return;
    }
    if (queue_.size() >= capacity_) {
      queue_.pop_front();
      ++dropped_items_;
    }
    queue_.push_back(std::move(item));
    cond_.notify_one();
  }

  bool WaitPop(T* item) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this]() { return stopped_ || !queue_.empty(); });
    if (queue_.empty()) {
      return false;
    }
    *item = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
  }

  void Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    cond_.notify_all();
  }

  std::size_t dropped_items() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_items_;
  }

 private:
  const std::size_t capacity_;
  mutable std::mutex mutex_;
  std::condition_variable cond_;
  std::deque<T> queue_;
  bool stopped_;
  std::size_t dropped_items_;
};

template <typename T>
class BlockingQueue {
 public:
  BlockingQueue() : stopped_(false) {}

  void Push(T item) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
      return;
    }
    queue_.push_back(std::move(item));
    cond_.notify_one();
  }

  bool WaitPop(T* item) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this]() { return stopped_ || !queue_.empty(); });
    if (queue_.empty()) {
      return false;
    }
    *item = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }

  void Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    cond_.notify_all();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cond_;
  std::deque<T> queue_;
  bool stopped_;
};

struct FramePacket {
  std::uint64_t index = 0;
  cv::Mat image;
  std::chrono::steady_clock::time_point captured_at;
  std::chrono::steady_clock::time_point inferred_at;
  double work_ms = 0.0;
  std::size_t detections = 0;
  bool ran_inference = false;
  bool used_roi_crop = false;
};

struct PipelineStats {
  std::atomic<std::uint64_t> captured_frames{0};
  std::atomic<std::uint64_t> inferred_frames{0};
  std::atomic<std::uint64_t> published_frames{0};
  std::atomic<std::uint64_t> npu_inference_runs{0};
  std::atomic<std::uint64_t> reused_frames{0};
  std::atomic<std::uint64_t> roi_crop_runs{0};
  std::atomic<std::uint64_t> dispatch_dropped_frames{0};
};

struct RoiConfig {
  bool enabled = true;
  float margin_ratio = kDefaultRoiMarginRatio;
  float min_coverage_ratio = kDefaultRoiMinCoverageRatio;
  int full_frame_refresh = kDefaultRoiFullFrameRefresh;
};

enum class TrackMode {
  kMotion,
  kOpticalFlow,
};

struct BoxSmootherConfig {
  bool enabled = true;
  float alpha = 0.60f;
  float min_iou = 0.10f;
};

struct CameraTuneConfig {
  bool enabled = true;
  std::string match_name = "HBS Camera";
  int zoom_absolute = 20;
  bool focus_auto = false;
  int focus_absolute = 260;
  int settle_ms = 350;
  int warmup_grabs = 6;
};

struct InferResult {
  FramePacket packet;
  std::vector<Detection> detections;
};

struct DispatchOrderState {
  std::mutex mutex;
  std::deque<std::uint64_t> expected_indices;
};

const char* ClassName(int class_id) {
  if (class_id >= 0 && class_id < 80) {
    return kCocoClassNames[class_id];
  }
  return "unknown";
}

bool ParseEnvBool(const char* name, bool default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value;
  }
  return value[0] != '0';
}

float ParseEnvFloat(const char* name, float default_value, float min_value, float max_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value;
  }
  return std::clamp(std::strtof(value, nullptr), min_value, max_value);
}

int ParseEnvInt(const char* name, int default_value, int min_value, int max_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value;
  }
  return std::clamp(std::atoi(value), min_value, max_value);
}

std::string ParseEnvString(const char* name, std::string default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return default_value;
  }
  return value;
}

RoiConfig LoadRoiConfig() {
  RoiConfig config;
  config.enabled = ParseEnvBool("RK_YOLO_DYNAMIC_ROI", true);
  config.margin_ratio =
      ParseEnvFloat("RK_YOLO_ROI_MARGIN", kDefaultRoiMarginRatio, 0.0f, 1.0f);
  config.min_coverage_ratio = ParseEnvFloat("RK_YOLO_ROI_MIN_COVERAGE",
                                            kDefaultRoiMinCoverageRatio, 0.2f, 1.0f);
  config.full_frame_refresh =
      ParseEnvInt("RK_YOLO_ROI_REFRESH", kDefaultRoiFullFrameRefresh, 1, 120);
  return config;
}

int LoadInferWorkers() {
  return ParseEnvInt("RK_YOLO_INFER_WORKERS", 1, 1, 4);
}

BoxSmootherConfig LoadBoxSmootherConfig() {
  BoxSmootherConfig config;
  config.enabled = ParseEnvBool("RK_YOLO_BOX_SMOOTH", true);
  config.alpha = ParseEnvFloat("RK_YOLO_BOX_SMOOTH_ALPHA", 0.60f, 0.05f, 1.0f);
  config.min_iou = ParseEnvFloat("RK_YOLO_BOX_SMOOTH_IOU", 0.10f, 0.0f, 0.95f);
  return config;
}

CameraTuneConfig LoadCameraTuneConfig() {
  CameraTuneConfig config;
  config.enabled = ParseEnvBool("RK_YOLO_CAMERA_TUNE", true);
  config.match_name = ParseEnvString("RK_YOLO_CAMERA_MATCH", config.match_name);
  config.zoom_absolute = ParseEnvInt("RK_YOLO_CAMERA_ZOOM", config.zoom_absolute, 0, 99);
  config.focus_auto = ParseEnvBool("RK_YOLO_CAMERA_FOCUS_AUTO", config.focus_auto);
  config.focus_absolute =
      ParseEnvInt("RK_YOLO_CAMERA_FOCUS", config.focus_absolute, 0, 550);
  config.settle_ms = ParseEnvInt("RK_YOLO_CAMERA_SETTLE_MS", config.settle_ms, 0, 5000);
  config.warmup_grabs = ParseEnvInt("RK_YOLO_CAMERA_WARMUP_GRABS", config.warmup_grabs, 0, 30);
  return config;
}

TrackMode LoadTrackMode() {
  const char* value = std::getenv("RK_YOLO_TRACK_MODE");
  if (value == nullptr || value[0] == '\0') {
    return TrackMode::kMotion;
  }

  const std::string mode(value);
  if (mode == "optflow") {
    return TrackMode::kOpticalFlow;
  }
  return TrackMode::kMotion;
}

const char* TrackModeName(TrackMode mode) {
  switch (mode) {
    case TrackMode::kOpticalFlow:
      return "optflow";
    case TrackMode::kMotion:
    default:
      return "motion";
  }
}

std::string BuildLabel(const Detection& det) {
  std::ostringstream oss;
  oss << ClassName(det.class_id) << " " << std::fixed << std::setprecision(2) << det.score;
  return oss.str();
}

void DrawDetections(cv::Mat* frame, const std::vector<Detection>& detections) {
  for (const Detection& det : detections) {
    const cv::Scalar color(0, 255, 0);
    cv::rectangle(*frame, det.box, color, 2);

    const std::string label = BuildLabel(det);
    int baseline = 0;
    const cv::Size label_size =
        cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);

    const int box_x = det.box.x;
    const int box_y = std::max(0, det.box.y - label_size.height - 6);
    const int box_w = label_size.width + 6;
    const int box_h = label_size.height + baseline + 6;

    cv::rectangle(*frame, cv::Rect(box_x, box_y, box_w, box_h), color, cv::FILLED);
    cv::putText(*frame, label, cv::Point(box_x + 3, box_y + label_size.height + 1),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1, cv::LINE_AA);
  }
}

cv::Rect ClampRect(const cv::Rect& box, const cv::Size& frame_size) {
  const int x = std::clamp(box.x, 0, std::max(0, frame_size.width - 1));
  const int y = std::clamp(box.y, 0, std::max(0, frame_size.height - 1));
  const int max_width = std::max(0, frame_size.width - x);
  const int max_height = std::max(0, frame_size.height - y);
  const int width = std::clamp(box.width, 0, max_width);
  const int height = std::clamp(box.height, 0, max_height);
  return cv::Rect(x, y, width, height);
}

std::vector<Detection> TrackDetections(const cv::Mat& prev_gray, const cv::Mat& current_gray,
                                       const std::vector<Detection>& previous_detections) {
  if (prev_gray.empty() || current_gray.empty() || previous_detections.empty()) {
    return previous_detections;
  }

  std::vector<Detection> tracked = previous_detections;
  for (Detection& det : tracked) {
    const cv::Rect safe_box = ClampRect(det.box, prev_gray.size());
    if (safe_box.width < 12 || safe_box.height < 12) {
      det.box = safe_box;
      continue;
    }

    cv::Mat mask(prev_gray.size(), CV_8UC1, cv::Scalar(0));
    cv::rectangle(mask, safe_box, cv::Scalar(255), cv::FILLED);

    std::vector<cv::Point2f> prev_points;
    cv::goodFeaturesToTrack(prev_gray, prev_points, 24, 0.01, 4.0, mask, 3, false, 0.04);
    if (prev_points.size() < 4) {
      det.box = safe_box;
      continue;
    }

    std::vector<cv::Point2f> current_points;
    std::vector<unsigned char> status;
    std::vector<float> errors;
    cv::calcOpticalFlowPyrLK(prev_gray, current_gray, prev_points, current_points, status, errors);

    std::vector<float> delta_x;
    std::vector<float> delta_y;
    delta_x.reserve(prev_points.size());
    delta_y.reserve(prev_points.size());
    for (std::size_t i = 0; i < prev_points.size(); ++i) {
      if (status[i]) {
        delta_x.push_back(current_points[i].x - prev_points[i].x);
        delta_y.push_back(current_points[i].y - prev_points[i].y);
      }
    }

    if (delta_x.size() < 4) {
      det.box = safe_box;
      continue;
    }

    const auto middle_x = delta_x.begin() + static_cast<long>(delta_x.size() / 2);
    const auto middle_y = delta_y.begin() + static_cast<long>(delta_y.size() / 2);
    std::nth_element(delta_x.begin(), middle_x, delta_x.end());
    std::nth_element(delta_y.begin(), middle_y, delta_y.end());
    const int shift_x = static_cast<int>(std::lround(*middle_x));
    const int shift_y = static_cast<int>(std::lround(*middle_y));
    det.box = ClampRect(
        cv::Rect(safe_box.x + shift_x, safe_box.y + shift_y, safe_box.width, safe_box.height),
        current_gray.size());
  }

  return tracked;
}

std::vector<Detection> PredictDetections(const std::vector<Detection>& previous_detections,
                                         const std::vector<Detection>& current_detections,
                                         const cv::Size& frame_size) {
  if (previous_detections.empty() || current_detections.empty()) {
    return current_detections;
  }

  std::vector<Detection> predicted = current_detections;
  const std::size_t shared = std::min(previous_detections.size(), current_detections.size());
  constexpr int kMaxStepPixels = 24;

  for (std::size_t i = 0; i < shared; ++i) {
    if (previous_detections[i].class_id != current_detections[i].class_id) {
      continue;
    }

    const int dx = std::clamp(current_detections[i].box.x - previous_detections[i].box.x,
                              -kMaxStepPixels, kMaxStepPixels);
    const int dy = std::clamp(current_detections[i].box.y - previous_detections[i].box.y,
                              -kMaxStepPixels, kMaxStepPixels);
    predicted[i].box = ClampRect(
        cv::Rect(current_detections[i].box.x + dx, current_detections[i].box.y + dy,
                 current_detections[i].box.width, current_detections[i].box.height),
        frame_size);
  }

  return predicted;
}

cv::Rect BuildInferenceRoi(const cv::Size& frame_size, const std::vector<Detection>& detections,
                           const RoiConfig& roi_config) {
  if (detections.empty()) {
    return cv::Rect(0, 0, frame_size.width, frame_size.height);
  }

  int min_x = frame_size.width;
  int min_y = frame_size.height;
  int max_x = 0;
  int max_y = 0;
  for (const Detection& det : detections) {
    const cv::Rect safe_box = ClampRect(det.box, frame_size);
    min_x = std::min(min_x, safe_box.x);
    min_y = std::min(min_y, safe_box.y);
    max_x = std::max(max_x, safe_box.x + safe_box.width);
    max_y = std::max(max_y, safe_box.y + safe_box.height);
  }

  if (max_x <= min_x || max_y <= min_y) {
    return cv::Rect(0, 0, frame_size.width, frame_size.height);
  }

  const int union_width = max_x - min_x;
  const int union_height = max_y - min_y;
  const int margin_x = static_cast<int>(std::lround(union_width * roi_config.margin_ratio));
  const int margin_y = static_cast<int>(std::lround(union_height * roi_config.margin_ratio));
  const int min_width =
      static_cast<int>(std::lround(frame_size.width * roi_config.min_coverage_ratio));
  const int min_height =
      static_cast<int>(std::lround(frame_size.height * roi_config.min_coverage_ratio));

  int roi_x = min_x - margin_x;
  int roi_y = min_y - margin_y;
  int roi_w = union_width + margin_x * 2;
  int roi_h = union_height + margin_y * 2;

  if (roi_w < min_width) {
    const int grow = min_width - roi_w;
    roi_x -= grow / 2;
    roi_w = min_width;
  }
  if (roi_h < min_height) {
    const int grow = min_height - roi_h;
    roi_y -= grow / 2;
    roi_h = min_height;
  }

  return ClampRect(cv::Rect(roi_x, roi_y, roi_w, roi_h), frame_size);
}

std::vector<Detection> OffsetDetections(const std::vector<Detection>& detections,
                                        const cv::Point& offset, const cv::Size& frame_size) {
  std::vector<Detection> shifted = detections;
  for (Detection& det : shifted) {
    det.box = ClampRect(cv::Rect(det.box.x + offset.x, det.box.y + offset.y, det.box.width,
                                 det.box.height),
                        frame_size);
  }
  return shifted;
}

std::vector<Detection> SmoothDetections(const std::vector<Detection>& current_detections,
                                        const std::vector<Detection>& previous_detections,
                                        const cv::Size& frame_size,
                                        const BoxSmootherConfig& smoother_config) {
  if (!smoother_config.enabled || current_detections.empty() || previous_detections.empty()) {
    return current_detections;
  }

  std::vector<Detection> smoothed = current_detections;
  std::vector<bool> previous_used(previous_detections.size(), false);
  auto rect_iou = [](const cv::Rect2f& a, const cv::Rect2f& b) {
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
  };

  for (Detection& current : smoothed) {
    float best_iou = smoother_config.min_iou;
    int best_index = -1;

    for (std::size_t i = 0; i < previous_detections.size(); ++i) {
      if (previous_used[i] || previous_detections[i].class_id != current.class_id) {
        continue;
      }
      const cv::Rect2f current_box(current.box);
      const cv::Rect2f previous_box(previous_detections[i].box);
      const float iou = rect_iou(current_box, previous_box);
      if (iou > best_iou) {
        best_iou = iou;
        best_index = static_cast<int>(i);
      }
    }

    if (best_index < 0) {
      continue;
    }

    previous_used[best_index] = true;
    const Detection& previous = previous_detections[best_index];
    const float alpha = smoother_config.alpha;
    const float inv_alpha = 1.0f - alpha;
    const int x = static_cast<int>(std::lround(previous.box.x * inv_alpha + current.box.x * alpha));
    const int y = static_cast<int>(std::lround(previous.box.y * inv_alpha + current.box.y * alpha));
    const int w =
        static_cast<int>(std::lround(previous.box.width * inv_alpha + current.box.width * alpha));
    const int h = static_cast<int>(
        std::lround(previous.box.height * inv_alpha + current.box.height * alpha));
    current.box = ClampRect(cv::Rect(x, y, w, h), frame_size);
    current.score = previous.score * inv_alpha + current.score * alpha;
  }

  return smoothed;
}

std::string ResolveModelPath(int argc, char** argv) {
  if (argc >= 3) {
    return argv[2];
  }

  namespace fs = std::filesystem;
  const std::vector<fs::path> candidates = {
      "yolov10n.rknn",
      "../yolov10n.rknn",
      "../../yolov10n.rknn",
      "yolov10n.wsl.rk3588.fp.rknn",
      "../yolov10n.wsl.rk3588.fp.rknn",
      "../../yolov10n.wsl.rk3588.fp.rknn",
  };

  for (const fs::path& candidate : candidates) {
    if (fs::exists(candidate)) {
      return candidate.lexically_normal().string();
    }
  }

  return "../../yolov10n.rknn";
}

std::string TrimCopy(const std::string& value) {
  const std::string whitespace = " \t\r\n";
  const std::size_t begin = value.find_first_not_of(whitespace);
  if (begin == std::string::npos) {
    return "";
  }
  const std::size_t end = value.find_last_not_of(whitespace);
  return value.substr(begin, end - begin + 1);
}

std::string ReadCameraName(const std::string& device) {
  const std::filesystem::path sysfs_name =
      std::filesystem::path("/sys/class/video4linux") / std::filesystem::path(device).filename() /
      "name";
  std::ifstream input(sysfs_name);
  if (!input.good()) {
    return "";
  }

  std::string line;
  std::getline(input, line);
  return TrimCopy(line);
}

bool ApplyCameraTune(const std::string& device, cv::VideoCapture* cap,
                     const CameraTuneConfig& config, std::string* status) {
  const std::string camera_name = ReadCameraName(device);
  const std::string display_name = camera_name.empty() ? "unknown" : camera_name;

  if (!config.enabled) {
    *status = "camera_tune=off";
    return false;
  }

  if (!config.match_name.empty() && display_name.find(config.match_name) == std::string::npos) {
    *status =
        "camera_tune=skip model=\"" + display_name + "\" expected~=\"" + config.match_name + "\"";
    return false;
  }

  std::ostringstream command;
  command << "v4l2-ctl -d " << device << " -c focus_auto=" << (config.focus_auto ? 1 : 0);
  if (!config.focus_auto) {
    command << ",focus_absolute=" << config.focus_absolute;
  }
  command << ",zoom_absolute=" << config.zoom_absolute << " >/dev/null 2>&1";

  const int rc = std::system(command.str().c_str());
  if (rc != 0) {
    *status = "camera_tune=failed model=\"" + display_name + "\" rc=" + std::to_string(rc);
    return false;
  }

  if (config.settle_ms > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(config.settle_ms));
  }
  if (cap != nullptr) {
    for (int i = 0; i < config.warmup_grabs; ++i) {
      if (!cap->grab()) {
        break;
      }
    }
  }

  std::ostringstream applied;
  applied << "camera_tune=applied model=\"" << display_name << "\" zoom=" << config.zoom_absolute
          << " focus_auto=" << (config.focus_auto ? 1 : 0);
  if (!config.focus_auto) {
    applied << " focus=" << config.focus_absolute;
  }
  applied << " settle_ms=" << config.settle_ms << " warmup_grabs=" << config.warmup_grabs;
  *status = applied.str();
  return true;
}

bool TryOpenCameraDevice(const std::string& device, int width, int height, int fps,
                         cv::VideoCapture* cap) {
  cap->release();
  if (!cap->open(device, cv::CAP_V4L2)) {
    return false;
  }

  cap->set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
  cap->set(cv::CAP_PROP_FRAME_WIDTH, width);
  cap->set(cv::CAP_PROP_FRAME_HEIGHT, height);
  cap->set(cv::CAP_PROP_FPS, fps);
  cap->set(cv::CAP_PROP_BUFFERSIZE, 1);

  cv::Mat probe_frame;
  if (!cap->read(probe_frame) || probe_frame.empty()) {
    cap->release();
    return false;
  }
  return true;
}

std::vector<std::string> BuildCameraCandidates(const std::string& preferred_device) {
  namespace fs = std::filesystem;

  std::vector<std::string> candidates;
  candidates.push_back(preferred_device);

  constexpr const char kPrefix[] = "/dev/video";
  const std::string prefix(kPrefix);
  if (preferred_device.rfind(prefix, 0) != 0) {
    return candidates;
  }

  const std::string suffix = preferred_device.substr(prefix.size());
  if (suffix.empty() || suffix.find_first_not_of("0123456789") != std::string::npos) {
    return candidates;
  }

  const int preferred_index = std::stoi(suffix);
  std::vector<std::pair<int, std::string>> scored;

  for (const fs::directory_entry& entry : fs::directory_iterator("/dev")) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("video", 0) != 0) {
      continue;
    }
    const std::string index_text = name.substr(5);
    if (index_text.empty() || index_text.find_first_not_of("0123456789") != std::string::npos) {
      continue;
    }

    const int index = std::stoi(index_text);
    scored.emplace_back(std::abs(index - preferred_index), entry.path().string());
  }

  std::sort(scored.begin(), scored.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  for (const auto& item : scored) {
    if (item.second != preferred_device) {
      candidates.push_back(item.second);
    }
  }

  return candidates;
}

bool OpenCamera(const std::string& requested_device, int width, int height, int fps,
                cv::VideoCapture* cap, std::string* resolved_device) {
  for (const std::string& candidate : BuildCameraCandidates(requested_device)) {
    if (TryOpenCameraDevice(candidate, width, height, fps, cap)) {
      *resolved_device = candidate;
      return true;
    }
  }

  resolved_device->clear();
  return false;
}

class RtspPublisher {
 public:
  RtspPublisher(int width, int height, int fps, int port, std::string mount_path)
      : width_(width),
        height_(height),
        fps_(fps),
        port_(port),
        mount_path_(std::move(mount_path)),
        loop_(nullptr),
        server_(nullptr),
        factory_(nullptr),
        appsrc_(nullptr),
        loop_started_(false) {}

  ~RtspPublisher() { Stop(); }

  bool Start() {
    loop_ = g_main_loop_new(nullptr, FALSE);
    if (loop_ == nullptr) {
      return false;
    }

    server_ = gst_rtsp_server_new();
    factory_ = gst_rtsp_media_factory_new();
    if (server_ == nullptr || factory_ == nullptr) {
      return false;
    }

    std::ostringstream port_text;
    port_text << port_;
    gst_rtsp_server_set_service(server_, port_text.str().c_str());

    const std::string launch =
        "( appsrc name=mysrc is-live=true format=time do-timestamp=true "
        "caps=video/x-raw,format=BGR,width=" +
        std::to_string(width_) + ",height=" + std::to_string(height_) + ",framerate=" +
        std::to_string(fps_) + "/1 ! videoconvert ! mpph264enc ! h264parse config-interval=1 ! "
                              "rtph264pay name=pay0 pt=96 config-interval=1 )";
    gst_rtsp_media_factory_set_launch(factory_, launch.c_str());
    gst_rtsp_media_factory_set_shared(factory_, TRUE);
    g_signal_connect(factory_, "media-configure", G_CALLBACK(&RtspPublisher::OnMediaConfigure),
                     this);

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server_);
    gst_rtsp_mount_points_add_factory(mounts, mount_path_.c_str(), factory_);
    g_object_unref(mounts);

    if (gst_rtsp_server_attach(server_, nullptr) == 0) {
      return false;
    }

    loop_thread_ = std::thread([this]() {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_started_ = true;
      }
      cond_.notify_all();
      g_main_loop_run(loop_);
    });

    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this]() { return loop_started_; });
    return true;
  }

  void Stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (appsrc_ != nullptr) {
        gst_app_src_end_of_stream(GST_APP_SRC(appsrc_));
        g_object_unref(appsrc_);
        appsrc_ = nullptr;
      }
    }

    if (loop_ != nullptr) {
      g_main_loop_quit(loop_);
    }
    if (loop_thread_.joinable()) {
      loop_thread_.join();
    }

    if (server_ != nullptr) {
      g_object_unref(server_);
      server_ = nullptr;
    }
    factory_ = nullptr;

    if (loop_ != nullptr) {
      g_main_loop_unref(loop_);
      loop_ = nullptr;
    }
  }

  bool HasClient() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return appsrc_ != nullptr;
  }

  bool PushFrame(const cv::Mat& frame, std::uint64_t frame_index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (appsrc_ == nullptr) {
      return false;
    }

    const std::size_t bytes = frame.total() * frame.elemSize();
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, bytes, nullptr);
    if (buffer == nullptr) {
      return false;
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
      gst_buffer_unref(buffer);
      return false;
    }
    std::memcpy(map.data, frame.data, bytes);
    gst_buffer_unmap(buffer, &map);

    GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(frame_index, GST_SECOND, fps_);
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, GST_SECOND, fps_);

    const GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buffer);
    if (ret != GST_FLOW_OK) {
      if (appsrc_ != nullptr) {
        g_object_unref(appsrc_);
        appsrc_ = nullptr;
      }
      return false;
    }
    return true;
  }

 private:
  static void OnMediaConfigure(GstRTSPMediaFactory*, GstRTSPMedia* media, gpointer user_data) {
    RtspPublisher* self = static_cast<RtspPublisher*>(user_data);
    GstElement* element = gst_rtsp_media_get_element(media);
    GstElement* src = gst_bin_get_by_name_recurse_up(GST_BIN(element), "mysrc");
    {
      std::lock_guard<std::mutex> lock(self->mutex_);
      if (self->appsrc_ != nullptr) {
        g_object_unref(self->appsrc_);
      }
      self->appsrc_ = src;
    }
    gst_util_set_object_arg(G_OBJECT(src), "format", "time");
    g_object_set(G_OBJECT(src), "stream-type", 0, "is-live", TRUE, "block", TRUE, nullptr);
    g_signal_connect(media, "unprepared", G_CALLBACK(&RtspPublisher::OnMediaUnprepared), self);
    gst_object_unref(element);
  }

  static void OnMediaUnprepared(GstRTSPMedia*, gpointer user_data) {
    RtspPublisher* self = static_cast<RtspPublisher*>(user_data);
    std::lock_guard<std::mutex> lock(self->mutex_);
    if (self->appsrc_ != nullptr) {
      g_object_unref(self->appsrc_);
      self->appsrc_ = nullptr;
    }
  }

  int width_;
  int height_;
  int fps_;
  int port_;
  std::string mount_path_;

  GMainLoop* loop_;
  GstRTSPServer* server_;
  GstRTSPMediaFactory* factory_;

  mutable std::mutex mutex_;
  std::condition_variable cond_;
  GstElement* appsrc_;
  bool loop_started_;
  std::thread loop_thread_;
};

void PrintUsage(const char* argv0) {
  std::cout << "Usage: " << argv0
            << " [device=/dev/video48] [model=../yolov10n.rknn] [mount=/yolo] [width=640]"
               " [height=480] [fps=15] [score=0.30] [nms=0.45] [port=8554]"
               " [detect_every_n=1]"
            << std::endl;
}

void CaptureLoop(cv::VideoCapture* cap, int width, int height, RtspPublisher* publisher,
                 BoundedQueue<FramePacket>* capture_queue, PipelineStats* stats) {
  std::uint64_t frame_index = 0;
  cv::Mat frame;

  while (!g_stop.load()) {
    if (!publisher->HasClient()) {
      capture_queue->Clear();
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }

    if (!cap->read(frame) || frame.empty()) {
      std::cerr << "camera read failed" << std::endl;
      g_stop.store(true);
      break;
    }

    if (frame.cols != width || frame.rows != height) {
      cv::resize(frame, frame, cv::Size(width, height));
    }

    FramePacket packet;
    packet.index = frame_index++;
    packet.image = frame.clone();
    packet.captured_at = std::chrono::steady_clock::now();
    capture_queue->Push(std::move(packet));
    ++stats->captured_frames;
  }

  capture_queue->Stop();
}

void InferenceLoop(YoloRknnDetector* detector, float score_threshold, float nms_threshold,
                   int detect_every_n, RoiConfig roi_config, TrackMode track_mode,
                   BoxSmootherConfig smoother_config,
                   RtspPublisher* publisher,
                   BoundedQueue<FramePacket>* capture_queue,
                   BoundedQueue<FramePacket>* publish_queue, PipelineStats* stats) {
  FramePacket packet;
  std::vector<Detection> last_detections;
  std::vector<Detection> previous_tracked_detections;
  std::vector<Detection> previous_displayed_detections;
  bool have_last_detections = false;
  cv::Mat previous_gray;
  std::uint64_t inference_runs = 0;

  while (capture_queue->WaitPop(&packet) && !g_stop.load()) {
    if (!publisher->HasClient()) {
      publish_queue->Clear();
      last_detections.clear();
      previous_tracked_detections.clear();
      previous_displayed_detections.clear();
      have_last_detections = false;
      previous_gray.release();
      continue;
    }

    const bool should_infer =
        !have_last_detections || detect_every_n <= 1 || (packet.index % detect_every_n == 0);
    std::vector<Detection> detections;
    cv::Mat current_gray;
    if (!should_infer && track_mode == TrackMode::kOpticalFlow) {
      cv::cvtColor(packet.image, current_gray, cv::COLOR_BGR2GRAY);
    }

    if (should_infer) {
      const auto infer_start = std::chrono::steady_clock::now();
      const bool should_try_roi =
          roi_config.enabled && have_last_detections &&
          (roi_config.full_frame_refresh <= 1 ||
           (inference_runs % static_cast<std::uint64_t>(roi_config.full_frame_refresh) != 0));

      cv::Rect inference_roi(0, 0, packet.image.cols, packet.image.rows);
      cv::Mat infer_view = packet.image;
      if (should_try_roi) {
        inference_roi = BuildInferenceRoi(packet.image.size(), last_detections, roi_config);
        if (inference_roi.width > 0 && inference_roi.height > 0 &&
            (inference_roi.width < packet.image.cols || inference_roi.height < packet.image.rows)) {
          infer_view = packet.image(inference_roi).clone();
          packet.used_roi_crop = true;
          ++stats->roi_crop_runs;
        } else {
          inference_roi = cv::Rect(0, 0, packet.image.cols, packet.image.rows);
        }
      }

      detections = detector->Infer(infer_view, score_threshold, nms_threshold);
      if (packet.used_roi_crop) {
        detections = OffsetDetections(detections, inference_roi.tl(), packet.image.size());
      }
      const auto infer_end = std::chrono::steady_clock::now();
      packet.work_ms =
          std::chrono::duration<double, std::milli>(infer_end - infer_start).count();
      packet.inferred_at = infer_end;
      packet.ran_inference = true;
      previous_tracked_detections = last_detections;
      last_detections = detections;
      have_last_detections = true;
      if (track_mode == TrackMode::kOpticalFlow) {
        cv::cvtColor(packet.image, previous_gray, cv::COLOR_BGR2GRAY);
      } else {
        previous_gray.release();
      }
      ++stats->npu_inference_runs;
      ++inference_runs;
    } else {
      const auto track_start = std::chrono::steady_clock::now();
      if (track_mode == TrackMode::kOpticalFlow) {
        detections = TrackDetections(previous_gray, current_gray, last_detections);
      } else {
        detections =
            PredictDetections(previous_tracked_detections, last_detections, packet.image.size());
      }
      const auto track_end = std::chrono::steady_clock::now();
      packet.work_ms =
          std::chrono::duration<double, std::milli>(track_end - track_start).count();
      packet.inferred_at = track_end;
      packet.ran_inference = false;
      previous_tracked_detections = last_detections;
      last_detections = detections;
      if (track_mode == TrackMode::kOpticalFlow) {
        previous_gray = current_gray;
      }
      ++stats->reused_frames;
    }

    std::vector<Detection> displayed_detections =
        SmoothDetections(detections, previous_displayed_detections, packet.image.size(),
                         smoother_config);
    DrawDetections(&packet.image, displayed_detections);
    packet.detections = displayed_detections.size();
    previous_displayed_detections = displayed_detections;

    publish_queue->Push(std::move(packet));
    ++stats->inferred_frames;
  }

  publish_queue->Stop();
}

void InferWorkerLoop(const std::string& model_path, float score_threshold, float nms_threshold,
                     BlockingQueue<FramePacket>* request_queue,
                     BlockingQueue<InferResult>* result_queue,
                     std::atomic<int>* live_workers) {
  YoloRknnDetector detector;
  if (!detector.Load(model_path)) {
    std::cerr << "worker failed to load model: " << model_path << std::endl;
    g_stop.store(true);
  } else {
    FramePacket packet;
    while (request_queue->WaitPop(&packet) && !g_stop.load()) {
      const auto infer_start = std::chrono::steady_clock::now();
      std::vector<Detection> detections =
          detector.Infer(packet.image, score_threshold, nms_threshold);
      const auto infer_end = std::chrono::steady_clock::now();

      packet.work_ms =
          std::chrono::duration<double, std::milli>(infer_end - infer_start).count();
      packet.inferred_at = infer_end;
      packet.ran_inference = true;
      packet.used_roi_crop = false;
      result_queue->Push({std::move(packet), std::move(detections)});
    }
  }

  if (live_workers->fetch_sub(1) == 1) {
    result_queue->Stop();
  }
}

void MultiWorkerDispatchLoop(BoundedQueue<FramePacket>* capture_queue,
                             std::vector<std::shared_ptr<BlockingQueue<FramePacket>>>* worker_queues,
                             DispatchOrderState* order_state, PipelineStats* stats) {
  constexpr std::size_t kMaxQueuedPerWorker = 1;
  FramePacket packet;
  while (capture_queue->WaitPop(&packet) && !g_stop.load()) {
    std::size_t best_worker = 0;
    std::size_t best_size = std::numeric_limits<std::size_t>::max();
    for (std::size_t i = 0; i < worker_queues->size(); ++i) {
      const std::size_t queue_size = worker_queues->at(i)->size();
      if (queue_size < best_size) {
        best_size = queue_size;
        best_worker = i;
      }
    }

    if (best_size > kMaxQueuedPerWorker) {
      ++stats->dispatch_dropped_frames;
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(order_state->mutex);
      order_state->expected_indices.push_back(packet.index);
    }
    worker_queues->at(best_worker)->Push(std::move(packet));
  }

  for (const auto& queue : *worker_queues) {
    queue->Stop();
  }
}

void MultiWorkerResultLoop(BlockingQueue<InferResult>* result_queue, DispatchOrderState* order_state,
                           BoundedQueue<FramePacket>* publish_queue, PipelineStats* stats) {
  std::map<std::uint64_t, InferResult> pending_results;
  InferResult result;
  while (true) {
    const bool got_result = result_queue->WaitPop(&result);
    if (got_result) {
      pending_results.emplace(result.packet.index, std::move(result));
    }

    while (true) {
      std::uint64_t next_index = 0;
      {
        std::lock_guard<std::mutex> lock(order_state->mutex);
        if (order_state->expected_indices.empty()) {
          break;
        }
        next_index = order_state->expected_indices.front();
      }

      auto pending_it = pending_results.find(next_index);
      if (pending_it == pending_results.end()) {
        break;
      }

      InferResult ready = std::move(pending_it->second);
      pending_results.erase(pending_it);
      {
        std::lock_guard<std::mutex> lock(order_state->mutex);
        if (!order_state->expected_indices.empty() &&
            order_state->expected_indices.front() == next_index) {
          order_state->expected_indices.pop_front();
        }
      }

      DrawDetections(&ready.packet.image, ready.detections);
      ready.packet.detections = ready.detections.size();
      ++stats->npu_inference_runs;
      ++stats->inferred_frames;
      publish_queue->Push(std::move(ready.packet));
    }

    if (!got_result) {
      break;
    }
  }

  publish_queue->Stop();
}

void PublishLoop(RtspPublisher* publisher, BoundedQueue<FramePacket>* capture_queue,
                 BoundedQueue<FramePacket>* publish_queue, PipelineStats* stats) {
  auto last_report = std::chrono::steady_clock::now();
  auto interval_start = last_report;
  std::uint64_t interval_frames = 0;
  std::uint64_t last_npu_inference_runs = 0;
  std::uint64_t last_roi_crop_runs = 0;

  FramePacket packet;
  while (publish_queue->WaitPop(&packet) && !g_stop.load()) {
    if (!publisher->HasClient()) {
      continue;
    }

    if (!publisher->PushFrame(packet.image, packet.index)) {
      std::cout << "rtsp client disconnected, waiting for the next client..." << std::endl;
      publish_queue->Clear();
      continue;
    }

    ++stats->published_frames;
    ++interval_frames;

    const auto now = std::chrono::steady_clock::now();
    if (now - last_report >= std::chrono::seconds(2)) {
      const double wall_seconds =
          std::chrono::duration<double>(now - interval_start).count();
      const std::uint64_t total_npu_inference_runs = stats->npu_inference_runs.load();
      const std::uint64_t total_reused_frames = stats->reused_frames.load();
      const std::uint64_t total_roi_crop_runs = stats->roi_crop_runs.load();
      const std::uint64_t total_dispatch_dropped = stats->dispatch_dropped_frames.load();
      const double stream_fps = (wall_seconds > 0.0) ? (interval_frames / wall_seconds) : 0.0;
      const double npu_fps =
          (wall_seconds > 0.0)
              ? (static_cast<double>(total_npu_inference_runs - last_npu_inference_runs) /
                 wall_seconds)
              : 0.0;
      const double roi_fps =
          (wall_seconds > 0.0)
              ? (static_cast<double>(total_roi_crop_runs - last_roi_crop_runs) / wall_seconds)
              : 0.0;
      const double end_to_end_ms =
          std::chrono::duration<double, std::milli>(now - packet.captured_at).count();
      std::cout << "captured=" << stats->captured_frames.load()
                << " inferred=" << stats->inferred_frames.load()
                << " published=" << stats->published_frames.load()
                << " npu_infer_runs=" << total_npu_inference_runs
                << " reused_frames=" << total_reused_frames
                << " roi_crop_runs=" << total_roi_crop_runs
                << " dispatch_dropped=" << total_dispatch_dropped
                << " detections=" << packet.detections
                << " last_mode="
                << (packet.ran_inference ? (packet.used_roi_crop ? "infer_roi" : "infer_full")
                                         : "reuse")
                << " work_ms=" << std::fixed << std::setprecision(2) << packet.work_ms
                << " stream_fps=" << std::fixed << std::setprecision(2) << stream_fps
                << " npu_fps=" << std::fixed << std::setprecision(2) << npu_fps
                << " roi_fps=" << std::fixed << std::setprecision(2) << roi_fps
                << " end_to_end_ms=" << std::fixed << std::setprecision(2) << end_to_end_ms
                << " dropped_capture=" << capture_queue->dropped_items()
                << " dropped_publish=" << publish_queue->dropped_items()
                << std::endl;
      last_report = now;
      interval_start = now;
      interval_frames = 0;
      last_npu_inference_runs = total_npu_inference_runs;
      last_roi_crop_runs = total_roi_crop_runs;
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 11) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);
  gst_init(&argc, &argv);

  const std::string device = (argc >= 2) ? argv[1] : "/dev/video48";
  const std::string model_path = ResolveModelPath(argc, argv);
  const std::string mount_path = (argc >= 4) ? argv[3] : "/yolo";
  const int width = (argc >= 5) ? std::atoi(argv[4]) : kDefaultWidth;
  const int height = (argc >= 6) ? std::atoi(argv[5]) : kDefaultHeight;
  const int fps = (argc >= 7) ? std::atoi(argv[6]) : kDefaultFps;
  const float score_threshold =
      (argc >= 8) ? std::strtof(argv[7], nullptr) : kDefaultScoreThreshold;
  const float nms_threshold =
      (argc >= 9) ? std::strtof(argv[8], nullptr) : kDefaultNmsThreshold;
  const int port = (argc >= 10) ? std::atoi(argv[9]) : 8554;
  const int detect_every_n =
      std::max(1, (argc >= 11) ? std::atoi(argv[10]) : kDefaultDetectEveryN);
  const RoiConfig roi_config = LoadRoiConfig();
  const TrackMode track_mode = LoadTrackMode();
  const BoxSmootherConfig smoother_config = LoadBoxSmootherConfig();
  const CameraTuneConfig camera_tune_config = LoadCameraTuneConfig();
  const int infer_workers = LoadInferWorkers();
  const bool use_multi_worker_mode = infer_workers > 1 && detect_every_n == 1;

  cv::VideoCapture cap;
  std::string resolved_device;
  if (!OpenCamera(device, width, height, fps, &cap, &resolved_device)) {
    std::cerr << "failed to open camera: " << device << std::endl;
    return 2;
  }
  std::string camera_tune_status;
  ApplyCameraTune(resolved_device, &cap, camera_tune_config, &camera_tune_status);

  YoloRknnDetector detector;
  if (!detector.Load(model_path)) {
    std::cerr << "failed to load model: " << model_path << std::endl;
    return 3;
  }
  if (use_multi_worker_mode) {
    detector.Release();
  }

  RtspPublisher publisher(width, height, fps, port, mount_path);
  if (!publisher.Start()) {
    std::cerr << "failed to start RTSP publisher" << std::endl;
    return 4;
  }

  std::cout << "camera=" << resolved_device << std::endl;
  if (resolved_device != device) {
    std::cout << "camera fallback used for requested device " << device << std::endl;
  }
  std::cout << camera_tune_status << std::endl;
  std::cout << "model=" << model_path << std::endl;
  std::cout << "rtsp path=rtsp://<board-ip>:" << port << mount_path << std::endl;
  std::cout << "pipeline=capture -> infer -> publish" << std::endl;
  std::cout << "detect_every_n=" << detect_every_n << std::endl;
  std::cout << "track_mode=" << TrackModeName(track_mode) << std::endl;
  std::cout << "box_smooth=" << (smoother_config.enabled ? "on" : "off")
            << " alpha=" << smoother_config.alpha
            << " min_iou=" << smoother_config.min_iou << std::endl;
  std::cout << "infer_workers=" << infer_workers << std::endl;
  std::cout << "dynamic_roi=" << (roi_config.enabled ? "on" : "off")
            << " margin=" << roi_config.margin_ratio
            << " min_coverage=" << roi_config.min_coverage_ratio
            << " refresh=" << roi_config.full_frame_refresh << std::endl;
  if (infer_workers > 1 && !use_multi_worker_mode) {
    std::cout << "multi-context mode is only enabled when detect_every_n=1; "
                 "falling back to the current sequential tracking pipeline"
              << std::endl;
  }
  if (use_multi_worker_mode) {
    std::cout << "multi-context full-frame inference mode enabled" << std::endl;
  }
  std::cout << "waiting for RTSP client connection..." << std::endl;

  PipelineStats stats;
  BoundedQueue<FramePacket> capture_queue(kCaptureQueueCapacity);
  BoundedQueue<FramePacket> publish_queue(kPublishQueueCapacity);
  std::thread capture_thread(CaptureLoop, &cap, width, height, &publisher, &capture_queue, &stats);
  std::thread publish_thread(PublishLoop, &publisher, &capture_queue, &publish_queue, &stats);

  std::thread infer_thread;
  std::thread dispatch_thread;
  std::thread result_thread;
  std::vector<std::thread> worker_threads;
  std::vector<std::shared_ptr<BlockingQueue<FramePacket>>> worker_queues;
  std::shared_ptr<BlockingQueue<InferResult>> result_queue;
  DispatchOrderState order_state;
  std::atomic<int> live_workers{0};

  if (use_multi_worker_mode) {
    result_queue = std::make_shared<BlockingQueue<InferResult>>();
    worker_queues.reserve(static_cast<std::size_t>(infer_workers));
    live_workers.store(infer_workers);
    for (int i = 0; i < infer_workers; ++i) {
      auto queue = std::make_shared<BlockingQueue<FramePacket>>();
      worker_threads.emplace_back(InferWorkerLoop, model_path, score_threshold, nms_threshold,
                                  queue.get(), result_queue.get(), &live_workers);
      worker_queues.push_back(queue);
    }
    dispatch_thread =
        std::thread(MultiWorkerDispatchLoop, &capture_queue, &worker_queues, &order_state, &stats);
    result_thread = std::thread(MultiWorkerResultLoop, result_queue.get(), &order_state,
                                &publish_queue, &stats);
  } else {
    infer_thread = std::thread(InferenceLoop, &detector, score_threshold, nms_threshold,
                               detect_every_n, roi_config, track_mode, smoother_config, &publisher,
                               &capture_queue, &publish_queue, &stats);
  }

  while (!g_stop.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  capture_queue.Stop();
  if (dispatch_thread.joinable()) {
    dispatch_thread.join();
  }
  for (auto& queue : worker_queues) {
    queue->Stop();
  }
  for (std::thread& worker : worker_threads) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  if (result_queue) {
    result_queue->Stop();
  }
  publish_queue.Stop();
  if (capture_thread.joinable()) {
    capture_thread.join();
  }
  if (infer_thread.joinable()) {
    infer_thread.join();
  }
  if (result_thread.joinable()) {
    result_thread.join();
  }
  if (publish_thread.joinable()) {
    publish_thread.join();
  }

  publisher.Stop();
  return 0;
}
