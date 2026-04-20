#include "yolo_legacy_roi.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

std::vector<float> BuildLegacyYoloProbBuffer(const std::vector<Detection>& detections,
                                             std::size_t max_detections) {
  std::vector<float> prob(max_detections * kLegacyYoloStride + 1, 0.0f);
  const std::size_t count = std::min(detections.size(), max_detections);
  prob[0] = static_cast<float>(count);

  for (std::size_t i = 0; i < count; ++i) {
    const Detection& det = detections[i];
    const std::size_t offset = 1 + i * kLegacyYoloStride;
    prob[offset + 0] = static_cast<float>(det.box.x);
    prob[offset + 1] = static_cast<float>(det.box.y);
    prob[offset + 2] = static_cast<float>(det.box.x + det.box.width);
    prob[offset + 3] = static_cast<float>(det.box.y + det.box.height);
    prob[offset + 4] = det.score;
    prob[offset + 5] = static_cast<float>(det.class_id);
  }

  return prob;
}

std::string BuildLegacyRoiJson(const std::vector<Detection>& detections) {
  std::ostringstream oss;
  oss << "{\"pos\":[";

  for (std::size_t i = 0; i < detections.size(); ++i) {
    const Detection& det = detections[i];
    if (i > 0) {
      oss << ",";
    }

    oss << "{\"prob\":" << std::fixed << std::setprecision(4) << det.score << ","
        << "\"id\":" << det.class_id << ","
        << "\"x\":" << det.box.x << ","
        << "\"y\":" << det.box.y << ","
        << "\"w\":" << det.box.width << ","
        << "\"h\":" << det.box.height << "}";
  }

  oss << "]}";
  return oss.str();
}
