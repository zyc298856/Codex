#include "yolo_legacy_roi.h"
#include "yolo_rknn.h"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
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

constexpr float kDefaultScoreThreshold = 0.30f;
constexpr float kDefaultNmsThreshold = 0.45f;
constexpr int kDefaultAlarmHoldFrames = 5;

struct AlarmConfig {
  bool overlay_enabled = true;
  int hold_frames = kDefaultAlarmHoldFrames;
  std::string gpio_value_path;
};

struct AlarmState {
  bool active = false;
  bool previous_active = false;
  int missed_frames = 0;
  int event_count = 0;
};

struct FramePacket {
  int frame_index = 0;
  cv::Mat frame;
};

struct PreparedPacket {
  int frame_index = 0;
  cv::Mat frame;
  YoloRknnDetector::PreparedInput prepared;
  InferProfile profile;
};

struct ResultPacket {
  int frame_index = 0;
  cv::Mat frame;
  std::vector<Detection> detections;
  InferProfile profile;
  double infer_ms = 0.0;
};

template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(std::max<std::size_t>(1, capacity)) {}

  bool Push(T item) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_full_.wait(lock, [&] { return closed_ || queue_.size() < capacity_; });
    if (closed_) {
      return false;
    }
    queue_.push(std::move(item));
    not_empty_.notify_one();
    return true;
  }

  bool Pop(T* item) {
    std::unique_lock<std::mutex> lock(mutex_);
    not_empty_.wait(lock, [&] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
      return false;
    }
    *item = std::move(queue_.front());
    queue_.pop();
    not_full_.notify_one();
    return true;
  }

  void Close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
  }

 private:
  std::size_t capacity_;
  std::queue<T> queue_;
  std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  bool closed_ = false;
};

bool EnvFlagEnabled(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

bool ParseEnvBool(const char* name, bool default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value;
  }
  return value[0] != '0';
}

int ParseEnvInt(const char* name, int default_value, int min_value, int max_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value;
  }
  return std::clamp(std::atoi(value), min_value, max_value);
}

AlarmConfig LoadAlarmConfig() {
  AlarmConfig config;
  config.overlay_enabled = ParseEnvBool("RK_YOLO_ALARM_OVERLAY", true);
  config.hold_frames = ParseEnvInt("RK_YOLO_ALARM_HOLD_FRAMES", kDefaultAlarmHoldFrames, 0, 300);
  const char* gpio_value_path = std::getenv("RK_YOLO_GPIO_VALUE_PATH");
  if (gpio_value_path != nullptr) {
    config.gpio_value_path = gpio_value_path;
  }
  return config;
}

void PrintUsage(const char* argv0) {
  std::cout << "Usage: " << argv0
            << " <input_video> <output_video> [model_path=<auto>] [score_thresh=0.30] "
               "[nms_thresh=0.45] [detections_csv] [roi_jsonl] [alarm_csv]"
            << std::endl;
}

const char* ClassName(int class_id, int model_class_count) {
  if (model_class_count == 1 && class_id == 0) {
    return "drone";
  }
  if (class_id >= 0 && class_id < 80) {
    return kCocoClassNames[class_id];
  }
  return "unknown";
}

std::string BuildLabel(const Detection& det, int model_class_count) {
  std::ostringstream oss;
  oss << ClassName(det.class_id, model_class_count) << " " << std::fixed << std::setprecision(2)
      << det.score;
  return oss.str();
}

float MaxScore(const std::vector<Detection>& detections) {
  float max_score = 0.0f;
  for (const Detection& det : detections) {
    max_score = std::max(max_score, det.score);
  }
  return max_score;
}

void UpdateAlarmState(const std::vector<Detection>& detections, const AlarmConfig& config,
                      AlarmState* state) {
  state->previous_active = state->active;
  if (!detections.empty()) {
    state->active = true;
    state->missed_frames = 0;
    return;
  }

  if (state->active && state->missed_frames < config.hold_frames) {
    ++state->missed_frames;
    return;
  }

  state->active = false;
  state->missed_frames = 0;
}

void DrawAlarmOverlay(cv::Mat* frame, const AlarmState& state,
                      const std::vector<Detection>& detections) {
  if (frame == nullptr || frame->empty()) {
    return;
  }

  const int bar_h = std::max(34, frame->rows / 14);
  const cv::Scalar bg = state.active ? cv::Scalar(0, 0, 220) : cv::Scalar(40, 120, 40);
  cv::rectangle(*frame, cv::Rect(0, 0, frame->cols, bar_h), bg, cv::FILLED);

  std::ostringstream oss;
  if (state.active) {
    oss << "UAV ALERT | targets=" << detections.size() << " | max_score=" << std::fixed
        << std::setprecision(2) << MaxScore(detections);
  } else {
    oss << "NORMAL | no target";
  }

  cv::putText(*frame, oss.str(), cv::Point(12, std::min(bar_h - 9, 30)),
              cv::FONT_HERSHEY_SIMPLEX, 0.72, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
}

std::string ResolveModelPath(int argc, char** argv) {
  if (argc >= 4) {
    return argv[3];
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

void DrawDetections(cv::Mat* frame, const std::vector<Detection>& detections, int model_class_count) {
  for (const Detection& det : detections) {
    const cv::Scalar color = cv::Scalar(0, 255, 0);
    cv::rectangle(*frame, det.box, color, 2);

    const std::string label = BuildLabel(det, model_class_count);
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

void WriteDetectionRows(std::ofstream* csv_file, int frame_index,
                        const std::vector<Detection>& detections, int model_class_count) {
  for (const Detection& det : detections) {
    *csv_file << frame_index << "," << det.class_id << ","
              << ClassName(det.class_id, model_class_count) << "," << std::fixed
              << std::setprecision(4) << det.score << "," << det.box.x << "," << det.box.y
              << "," << det.box.width << "," << det.box.height << "\n";
  }
}

void WriteAlarmEvent(std::ofstream* alarm_file, int frame_index, const AlarmState& state,
                     const std::vector<Detection>& detections) {
  if (alarm_file == nullptr || !alarm_file->is_open() || state.active == state.previous_active) {
    return;
  }

  *alarm_file << frame_index << "," << (state.active ? "alarm_on" : "alarm_off") << ","
              << (state.active ? 1 : 0) << "," << detections.size() << "," << std::fixed
              << std::setprecision(4) << MaxScore(detections) << "\n";
}

void WriteGpioAlarmState(const AlarmConfig& config, const AlarmState& state, bool force = false) {
  if (config.gpio_value_path.empty() || (!force && state.active == state.previous_active)) {
    return;
  }

  std::ofstream gpio_file(config.gpio_value_path, std::ios::out | std::ios::trunc);
  if (!gpio_file.is_open()) {
    std::cerr << "failed to write gpio alarm state: " << config.gpio_value_path << std::endl;
    return;
  }
  gpio_file << (state.active ? "1" : "0") << "\n";
}

void PrintProfileHeader() {
  std::cout
      << "profile_csv_header,frame,input_mode,prepare_ms,input_set_or_update_ms,rknn_run_ms,"
         "outputs_get_ms,decode_nms_ms,outputs_release_ms,render_ms,total_work_ms,detections"
      << std::endl;
}

void PrintProfileRow(int frame_index, const InferProfile& profile, double render_ms) {
  const double total_work_ms = profile.total_ms + render_ms;
  std::cout << "profile_csv," << frame_index << ","
            << (profile.zero_copy_input ? "zero_copy" : "rknn_inputs_set") << ","
            << std::fixed << std::setprecision(2) << profile.prepare_ms << ","
            << profile.inputs_set_ms << "," << profile.run_ms << ","
            << profile.outputs_get_ms << "," << profile.decode_ms << ","
            << profile.outputs_release_ms << "," << render_ms << ","
            << total_work_ms << "," << profile.detections << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    PrintUsage(argv[0]);
    return 1;
  }

  const std::string input_video = argv[1];
  const std::string output_video = argv[2];
  const std::string model_path = ResolveModelPath(argc, argv);
  const float score_threshold =
      (argc >= 5) ? std::strtof(argv[4], nullptr) : kDefaultScoreThreshold;
  const float nms_threshold =
      (argc >= 6) ? std::strtof(argv[5], nullptr) : kDefaultNmsThreshold;
  const std::string detections_csv =
      (argc >= 7) ? argv[6] : (output_video + ".detections.csv");
  const std::string roi_jsonl = (argc >= 8) ? argv[7] : (output_video + ".roi.jsonl");
  const std::string alarm_csv = (argc >= 9) ? argv[8] : (output_video + ".alarm_events.csv");
  const bool profile_enabled = EnvFlagEnabled("RK_YOLO_PROFILE");
  const AlarmConfig alarm_config = LoadAlarmConfig();

  YoloRknnDetector detector;
  if (!detector.Load(model_path)) {
    std::cerr << "failed to load model: " << model_path << std::endl;
    return 2;
  }
  const int model_class_count = detector.class_count();

  cv::VideoCapture cap(input_video);
  if (!cap.isOpened()) {
    std::cerr << "failed to open input video: " << input_video << std::endl;
    return 3;
  }

  const int frame_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
  const int frame_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
  double fps = cap.get(cv::CAP_PROP_FPS);
  if (fps <= 1.0) {
    fps = 25.0;
  }

  cv::VideoWriter writer;
  const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
  if (!writer.open(output_video, fourcc, fps, cv::Size(frame_width, frame_height))) {
    std::cerr << "failed to open output video: " << output_video << std::endl;
    return 4;
  }

  std::ofstream csv_file(detections_csv, std::ios::out | std::ios::trunc);
  if (!csv_file.is_open()) {
    std::cerr << "failed to open detections csv: " << detections_csv << std::endl;
    return 5;
  }
  csv_file << "frame_index,class_id,class_name,score,left,top,width,height\n";

  std::ofstream roi_file(roi_jsonl, std::ios::out | std::ios::trunc);
  if (!roi_file.is_open()) {
    std::cerr << "failed to open roi jsonl: " << roi_jsonl << std::endl;
    return 6;
  }

  std::ofstream alarm_file(alarm_csv, std::ios::out | std::ios::trunc);
  if (!alarm_file.is_open()) {
    std::cerr << "failed to open alarm csv: " << alarm_csv << std::endl;
    return 7;
  }
  alarm_file << "frame_index,event,active,detections,max_score\n";

  std::cout << "processing video " << input_video << " -> " << output_video << std::endl;
  std::cout << "model path=" << model_path << std::endl;
  std::cout << "score threshold=" << score_threshold << ", nms threshold=" << nms_threshold << std::endl;
  std::cout << "detections csv=" << detections_csv << std::endl;
  std::cout << "roi jsonl=" << roi_jsonl << std::endl;
  std::cout << "alarm csv=" << alarm_csv << std::endl;
  std::cout << "alarm_overlay=" << (alarm_config.overlay_enabled ? "on" : "off")
            << " hold_frames=" << alarm_config.hold_frames << std::endl;
  std::cout << "gpio_alarm_path="
            << (alarm_config.gpio_value_path.empty() ? "<disabled>" : alarm_config.gpio_value_path)
            << std::endl;
  const bool pipeline_enabled = EnvFlagEnabled("RK_YOLO_PIPELINE");
  const bool staged_pipeline_enabled = pipeline_enabled && EnvFlagEnabled("RK_YOLO_PIPELINE_STAGED");
  const int pipeline_queue_size = ParseEnvInt("RK_YOLO_PIPELINE_QUEUE", 4, 1, 64);
  std::cout << "profile=" << (profile_enabled ? "on" : "off")
            << ", zero_copy_input=" << (detector.zero_copy_input_enabled() ? "on" : "off")
            << ", rga_required=" << (detector.rga_required() ? "on" : "off")
            << ", pipeline=" << (pipeline_enabled ? "on" : "off")
            << ", staged_pipeline=" << (staged_pipeline_enabled ? "on" : "off")
            << ", pipeline_queue=" << pipeline_queue_size
            << std::endl;
  if (profile_enabled) {
    PrintProfileHeader();
  }

  int frame_index = 0;
  int detected_frames = 0;
  double total_ms = 0.0;
  int total_detections = 0;
  AlarmState alarm_state;
  WriteGpioAlarmState(alarm_config, alarm_state, true);

  auto handle_result = [&](ResultPacket result) {
    total_ms += result.infer_ms;

    if (!result.detections.empty()) {
      ++detected_frames;
    }
    total_detections += static_cast<int>(result.detections.size());

    const auto render_start = std::chrono::steady_clock::now();
    DrawDetections(&result.frame, result.detections, model_class_count);
    UpdateAlarmState(result.detections, alarm_config, &alarm_state);
    if (alarm_config.overlay_enabled) {
      DrawAlarmOverlay(&result.frame, alarm_state, result.detections);
    }
    if (alarm_state.active != alarm_state.previous_active) {
      ++alarm_state.event_count;
      std::cout << "alarm_event frame=" << result.frame_index
                << " state=" << (alarm_state.active ? "on" : "off")
                << " detections=" << result.detections.size() << std::endl;
      WriteGpioAlarmState(alarm_config, alarm_state);
    }
    WriteAlarmEvent(&alarm_file, result.frame_index, alarm_state, result.detections);
    WriteDetectionRows(&csv_file, result.frame_index, result.detections, model_class_count);
    roi_file << BuildLegacyRoiJson(result.detections) << "\n";
    writer.write(result.frame);
    const auto render_end = std::chrono::steady_clock::now();
    const double render_ms =
        std::chrono::duration<double, std::milli>(render_end - render_start).count();

    frame_index = result.frame_index;
    if (profile_enabled) {
      PrintProfileRow(result.frame_index, result.profile, render_ms);
    } else {
      std::cout << "frame=" << result.frame_index
                << " detections=" << result.detections.size()
                << " infer_ms=" << std::fixed << std::setprecision(2) << result.infer_ms
                << std::endl;
    }
  };

  if (pipeline_enabled) {
    BoundedQueue<FramePacket> frame_queue(static_cast<std::size_t>(pipeline_queue_size));
    BoundedQueue<ResultPacket> result_queue(static_cast<std::size_t>(pipeline_queue_size));
    BoundedQueue<PreparedPacket> prepared_queue(static_cast<std::size_t>(pipeline_queue_size));

    std::thread capture_thread([&] {
      cv::Mat captured;
      int captured_index = 0;
      while (cap.read(captured)) {
        FramePacket packet;
        packet.frame_index = ++captured_index;
        packet.frame = std::move(captured);
        if (!frame_queue.Push(std::move(packet))) {
          break;
        }
      }
      frame_queue.Close();
    });

    std::thread prepare_thread;
    if (staged_pipeline_enabled) {
      prepare_thread = std::thread([&] {
        FramePacket packet;
        while (frame_queue.Pop(&packet)) {
          PreparedPacket prepared;
          prepared.frame_index = packet.frame_index;
          prepared.frame = std::move(packet.frame);
          if (!detector.PrepareFrame(prepared.frame, &prepared.prepared, &prepared.profile)) {
            continue;
          }
          if (!prepared_queue.Push(std::move(prepared))) {
            break;
          }
        }
        prepared_queue.Close();
      });
    }

    std::thread infer_thread([&] {
      if (staged_pipeline_enabled) {
        PreparedPacket packet;
        while (prepared_queue.Pop(&packet)) {
          ResultPacket result;
          result.frame_index = packet.frame_index;
          result.frame = std::move(packet.frame);
          result.profile = packet.profile;
          const auto infer_start = std::chrono::steady_clock::now();
          result.detections =
              detector.InferPrepared(packet.prepared, score_threshold, nms_threshold, &result.profile);
          const auto infer_end = std::chrono::steady_clock::now();
          result.infer_ms =
              std::chrono::duration<double, std::milli>(infer_end - infer_start).count() +
              result.profile.prepare_ms;
          if (!result_queue.Push(std::move(result))) {
            break;
          }
        }
      } else {
        FramePacket packet;
        while (frame_queue.Pop(&packet)) {
          ResultPacket result;
          result.frame_index = packet.frame_index;
          result.frame = std::move(packet.frame);
          const auto infer_start = std::chrono::steady_clock::now();
          result.detections =
              detector.InferProfiled(result.frame, score_threshold, nms_threshold, &result.profile);
          const auto infer_end = std::chrono::steady_clock::now();
          result.infer_ms =
              std::chrono::duration<double, std::milli>(infer_end - infer_start).count();
          if (!result_queue.Push(std::move(result))) {
            break;
          }
        }
      }
      result_queue.Close();
    });

    ResultPacket result;
    while (result_queue.Pop(&result)) {
      handle_result(std::move(result));
    }

    frame_queue.Close();
    prepared_queue.Close();
    result_queue.Close();
    if (capture_thread.joinable()) {
      capture_thread.join();
    }
    if (prepare_thread.joinable()) {
      prepare_thread.join();
    }
    if (infer_thread.joinable()) {
      infer_thread.join();
    }
  } else {
    cv::Mat frame;
    while (cap.read(frame)) {
      ResultPacket result;
      result.frame_index = frame_index + 1;
      result.frame = frame;
      const auto infer_start = std::chrono::steady_clock::now();
      result.detections =
          profile_enabled ? detector.InferProfiled(result.frame, score_threshold, nms_threshold,
                                                   &result.profile)
                          : detector.Infer(result.frame, score_threshold, nms_threshold);
      const auto infer_end = std::chrono::steady_clock::now();
      result.infer_ms =
          std::chrono::duration<double, std::milli>(infer_end - infer_start).count();
      handle_result(std::move(result));
    }
  }

  const double avg_ms = (frame_index > 0) ? (total_ms / frame_index) : 0.0;
  std::cout << "done. frames=" << frame_index << ", frames_with_detections=" << detected_frames
            << ", total_detections=" << total_detections
            << ", alarm_events=" << alarm_state.event_count
            << ", avg_infer_ms=" << std::fixed << std::setprecision(2) << avg_ms << std::endl;
  std::cout << "results saved to " << output_video << ", " << detections_csv
            << ", " << roi_jsonl << " and " << alarm_csv << std::endl;

  return 0;
}
