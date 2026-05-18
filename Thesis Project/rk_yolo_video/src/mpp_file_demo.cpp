#include "yolo_rknn.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef HAVE_MPP
#include <mpp_buffer.h>
#include <mpp_err.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <rk_mpi.h>
#endif

#ifdef HAVE_RGA
#include <rga/im2d.h>
#include <rga/rga.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

bool EnvFlagEnabled(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return false;
  }
  return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
         std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "on") == 0 ||
         std::strcmp(value, "ON") == 0 || std::strcmp(value, "yes") == 0 ||
         std::strcmp(value, "YES") == 0;
}

bool EnvFlag(const char* name, bool default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value;
  }
  return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
         std::strcmp(value, "TRUE") == 0 || std::strcmp(value, "on") == 0 ||
         std::strcmp(value, "ON") == 0 || std::strcmp(value, "yes") == 0 ||
         std::strcmp(value, "YES") == 0;
}

float EnvFloat(const char* name, float default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value;
  }
  try {
    return std::stof(value);
  } catch (...) {
    return default_value;
  }
}

int EnvInt(const char* name, int default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value;
  }
  try {
    return std::stoi(value);
  } catch (...) {
    return default_value;
  }
}

#if defined(HAVE_RGA) && defined(HAVE_MPP)

std::mutex& RgaOperationMutex() {
  static std::mutex mutex;
  return mutex;
}

struct DecodedFrame {
  MppFrame frame = nullptr;
  int width = 0;
  int height = 0;
  int hor_stride = 0;
  int ver_stride = 0;
  int rga_format = 0;
  int dma_fd = -1;
};

struct OsdIgnoreConfig {
  bool enabled = false;
  float left = 0.0f;
  float top = 0.55f;
  float right = 0.42f;
  float bottom = 1.0f;
};

std::vector<Detection> FilterOsdDetections(const std::vector<Detection>& detections,
                                           int width, int height,
                                           const OsdIgnoreConfig& config) {
  if (!config.enabled || width <= 0 || height <= 0 || detections.empty()) {
    return detections;
  }

  const float left = std::max(0.0f, std::min(1.0f, config.left));
  const float top = std::max(0.0f, std::min(1.0f, config.top));
  const float right = std::max(left, std::min(1.0f, config.right));
  const float bottom = std::max(top, std::min(1.0f, config.bottom));

  std::vector<Detection> filtered;
  filtered.reserve(detections.size());
  for (const Detection& det : detections) {
    const float cx = (det.box.x + det.box.width * 0.5f) / static_cast<float>(width);
    const float cy = (det.box.y + det.box.height * 0.5f) / static_cast<float>(height);
    if (cx >= left && cx <= right && cy >= top && cy <= bottom) {
      continue;
    }
    filtered.push_back(det);
  }
  return filtered;
}

float DetectionIou(const cv::Rect& a, const cv::Rect& b) {
  const int intersection = (a & b).area();
  const int union_area = a.area() + b.area() - intersection;
  if (union_area <= 0) {
    return 0.0f;
  }
  return static_cast<float>(intersection) / static_cast<float>(union_area);
}

cv::Point2f BoxCenter(const cv::Rect& box) {
  return cv::Point2f(box.x + box.width * 0.5f, box.y + box.height * 0.5f);
}

bool CenterInsideExpandedBox(const cv::Point2f& center, const cv::Rect& box,
                             float expand_ratio) {
  const float expand_x = box.width * expand_ratio;
  const float expand_y = box.height * expand_ratio;
  return center.x >= box.x - expand_x && center.x <= box.x + box.width + expand_x &&
         center.y >= box.y - expand_y && center.y <= box.y + box.height + expand_y;
}

bool LooksLikeSameVisualTarget(const Detection& candidate, const Detection& kept,
                               int width, int height) {
  if (candidate.class_id != kept.class_id || candidate.box.empty() ||
      kept.box.empty()) {
    return false;
  }

  const float iou = DetectionIou(candidate.box, kept.box);
  if (iou >= 0.12f) {
    return true;
  }

  const cv::Point2f candidate_center = BoxCenter(candidate.box);
  const cv::Point2f kept_center = BoxCenter(kept.box);
  const float center_distance =
      std::hypot(candidate_center.x - kept_center.x,
                 candidate_center.y - kept_center.y);
  const float image_near_threshold =
      std::max(28.0f, 0.105f * static_cast<float>(std::min(width, height)));
  const float local_near_threshold =
      std::max(32.0f, 0.75f * static_cast<float>(
                            std::max({candidate.box.width, candidate.box.height,
                                      kept.box.width, kept.box.height})));

  const float candidate_area = static_cast<float>(std::max(1, candidate.box.area()));
  const float kept_area = static_cast<float>(std::max(1, kept.box.area()));
  const float log_area_delta = std::fabs(std::log(candidate_area / kept_area));
  const bool compatible_scale = log_area_delta <= 1.65f;

  if (compatible_scale &&
      center_distance <= std::max(image_near_threshold, local_near_threshold)) {
    return true;
  }

  const bool nested_centers =
      CenterInsideExpandedBox(candidate_center, kept.box, 0.45f) ||
      CenterInsideExpandedBox(kept_center, candidate.box, 0.45f);
  return compatible_scale && nested_centers &&
         center_distance <= std::max(48.0f, local_near_threshold * 1.35f);
}

std::vector<Detection> SuppressNearbyDuplicateDetections(
    const std::vector<Detection>& detections, int width, int height, bool enabled) {
  if (!enabled || detections.size() <= 1 || width <= 0 || height <= 0) {
    return detections;
  }

  std::vector<Detection> sorted = detections;
  std::sort(sorted.begin(), sorted.end(), [](const Detection& a, const Detection& b) {
    return a.score > b.score;
  });

  std::vector<Detection> kept;
  kept.reserve(sorted.size());
  for (const Detection& det : sorted) {
    bool duplicate = false;
    for (const Detection& existing : kept) {
      if (LooksLikeSameVisualTarget(det, existing, width, height)) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      kept.push_back(det);
    }
  }
  return kept;
}

class MppFrameGuard {
 public:
  explicit MppFrameGuard(MppFrame frame = nullptr) : frame_(frame) {}
  ~MppFrameGuard() { Reset(nullptr); }

  MppFrameGuard(const MppFrameGuard&) = delete;
  MppFrameGuard& operator=(const MppFrameGuard&) = delete;

  MppFrame get() const { return frame_; }
  MppFrame release() {
    MppFrame frame = frame_;
    frame_ = nullptr;
    return frame;
  }
  void Reset(MppFrame frame) {
    if (frame_ != nullptr) {
      mpp_frame_deinit(&frame_);
    }
    frame_ = frame;
  }

 private:
  MppFrame frame_;
};

struct DecodedFrameOwner {
  ~DecodedFrameOwner() {
    if (decoded.frame != nullptr) {
      MppFrameGuard guard(decoded.frame);
      decoded.frame = nullptr;
    }
  }

  DecodedFrame decoded;
};

struct FrameTask {
  int frame_index = 0;
  double decode_ms = 0.0;
  std::shared_ptr<DecodedFrameOwner> frame;
};

struct FrameResult {
  int frame_index = 0;
  int worker_index = -1;
  double decode_ms = 0.0;
  bool inferred = false;
  std::vector<Detection> detections;
  InferProfile profile;
  std::shared_ptr<DecodedFrameOwner> frame;
};

class FrameTaskQueue {
 public:
  explicit FrameTaskQueue(std::size_t max_depth) : max_depth_(std::max<std::size_t>(1, max_depth)) {}

  bool Push(std::shared_ptr<FrameTask> task) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [&]() { return closed_ || queue_.size() < max_depth_; });
    if (closed_) {
      return false;
    }
    queue_.push_back(std::move(task));
    not_empty_.notify_one();
    return true;
  }

  bool Pop(std::shared_ptr<FrameTask>* task) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [&]() { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
      return false;
    }
    *task = std::move(queue_.front());
    queue_.pop_front();
    not_full_.notify_one();
    return true;
  }

  void Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    not_empty_.notify_all();
    not_full_.notify_all();
  }

 private:
  std::size_t max_depth_;
  std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  std::deque<std::shared_ptr<FrameTask>> queue_;
  bool closed_ = false;
};

class FrameResultQueue {
 public:
  void Push(FrameResult result) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(std::move(result));
    not_empty_.notify_one();
  }

  bool Pop(FrameResult* result) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [&]() { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
      return false;
    }
    *result = std::move(queue_.front());
    queue_.pop_front();
    return true;
  }

  void Close() {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    not_empty_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable not_empty_;
  std::deque<FrameResult> queue_;
  bool closed_ = false;
};

int RgaFormatFromMpp(MppFrameFormat fmt) {
  const MppFrameFormat clean_fmt =
      static_cast<MppFrameFormat>(static_cast<int>(fmt) & MPP_FRAME_FMT_MASK);
  switch (clean_fmt) {
    case MPP_FMT_YUV420SP:
      return RK_FORMAT_YCbCr_420_SP;
    case MPP_FMT_YUV420SP_VU:
      return RK_FORMAT_YCrCb_420_SP;
    case MPP_FMT_YUV422SP:
      return RK_FORMAT_YCbCr_422_SP;
    case MPP_FMT_YUV422SP_VU:
      return RK_FORMAT_YCrCb_422_SP;
    default:
      return 0;
  }
}

std::string MppFrameFormatName(MppFrameFormat fmt) {
  const MppFrameFormat clean_fmt =
      static_cast<MppFrameFormat>(static_cast<int>(fmt) & MPP_FRAME_FMT_MASK);
  switch (clean_fmt) {
    case MPP_FMT_YUV420SP:
      return "YUV420SP/NV12";
    case MPP_FMT_YUV420SP_VU:
      return "YUV420SP_VU/NV21";
    case MPP_FMT_YUV422SP:
      return "YUV422SP";
    case MPP_FMT_YUV422SP_VU:
      return "YUV422SP_VU";
    default:
      return "unsupported(" + std::to_string(static_cast<int>(fmt)) + ")";
  }
}

bool ConvertDmaFrameToBgr(const DecodedFrame& frame, cv::Mat* bgr) {
  if (bgr == nullptr || frame.dma_fd < 0 || frame.width <= 0 || frame.height <= 0 ||
      frame.hor_stride <= 0 || frame.ver_stride <= 0 || frame.rga_format == 0) {
    return false;
  }
  const int bgr_stride = (frame.width + 15) & ~15;
  cv::Mat aligned_bgr(frame.height, bgr_stride, CV_8UC3);
  if (!aligned_bgr.isContinuous()) {
    aligned_bgr = aligned_bgr.clone();
  }

  rga_buffer_t src = wrapbuffer_fd(frame.dma_fd, frame.width, frame.height,
                                   frame.rga_format, frame.hor_stride,
                                   frame.ver_stride);
  rga_buffer_t dst = wrapbuffer_virtualaddr(aligned_bgr.data, frame.width, frame.height,
                                            RK_FORMAT_BGR_888, bgr_stride,
                                            frame.height);
  im_rect src_rect{0, 0, frame.width, frame.height};
  im_rect dst_rect{0, 0, frame.width, frame.height};
  IM_STATUS status = IM_STATUS_SUCCESS;
  {
    std::lock_guard<std::mutex> lock(RgaOperationMutex());
    status = improcess(src, dst, {}, src_rect, dst_rect, {}, -1, nullptr, nullptr,
                       IM_SYNC);
  }
  if (status != IM_STATUS_SUCCESS) {
    std::cerr << "RGA DMA frame -> BGR visualization failed: " << imStrError(status)
              << std::endl;
    return false;
  }
  *bgr = aligned_bgr(cv::Rect(0, 0, frame.width, frame.height)).clone();
  return true;
}

void DrawDetections(cv::Mat* frame, const std::vector<Detection>& detections,
                    const char* count_label = "targets") {
  if (frame == nullptr || frame->empty()) {
    return;
  }
  const int thickness = std::max(2, frame->cols / 640);
  const double font_scale = std::max(0.6, frame->cols / 1280.0);
  float best_score = 0.0f;
  for (const Detection& det : detections) {
    best_score = std::max(best_score, det.score);
  }

  const bool alert = !detections.empty();
  const cv::Scalar banner_color = alert ? cv::Scalar(0, 0, 220) : cv::Scalar(40, 140, 40);
  const int banner_h = std::max(34, frame->rows / 24);
  cv::rectangle(*frame, cv::Rect(0, 0, frame->cols, banner_h), banner_color, cv::FILLED);

  std::ostringstream banner;
  if (alert) {
    banner << "UAV ALERT | " << count_label << "=" << detections.size() << " | max_score="
           << std::fixed << std::setprecision(2) << best_score;
  } else {
    banner << "NORMAL | " << count_label << "=0";
  }
  cv::putText(*frame, banner.str(), cv::Point(12, banner_h - 10),
              cv::FONT_HERSHEY_SIMPLEX, font_scale, cv::Scalar(255, 255, 255),
              thickness, cv::LINE_AA);

  for (const Detection& det : detections) {
    cv::Rect box = det.box & cv::Rect(0, 0, frame->cols, frame->rows);
    if (box.empty()) {
      continue;
    }
    cv::rectangle(*frame, box, cv::Scalar(0, 255, 0), thickness);
    std::ostringstream label;
    label << "drone " << std::fixed << std::setprecision(2) << det.score;
    int baseline = 0;
    const cv::Size text_size =
        cv::getTextSize(label.str(), cv::FONT_HERSHEY_SIMPLEX, font_scale,
                        thickness, &baseline);
    const int label_y = std::max(banner_h + text_size.height + 4, box.y);
    cv::Rect bg(box.x, std::max(0, label_y - text_size.height - 6),
                std::min(text_size.width + 8, frame->cols - box.x),
                text_size.height + baseline + 8);
    if (bg.width > 0 && bg.height > 0) {
      cv::rectangle(*frame, bg, cv::Scalar(0, 255, 0), cv::FILLED);
    }
    cv::putText(*frame, label.str(), cv::Point(box.x + 4, label_y),
                cv::FONT_HERSHEY_SIMPLEX, font_scale, cv::Scalar(0, 0, 0),
                thickness, cv::LINE_AA);
  }
}

class DisplayBoxStabilizer {
 public:
  explicit DisplayBoxStabilizer(bool stable_mode = true,
                                bool fast_follow = false)
      : stable_mode_(stable_mode), fast_follow_(fast_follow) {}

  std::vector<Detection> Update(const std::vector<Detection>& detections) {
    const Detection* best = ChooseDetection(detections);

    if (best == nullptr) {
      if (pending_hits_ > 0 && ++pending_misses_ > kMaxPendingMisses) {
        pending_hits_ = 0;
        pending_misses_ = 0;
      }
      if (has_box_ && hold_frames_ < MaxHoldFrames()) {
        ++hold_frames_;
        if (fast_follow_) {
          const float max_predict_step = std::max(2.0f, std::max(width_, height_) * 0.055f);
          center_x_ += Clamp(velocity_x_ * 0.65f, -max_predict_step, max_predict_step);
          center_y_ += Clamp(velocity_y_ * 0.65f, -max_predict_step, max_predict_step);
          velocity_x_ *= 0.80f;
          velocity_y_ *= 0.80f;
        }
        Detection held;
        held.class_id = 0;
        held.score = std::max(0.01f, last_score_ * 0.96f);
        held.box = CurrentRect();
        return {held};
      }
      has_box_ = false;
      hold_frames_ = 0;
      return {};
    }

    hold_frames_ = 0;
    if (!has_box_) {
      if (!AcceptInitialDetection(*best)) {
        return {};
      }
      has_box_ = true;
      center_x_ = best->box.x + best->box.width * 0.5f;
      center_y_ = best->box.y + best->box.height * 0.5f;
      width_ = static_cast<float>(best->box.width);
      height_ = static_cast<float>(best->box.height);
      last_score_ = best->score;
      velocity_x_ = 0.0f;
      velocity_y_ = 0.0f;
      return {*best};
    }

    const cv::Point2f curr_center(best->box.x + best->box.width * 0.5f,
                                  best->box.y + best->box.height * 0.5f);
    const cv::Point2f prev_center(center_x_, center_y_);
    const float prev_area = std::max(1.0f, width_ * height_);
    const float curr_area = static_cast<float>(std::max(1, best->box.area()));
    const float area_ratio = curr_area / prev_area;
    const float center_delta =
        std::hypot(curr_center.x - prev_center.x, curr_center.y - prev_center.y);

    float center_alpha =
        fast_follow_ ? 0.34f : (stable_mode_ ? 0.045f : 0.070f);
    float max_center_step_ratio =
        fast_follow_ ? 0.18f : (stable_mode_ ? 0.008f : 0.014f);
    if (!fast_follow_ && center_delta > std::max(width_, height_) * 0.25f) {
      center_alpha = stable_mode_ ? 0.020f : 0.035f;
      max_center_step_ratio = stable_mode_ ? 0.004f : 0.008f;
    }

    // Width/height are deliberately much slower than the center. Raw detector
    // boxes on low-confidence frames can breathe even when the visual target is
    // almost unchanged, so the presentation box should not follow every scale
    // fluctuation.
    float size_alpha = stable_mode_ ? 0.006f : 0.018f;
    float max_size_step_ratio = stable_mode_ ? 0.0025f : 0.007f;
    if (area_ratio > 1.35f || area_ratio < 0.75f) {
      size_alpha = stable_mode_ ? 0.003f : 0.010f;
      max_size_step_ratio = stable_mode_ ? 0.0015f : 0.004f;
    }

    const float max_center_step =
        std::max(3.0f, std::max(width_, height_) * max_center_step_ratio);
    const float prev_x = center_x_;
    const float prev_y = center_y_;
    center_x_ = SmoothCoordinate(center_x_, curr_center.x, center_alpha,
                                 max_center_step);
    center_y_ = SmoothCoordinate(center_y_, curr_center.y, center_alpha,
                                 max_center_step);
    width_ = SmoothDimension(width_, static_cast<float>(best->box.width),
                             size_alpha, max_size_step_ratio);
    height_ = SmoothDimension(height_, static_cast<float>(best->box.height),
                              size_alpha, max_size_step_ratio);
    last_score_ = SmoothFloat(last_score_, best->score, stable_mode_ ? 0.18f : 0.25f);
    velocity_x_ = SmoothFloat(velocity_x_, center_x_ - prev_x,
                              fast_follow_ ? 0.35f : 0.20f);
    velocity_y_ = SmoothFloat(velocity_y_, center_y_ - prev_y,
                              fast_follow_ ? 0.35f : 0.20f);

    Detection smoothed = *best;
    smoothed.box = CurrentRect();
    smoothed.score = last_score_;
    return {smoothed};
  }

 private:
  bool AcceptInitialDetection(const Detection& det) {
    if (!stable_mode_ || det.score >= kImmediateLockScore) {
      pending_hits_ = 0;
      pending_misses_ = 0;
      return true;
    }

    if (pending_hits_ == 0 || !PendingMatches(det)) {
      pending_box_ = det.box;
      pending_score_ = det.score;
      pending_hits_ = 1;
      pending_misses_ = 0;
      return false;
    }

    pending_box_ = det.box;
    pending_score_ = SmoothFloat(pending_score_, det.score, 0.35f);
    pending_misses_ = 0;
    ++pending_hits_;
    if (pending_hits_ >= kRequiredInitialHits) {
      pending_hits_ = 0;
      return true;
    }
    return false;
  }

  bool PendingMatches(const Detection& det) const {
    const float pending_scale =
        std::max(12.0f, static_cast<float>(std::max(pending_box_.width, pending_box_.height)));
    const float cx0 = pending_box_.x + pending_box_.width * 0.5f;
    const float cy0 = pending_box_.y + pending_box_.height * 0.5f;
    const float cx1 = det.box.x + det.box.width * 0.5f;
    const float cy1 = det.box.y + det.box.height * 0.5f;
    const float norm_delta = std::hypot(cx1 - cx0, cy1 - cy0) / pending_scale;
    return RectIou(pending_box_, det.box) > 0.08f || norm_delta < 0.36f;
  }

  const Detection* ChooseDetection(const std::vector<Detection>& detections) const {
    if (detections.empty()) {
      return nullptr;
    }
    if (!has_box_) {
      return HighestScoreDetection(detections);
    }

    const cv::Rect prev_box = CurrentRect();
    const float prev_scale = std::max(12.0f, std::max(width_, height_));
    const float prev_area = std::max(1.0f, width_ * height_);
    const float predicted_x = center_x_ + (fast_follow_ ? velocity_x_ : 0.0f);
    const float predicted_y = center_y_ + (fast_follow_ ? velocity_y_ : 0.0f);

    const Detection* best = nullptr;
    float best_assoc = -1e9f;
    for (const Detection& det : detections) {
      const float iou = RectIou(prev_box, det.box);
      const float det_cx = det.box.x + det.box.width * 0.5f;
      const float det_cy = det.box.y + det.box.height * 0.5f;
      const float norm_center_delta =
          std::hypot(det_cx - predicted_x, det_cy - predicted_y) / prev_scale;
      const float area_ratio =
          static_cast<float>(std::max(1, det.box.area())) / prev_area;
      const float log_area_delta = std::abs(std::log(std::max(0.05f, area_ratio)));
      if (stable_mode_ && log_area_delta > kMaxStableLogAreaDelta && iou < 0.35f) {
        continue;
      }

      // Presentation mode: keep the displayed target locked unless the new box
      // is spatially plausible. This avoids jumping to a different high-score
      // candidate when the detector briefly produces unstable boxes.
      const bool plausible =
          stable_mode_
              ? (iou > 0.16f ||
                 norm_center_delta < (fast_follow_ ? 0.48f : 0.16f) ||
                 (fast_follow_ && det.score >= last_score_ + 0.08f &&
                  norm_center_delta < 0.72f))
              : (iou > 0.10f || norm_center_delta < 0.26f);
      if (!plausible) {
        continue;
      }

      const float assoc =
          stable_mode_
              ? det.score + 5.0f * iou -
                    (fast_follow_ ? 0.95f : 2.40f) * norm_center_delta -
                    1.20f * log_area_delta
              : det.score + 3.5f * iou - 1.15f * norm_center_delta -
                    0.55f * log_area_delta;
      if (best == nullptr || assoc > best_assoc) {
        best = &det;
        best_assoc = assoc;
      }
    }
    if (stable_mode_ && best != nullptr && best_assoc < kMinStableAssoc) {
      return nullptr;
    }
    return best;
  }

  static const Detection* HighestScoreDetection(
      const std::vector<Detection>& detections) {
    const Detection* best = nullptr;
    for (const Detection& det : detections) {
      if (best == nullptr || det.score > best->score) {
        best = &det;
      }
    }
    return best;
  }

  static float RectIou(const cv::Rect& a, const cv::Rect& b) {
    const cv::Rect inter = a & b;
    const float inter_area = static_cast<float>(std::max(0, inter.area()));
    const float union_area =
        static_cast<float>(std::max(1, a.area() + b.area() - inter.area()));
    return inter_area / union_area;
  }

  static float SmoothFloat(float previous, float current, float alpha) {
    return previous * (1.0f - alpha) + current * alpha;
  }

  static float Clamp(float value, float low, float high) {
    return std::max(low, std::min(high, value));
  }

  static float SmoothCoordinate(float previous, float current, float alpha,
                                float max_step) {
    const float smoothed = SmoothFloat(previous, current, alpha);
    const float delta = std::max(-max_step, std::min(max_step, smoothed - previous));
    return previous + delta;
  }

  static float SmoothDimension(float previous, float current, float alpha,
                               float max_step_ratio) {
    const float smoothed = SmoothFloat(previous, current, alpha);
    const float max_step = std::max(2.0f, previous * max_step_ratio);
    const float delta = std::max(-max_step, std::min(max_step, smoothed - previous));
    return std::max(2.0f, previous + delta);
  }

  cv::Rect CurrentRect() const {
    const int w = std::max(2, static_cast<int>(std::round(width_)));
    const int h = std::max(2, static_cast<int>(std::round(height_)));
    const int x = static_cast<int>(std::round(center_x_ - w * 0.5f));
    const int y = static_cast<int>(std::round(center_y_ - h * 0.5f));
    return cv::Rect(x, y, w, h);
  }

  int MaxHoldFrames() const {
    if (fast_follow_) {
      return stable_mode_ ? 10 : 8;
    }
    return stable_mode_ ? 40 : 24;
  }

  static constexpr int kRequiredInitialHits = 3;
  static constexpr int kMaxPendingMisses = 3;
  static constexpr float kImmediateLockScore = 0.62f;
  static constexpr float kMaxStableLogAreaDelta = 0.92f;
  static constexpr float kMinStableAssoc = -0.25f;
  bool stable_mode_ = true;
  bool fast_follow_ = false;
  bool has_box_ = false;
  int hold_frames_ = 0;
  int pending_hits_ = 0;
  int pending_misses_ = 0;
  cv::Rect pending_box_;
  float pending_score_ = 0.0f;
  float center_x_ = 0.0f;
  float center_y_ = 0.0f;
  float width_ = 0.0f;
  float height_ = 0.0f;
  float last_score_ = 0.0f;
  float velocity_x_ = 0.0f;
  float velocity_y_ = 0.0f;
};

class MppH264FileDecoder {
 public:
  MppH264FileDecoder() = default;
  ~MppH264FileDecoder() { Release(); }

  MppH264FileDecoder(const MppH264FileDecoder&) = delete;
  MppH264FileDecoder& operator=(const MppH264FileDecoder&) = delete;

  bool Init() {
    Release();

    MPP_RET ret = mpp_create(&ctx_, &mpi_);
    if (ret != MPP_OK || ctx_ == nullptr || mpi_ == nullptr) {
      std::cerr << "mpp_create failed ret=" << ret << std::endl;
      return false;
    }

    RK_U32 split = 1;
    ret = mpi_->control(ctx_, MPP_DEC_SET_PARSER_SPLIT_MODE, &split);
    if (ret != MPP_OK) {
      std::cerr << "MPP_DEC_SET_PARSER_SPLIT_MODE warning ret=" << ret << std::endl;
    }

    ret = mpp_init(ctx_, MPP_CTX_DEC, MPP_VIDEO_CodingAVC);
    if (ret != MPP_OK) {
      std::cerr << "mpp_init H264 decoder failed ret=" << ret << std::endl;
      Release();
      return false;
    }

    RK_S64 output_timeout_ms = 10;
    ret = mpi_->control(ctx_, MPP_SET_OUTPUT_TIMEOUT, &output_timeout_ms);
    if (ret != MPP_OK) {
      std::cerr << "MPP_SET_OUTPUT_TIMEOUT warning ret=" << ret << std::endl;
    }

    initialized_ = true;
    return true;
  }

  bool Feed(const void* data, std::size_t size, std::vector<DecodedFrame>* frames,
            double* decode_ms) {
    if (!initialized_ || data == nullptr || size == 0 || frames == nullptr) {
      return false;
    }
    if (decode_ms != nullptr) {
      *decode_ms = 0.0;
    }

    MppPacket packet = nullptr;
    MPP_RET ret = mpp_packet_init(&packet, const_cast<void*>(data), size);
    if (ret != MPP_OK || packet == nullptr) {
      std::cerr << "mpp_packet_init failed ret=" << ret << std::endl;
      return false;
    }

    const auto decode_start = Clock::now();
    const int max_input_retries =
        std::max(3, EnvInt("RK_YOLO_MPP_INPUT_RETRIES", 40));
    for (int retry = 0; retry < max_input_retries; ++retry) {
      ret = mpi_->decode_put_packet(ctx_, packet);
      if (ret == MPP_OK) {
        break;
      }
      if (ret != MPP_ERR_BUFFER_FULL) {
        break;
      }
      ++buffer_full_count_;
      if (buffer_full_count_ <= 3 || buffer_full_count_ % 60 == 0) {
        std::cerr << "mpp input queue full, draining decoded frames count="
                  << buffer_full_count_ << " retry=" << retry + 1 << "/"
                  << max_input_retries << std::endl;
      }
      DrainAvailable(frames, 32, nullptr);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (ret != MPP_OK) {
      const auto decode_end = Clock::now();
      mpp_packet_deinit(&packet);
      if (decode_ms != nullptr) {
        *decode_ms = ElapsedMs(decode_start, decode_end);
      }
      std::cerr << "mpp decode_put_packet failed ret=" << ret
                << " after retries; current packet was not consumed" << std::endl;
      return false;
    }

    DrainAvailable(frames, 32, nullptr);
    const auto decode_end = Clock::now();
    mpp_packet_deinit(&packet);
    if (decode_ms != nullptr) {
      *decode_ms = ElapsedMs(decode_start, decode_end);
    }
    return true;
  }

  bool Flush(std::vector<DecodedFrame>* frames) {
    if (!initialized_ || frames == nullptr) {
      return false;
    }
    const int flush_rounds = std::max(20, EnvInt("RK_YOLO_MPP_FLUSH_ROUNDS", 240));
    const int flush_idle_limit =
        std::max(10, EnvInt("RK_YOLO_MPP_FLUSH_IDLE_ROUNDS", 50));
    const int flush_sleep_ms =
        std::max(1, EnvInt("RK_YOLO_MPP_FLUSH_SLEEP_MS", 5));

    MppPacket packet = nullptr;
    MPP_RET ret = mpp_packet_init(&packet, nullptr, 0);
    if (ret == MPP_OK && packet != nullptr) {
      mpp_packet_set_eos(packet);
      for (int retry = 0; retry < 8; ++retry) {
        ret = mpi_->decode_put_packet(ctx_, packet);
        if (ret == MPP_OK) {
          break;
        }
        if (ret != MPP_ERR_BUFFER_FULL) {
          std::cerr << "mpp eos packet failed ret=" << ret << std::endl;
          break;
        }
        DrainAvailable(frames, 32, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(flush_sleep_ms));
      }
      mpp_packet_deinit(&packet);
    }

    bool saw_eos = false;
    int idle_rounds = 0;
    int drained_frames = 0;
    for (int round = 0; round < flush_rounds && !saw_eos &&
                        idle_rounds < flush_idle_limit;
         ++round) {
      const int drained = DrainAvailable(frames, 32, &saw_eos);
      drained_frames += drained;
      if (drained == 0) {
        ++idle_rounds;
        std::this_thread::sleep_for(std::chrono::milliseconds(flush_sleep_ms));
      } else {
        idle_rounds = 0;
      }
    }
    std::cout << "mpp_flush drained=" << drained_frames
              << " saw_eos=" << (saw_eos ? "yes" : "no")
              << " idle_rounds=" << idle_rounds << std::endl;
    return true;
  }

  void Release() {
    if (ctx_ != nullptr) {
      if (mpi_ != nullptr) {
        mpi_->reset(ctx_);
      }
      mpp_destroy(ctx_);
      ctx_ = nullptr;
      mpi_ = nullptr;
    }
    if (frame_group_ != nullptr) {
      mpp_buffer_group_put(frame_group_);
      frame_group_ = nullptr;
    }
    initialized_ = false;
  }

 private:
  int DrainAvailable(std::vector<DecodedFrame>* frames, int max_frames,
                     bool* saw_eos) {
    if (frames == nullptr) {
      return 0;
    }
    int drained = 0;
    int empty_polls = 0;
    for (int i = 0; i < max_frames && empty_polls < 10;) {
      MppFrame raw_frame = nullptr;
      MPP_RET ret = mpi_->decode_get_frame(ctx_, &raw_frame);
      if (ret != MPP_OK || raw_frame == nullptr) {
        ++empty_polls;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      empty_polls = 0;
      ++i;
      ++drained;
      DecodedFrame decoded;
      if (HandleDecodedFrame(raw_frame, &decoded, saw_eos) &&
          decoded.frame != nullptr) {
        frames->push_back(decoded);
      }
    }
    return drained;
  }

  bool HandleDecodedFrame(MppFrame raw_frame, DecodedFrame* decoded,
                          bool* saw_eos) {
    MppFrameGuard frame(raw_frame);
    if (decoded == nullptr) {
      return false;
    }
    const bool is_eos = mpp_frame_get_eos(frame.get());
    if (is_eos && saw_eos != nullptr) {
      *saw_eos = true;
    }
    if (mpp_frame_get_info_change(frame.get())) {
      const size_t buf_size = static_cast<size_t>(mpp_frame_get_buf_size(frame.get()));
      MPP_RET ret = MPP_OK;
      if (frame_group_ == nullptr) {
        ret = mpp_buffer_group_get_internal(&frame_group_, MPP_BUFFER_TYPE_DRM);
        if (ret != MPP_OK) {
          std::cerr << "mpp_buffer_group_get_internal failed ret=" << ret << std::endl;
          return false;
        }
        ret = mpi_->control(ctx_, MPP_DEC_SET_EXT_BUF_GROUP, frame_group_);
        if (ret != MPP_OK) {
          std::cerr << "MPP_DEC_SET_EXT_BUF_GROUP failed ret=" << ret << std::endl;
          return false;
        }
      } else {
        ret = mpp_buffer_group_clear(frame_group_);
        if (ret != MPP_OK) {
          std::cerr << "mpp_buffer_group_clear warning ret=" << ret << std::endl;
        }
      }
      const int buffer_limit =
          std::max(8, EnvInt("RK_YOLO_MPP_BUFFER_LIMIT", 24));
      ret = mpp_buffer_group_limit_config(frame_group_, buf_size, buffer_limit);
      if (ret != MPP_OK) {
        std::cerr << "mpp_buffer_group_limit_config warning ret=" << ret << std::endl;
      }
      ret = mpi_->control(ctx_, MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
      if (ret != MPP_OK) {
        std::cerr << "MPP_DEC_SET_INFO_CHANGE_READY failed ret=" << ret << std::endl;
      }
      return false;
    }
    if (mpp_frame_get_errinfo(frame.get())) {
      ++skipped_error_frames_;
      if (skipped_error_frames_ <= 3 || skipped_error_frames_ % 60 == 0) {
        std::cerr << "mpp decoded frame has error info, skipping count="
                  << skipped_error_frames_ << std::endl;
      }
      return false;
    }

    MppFrameFormat fmt = mpp_frame_get_fmt(frame.get());
#ifdef MPP_FRAME_FMT_IS_FBC
    if (MPP_FRAME_FMT_IS_FBC(fmt)) {
      std::cerr << "mpp decoded frame is FBC format, unsupported for this validator"
                << std::endl;
      return false;
    }
#endif
    const int rga_format = RgaFormatFromMpp(fmt);
    if (rga_format == 0) {
      std::cerr << "unsupported mpp frame format: " << MppFrameFormatName(fmt)
                << std::endl;
      return false;
    }

    MppBuffer buffer = mpp_frame_get_buffer(frame.get());
    if (buffer == nullptr) {
      if (!is_eos) {
        std::cerr << "mpp decoded frame has no buffer" << std::endl;
      }
      return false;
    }
    const int dma_fd = mpp_buffer_get_fd(buffer);
    if (dma_fd < 0) {
      std::cerr << "mpp decoded buffer has no dma fd" << std::endl;
      return false;
    }

    decoded->frame = frame.release();
    decoded->width = static_cast<int>(mpp_frame_get_width(decoded->frame));
    decoded->height = static_cast<int>(mpp_frame_get_height(decoded->frame));
    decoded->hor_stride = static_cast<int>(mpp_frame_get_hor_stride(decoded->frame));
    decoded->ver_stride = static_cast<int>(mpp_frame_get_ver_stride(decoded->frame));
    decoded->rga_format = rga_format;
    decoded->dma_fd = dma_fd;
    return true;
  }

  MppCtx ctx_ = nullptr;
  MppApi* mpi_ = nullptr;
  MppBufferGroup frame_group_ = nullptr;
  bool initialized_ = false;
  int buffer_full_count_ = 0;
  int skipped_error_frames_ = 0;
};

#endif

void PrintUsage(const char* program) {
  std::cerr << "Usage: " << program
            << " <model.rknn> <input.h264> [conf=0.24] [nms=0.45]"
            << " [frames=300] [chunk_size=4096] [output_video.mp4] [output_fps=20]"
            << " [detect_every_n=1]"
            << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(HAVE_RGA) || !defined(HAVE_MPP)
  (void)argc;
  (void)argv;
  std::cerr << "rk_yolo_mpp_file_demo requires librga and Rockchip MPP at build time"
            << std::endl;
  return 2;
#else
  if (argc < 3) {
    PrintUsage(argv[0]);
    return 1;
  }

  const std::string model_path = argv[1];
  const std::string input_path = argv[2];
  const float conf = (argc > 3) ? std::stof(argv[3]) : 0.24f;
  const float nms = (argc > 4) ? std::stof(argv[4]) : 0.45f;
  const int max_frames = (argc > 5) ? std::stoi(argv[5]) : 300;
  const int chunk_size = (argc > 6) ? std::stoi(argv[6]) : 4096;
  const std::string output_video_path = (argc > 7) ? argv[7] : "";
  const double output_fps = (argc > 8) ? std::stod(argv[8]) : 20.0;
  const int detect_every_n =
      std::max(1, (argc > 9) ? std::stoi(argv[9])
                             : EnvInt("RK_YOLO_DETECT_EVERY_N", 1));
  const int npu_core_mask = EnvInt("RK_YOLO_NPU_CORE_MASK", -1);
  const int npu_workers =
      std::max(1, std::min(3, EnvInt("RK_YOLO_NPU_WORKERS", 1)));

  setenv("RK_YOLO_ZERO_COPY_INPUT", "1", 1);
  setenv("RK_YOLO_RGA_LETTERBOX", "1", 0);
  const bool visualize_all_detections = EnvFlagEnabled("RK_YOLO_VIS_MULTI");
  const bool route_b_stable = EnvFlag("RK_YOLO_ROUTE_B_STABLE", true);
  const bool route_b_fast_follow =
      EnvFlag("RK_YOLO_ROUTE_B_FAST_FOLLOW", false);
  const bool route_b_dedup = EnvFlag("RK_YOLO_ROUTE_B_DEDUP", true);
  const OsdIgnoreConfig osd_ignore{
      EnvFlag("RK_YOLO_IGNORE_OSD", false),
      EnvFloat("RK_YOLO_OSD_LEFT", 0.0f),
      EnvFloat("RK_YOLO_OSD_TOP", 0.55f),
      EnvFloat("RK_YOLO_OSD_RIGHT", 0.42f),
      EnvFloat("RK_YOLO_OSD_BOTTOM", 1.0f)};

  std::cout << "aggressive route B file validator enabled" << std::endl;
  std::cout << "path=H264 elementary stream -> MPP decode -> MppFrame fd"
            << " -> RGA letterbox -> RKNN input memory -> NPU" << std::endl;
  std::cout << "input=" << input_path << " conf=" << conf << " nms=" << nms
            << " frames=" << max_frames << " chunk_size=" << chunk_size
            << " detect_every_n=" << detect_every_n
            << " npu_core_mask=" << npu_core_mask
            << " npu_workers=" << npu_workers
            << std::endl;
  if (!output_video_path.empty()) {
    std::cout << "visual_output=" << output_video_path << " fps=" << output_fps
              << " note=visualization uses an extra RGA DMA->BGR copy for drawing"
              << std::endl;
    std::cout << "visual_multi_target="
              << (visualize_all_detections ? "on" : "off")
              << " note=off keeps the original single-target stabilized demo view"
              << std::endl;
    std::cout << "route_b_stable=" << (route_b_stable ? "on" : "off")
              << " note=on confirms low-score targets and slows display-box scale changes"
              << std::endl;
    std::cout << "route_b_fast_follow="
              << (route_b_fast_follow ? "on" : "off")
              << " note=on tracks moving targets with less display lag"
              << std::endl;
    std::cout << "route_b_dedup=" << (route_b_dedup ? "on" : "off")
              << " note=on suppresses nearby duplicate boxes around the same target"
              << std::endl;
    std::cout << "ignore_osd=" << (osd_ignore.enabled ? "on" : "off")
              << " region=[" << osd_ignore.left << "," << osd_ignore.top
              << "," << osd_ignore.right << "," << osd_ignore.bottom
              << "] note=use for public videos with on-screen telemetry text"
              << std::endl;
  }

  std::ifstream input(input_path, std::ios::binary);
  if (!input) {
    std::cerr << "failed to open H264 input: " << input_path << std::endl;
    return 1;
  }

  std::unique_ptr<YoloRknnDetector> detector;
  if (npu_workers == 1) {
    detector = std::make_unique<YoloRknnDetector>();
    if (!detector->Load(model_path)) {
      std::cerr << "failed to load model: " << model_path << std::endl;
      return 1;
    }
    if (npu_core_mask >= 0 && !detector->SetCoreMask(npu_core_mask)) {
      return 1;
    }
    if (!detector->zero_copy_input_enabled()) {
      std::cerr << "RKNN zero-copy input memory is not enabled; route B cannot continue"
                << std::endl;
      return 1;
    }
  }

  MppH264FileDecoder decoder;
  if (!decoder.Init()) {
    return 1;
  }

  if (npu_workers > 1) {
    std::vector<std::unique_ptr<YoloRknnDetector>> workers;
    workers.reserve(static_cast<std::size_t>(npu_workers));
    const int worker_core_masks[3] = {1, 2, 4};
    for (int i = 0; i < npu_workers; ++i) {
      auto worker = std::make_unique<YoloRknnDetector>();
      if (!worker->Load(model_path)) {
        std::cerr << "failed to load model for worker " << i << ": "
                  << model_path << std::endl;
        return 1;
      }
      if (!worker->SetCoreMask(worker_core_masks[i])) {
        std::cerr << "failed to bind worker " << i << " to NPU core mask "
                  << worker_core_masks[i] << std::endl;
        return 1;
      }
      if (!worker->zero_copy_input_enabled()) {
        std::cerr << "RKNN zero-copy input memory is not enabled for worker "
                  << i << std::endl;
        return 1;
      }
      std::cout << "npu_worker=" << i
                << " core_mask=" << worker_core_masks[i] << std::endl;
      workers.push_back(std::move(worker));
    }

    struct WorkerStats {
      int inferred_frames = 0;
      int detected_frames = 0;
      std::size_t total_detections = 0;
      double prepare_sum = 0.0;
      double run_sum = 0.0;
      double total_sum = 0.0;
    };

    struct StreamStats {
      int decoded_frames = 0;
      int inferred_frames = 0;
      int detected_frames = 0;
      int display_detected_frames = 0;
      int visualized_frames = 0;
      std::size_t total_detections = 0;
      std::size_t total_display_detections = 0;
      double decode_sum = 0.0;
      double prepare_sum = 0.0;
      double run_sum = 0.0;
      double total_sum = 0.0;
    };

    const std::size_t stream_queue_depth = static_cast<std::size_t>(
        std::max(2, EnvInt("RK_YOLO_STREAM_QUEUE_DEPTH", npu_workers * 4)));
    FrameTaskQueue task_queue(stream_queue_depth);
    FrameResultQueue result_queue;
    std::vector<WorkerStats> worker_stats(static_cast<std::size_t>(npu_workers));
    StreamStats stream_stats;
    std::atomic<bool> worker_failed{false};
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(npu_workers));
    DisplayBoxStabilizer display_stabilizer(route_b_stable,
                                            route_b_fast_follow);
    cv::VideoWriter writer;
    const auto wall_start = Clock::now();

    std::cout << "parallel_stream_queue_depth=" << stream_queue_depth
              << " note=streaming decode feeds DMA frames directly to workers"
              << std::endl;

    for (int worker_idx = 0; worker_idx < npu_workers; ++worker_idx) {
      threads.emplace_back([&, worker_idx]() {
        YoloRknnDetector& worker = *workers[static_cast<std::size_t>(worker_idx)];
        WorkerStats& stats = worker_stats[static_cast<std::size_t>(worker_idx)];
        std::shared_ptr<FrameTask> task;
        while (task_queue.Pop(&task)) {
          if (!task || !task->frame || task->frame->decoded.frame == nullptr) {
            continue;
          }
          const DecodedFrame& decoded = task->frame->decoded;
          FrameResult result;
          result.frame_index = task->frame_index;
          result.worker_index = worker_idx;
          result.decode_ms = task->decode_ms;
          result.frame = task->frame;

          InferProfile profile;
          YoloRknnDetector::LetterBoxInfo letterbox;
          bool prepared = false;
          {
            std::lock_guard<std::mutex> lock(RgaOperationMutex());
            prepared = worker.PrepareDmaFdToBoundInputStrided(
                decoded.dma_fd, decoded.width, decoded.height, decoded.hor_stride,
                decoded.ver_stride, decoded.rga_format, &letterbox, &profile);
          }
          if (!prepared) {
            std::cerr << "worker=" << worker_idx
                      << " failed to prepare frame=" << task->frame_index
                      << std::endl;
            result.profile = profile;
            result_queue.Push(std::move(result));
            worker_failed = true;
            continue;
          }

          result.detections = worker.InferBoundInput(letterbox, conf, nms, &profile);
          result.profile = profile;
          result.inferred = true;
          ++stats.inferred_frames;
          stats.prepare_sum += profile.prepare_ms;
          stats.run_sum += profile.run_ms;
          stats.total_sum += profile.total_ms;
          if (!result.detections.empty()) {
            ++stats.detected_frames;
            stats.total_detections += result.detections.size();
          }
          if (task->frame_index <= 5 || task->frame_index % 30 == 0) {
            std::cout << "parallel_frame=" << task->frame_index
                      << " worker=" << worker_idx
                      << " det=" << result.detections.size()
                      << " prepare_ms=" << std::fixed << std::setprecision(2)
                      << profile.prepare_ms << " run_ms=" << profile.run_ms
                      << " total_ms=" << profile.total_ms << std::endl;
          }
          result_queue.Push(std::move(result));
        }
      });
    }

    std::thread result_thread([&]() {
      std::map<int, FrameResult> pending;
      int next_output_frame = 1;

      auto consume_result = [&](FrameResult result) {
        if (!result.frame || result.frame->decoded.frame == nullptr) {
          return;
        }
        const DecodedFrame& decoded = result.frame->decoded;
        ++stream_stats.decoded_frames;
        stream_stats.decode_sum += result.decode_ms;

        if (result.inferred) {
          ++stream_stats.inferred_frames;
          stream_stats.prepare_sum += result.profile.prepare_ms;
          stream_stats.run_sum += result.profile.run_ms;
          stream_stats.total_sum += result.profile.total_ms;
          if (!result.detections.empty()) {
            ++stream_stats.detected_frames;
            stream_stats.total_detections += result.detections.size();
          }
        }

        const std::vector<Detection> visual_source =
            SuppressNearbyDuplicateDetections(
                FilterOsdDetections(result.detections, decoded.width, decoded.height,
                                    osd_ignore),
                decoded.width, decoded.height, route_b_dedup);
        const std::vector<Detection> display_detections =
            visualize_all_detections ? visual_source
                                     : display_stabilizer.Update(visual_source);
        if (!display_detections.empty()) {
          ++stream_stats.display_detected_frames;
          stream_stats.total_display_detections += display_detections.size();
        }

        if (!output_video_path.empty()) {
          cv::Mat bgr;
          if (ConvertDmaFrameToBgr(decoded, &bgr)) {
            DrawDetections(&bgr, display_detections,
                           visualize_all_detections ? "boxes" : "targets");
            if (!writer.isOpened()) {
              const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
              if (!writer.open(output_video_path, fourcc, output_fps, bgr.size())) {
                std::cerr << "failed to open output video writer: "
                          << output_video_path << std::endl;
              }
            }
            if (writer.isOpened()) {
              writer.write(bgr);
              ++stream_stats.visualized_frames;
            }
          }
        }

        if (result.frame_index <= 5 || result.frame_index % 30 == 0 ||
            !display_detections.empty()) {
          std::cout << "stream_frame=" << result.frame_index
                    << " worker=" << result.worker_index
                    << " det=" << result.detections.size()
                    << " display_det=" << display_detections.size()
                    << " decode_ms=" << std::fixed << std::setprecision(2)
                    << result.decode_ms
                    << " prepare_ms=" << result.profile.prepare_ms
                    << " run_ms=" << result.profile.run_ms
                    << " total_ms=" << result.profile.total_ms;
          if (!result.detections.empty()) {
            std::cout << " best_score=" << result.detections.front().score;
          }
          std::cout << std::endl;
        }
      };

      FrameResult result;
      while (result_queue.Pop(&result)) {
        pending.emplace(result.frame_index, std::move(result));
        result = FrameResult();
        while (true) {
          auto it = pending.find(next_output_frame);
          if (it == pending.end()) {
            break;
          }
          consume_result(std::move(it->second));
          pending.erase(it);
          ++next_output_frame;
        }
      }

      for (auto& entry : pending) {
        consume_result(std::move(entry.second));
      }
    });

    std::vector<unsigned char> buffer(
        static_cast<std::size_t>(std::max(chunk_size, 4096)));
    int chunks = 0;
    int produced_frames = 0;

    auto enqueue_frames = [&](std::vector<DecodedFrame>* frames, double decode_ms) {
      if (frames == nullptr) {
        return true;
      }
      for (DecodedFrame& decoded : *frames) {
        if (decoded.frame == nullptr) {
          continue;
        }
        if (produced_frames >= max_frames) {
          MppFrameGuard unused(decoded.frame);
          decoded.frame = nullptr;
          continue;
        }
        auto owner = std::make_shared<DecodedFrameOwner>();
        owner->decoded = decoded;
        decoded.frame = nullptr;

        auto task = std::make_shared<FrameTask>();
        task->frame_index = ++produced_frames;
        task->decode_ms = decode_ms;
        task->frame = std::move(owner);
        if (!task_queue.Push(std::move(task))) {
          return false;
        }
      }
      frames->clear();
      return true;
    };

    while (input && produced_frames < max_frames) {
      input.read(reinterpret_cast<char*>(buffer.data()),
                 static_cast<std::streamsize>(buffer.size()));
      const std::streamsize got = input.gcount();
      if (got <= 0) {
        break;
      }
      ++chunks;

      std::vector<DecodedFrame> frames;
      double decode_ms = 0.0;
      if (!decoder.Feed(buffer.data(), static_cast<std::size_t>(got), &frames,
                        &decode_ms)) {
        break;
      }
      if (!enqueue_frames(&frames, decode_ms)) {
        break;
      }
    }

    if (produced_frames < max_frames) {
      std::vector<DecodedFrame> frames;
      decoder.Flush(&frames);
      enqueue_frames(&frames, 0.0);
    }

    task_queue.Close();
    for (std::thread& thread : threads) {
      thread.join();
    }
    result_queue.Close();
    result_thread.join();

    if (writer.isOpened()) {
      writer.release();
    }

    int inferred_frames = 0;
    int detected_frames = 0;
    std::size_t total_detections = 0;
    double prepare_sum = 0.0;
    double run_sum = 0.0;
    double total_sum = 0.0;
    for (int i = 0; i < npu_workers; ++i) {
      const WorkerStats& stats = worker_stats[static_cast<std::size_t>(i)];
      inferred_frames += stats.inferred_frames;
      detected_frames += stats.detected_frames;
      total_detections += stats.total_detections;
      prepare_sum += stats.prepare_sum;
      run_sum += stats.run_sum;
      total_sum += stats.total_sum;
      std::cout << "worker_summary worker=" << i
                << " inferred_frames=" << stats.inferred_frames
                << " detected_frames=" << stats.detected_frames
                << " total_detections=" << stats.total_detections
                << " avg_prepare_ms="
                << (stats.inferred_frames > 0
                        ? stats.prepare_sum / stats.inferred_frames
                        : 0.0)
                << " avg_run_ms="
                << (stats.inferred_frames > 0
                        ? stats.run_sum / stats.inferred_frames
                        : 0.0)
                << " avg_total_ms="
                << (stats.inferred_frames > 0
                        ? stats.total_sum / stats.inferred_frames
                        : 0.0)
                << std::endl;
    }

    const auto wall_end = Clock::now();
    const double wall_s =
        std::chrono::duration<double>(wall_end - wall_start).count();
    std::cout << "summary chunks=" << chunks
              << " decoded_frames=" << stream_stats.decoded_frames
              << " inferred_frames=" << stream_stats.inferred_frames
              << " reused_frames=0"
              << " detected_frames=" << stream_stats.detected_frames
              << " display_detected_frames=" << stream_stats.display_detected_frames
              << " total_detections=" << stream_stats.total_detections
              << " total_display_detections="
              << stream_stats.total_display_detections
              << " visualized_frames=" << stream_stats.visualized_frames
              << " parallel_workers=" << npu_workers
              << " produced_frames=" << produced_frames
              << " worker_failed=" << (worker_failed ? 1 : 0)
              << " wall_fps="
              << (wall_s > 0.0 ? stream_stats.decoded_frames / wall_s : 0.0)
              << " npu_fps="
              << (wall_s > 0.0 ? stream_stats.inferred_frames / wall_s : 0.0)
              << " avg_decode_ms="
              << (stream_stats.decoded_frames > 0
                      ? stream_stats.decode_sum / stream_stats.decoded_frames
                      : 0.0)
              << " avg_prepare_ms="
              << (stream_stats.inferred_frames > 0
                      ? stream_stats.prepare_sum / stream_stats.inferred_frames
                      : 0.0)
              << " avg_run_ms="
              << (stream_stats.inferred_frames > 0
                      ? stream_stats.run_sum / stream_stats.inferred_frames
                      : 0.0)
              << " avg_total_ms="
              << (stream_stats.inferred_frames > 0
                      ? stream_stats.total_sum / stream_stats.inferred_frames
                      : 0.0)
              << std::endl;

    (void)inferred_frames;
    (void)detected_frames;
    (void)total_detections;
    (void)prepare_sum;
    (void)run_sum;
    (void)total_sum;
    return stream_stats.inferred_frames > 0 ? 0 : 1;
  }

  std::vector<unsigned char> buffer(static_cast<std::size_t>(std::max(chunk_size, 4096)));
  int chunks = 0;
  int decoded_frames = 0;
  int inferred_frames = 0;
  int reused_frames = 0;
  int detected_frames = 0;
  int display_detected_frames = 0;
  std::size_t total_detections = 0;
  std::size_t total_display_detections = 0;
  double decode_sum = 0.0;
  double prepare_sum = 0.0;
  double run_sum = 0.0;
  double total_sum = 0.0;
  int visualized_frames = 0;
  cv::VideoWriter writer;
  DisplayBoxStabilizer display_stabilizer(route_b_stable,
                                          route_b_fast_follow);
  std::vector<Detection> latest_detections;
  const auto wall_start = Clock::now();

  auto process_frames = [&](std::vector<DecodedFrame>* frames, double decode_ms) {
    if (frames == nullptr) {
      return;
    }
    for (DecodedFrame& decoded : *frames) {
      if (decoded.frame == nullptr || decoded_frames >= max_frames) {
        if (decoded.frame != nullptr) {
          MppFrameGuard unused(decoded.frame);
        }
        continue;
      }
      ++decoded_frames;
      decode_sum += decode_ms;
      MppFrameGuard frame_guard(decoded.frame);

      InferProfile profile;
      YoloRknnDetector::LetterBoxInfo letterbox;
      std::vector<Detection> detections;
      const bool should_infer =
          detect_every_n <= 1 || ((decoded_frames - 1) % detect_every_n == 0) ||
          latest_detections.empty();
      if (should_infer) {
        bool prepared = false;
        {
          std::lock_guard<std::mutex> lock(RgaOperationMutex());
          prepared = detector->PrepareDmaFdToBoundInputStrided(
              decoded.dma_fd, decoded.width, decoded.height, decoded.hor_stride,
              decoded.ver_stride, decoded.rga_format, &letterbox, &profile);
        }
        if (prepared) {
          detections = detector->InferBoundInput(letterbox, conf, nms, &profile);
          latest_detections = detections;
          ++inferred_frames;
        } else {
          std::cerr << "RGA -> RKNN bound input preparation failed" << std::endl;
          latest_detections.clear();
        }
      } else {
        detections = latest_detections;
        ++reused_frames;
      }

      if (should_infer && !detections.empty()) {
        ++detected_frames;
        total_detections += detections.size();
      }

      const std::vector<Detection> visual_source =
          SuppressNearbyDuplicateDetections(
              FilterOsdDetections(detections, decoded.width, decoded.height,
                                  osd_ignore),
              decoded.width, decoded.height, route_b_dedup);
      const std::vector<Detection> display_detections =
          visualize_all_detections ? visual_source
                                   : display_stabilizer.Update(visual_source);
      if (!display_detections.empty()) {
        ++display_detected_frames;
        total_display_detections += display_detections.size();
      }

      if (!output_video_path.empty()) {
        cv::Mat bgr;
        if (ConvertDmaFrameToBgr(decoded, &bgr)) {
          DrawDetections(&bgr, display_detections,
                         visualize_all_detections ? "boxes" : "targets");
          if (!writer.isOpened()) {
            const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
            if (!writer.open(output_video_path, fourcc, output_fps, bgr.size())) {
              std::cerr << "failed to open output video writer: "
                        << output_video_path << std::endl;
            }
          }
          if (writer.isOpened()) {
            writer.write(bgr);
            ++visualized_frames;
          }
        }
      }

      if (should_infer) {
        prepare_sum += profile.prepare_ms;
        run_sum += profile.run_ms;
        total_sum += profile.total_ms;
      }

      if (decoded_frames <= 5 || decoded_frames % 30 == 0 ||
          (should_infer && !detections.empty())) {
        std::cout << "frame=" << decoded_frames << " infer="
                  << (should_infer ? "yes" : "reuse") << " inferred="
                  << inferred_frames << " chunks=" << chunks
                  << " decoded=" << decoded_frames << " size=" << decoded.width
                  << "x" << decoded.height << " stride=" << decoded.hor_stride
                  << "x" << decoded.ver_stride << " det=" << detections.size()
                  << " display_det=" << display_detections.size()
                  << " decode_ms=" << std::fixed << std::setprecision(2)
                  << decode_ms << " prepare_ms=" << profile.prepare_ms
                  << " run_ms=" << profile.run_ms
                  << " total_ms=" << profile.total_ms;
        if (!detections.empty()) {
          std::cout << " best_score=" << detections.front().score;
        }
        std::cout << std::endl;
      }
    }
    frames->clear();
  };

  while (input && decoded_frames < max_frames) {
    input.read(reinterpret_cast<char*>(buffer.data()),
               static_cast<std::streamsize>(buffer.size()));
    const std::streamsize got = input.gcount();
    if (got <= 0) {
      break;
    }
    ++chunks;

    std::vector<DecodedFrame> frames;
    double decode_ms = 0.0;
    if (!decoder.Feed(buffer.data(), static_cast<std::size_t>(got), &frames,
                      &decode_ms)) {
      break;
    }
    process_frames(&frames, decode_ms);
  }

  if (decoded_frames < max_frames) {
    std::vector<DecodedFrame> frames;
    decoder.Flush(&frames);
    process_frames(&frames, 0.0);
  }

  const auto wall_end = Clock::now();
  const double wall_s = std::chrono::duration<double>(wall_end - wall_start).count();
  std::cout << "summary chunks=" << chunks
            << " decoded_frames=" << decoded_frames
            << " inferred_frames=" << inferred_frames
            << " reused_frames=" << reused_frames
            << " detected_frames=" << detected_frames
            << " display_detected_frames=" << display_detected_frames
            << " total_detections=" << total_detections
            << " total_display_detections=" << total_display_detections
            << " visualized_frames=" << visualized_frames
            << " wall_fps=" << (wall_s > 0.0 ? decoded_frames / wall_s : 0.0)
            << " npu_fps=" << (wall_s > 0.0 ? inferred_frames / wall_s : 0.0)
            << " avg_decode_ms="
            << (decoded_frames > 0 ? decode_sum / decoded_frames : 0.0)
            << " avg_prepare_ms="
            << (inferred_frames > 0 ? prepare_sum / inferred_frames : 0.0)
            << " avg_run_ms=" << (inferred_frames > 0 ? run_sum / inferred_frames : 0.0)
            << " avg_total_ms="
            << (inferred_frames > 0 ? total_sum / inferred_frames : 0.0)
            << std::endl;

  return inferred_frames > 0 ? 0 : 1;
#endif
}
