#include "yolo_legacy_roi.h"
#include "yolo_rknn.h"

#include <opencv2/opencv.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
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

bool EnvFlagEnabled(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && value[0] != '\0' && value[0] != '0';
}

void PrintUsage(const char* argv0) {
  std::cout << "Usage: " << argv0
            << " <input_video> <output_video> [model_path=<auto>] [score_thresh=0.30] "
               "[nms_thresh=0.45] [detections_csv] [roi_jsonl]"
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
  const bool profile_enabled = EnvFlagEnabled("RK_YOLO_PROFILE");

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

  std::cout << "processing video " << input_video << " -> " << output_video << std::endl;
  std::cout << "model path=" << model_path << std::endl;
  std::cout << "score threshold=" << score_threshold << ", nms threshold=" << nms_threshold << std::endl;
  std::cout << "detections csv=" << detections_csv << std::endl;
  std::cout << "roi jsonl=" << roi_jsonl << std::endl;
  std::cout << "profile=" << (profile_enabled ? "on" : "off")
            << ", zero_copy_input=" << (detector.zero_copy_input_enabled() ? "on" : "off")
            << std::endl;
  if (profile_enabled) {
    PrintProfileHeader();
  }

  int frame_index = 0;
  int detected_frames = 0;
  double total_ms = 0.0;
  int total_detections = 0;

  cv::Mat frame;
  while (cap.read(frame)) {
    const auto start = std::chrono::steady_clock::now();
    InferProfile profile;
    std::vector<Detection> detections =
        profile_enabled ? detector.InferProfiled(frame, score_threshold, nms_threshold, &profile)
                        : detector.Infer(frame, score_threshold, nms_threshold);
    const auto end = std::chrono::steady_clock::now();
    const double infer_ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    total_ms += infer_ms;

    if (!detections.empty()) {
      ++detected_frames;
    }
    total_detections += static_cast<int>(detections.size());

    const auto render_start = std::chrono::steady_clock::now();
    DrawDetections(&frame, detections, model_class_count);
    WriteDetectionRows(&csv_file, frame_index + 1, detections, model_class_count);
    roi_file << BuildLegacyRoiJson(detections) << "\n";
    writer.write(frame);
    const auto render_end = std::chrono::steady_clock::now();
    const double render_ms =
        std::chrono::duration<double, std::milli>(render_end - render_start).count();

    ++frame_index;
    if (profile_enabled) {
      PrintProfileRow(frame_index, profile, render_ms);
    } else {
      std::cout << "frame=" << frame_index << " detections=" << detections.size()
                << " infer_ms=" << std::fixed << std::setprecision(2) << infer_ms << std::endl;
    }
  }

  const double avg_ms = (frame_index > 0) ? (total_ms / frame_index) : 0.0;
  std::cout << "done. frames=" << frame_index << ", frames_with_detections=" << detected_frames
            << ", total_detections=" << total_detections
            << ", avg_infer_ms=" << std::fixed << std::setprecision(2) << avg_ms << std::endl;
  std::cout << "results saved to " << output_video << ", " << detections_csv
            << " and " << roi_jsonl << std::endl;

  return 0;
}
