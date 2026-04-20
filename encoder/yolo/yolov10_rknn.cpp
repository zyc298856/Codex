#include "yolov10_rknn.h"

#include "../../rk_yolo_video/include/yolo_encoder_adapter.h"
#include "../cJSON.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

struct YoloRknnHandle {
  YoloEncoderAdapter adapter;
  int width = 0;
  int height = 0;
  float nms_threshold = 0.45f;
  std::string model_path;
  bool warned_draw_unsupported = false;
};

bool FileExists(const std::string& path) {
  std::ifstream file(path.c_str(), std::ios::binary);
  return file.good();
}

std::string ResolveModelPath() {
  const char* env_path = std::getenv("YOLOV10_RKNN_MODEL");
  if (env_path != nullptr && env_path[0] != '\0') {
    return env_path;
  }

  const char* candidates[] = {
      "yolov10n.rknn",
      "../yolov10n.rknn",
      "../../yolov10n.rknn",
      "../../../yolov10n.rknn",
      "../../rk_yolo_video/yolov10n.rknn",
      "../../../rk_yolo_video/yolov10n.rknn",
  };

  for (const char* candidate : candidates) {
    if (FileExists(candidate)) {
      return candidate;
    }
  }

  return "yolov10n.rknn";
}

void OutputRoiDetections(AVElement_t* p_video, const std::vector<Detection>& detections) {
  cJSON* root = cJSON_CreateObject();
  if (root == nullptr) {
    return;
  }

  cJSON* objects = cJSON_CreateArray();
  if (objects == nullptr) {
    cJSON_Delete(root);
    return;
  }

  for (std::size_t i = 0; i < detections.size(); ++i) {
    const Detection& det = detections[i];
    cJSON* item = cJSON_CreateObject();
    if (item == nullptr) {
      continue;
    }

    cJSON_AddNumberToObject(item, "prob", det.score);
    cJSON_AddNumberToObject(item, "id", det.class_id);
    cJSON_AddNumberToObject(item, "x", det.box.x);
    cJSON_AddNumberToObject(item, "y", det.box.y);
    cJSON_AddNumberToObject(item, "w", det.box.width);
    cJSON_AddNumberToObject(item, "h", det.box.height);
    cJSON_AddItemToArray(objects, item);
  }

  cJSON_AddItemToObject(root, "pos", objects);

  char* text = cJSON_PrintUnformatted(root);
  if (text != nullptr) {
    block_t block_out;
    block_out.i_flags = 0;
    block_out.i_buffer = static_cast<int>(std::strlen(text)) + 1;
    block_out.p_buffer = reinterpret_cast<uint8_t*>(text);
    block_out.i_length = 0;
    block_out.i_pts = 0;
    block_out.i_dts = 0;
    block_out.i_extra = 0;
    BlockBufferWrite(&p_video->video_object, &block_out);
    free(text);
  }

  cJSON_Delete(root);
}

}  // namespace

void* open_yolo_rknn(int i_width, int i_height) {
  std::unique_ptr<YoloRknnHandle> handle(new YoloRknnHandle());
  handle->width = i_width;
  handle->height = i_height;
  handle->model_path = ResolveModelPath();

  if (!handle->adapter.LoadModel(handle->model_path)) {
    std::fprintf(stderr, "open_yolo_rknn: failed to load model %s\n", handle->model_path.c_str());
    return nullptr;
  }

  std::fprintf(stdout, "open_yolo_rknn: loaded model %s for %dx%d\n", handle->model_path.c_str(),
               i_width, i_height);
  return handle.release();
}

int yolo_infer_rknn(AVElement_t* p_video, void* param, unsigned char* src, int stride_y,
                    int b_object_show) {
  YoloRknnHandle* handle = reinterpret_cast<YoloRknnHandle*>(param);
  if (handle == nullptr || src == nullptr || p_video == nullptr) {
    return 0;
  }

  LegacyYoloOutput output;
  if (!handle->adapter.InferI420(src, handle->width, handle->height, stride_y, p_video->f_prob,
                                 handle->nms_threshold, &output)) {
    return 0;
  }

  OutputRoiDetections(p_video, output.detections);

  if (b_object_show && !handle->warned_draw_unsupported) {
    std::fprintf(stdout,
                 "yolo_infer_rknn: overlay drawing is not wired in this parallel RK path yet\n");
    handle->warned_draw_unsupported = true;
  }

  return static_cast<int>(output.detections.size());
}

void close_yolo_rknn(void* param) {
  YoloRknnHandle* handle = reinterpret_cast<YoloRknnHandle*>(param);
  if (handle == nullptr) {
    return;
  }

  handle->adapter.Release();
  delete handle;
}
