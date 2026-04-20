#pragma once

#include "yolo_rknn.h"

#include <cstddef>
#include <string>
#include <vector>

constexpr std::size_t kLegacyYoloMaxDetections = 1000;
constexpr std::size_t kLegacyYoloStride = 6;

std::vector<float> BuildLegacyYoloProbBuffer(
    const std::vector<Detection>& detections,
    std::size_t max_detections = kLegacyYoloMaxDetections);

std::string BuildLegacyRoiJson(const std::vector<Detection>& detections);
