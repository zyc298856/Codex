#include "yolo_rknn.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#ifdef MPP_DMA_RTSP_DEMO
#ifdef HAVE_GST_ALLOCATORS
#include <gst/allocators/gstdmabuf.h>
#endif
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#endif

#ifdef HAVE_MPP
#include <mpp_buffer.h>
#include <mpp_err.h>
#include <mpp_frame.h>
#include <mpp_packet.h>
#include <rk_mpi.h>
#endif

#ifdef HAVE_RGA
#include <linux/videodev2.h>
#include <poll.h>
#include <rga/im2d.h>
#include <rga/rga.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

double ElapsedMs(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

#ifdef MPP_DMA_RTSP_DEMO
std::atomic_bool g_stop_requested{false};

void HandleSignal(int) {
  g_stop_requested.store(true);
}

bool EnvFlag(const char* name, bool default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return default_value;
  }
  std::string normalized(value);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return normalized != "0" && normalized != "false" && normalized != "off" &&
         normalized != "no";
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

std::string EnvString(const char* name, const std::string& default_value) {
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return default_value;
  }
  return value;
}

int EnvIntClamped(const char* name, int default_value, int min_value,
                  int max_value) {
  return std::clamp(EnvInt(name, default_value), min_value, max_value);
}

float EnvFloatClamped(const char* name, float default_value, float min_value,
                      float max_value) {
  return std::clamp(EnvFloat(name, default_value), min_value, max_value);
}

struct CameraTuneConfig {
  bool enabled = true;
  std::string match_name = "HBS Camera";
  int zoom_absolute = 20;
  bool focus_auto = true;
  int focus_absolute = 260;
  int settle_ms = 300;
  bool focus_startup_lock = true;
  int focus_lock_ms = 1200;
  bool refocus_after_zoom = true;
  int refocus_zoom_delta = 8;
  int refocus_cooldown_frames = 80;
  int refocus_settle_ms = 800;
};

struct AutoZoomConfig {
  bool enabled = false;
  std::string match_name = "HBS Camera";
  int min_zoom = 0;
  int max_zoom = 60;
  int step = 4;
  int cooldown_frames = 12;
  int lost_frames_to_zoom_out = 45;
  float target_min_ratio = 0.055f;
  float target_max_ratio = 0.24f;
};

struct AutoZoomState {
  int current_zoom = 0;
  std::uint64_t last_adjust_frame = 0;
  std::uint64_t last_refocus_frame = 0;
  int lost_frames = 0;
  bool initialized = false;
  int zoom_at_last_refocus = 0;
  bool refocus_initialized = false;
};

CameraTuneConfig LoadCameraTuneConfig() {
  CameraTuneConfig config;
  config.enabled = EnvFlag("RK_YOLO_CAMERA_TUNE", config.enabled);
  config.match_name = EnvString("RK_YOLO_CAMERA_MATCH", config.match_name);
  config.zoom_absolute =
      EnvIntClamped("RK_YOLO_CAMERA_ZOOM", config.zoom_absolute, 0, 99);
  config.focus_auto =
      EnvFlag("RK_YOLO_CAMERA_FOCUS_AUTO", config.focus_auto);
  config.focus_absolute =
      EnvIntClamped("RK_YOLO_CAMERA_FOCUS", config.focus_absolute, 0, 550);
  config.settle_ms =
      EnvIntClamped("RK_YOLO_CAMERA_SETTLE_MS", config.settle_ms, 0, 5000);
  config.focus_startup_lock = EnvFlag("RK_YOLO_CAMERA_FOCUS_STARTUP_LOCK",
                                      config.focus_startup_lock);
  config.focus_lock_ms =
      EnvIntClamped("RK_YOLO_CAMERA_FOCUS_LOCK_MS", config.focus_lock_ms, 0, 5000);
  config.refocus_after_zoom = EnvFlag("RK_YOLO_CAMERA_REFOCUS_AFTER_ZOOM",
                                      config.refocus_after_zoom);
  config.refocus_zoom_delta = EnvIntClamped(
      "RK_YOLO_CAMERA_REFOCUS_ZOOM_DELTA", config.refocus_zoom_delta, 1, 99);
  config.refocus_cooldown_frames = EnvIntClamped(
      "RK_YOLO_CAMERA_REFOCUS_COOLDOWN", config.refocus_cooldown_frames, 1, 1800);
  config.refocus_settle_ms = EnvIntClamped(
      "RK_YOLO_CAMERA_REFOCUS_SETTLE_MS", config.refocus_settle_ms, 0, 5000);
  return config;
}

AutoZoomConfig LoadAutoZoomConfig() {
  AutoZoomConfig config;
  config.enabled = EnvFlag("RK_YOLO_AUTO_ZOOM", config.enabled);
  config.match_name = EnvString("RK_YOLO_AUTO_ZOOM_MATCH", config.match_name);
  config.min_zoom =
      EnvIntClamped("RK_YOLO_AUTO_ZOOM_MIN", config.min_zoom, 0, 99);
  config.max_zoom =
      EnvIntClamped("RK_YOLO_AUTO_ZOOM_MAX", config.max_zoom, 0, 99);
  if (config.max_zoom < config.min_zoom) {
    config.max_zoom = config.min_zoom;
  }
  config.step = EnvIntClamped("RK_YOLO_AUTO_ZOOM_STEP", config.step, 1, 30);
  config.cooldown_frames =
      EnvIntClamped("RK_YOLO_AUTO_ZOOM_COOLDOWN", config.cooldown_frames, 1, 600);
  config.lost_frames_to_zoom_out = EnvIntClamped(
      "RK_YOLO_AUTO_ZOOM_LOST_FRAMES", config.lost_frames_to_zoom_out, 1, 1800);
  config.target_min_ratio = EnvFloatClamped(
      "RK_YOLO_AUTO_ZOOM_MIN_RATIO", config.target_min_ratio, 0.005f, 0.95f);
  config.target_max_ratio = EnvFloatClamped(
      "RK_YOLO_AUTO_ZOOM_MAX_RATIO", config.target_max_ratio, 0.01f, 0.99f);
  if (config.target_max_ratio < config.target_min_ratio) {
    config.target_max_ratio = config.target_min_ratio;
  }
  return config;
}

std::string DeviceBasename(const std::string& device) {
  const std::size_t pos = device.find_last_of('/');
  if (pos == std::string::npos) {
    return device;
  }
  return device.substr(pos + 1);
}

std::string ReadCameraName(const std::string& device) {
  const std::string base = DeviceBasename(device);
  if (base.empty()) {
    return "";
  }
  std::ifstream input("/sys/class/video4linux/" + base + "/name");
  std::string name;
  std::getline(input, name);
  return name;
}

bool CameraNameAllowed(const std::string& device, const std::string& match_name,
                       std::string* display_name) {
  const std::string name = ReadCameraName(device);
  if (display_name != nullptr) {
    *display_name = name.empty() ? "unknown" : name;
  }
  return match_name.empty() || name.find(match_name) != std::string::npos;
}

bool ApplyFocusAuto(const std::string& device, bool enabled) {
  std::ostringstream command;
  command << "v4l2-ctl -d " << device << " -c focus_auto="
          << (enabled ? 1 : 0) << " >/dev/null 2>&1";
  return std::system(command.str().c_str()) == 0;
}

bool ReadFocusAbsolute(const std::string& device, int fallback_focus,
                       int* focus_absolute) {
  if (focus_absolute == nullptr) {
    return false;
  }
  std::ostringstream command;
  command << "v4l2-ctl -d " << device << " -C focus_absolute 2>/dev/null";
  FILE* pipe = popen(command.str().c_str(), "r");
  if (pipe == nullptr) {
    *focus_absolute = fallback_focus;
    return false;
  }
  char buffer[256] = {};
  std::string output;
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }
  const int rc = pclose(pipe);
  const std::size_t colon = output.find(':');
  if (rc != 0 || colon == std::string::npos) {
    *focus_absolute = fallback_focus;
    return false;
  }
  try {
    *focus_absolute = std::stoi(output.substr(colon + 1));
    return true;
  } catch (...) {
    *focus_absolute = fallback_focus;
    return false;
  }
}

bool ApplyFocusManual(const std::string& device, int focus_absolute) {
  std::ostringstream command;
  command << "v4l2-ctl -d " << device << " -c focus_auto=0,focus_absolute="
          << focus_absolute << " >/dev/null 2>&1";
  return std::system(command.str().c_str()) == 0;
}

bool TriggerAutofocusAndLock(const std::string& device,
                             const CameraTuneConfig& config, int settle_ms,
                             const std::string& reason, int* locked_focus) {
  if (!ApplyFocusAuto(device, true)) {
    std::cerr << "focus_lock failed to enable AF reason=" << reason << std::endl;
    return false;
  }
  if (settle_ms > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));
  }

  int focus = config.focus_absolute;
  const bool read_ok = ReadFocusAbsolute(device, config.focus_absolute, &focus);
  if (!ApplyFocusManual(device, focus)) {
    std::cerr << "focus_lock failed to lock focus reason=" << reason
              << " requested_focus=" << focus << std::endl;
    return false;
  }
  if (locked_focus != nullptr) {
    *locked_focus = focus;
  }
  std::cout << "focus_lock reason=" << reason << " focus=" << focus
            << " read_current=" << (read_ok ? "yes" : "fallback")
            << " settle_ms=" << settle_ms << std::endl;
  return true;
}

bool ApplyCameraTuning(const std::string& device, const CameraTuneConfig& config,
                       std::string* status) {
  if (!config.enabled) {
    if (status != nullptr) {
      *status = "camera_tune=off";
    }
    return false;
  }

  std::string display_name;
  if (!CameraNameAllowed(device, config.match_name, &display_name)) {
    if (status != nullptr) {
      *status = "camera_tune=skip model=\"" + display_name + "\" expected~=\"" +
                config.match_name + "\"";
    }
    return false;
  }

  std::ostringstream command;
  command << "v4l2-ctl -d " << device << " -c focus_auto="
          << (config.focus_auto ? 1 : 0);
  if (!config.focus_auto) {
    command << ",focus_absolute=" << config.focus_absolute;
  }
  command << ",zoom_absolute=" << config.zoom_absolute << " >/dev/null 2>&1";

  const int rc = std::system(command.str().c_str());
  if (rc != 0) {
    if (status != nullptr) {
      *status = "camera_tune=failed model=\"" + display_name +
                "\" rc=" + std::to_string(rc);
    }
    return false;
  }

  if (config.settle_ms > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(config.settle_ms));
  }

  int locked_focus = config.focus_absolute;
  bool startup_locked = false;
  if (config.focus_auto && config.focus_startup_lock) {
    startup_locked = TriggerAutofocusAndLock(
        device, config, config.focus_lock_ms, "startup", &locked_focus);
  }

  if (status != nullptr) {
    std::ostringstream applied;
    applied << "camera_tune=applied model=\"" << display_name << "\" zoom="
            << config.zoom_absolute << " focus_auto="
            << (config.focus_auto ? 1 : 0);
    if (!config.focus_auto || startup_locked) {
      applied << " focus=" << (startup_locked ? locked_focus : config.focus_absolute);
    }
    applied << " startup_lock=" << (startup_locked ? "on" : "off")
            << " settle_ms=" << config.settle_ms;
    *status = applied.str();
  }
  return true;
}

bool ApplyZoomAbsolute(const std::string& device, int zoom_absolute) {
  std::ostringstream command;
  command << "v4l2-ctl -d " << device << " -c zoom_absolute=" << zoom_absolute
          << " >/dev/null 2>&1";
  return std::system(command.str().c_str()) == 0;
}

float LargestBoxRatio(const std::vector<Detection>& detections, int width, int height) {
  if (width <= 0 || height <= 0) {
    return 0.0f;
  }
  float largest_ratio = 0.0f;
  for (const Detection& det : detections) {
    if (det.box.empty()) {
      continue;
    }
    const float width_ratio = static_cast<float>(det.box.width) / width;
    const float height_ratio = static_cast<float>(det.box.height) / height;
    largest_ratio = std::max(largest_ratio, std::max(width_ratio, height_ratio));
  }
  return largest_ratio;
}

bool AutoZoomCameraAllowed(const std::string& device, const AutoZoomConfig& config,
                           std::string* status) {
  if (!config.enabled) {
    if (status != nullptr) {
      *status = "auto_zoom=off";
    }
    return false;
  }

  std::string display_name;
  if (!CameraNameAllowed(device, config.match_name, &display_name)) {
    if (status != nullptr) {
      *status = "auto_zoom=skip model=\"" + display_name + "\" expected~=\"" +
                config.match_name + "\"";
    }
    return false;
  }

  if (status != nullptr) {
    std::ostringstream oss;
    oss << "auto_zoom=on model=\"" << display_name << "\" range=["
        << config.min_zoom << "," << config.max_zoom << "] step=" << config.step
        << " cooldown_frames=" << config.cooldown_frames
        << " lost_frames=" << config.lost_frames_to_zoom_out
        << " target_ratio=[" << config.target_min_ratio << ","
        << config.target_max_ratio << "]";
    *status = oss.str();
  }
  return true;
}

void UpdateAutoZoom(const std::string& device, const AutoZoomConfig& config,
                    const CameraTuneConfig& camera_config,
                    AutoZoomState* state, std::uint64_t frame_index, int width,
                    int height, const std::vector<Detection>& detections) {
  if (!config.enabled || state == nullptr || device.empty()) {
    return;
  }

  if (!state->initialized) {
    state->current_zoom = std::clamp(state->current_zoom, config.min_zoom,
                                    config.max_zoom);
    state->initialized = true;
    state->zoom_at_last_refocus = state->current_zoom;
    state->refocus_initialized = true;
  }

  const bool in_cooldown =
      frame_index < state->last_adjust_frame +
                        static_cast<std::uint64_t>(config.cooldown_frames);
  int desired_zoom = state->current_zoom;
  std::string reason;
  const float largest_ratio = LargestBoxRatio(detections, width, height);

  if (detections.empty()) {
    ++state->lost_frames;
    if (state->lost_frames >= config.lost_frames_to_zoom_out && !in_cooldown) {
      desired_zoom = std::max(config.min_zoom, state->current_zoom - config.step);
      reason = "target_lost";
      state->lost_frames = 0;
    }
  } else {
    state->lost_frames = 0;
    if (!in_cooldown && largest_ratio < config.target_min_ratio) {
      desired_zoom = std::min(config.max_zoom, state->current_zoom + config.step);
      reason = "target_small";
    } else if (!in_cooldown && largest_ratio > config.target_max_ratio) {
      desired_zoom = std::max(config.min_zoom, state->current_zoom - config.step);
      reason = "target_large";
    }
  }

  if (reason.empty() || desired_zoom == state->current_zoom) {
    return;
  }
  if (!ApplyZoomAbsolute(device, desired_zoom)) {
    std::cerr << "auto_zoom failed frame=" << frame_index
              << " requested_zoom=" << desired_zoom << std::endl;
    return;
  }
  state->current_zoom = desired_zoom;
  state->last_adjust_frame = frame_index;
  std::cout << "auto_zoom frame=" << frame_index << " zoom="
            << state->current_zoom << " reason=" << reason
            << " box_ratio=" << std::fixed << std::setprecision(3)
            << largest_ratio << std::endl;

  if (camera_config.refocus_after_zoom) {
    const bool focus_cooldown =
        frame_index < state->last_refocus_frame +
                          static_cast<std::uint64_t>(
                              camera_config.refocus_cooldown_frames);
    const int zoom_delta = std::abs(state->current_zoom - state->zoom_at_last_refocus);
    if (!focus_cooldown && zoom_delta >= camera_config.refocus_zoom_delta) {
      int locked_focus = camera_config.focus_absolute;
      if (TriggerAutofocusAndLock(device, camera_config,
                                  camera_config.refocus_settle_ms,
                                  "zoom_change", &locked_focus)) {
        state->last_refocus_frame = frame_index;
        state->zoom_at_last_refocus = state->current_zoom;
      }
    }
  }
}

struct dma_heap_allocation_data {
  std::uint64_t len;
  std::uint32_t fd;
  std::uint32_t fd_flags;
  std::uint64_t heap_flags;
};

#ifndef DMA_HEAP_IOC_MAGIC
#define DMA_HEAP_IOC_MAGIC 'H'
#endif

#ifndef DMA_HEAP_IOCTL_ALLOC
#define DMA_HEAP_IOCTL_ALLOC \
  _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)
#endif
#endif

#if defined(HAVE_RGA) && defined(HAVE_MPP)

struct V4l2Buffer {
  void* start = nullptr;
  std::size_t length = 0;
};

struct DecodedFrame {
  MppFrame frame = nullptr;
  int width = 0;
  int height = 0;
  int hor_stride = 0;
  int ver_stride = 0;
  int rga_format = 0;
  int dma_fd = -1;
};

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

int Xioctl(int fd, unsigned long request, void* arg) {
  int ret = 0;
  do {
    ret = ioctl(fd, request, arg);
  } while (ret == -1 && errno == EINTR);
  return ret;
}

unsigned int FourccForCodec(const std::string& codec) {
  if (codec == "mjpg" || codec == "mjpeg" || codec == "MJPG" || codec == "MJPEG") {
    return V4L2_PIX_FMT_MJPEG;
  }
  return V4L2_PIX_FMT_H264;
}

MppCodingType MppCodingForCodec(const std::string& codec) {
  if (codec == "mjpg" || codec == "mjpeg" || codec == "MJPG" || codec == "MJPEG") {
    return MPP_VIDEO_CodingMJPEG;
  }
  return MPP_VIDEO_CodingAVC;
}

std::string FourccToString(unsigned int fourcc) {
  std::string out(4, ' ');
  out[0] = static_cast<char>(fourcc & 0xff);
  out[1] = static_cast<char>((fourcc >> 8) & 0xff);
  out[2] = static_cast<char>((fourcc >> 16) & 0xff);
  out[3] = static_cast<char>((fourcc >> 24) & 0xff);
  return out;
}

bool QueueBuffer(int fd, int index) {
  v4l2_buffer buf = {};
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = static_cast<unsigned int>(index);
  if (Xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
    std::cerr << "VIDIOC_QBUF failed: " << std::strerror(errno) << std::endl;
    return false;
  }
  return true;
}

void CleanupCamera(int fd, std::vector<V4l2Buffer>* buffers, bool streaming) {
  if (fd >= 0 && streaming) {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    Xioctl(fd, VIDIOC_STREAMOFF, &type);
  }
  if (buffers != nullptr) {
    for (V4l2Buffer& buffer : *buffers) {
      if (buffer.start != nullptr && buffer.length > 0) {
        munmap(buffer.start, buffer.length);
        buffer.start = nullptr;
        buffer.length = 0;
      }
    }
  }
  if (fd >= 0) {
    close(fd);
  }
}

bool OpenCompressedCamera(const std::string& device, int requested_width,
                          int requested_height, int requested_fps,
                          unsigned int pixfmt, int* fd, int* actual_width,
                          int* actual_height, std::vector<V4l2Buffer>* buffers) {
  if (fd == nullptr || actual_width == nullptr || actual_height == nullptr ||
      buffers == nullptr) {
    return false;
  }

  *fd = open(device.c_str(), O_RDWR | O_NONBLOCK, 0);
  if (*fd < 0) {
    std::cerr << "open camera failed: " << device << " " << std::strerror(errno)
              << std::endl;
    return false;
  }

  v4l2_capability cap = {};
  if (Xioctl(*fd, VIDIOC_QUERYCAP, &cap) == -1) {
    std::cerr << "VIDIOC_QUERYCAP failed: " << std::strerror(errno) << std::endl;
    CleanupCamera(*fd, buffers, false);
    *fd = -1;
    return false;
  }
  if ((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0 ||
      (cap.capabilities & V4L2_CAP_STREAMING) == 0) {
    std::cerr << "camera must support V4L2 capture and streaming" << std::endl;
    CleanupCamera(*fd, buffers, false);
    *fd = -1;
    return false;
  }

  v4l2_format fmt = {};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = static_cast<unsigned int>(requested_width);
  fmt.fmt.pix.height = static_cast<unsigned int>(requested_height);
  fmt.fmt.pix.pixelformat = pixfmt;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;
  if (Xioctl(*fd, VIDIOC_S_FMT, &fmt) == -1) {
    std::cerr << "VIDIOC_S_FMT " << FourccToString(pixfmt)
              << " failed: " << std::strerror(errno) << std::endl;
    CleanupCamera(*fd, buffers, false);
    *fd = -1;
    return false;
  }
  if (fmt.fmt.pix.pixelformat != pixfmt) {
    std::cerr << "camera did not accept requested compressed format. got="
              << FourccToString(fmt.fmt.pix.pixelformat)
              << " expected=" << FourccToString(pixfmt) << std::endl;
    CleanupCamera(*fd, buffers, false);
    *fd = -1;
    return false;
  }

  *actual_width = static_cast<int>(fmt.fmt.pix.width);
  *actual_height = static_cast<int>(fmt.fmt.pix.height);

  if (requested_fps > 0) {
    v4l2_streamparm parm = {};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator =
        static_cast<unsigned int>(requested_fps);
    if (Xioctl(*fd, VIDIOC_S_PARM, &parm) == -1) {
      std::cerr << "VIDIOC_S_PARM warning: " << std::strerror(errno) << std::endl;
    }
  }

  v4l2_requestbuffers req = {};
  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_MMAP;
  if (Xioctl(*fd, VIDIOC_REQBUFS, &req) == -1 || req.count < 2) {
    std::cerr << "VIDIOC_REQBUFS MMAP failed: " << std::strerror(errno) << std::endl;
    CleanupCamera(*fd, buffers, false);
    *fd = -1;
    return false;
  }

  buffers->assign(req.count, V4l2Buffer{});
  for (unsigned int i = 0; i < req.count; ++i) {
    v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = i;
    if (Xioctl(*fd, VIDIOC_QUERYBUF, &buf) == -1) {
      std::cerr << "VIDIOC_QUERYBUF failed: " << std::strerror(errno) << std::endl;
      CleanupCamera(*fd, buffers, false);
      *fd = -1;
      return false;
    }

    (*buffers)[i].length = buf.length;
    (*buffers)[i].start =
        mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, buf.m.offset);
    if ((*buffers)[i].start == MAP_FAILED) {
      std::cerr << "mmap camera buffer failed: " << std::strerror(errno) << std::endl;
      (*buffers)[i].start = nullptr;
      CleanupCamera(*fd, buffers, false);
      *fd = -1;
      return false;
    }

    if (!QueueBuffer(*fd, static_cast<int>(i))) {
      CleanupCamera(*fd, buffers, false);
      *fd = -1;
      return false;
    }
  }

  v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (Xioctl(*fd, VIDIOC_STREAMON, &type) == -1) {
    std::cerr << "VIDIOC_STREAMON failed: " << std::strerror(errno) << std::endl;
    CleanupCamera(*fd, buffers, false);
    *fd = -1;
    return false;
  }

  std::cout << "camera opened: " << device << " " << *actual_width << "x"
            << *actual_height << " " << FourccToString(pixfmt)
            << " buffers=" << buffers->size() << std::endl;
  return true;
}

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

#ifdef MPP_DMA_RTSP_DEMO
class DmaHeapBuffer {
 public:
  DmaHeapBuffer() = default;
  DmaHeapBuffer(const DmaHeapBuffer&) = delete;
  DmaHeapBuffer& operator=(const DmaHeapBuffer&) = delete;
  DmaHeapBuffer(DmaHeapBuffer&& other) noexcept { MoveFrom(&other); }
  DmaHeapBuffer& operator=(DmaHeapBuffer&& other) noexcept {
    if (this != &other) {
      Reset();
      MoveFrom(&other);
    }
    return *this;
  }

  ~DmaHeapBuffer() { Reset(); }

  bool Allocate(std::size_t bytes) {
    Reset();
    static const char* kHeapCandidates[] = {
        "/dev/dma_heap/cma",
        "/dev/dma_heap/cma-uncached",
        "/dev/dma_heap/system",
    };
    int last_error = 0;
    std::string last_heap;

    for (const char* heap_path : kHeapCandidates) {
      const int heap_fd = open(heap_path, O_RDONLY | O_CLOEXEC);
      if (heap_fd < 0) {
        continue;
      }

      dma_heap_allocation_data data = {};
      data.len = bytes;
      data.fd_flags = O_RDWR | O_CLOEXEC;
      data.heap_flags = 0;
      if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &data) == 0 && data.fd >= 0) {
        close(heap_fd);
        fd_ = static_cast<int>(data.fd);
        size_ = bytes;
        heap_name_ = heap_path;
        return true;
      }

      const int saved_errno = errno;
      close(heap_fd);
      last_error = saved_errno;
      last_heap = heap_path;
    }
    if (last_error != 0) {
      std::cerr << "DMA heap allocation failed; last heap=" << last_heap << ": "
                << std::strerror(last_error) << std::endl;
    }
    return false;
  }

  void Reset() {
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
    size_ = 0;
    heap_name_.clear();
  }

  bool valid() const { return fd_ >= 0 && size_ > 0; }
  int fd() const { return fd_; }
  std::size_t size() const { return size_; }
  const std::string& heap_name() const { return heap_name_; }

 private:
  void MoveFrom(DmaHeapBuffer* other) {
    fd_ = other->fd_;
    size_ = other->size_;
    heap_name_ = std::move(other->heap_name_);
    other->fd_ = -1;
    other->size_ = 0;
  }

  int fd_ = -1;
  std::size_t size_ = 0;
  std::string heap_name_;
};

std::size_t Nv12BufferSize(int width, int height) {
  return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3 / 2;
}

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

int ClampInt(int value, int low, int high) {
  return std::max(low, std::min(value, high));
}

int AlignDown2(int value) {
  return value & ~1;
}

int AlignUp2(int value) {
  return (value + 1) & ~1;
}

im_rect AlignRectForNv12(const cv::Rect& input, int width, int height) {
  int x = AlignDown2(ClampInt(input.x, 0, std::max(0, width - 2)));
  int y = AlignDown2(ClampInt(input.y, 0, std::max(0, height - 2)));
  int right = AlignUp2(ClampInt(input.x + input.width, x + 2, width));
  int bottom = AlignUp2(ClampInt(input.y + input.height, y + 2, height));
  right = ClampInt(right, x + 2, width);
  bottom = ClampInt(bottom, y + 2, height);
  return im_rect{x, y, right - x, bottom - y};
}

void DrawDmaOverlayNv12(rga_buffer_t dst, int width, int height,
                        const std::vector<Detection>& detections) {
  (void)dst;
  (void)width;
  (void)height;
  (void)detections;
  // Production-candidate DMABUF output intentionally keeps visualization as a
  // clean NV12 hardware path. Drawing boxes on NV12 DMA buffers uses RGA
  // color-fill internally and is not stable across the current librga/kernel
  // combination. Use output_mode=bgr when a boxed RTSP visualization is needed.
}

bool ConvertDmaFrameToNv12Dma(const DecodedFrame& frame,
                              const std::vector<Detection>& detections,
                              DmaHeapBuffer* output) {
  if (output == nullptr || !output->valid() || frame.dma_fd < 0 || frame.width <= 0 ||
      frame.height <= 0 || frame.hor_stride <= 0 || frame.ver_stride <= 0 ||
      frame.rga_format == 0) {
    return false;
  }

  rga_buffer_t src = wrapbuffer_fd(frame.dma_fd, frame.width, frame.height,
                                   frame.rga_format, frame.hor_stride,
                                   frame.ver_stride);
  rga_buffer_t dst =
      wrapbuffer_fd(output->fd(), frame.width, frame.height, RK_FORMAT_YCbCr_420_SP);
  im_rect src_rect{0, 0, frame.width, frame.height};
  im_rect dst_rect{0, 0, frame.width, frame.height};
  IM_STATUS status =
      improcess(src, dst, {}, src_rect, dst_rect, {}, -1, nullptr, nullptr, IM_SYNC);
  if (status != IM_STATUS_SUCCESS) {
    std::cerr << "RGA DMA frame -> NV12 DMA output failed: " << imStrError(status)
              << std::endl;
    return false;
  }

  DrawDmaOverlayNv12(dst, frame.width, frame.height, detections);
  return true;
}

bool ConvertDmaFrameToBgr(const DecodedFrame& frame, cv::Mat* bgr) {
  if (bgr == nullptr || frame.dma_fd < 0 || frame.width <= 0 || frame.height <= 0 ||
      frame.hor_stride <= 0 || frame.ver_stride <= 0 || frame.rga_format == 0) {
    return false;
  }
  bgr->create(frame.height, frame.width, CV_8UC3);
  if (!bgr->isContinuous()) {
    *bgr = bgr->clone();
  }

  rga_buffer_t src = wrapbuffer_fd(frame.dma_fd, frame.width, frame.height,
                                   frame.rga_format, frame.hor_stride,
                                   frame.ver_stride);
  rga_buffer_t dst = wrapbuffer_virtualaddr(bgr->data, frame.width, frame.height,
                                            RK_FORMAT_BGR_888);
  im_rect src_rect{0, 0, frame.width, frame.height};
  im_rect dst_rect{0, 0, frame.width, frame.height};
  IM_STATUS status =
      improcess(src, dst, {}, src_rect, dst_rect, {}, -1, nullptr, nullptr, IM_SYNC);
  if (status != IM_STATUS_SUCCESS) {
    std::cerr << "RGA DMA frame -> BGR visualization failed: " << imStrError(status)
              << std::endl;
    return false;
  }
  return true;
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
        need_data_(false),
        pushed_frames_(0),
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

    const std::string service = std::to_string(port_);
    gst_rtsp_server_set_service(server_, service.c_str());
    const char* encoder_env = std::getenv("RK_YOLO_RTSP_ENCODER");
    std::string encoder = encoder_env == nullptr ? "mpp" : std::string(encoder_env);
    std::transform(encoder.begin(), encoder.end(), encoder.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool use_x264 = encoder == "x264" || encoder == "software";

    std::string encoder_launch;
    if (use_x264) {
      encoder_launch =
          " ! videoconvert ! video/x-raw,format=I420,width=" +
          std::to_string(width_) + ",height=" + std::to_string(height_) +
          ",framerate=" + std::to_string(fps_) + "/1"
          " ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=1200 "
          "byte-stream=true aud=true insert-vui=true bframes=0 sliced-threads=true threads=2 key-int-max=" +
          std::to_string(std::max(1, fps_)) +
          " ! h264parse config-interval=-1 ";
      std::cout << "RTSP BGR encoder=x264 software fallback" << std::endl;
    } else {
      encoder_launch =
          " ! videoconvert ! video/x-raw,format=NV12,width=" +
          std::to_string(width_) + ",height=" + std::to_string(height_) +
          ",framerate=" + std::to_string(fps_) + "/1"
          " ! mpph264enc header-mode=1 gop=" +
          std::to_string(std::max(1, fps_)) +
          " ! h264parse config-interval=1 ";
      std::cout << "RTSP BGR encoder=mpph264enc" << std::endl;
    }

    const std::string launch =
        "( appsrc name=mysrc is-live=true format=time do-timestamp=true "
        "caps=\"video/x-raw,format=BGR,width=" +
        std::to_string(width_) + ",height=" + std::to_string(height_) +
        ",framerate=" + std::to_string(fps_) + "/1\""
        " ! queue leaky=downstream max-size-buffers=2 "
        + encoder_launch +
        " ! video/x-h264,stream-format=byte-stream,alignment=au "
        " ! rtph264pay name=pay0 pt=96 config-interval=1 )";
    gst_rtsp_media_factory_set_launch(factory_, launch.c_str());
    gst_rtsp_media_factory_set_shared(factory_, TRUE);
    gst_rtsp_media_factory_set_suspend_mode(factory_, GST_RTSP_SUSPEND_MODE_NONE);
    gst_rtsp_media_factory_set_eos_shutdown(factory_, FALSE);
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
      need_data_ = false;
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
    GstElement* src = nullptr;
    std::uint64_t output_index = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (appsrc_ == nullptr || frame.empty() || !need_data_) {
        return false;
      }
      src = appsrc_;
      g_object_ref(src);
      output_index = pushed_frames_++;
    }

    const std::size_t bytes = frame.total() * frame.elemSize();
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, bytes, nullptr);
    if (buffer == nullptr) {
      g_object_unref(src);
      return false;
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
      gst_buffer_unref(buffer);
      g_object_unref(src);
      return false;
    }
    std::memcpy(map.data, frame.data, bytes);
    gst_buffer_unmap(buffer, &map);

    GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(output_index, GST_SECOND, fps_);
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, GST_SECOND, fps_);

    const GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(src), buffer);
    g_object_unref(src);
    if (ret != GST_FLOW_OK) {
      std::cerr << "RTSP BGR appsrc push failed: " << gst_flow_get_name(ret)
                << std::endl;
      // A newly connected RTSP media pipeline can briefly return FLUSHING while
      // changing state. Keep the appsrc alive and let the next frame retry; the
      // unprepared callback remains responsible for clearing disconnected media.
      return false;
    }
    return true;
  }

 private:
  static void OnMediaConfigure(GstRTSPMediaFactory*, GstRTSPMedia* media,
                               gpointer user_data) {
    RtspPublisher* self = static_cast<RtspPublisher*>(user_data);
    GstElement* element = gst_rtsp_media_get_element(media);
    GstElement* src = gst_bin_get_by_name_recurse_up(GST_BIN(element), "mysrc");
    {
      std::lock_guard<std::mutex> lock(self->mutex_);
      if (self->appsrc_ != nullptr) {
        g_object_unref(self->appsrc_);
      }
      self->appsrc_ = src;
      self->need_data_ = true;
      self->pushed_frames_ = 0;
    }
    gst_util_set_object_arg(G_OBJECT(src), "format", "time");
    g_object_set(G_OBJECT(src), "stream-type", 0, "is-live", TRUE, "block", FALSE,
                 "max-bytes",
                 static_cast<guint64>(self->width_) * self->height_ * 3 * 2,
                 nullptr);
    g_signal_connect(src, "need-data", G_CALLBACK(&RtspPublisher::OnNeedData), self);
    g_signal_connect(src, "enough-data", G_CALLBACK(&RtspPublisher::OnEnoughData), self);
    g_signal_connect(media, "unprepared", G_CALLBACK(&RtspPublisher::OnMediaUnprepared), self);
    gst_object_unref(element);
  }

  static void OnNeedData(GstElement*, guint, gpointer user_data) {
    RtspPublisher* self = static_cast<RtspPublisher*>(user_data);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->need_data_ = true;
  }

  static void OnEnoughData(GstElement*, gpointer user_data) {
    RtspPublisher* self = static_cast<RtspPublisher*>(user_data);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->need_data_ = false;
  }

  static void OnMediaUnprepared(GstRTSPMedia*, gpointer user_data) {
    RtspPublisher* self = static_cast<RtspPublisher*>(user_data);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->need_data_ = false;
    self->pushed_frames_ = 0;
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
  bool need_data_;
  std::uint64_t pushed_frames_;
  bool loop_started_;
  std::thread loop_thread_;
};

#ifdef HAVE_GST_ALLOCATORS
class RtspDmaNv12Publisher {
 public:
  RtspDmaNv12Publisher(int width, int height, int fps, int port,
                       std::string mount_path)
      : width_(width),
        height_(height),
        fps_(fps),
        port_(port),
        mount_path_(std::move(mount_path)),
        loop_(nullptr),
        server_(nullptr),
        factory_(nullptr),
        appsrc_(nullptr),
        allocator_(nullptr),
        need_data_(false),
        pushed_frames_(0),
        loop_started_(false) {}

  ~RtspDmaNv12Publisher() { Stop(); }

  bool Start() {
    allocator_ = gst_dmabuf_allocator_new();
    if (allocator_ == nullptr) {
      std::cerr << "failed to create GStreamer DMABUF allocator" << std::endl;
      return false;
    }

    loop_ = g_main_loop_new(nullptr, FALSE);
    if (loop_ == nullptr) {
      return false;
    }

    server_ = gst_rtsp_server_new();
    factory_ = gst_rtsp_media_factory_new();
    if (server_ == nullptr || factory_ == nullptr) {
      return false;
    }

    const std::string service = std::to_string(port_);
    gst_rtsp_server_set_service(server_, service.c_str());
    const std::string launch =
        "( appsrc name=mysrc is-live=true format=time do-timestamp=true "
        "caps=\"video/x-raw(memory:DMABuf),format=NV12,width=" +
        std::to_string(width_) + ",height=" + std::to_string(height_) +
        ",framerate=" + std::to_string(fps_) + "/1\""
        " ! queue leaky=downstream max-size-buffers=2 "
        " ! mpph264enc header-mode=1 gop=" +
        std::to_string(std::max(1, fps_)) +
        " ! h264parse config-interval=1 "
        " ! video/x-h264,stream-format=byte-stream,alignment=au "
        " ! rtph264pay name=pay0 pt=96 config-interval=1 )";
    gst_rtsp_media_factory_set_launch(factory_, launch.c_str());
    gst_rtsp_media_factory_set_shared(factory_, TRUE);
    gst_rtsp_media_factory_set_suspend_mode(factory_, GST_RTSP_SUSPEND_MODE_NONE);
    gst_rtsp_media_factory_set_eos_shutdown(factory_, FALSE);
    g_signal_connect(factory_, "media-configure",
                     G_CALLBACK(&RtspDmaNv12Publisher::OnMediaConfigure), this);

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
    if (allocator_ != nullptr) {
      g_object_unref(allocator_);
      allocator_ = nullptr;
    }
  }

  bool PushFrame(int dma_fd, std::size_t bytes, std::uint64_t frame_index) {
    GstElement* src = nullptr;
    GstAllocator* allocator = nullptr;
    std::uint64_t output_index = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (appsrc_ == nullptr || allocator_ == nullptr || dma_fd < 0 || bytes == 0 ||
          !need_data_) {
        return false;
      }
      src = appsrc_;
      allocator = allocator_;
      g_object_ref(src);
      g_object_ref(allocator);
      output_index = pushed_frames_++;
    }

    const int owned_fd = dup(dma_fd);
    if (owned_fd < 0) {
      std::cerr << "dup DMA fd for GStreamer failed: " << std::strerror(errno)
                << std::endl;
      g_object_unref(src);
      g_object_unref(allocator);
      return false;
    }
    GstMemory* memory = gst_dmabuf_allocator_alloc(allocator, owned_fd, bytes);
    g_object_unref(allocator);
    if (memory == nullptr) {
      close(owned_fd);
      g_object_unref(src);
      return false;
    }

    GstBuffer* buffer = gst_buffer_new();
    if (buffer == nullptr) {
      gst_memory_unref(memory);
      g_object_unref(src);
      return false;
    }
    gst_buffer_append_memory(buffer, memory);
    GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(output_index, GST_SECOND, fps_);
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(1, GST_SECOND, fps_);

    const GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(src), buffer);
    g_object_unref(src);
    if (ret != GST_FLOW_OK) {
      std::cerr << "RTSP DMABUF appsrc push failed: " << gst_flow_get_name(ret)
                << std::endl;
      // Treat transient pipeline state changes as retryable; OnMediaUnprepared
      // is the safe place to release appsrc after a client actually disconnects.
      return false;
    }
    return true;
  }

 private:
  static void OnMediaConfigure(GstRTSPMediaFactory*, GstRTSPMedia* media,
                               gpointer user_data) {
    RtspDmaNv12Publisher* self = static_cast<RtspDmaNv12Publisher*>(user_data);
    GstElement* element = gst_rtsp_media_get_element(media);
    GstElement* src = gst_bin_get_by_name_recurse_up(GST_BIN(element), "mysrc");
    {
      std::lock_guard<std::mutex> lock(self->mutex_);
      if (self->appsrc_ != nullptr) {
        g_object_unref(self->appsrc_);
      }
      self->appsrc_ = src;
      self->need_data_ = true;
      self->pushed_frames_ = 0;
    }
    gst_util_set_object_arg(G_OBJECT(src), "format", "time");
    const guint64 max_bytes =
        static_cast<guint64>(Nv12BufferSize(self->width_, self->height_) * 2);
    g_object_set(G_OBJECT(src), "stream-type", 0, "is-live", TRUE, "block", FALSE,
                 "max-bytes", max_bytes, nullptr);
    g_signal_connect(src, "need-data",
                     G_CALLBACK(&RtspDmaNv12Publisher::OnNeedData), self);
    g_signal_connect(src, "enough-data",
                     G_CALLBACK(&RtspDmaNv12Publisher::OnEnoughData), self);
    g_signal_connect(media, "unprepared",
                     G_CALLBACK(&RtspDmaNv12Publisher::OnMediaUnprepared), self);
    gst_object_unref(element);
  }

  static void OnNeedData(GstElement*, guint, gpointer user_data) {
    RtspDmaNv12Publisher* self = static_cast<RtspDmaNv12Publisher*>(user_data);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->need_data_ = true;
  }

  static void OnEnoughData(GstElement*, gpointer user_data) {
    RtspDmaNv12Publisher* self = static_cast<RtspDmaNv12Publisher*>(user_data);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->need_data_ = false;
  }

  static void OnMediaUnprepared(GstRTSPMedia*, gpointer user_data) {
    RtspDmaNv12Publisher* self = static_cast<RtspDmaNv12Publisher*>(user_data);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->need_data_ = false;
    self->pushed_frames_ = 0;
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
  GstAllocator* allocator_;
  bool need_data_;
  std::uint64_t pushed_frames_;
  bool loop_started_;
  std::thread loop_thread_;
};
#endif

int RouteBEnvInt(const char* name, int default_value, int min_value, int max_value) {
#ifdef MPP_DMA_RTSP_DEMO
  return EnvIntClamped(name, default_value, min_value, max_value);
#else
  (void)name;
  return std::clamp(default_value, min_value, max_value);
#endif
}

float RouteBEnvFloat(const char* name, float default_value, float min_value,
                     float max_value) {
#ifdef MPP_DMA_RTSP_DEMO
  return EnvFloatClamped(name, default_value, min_value, max_value);
#else
  (void)name;
  return std::clamp(default_value, min_value, max_value);
#endif
}

void DrawDetections(cv::Mat* frame, const std::vector<Detection>& detections) {
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
    banner << "UAV ALERT | targets=" << detections.size() << " | max_score="
           << std::fixed << std::setprecision(2) << best_score;
  } else {
    banner << "NORMAL | targets=0";
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
  explicit DisplayBoxStabilizer(bool stable_mode = true)
      : stable_mode_(stable_mode),
        max_hold_frames_(RouteBEnvInt("RK_YOLO_ROUTE_B_HOLD_FRAMES",
                                      stable_mode ? 40 : 24, 0, 240)),
        center_alpha_(RouteBEnvFloat("RK_YOLO_ROUTE_B_CENTER_ALPHA",
                                     stable_mode ? 0.045f : 0.070f, 0.001f, 0.5f)),
        center_step_ratio_(RouteBEnvFloat("RK_YOLO_ROUTE_B_CENTER_STEP_RATIO",
                                          stable_mode ? 0.008f : 0.014f, 0.001f, 0.2f)),
        center_jump_alpha_(RouteBEnvFloat("RK_YOLO_ROUTE_B_CENTER_JUMP_ALPHA",
                                          stable_mode ? 0.020f : 0.035f, 0.001f, 0.5f)),
        center_jump_step_ratio_(RouteBEnvFloat(
            "RK_YOLO_ROUTE_B_CENTER_JUMP_STEP_RATIO",
            stable_mode ? 0.004f : 0.008f, 0.001f, 0.2f)),
        size_alpha_(RouteBEnvFloat("RK_YOLO_ROUTE_B_SIZE_ALPHA",
                                   stable_mode ? 0.006f : 0.018f, 0.001f, 0.5f)),
        size_step_ratio_(RouteBEnvFloat("RK_YOLO_ROUTE_B_SIZE_STEP_RATIO",
                                        stable_mode ? 0.0025f : 0.007f, 0.0005f, 0.2f)),
        size_jump_alpha_(RouteBEnvFloat("RK_YOLO_ROUTE_B_SIZE_JUMP_ALPHA",
                                        stable_mode ? 0.003f : 0.010f, 0.001f, 0.5f)),
        size_jump_step_ratio_(RouteBEnvFloat(
            "RK_YOLO_ROUTE_B_SIZE_JUMP_STEP_RATIO",
            stable_mode ? 0.0015f : 0.004f, 0.0005f, 0.2f)) {}

  std::vector<Detection> Update(const std::vector<Detection>& detections) {
    const Detection* best = ChooseDetection(detections);

    if (best == nullptr) {
      if (pending_hits_ > 0 && ++pending_misses_ > kMaxPendingMisses) {
        pending_hits_ = 0;
        pending_misses_ = 0;
      }
      if (has_box_ && hold_frames_ < MaxHoldFrames()) {
        ++hold_frames_;
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

    float center_alpha = center_alpha_;
    float max_center_step_ratio = center_step_ratio_;
    if (center_delta > std::max(width_, height_) * 0.25f) {
      center_alpha = center_jump_alpha_;
      max_center_step_ratio = center_jump_step_ratio_;
    }

    float size_alpha = size_alpha_;
    float max_size_step_ratio = size_step_ratio_;
    if (area_ratio > 1.35f || area_ratio < 0.75f) {
      size_alpha = size_jump_alpha_;
      max_size_step_ratio = size_jump_step_ratio_;
    }

    const float max_center_step =
        std::max(3.0f, std::max(width_, height_) * max_center_step_ratio);
    center_x_ = SmoothCoordinate(center_x_, curr_center.x, center_alpha,
                                 max_center_step);
    center_y_ = SmoothCoordinate(center_y_, curr_center.y, center_alpha,
                                 max_center_step);
    width_ = SmoothDimension(width_, static_cast<float>(best->box.width),
                             size_alpha, max_size_step_ratio);
    height_ = SmoothDimension(height_, static_cast<float>(best->box.height),
                              size_alpha, max_size_step_ratio);
    last_score_ = SmoothFloat(last_score_, best->score, stable_mode_ ? 0.18f : 0.25f);

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

    const Detection* best = nullptr;
    float best_assoc = -1e9f;
    for (const Detection& det : detections) {
      const float iou = RectIou(prev_box, det.box);
      const float det_cx = det.box.x + det.box.width * 0.5f;
      const float det_cy = det.box.y + det.box.height * 0.5f;
      const float norm_center_delta =
          std::hypot(det_cx - center_x_, det_cy - center_y_) / prev_scale;
      const float area_ratio =
          static_cast<float>(std::max(1, det.box.area())) / prev_area;
      const float log_area_delta = std::abs(std::log(std::max(0.05f, area_ratio)));
      if (stable_mode_ && log_area_delta > kMaxStableLogAreaDelta && iou < 0.35f) {
        continue;
      }
      const bool plausible =
          stable_mode_ ? (iou > 0.16f || norm_center_delta < 0.16f)
                       : (iou > 0.10f || norm_center_delta < 0.26f);
      if (!plausible) {
        continue;
      }

      const float assoc =
          stable_mode_
              ? det.score + 5.0f * iou - 2.40f * norm_center_delta -
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

  int MaxHoldFrames() const { return max_hold_frames_; }

  static constexpr int kRequiredInitialHits = 3;
  static constexpr int kMaxPendingMisses = 3;
  static constexpr float kImmediateLockScore = 0.62f;
  static constexpr float kMaxStableLogAreaDelta = 0.92f;
  static constexpr float kMinStableAssoc = -0.25f;
  bool stable_mode_ = true;
  int max_hold_frames_ = 40;
  float center_alpha_ = 0.045f;
  float center_step_ratio_ = 0.008f;
  float center_jump_alpha_ = 0.020f;
  float center_jump_step_ratio_ = 0.004f;
  float size_alpha_ = 0.006f;
  float size_step_ratio_ = 0.0025f;
  float size_jump_alpha_ = 0.003f;
  float size_jump_step_ratio_ = 0.0015f;
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
};

struct AsyncInferStats {
  int submitted = 0;
  int skipped_busy = 0;
  int replaced_pending = 0;
  int copy_failed = 0;
  int inferred = 0;
  int detected = 0;
  int pool_size = 0;
  std::size_t total_detections = 0;
  double prepare_sum = 0.0;
  double run_sum = 0.0;
  double total_sum = 0.0;
};

class AsyncDmaInferWorker {
 public:
  AsyncDmaInferWorker(YoloRknnDetector* detector, float conf, float nms,
                      int width, int height, int pool_size)
      : detector_(detector),
        conf_(conf),
        nms_(nms),
        width_(width),
        height_(height),
        pool_size_(std::max(2, pool_size)) {}

  ~AsyncDmaInferWorker() { Stop(); }

  AsyncDmaInferWorker(const AsyncDmaInferWorker&) = delete;
  AsyncDmaInferWorker& operator=(const AsyncDmaInferWorker&) = delete;

  bool Start() {
    if (detector_ == nullptr || width_ <= 0 || height_ <= 0) {
      return false;
    }
    const std::size_t bytes = Nv12BufferSize(width_, height_);
    frame_buffers_.resize(static_cast<std::size_t>(pool_size_));
    free_indices_.clear();
    for (int i = 0; i < pool_size_; ++i) {
      if (!frame_buffers_[static_cast<std::size_t>(i)].Allocate(bytes)) {
        std::cerr << "async infer DMA staging buffer allocation failed at pool index "
                  << i << std::endl;
        frame_buffers_.clear();
        free_indices_.clear();
        return false;
      }
      free_indices_.push_back(static_cast<std::size_t>(i));
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = false;
      running_ = true;
      pending_index_ = kNoPending;
      pending_frame_index_ = 0;
      latest_detections_.clear();
      stats_ = AsyncInferStats{};
      stats_.pool_size = pool_size_;
    }
    worker_ = std::thread(&AsyncDmaInferWorker::Run, this);
    std::cout << "async_infer=on staging_dma_heap="
              << frame_buffers_.front().heap_name() << " bytes="
              << frame_buffers_.front().size() << " pool=" << pool_size_
              << std::endl;
    return true;
  }

  void Stop() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_) {
        return;
      }
      stop_ = true;
    }
    cond_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_ = false;
      pending_index_ = kNoPending;
      free_indices_.clear();
    }
  }

  bool SubmitLatest(const DecodedFrame& frame, std::uint64_t frame_index) {
    std::size_t buffer_index = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_ || stop_ || free_indices_.empty()) {
        ++stats_.skipped_busy;
        return false;
      }
      buffer_index = free_indices_.front();
      free_indices_.pop_front();
    }

    const bool copied =
        ConvertDmaFrameToNv12Dma(frame, {}, &frame_buffers_[buffer_index]);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!copied) {
        ++stats_.copy_failed;
        free_indices_.push_back(buffer_index);
        cond_.notify_all();
        return false;
      }
      if (pending_index_ != kNoPending) {
        free_indices_.push_back(pending_index_);
        ++stats_.replaced_pending;
      }
      pending_index_ = buffer_index;
      pending_frame_index_ = frame_index;
      ++stats_.submitted;
    }
    cond_.notify_one();
    return true;
  }

  std::vector<Detection> LatestDetections() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_detections_;
  }

  AsyncInferStats Stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
  }

 private:
  void Run() {
    while (true) {
      std::uint64_t local_frame_index = 0;
      std::size_t local_buffer_index = kNoPending;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this]() { return stop_ || pending_index_ != kNoPending; });
        if (stop_ && pending_index_ == kNoPending) {
          return;
        }
        local_buffer_index = pending_index_;
        local_frame_index = pending_frame_index_;
        pending_index_ = kNoPending;
      }

      YoloRknnDetector::LetterBoxInfo letterbox;
      InferProfile profile;
      std::vector<Detection> detections;
      if (detector_->PrepareDmaFdToBoundInputStrided(
              frame_buffers_[local_buffer_index].fd(), width_, height_, width_, height_,
              RK_FORMAT_YCbCr_420_SP, &letterbox, &profile)) {
        detections = detector_->InferBoundInput(letterbox, conf_, nms_, &profile);
      } else {
        std::cerr << "async RGA -> RKNN bound input preparation failed" << std::endl;
      }

      {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_detections_ = detections;
        ++stats_.inferred;
        if (!detections.empty()) {
          ++stats_.detected;
          stats_.total_detections += detections.size();
        }
        stats_.prepare_sum += profile.prepare_ms;
        stats_.run_sum += profile.run_ms;
        stats_.total_sum += profile.total_ms;
        free_indices_.push_back(local_buffer_index);
      }
      cond_.notify_all();

      if (local_frame_index <= 5 || local_frame_index % 60 == 0 ||
          !detections.empty()) {
        std::cout << "async_frame=" << local_frame_index
                  << " det=" << detections.size() << " prepare_ms="
                  << std::fixed << std::setprecision(2) << profile.prepare_ms
                  << " run_ms=" << profile.run_ms
                  << " total_ms=" << profile.total_ms << std::endl;
      }
    }
  }

  YoloRknnDetector* detector_ = nullptr;
  float conf_ = 0.0f;
  float nms_ = 0.0f;
  int width_ = 0;
  int height_ = 0;
  int pool_size_ = 2;
  static constexpr std::size_t kNoPending = static_cast<std::size_t>(-1);
  std::vector<DmaHeapBuffer> frame_buffers_;
  std::deque<std::size_t> free_indices_;
  mutable std::mutex mutex_;
  std::condition_variable cond_;
  std::thread worker_;
  bool running_ = false;
  bool stop_ = false;
  std::size_t pending_index_ = kNoPending;
  std::uint64_t pending_frame_index_ = 0;
  std::vector<Detection> latest_detections_;
  AsyncInferStats stats_;
};
#endif

class MppDecoder {
 public:
  MppDecoder() = default;
  ~MppDecoder() { Release(); }

  MppDecoder(const MppDecoder&) = delete;
  MppDecoder& operator=(const MppDecoder&) = delete;

  bool Init(MppCodingType coding) {
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

    ret = mpp_init(ctx_, MPP_CTX_DEC, coding);
    if (ret != MPP_OK) {
      std::cerr << "mpp_init decoder failed ret=" << ret << std::endl;
      Release();
      return false;
    }

    RK_S64 output_timeout_ms = 0;
    ret = mpi_->control(ctx_, MPP_SET_OUTPUT_TIMEOUT, &output_timeout_ms);
    if (ret != MPP_OK) {
      std::cerr << "MPP_SET_OUTPUT_TIMEOUT warning ret=" << ret << std::endl;
    }

    initialized_ = true;
    return true;
  }

  bool Decode(const void* data, std::size_t size, DecodedFrame* decoded,
              double* decode_ms) {
    if (!initialized_ || data == nullptr || size == 0 || decoded == nullptr) {
      return false;
    }
    *decoded = DecodedFrame{};
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
    ret = mpi_->decode_put_packet(ctx_, packet);
    if (ret != MPP_OK) {
      if (ret == MPP_ERR_BUFFER_FULL) {
        ++buffer_full_count_;
        if (buffer_full_count_ <= 3 || buffer_full_count_ % 60 == 0) {
          std::cerr << "mpp input queue full, draining decoded frames count="
                    << buffer_full_count_ << std::endl;
        }
        DrainDecodedFrames(decoded);
        const auto decode_end = Clock::now();
        mpp_packet_deinit(&packet);
        if (decode_ms != nullptr) {
          *decode_ms = ElapsedMs(decode_start, decode_end);
        }
        return true;
      }
      const auto decode_end = Clock::now();
      mpp_packet_deinit(&packet);
      if (decode_ms != nullptr) {
        *decode_ms = ElapsedMs(decode_start, decode_end);
      }
      std::cerr << "mpp decode_put_packet failed ret=" << ret << std::endl;
      return false;
    }

    DrainDecodedFrames(decoded);
    const auto decode_end = Clock::now();
    mpp_packet_deinit(&packet);
    if (decode_ms != nullptr) {
      *decode_ms = ElapsedMs(decode_start, decode_end);
    }
    return true;
  }

  void Release() {
    if (ctx_ != nullptr) {
      DrainFramesForRelease();
      if (mpi_ != nullptr) {
        mpi_->reset(ctx_);
      }
      mpp_destroy(ctx_);
      ctx_ = nullptr;
      mpi_ = nullptr;
    }
    initialized_ = false;
  }

 private:
  bool DrainDecodedFrames(DecodedFrame* decoded) {
    if (decoded == nullptr || decoded->frame != nullptr) {
      return true;
    }
    for (int i = 0; i < 16 && decoded->frame == nullptr; ++i) {
      MppFrame raw_frame = nullptr;
      MPP_RET ret = mpi_->decode_get_frame(ctx_, &raw_frame);
      if (ret != MPP_OK || raw_frame == nullptr) {
        return true;
      }
      HandleDecodedFrame(raw_frame, decoded);
    }
    return true;
  }

  void DrainFramesForRelease() {
    if (ctx_ == nullptr || mpi_ == nullptr) {
      return;
    }
    for (int i = 0; i < 64; ++i) {
      MppFrame raw_frame = nullptr;
      MPP_RET ret = mpi_->decode_get_frame(ctx_, &raw_frame);
      if (ret != MPP_OK || raw_frame == nullptr) {
        break;
      }
      MppFrameGuard frame(raw_frame);
    }
  }

  bool HandleDecodedFrame(MppFrame raw_frame, DecodedFrame* decoded) {
    MppFrameGuard frame(raw_frame);
    if (mpp_frame_get_info_change(frame.get())) {
      mpi_->control(ctx_, MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
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
      std::cerr << "mpp decoded frame is FBC format, unsupported for this RGA validator"
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
      std::cerr << "mpp decoded frame has no buffer" << std::endl;
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
  bool initialized_ = false;
  int buffer_full_count_ = 0;
  int skipped_error_frames_ = 0;
};

#endif

void PrintUsage(const char* program) {
#ifdef MPP_DMA_RTSP_DEMO
  std::cerr << "Usage: " << program
            << " <model.rknn> [device=/dev/video48] [width=640] [height=480]"
            << " [fps=15] [codec=h264|mjpg] [conf=0.24] [nms=0.45]"
            << " [frames=0] [rtsp_port=8562] [mount=/yolo_mpp]"
            << " [detect_every_n=3] [output_mode=bgr|dmabuf]" << std::endl
            << "frames=0 means run until Ctrl+C." << std::endl;
#else
  std::cerr << "Usage: " << program
            << " <model.rknn> [device=/dev/video48] [width=640] [height=480]"
            << " [fps=15] [codec=h264|mjpg] [conf=0.24] [nms=0.45]"
            << " [frames=300]" << std::endl;
#endif
}

}  // namespace

int main(int argc, char** argv) {
#if !defined(HAVE_RGA) || !defined(HAVE_MPP)
  (void)argc;
  (void)argv;
  std::cerr << "rk_yolo_mpp_dma_demo requires librga and Rockchip MPP at build time"
            << std::endl;
  return 2;
#else
#ifdef MPP_DMA_RTSP_DEMO
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);
  gst_init(&argc, &argv);
#endif

  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  const std::string model_path = argv[1];
  const std::string device = (argc > 2) ? argv[2] : "/dev/video48";
  const int width = (argc > 3) ? std::stoi(argv[3]) : 640;
  const int height = (argc > 4) ? std::stoi(argv[4]) : 480;
  const int fps = (argc > 5) ? std::stoi(argv[5]) : 15;
  const std::string codec = (argc > 6) ? argv[6] : "h264";
  const float conf = (argc > 7) ? std::stof(argv[7]) : 0.24f;
  const float nms = (argc > 8) ? std::stof(argv[8]) : 0.45f;
#ifdef MPP_DMA_RTSP_DEMO
  const int max_frames = (argc > 9) ? std::stoi(argv[9]) : 0;
  const int rtsp_port = (argc > 10) ? std::stoi(argv[10]) : 8562;
  const std::string mount_path = (argc > 11) ? argv[11] : "/yolo_mpp";
  const int detect_every_n = std::max(1, (argc > 12) ? std::stoi(argv[12]) : 3);
  const std::string output_mode = (argc > 13) ? argv[13] : "bgr";
  const bool async_infer_enabled = EnvFlag("RK_YOLO_ASYNC_INFER", false);
  const int async_pool_size = std::max(2, EnvInt("RK_YOLO_ASYNC_POOL", 3));
  const bool route_b_stable = EnvFlag("RK_YOLO_ROUTE_B_STABLE", true);
  const bool route_b_dedup = EnvFlag("RK_YOLO_ROUTE_B_DEDUP", true);
  const OsdIgnoreConfig osd_ignore{
      EnvFlag("RK_YOLO_IGNORE_OSD", false),
      EnvFloat("RK_YOLO_OSD_LEFT", 0.0f),
      EnvFloat("RK_YOLO_OSD_TOP", 0.55f),
      EnvFloat("RK_YOLO_OSD_RIGHT", 0.42f),
      EnvFloat("RK_YOLO_OSD_BOTTOM", 1.0f)};
  CameraTuneConfig camera_tune_config = LoadCameraTuneConfig();
  AutoZoomConfig auto_zoom_config = LoadAutoZoomConfig();
  AutoZoomState auto_zoom_state;
  auto_zoom_state.current_zoom = std::clamp(camera_tune_config.zoom_absolute,
                                           auto_zoom_config.min_zoom,
                                           auto_zoom_config.max_zoom);
  std::string camera_tune_status;
  std::string auto_zoom_status;
#else
  const int max_frames = (argc > 9) ? std::stoi(argv[9]) : 300;
  const int detect_every_n = 1;
  const bool async_infer_enabled = false;
  const bool route_b_stable = true;
#endif

  setenv("RK_YOLO_ZERO_COPY_INPUT", "1", 1);
  setenv("RK_YOLO_RGA_LETTERBOX", "1", 0);

  std::cout << "aggressive route B experimental path enabled" << std::endl;
#ifdef MPP_DMA_RTSP_DEMO
  std::cout << "mode=production-candidate RTSP visualization enabled" << std::endl;
#endif
  std::cout << "path=V4L2 compressed -> MPP decode -> MppFrame fd -> RGA letterbox"
            << " -> RKNN input memory -> NPU" << std::endl;
#ifdef MPP_DMA_RTSP_DEMO
  std::cout << "rtsp_output_mode=" << output_mode
            << " (bgr=boxed visualization, dmabuf=NV12 DMA performance stream)"
            << std::endl;
  std::cout << "async_infer=" << (async_infer_enabled ? "on" : "off")
            << " (set RK_YOLO_ASYNC_INFER=1 to decouple NPU from decode/output)"
            << std::endl;
  std::cout << "route_b_stable=" << (route_b_stable ? "on" : "off")
            << " (set RK_YOLO_ROUTE_B_STABLE=0 for rawer debug visualization)"
            << std::endl;
  std::cout << "route_b_dedup=" << (route_b_dedup ? "on" : "off")
            << " (set RK_YOLO_ROUTE_B_DEDUP=0 to show all raw nearby candidates)"
            << std::endl;
  std::cout << "ignore_osd=" << (osd_ignore.enabled ? "on" : "off")
            << " region=[" << osd_ignore.left << "," << osd_ignore.top
            << "," << osd_ignore.right << "," << osd_ignore.bottom
            << "] (set RK_YOLO_IGNORE_OSD=1 for public videos with telemetry text)"
            << std::endl;
  if (async_infer_enabled) {
    std::cout << "async_pool=" << async_pool_size
              << " (set RK_YOLO_ASYNC_POOL=N to tune staging DMA pool)"
              << std::endl;
  }
#endif
  std::cout << "codec=" << codec << " conf=" << conf << " nms=" << nms
            << " frames=" << max_frames << " detect_every_n=" << detect_every_n
            << std::endl;

#ifdef MPP_DMA_RTSP_DEMO
  ApplyCameraTuning(device, camera_tune_config, &camera_tune_status);
  if (!AutoZoomCameraAllowed(device, auto_zoom_config, &auto_zoom_status)) {
    auto_zoom_config.enabled = false;
  }
  std::cout << camera_tune_status << std::endl;
  std::cout << auto_zoom_status << std::endl;
#endif

  YoloRknnDetector detector;
  if (!detector.Load(model_path)) {
    std::cerr << "failed to load model: " << model_path << std::endl;
    return 1;
  }
  if (!detector.zero_copy_input_enabled()) {
    std::cerr << "RKNN zero-copy input memory is not enabled; route B cannot continue"
              << std::endl;
    return 1;
  }

  int fd = -1;
  int actual_width = 0;
  int actual_height = 0;
  std::vector<V4l2Buffer> buffers;
  bool streaming = false;
  const unsigned int pixfmt = FourccForCodec(codec);
  if (!OpenCompressedCamera(device, width, height, fps, pixfmt, &fd, &actual_width,
                            &actual_height, &buffers)) {
    return 1;
  }
  streaming = true;

  MppDecoder decoder;
  if (!decoder.Init(MppCodingForCodec(codec))) {
    CleanupCamera(fd, &buffers, streaming);
    return 1;
  }

#ifdef MPP_DMA_RTSP_DEMO
  bool dma_output_enabled = output_mode == "dmabuf" || output_mode == "dma";
#ifndef HAVE_GST_ALLOCATORS
  if (dma_output_enabled) {
    std::cerr << "GStreamer allocators support is not available; falling back to BGR RTSP"
              << std::endl;
    dma_output_enabled = false;
  }
#endif
  std::unique_ptr<RtspPublisher> bgr_publisher;
#ifdef HAVE_GST_ALLOCATORS
  std::unique_ptr<RtspDmaNv12Publisher> dma_publisher;
  std::vector<DmaHeapBuffer> dma_output_ring;
  std::size_t dma_output_index = 0;
  if (dma_output_enabled) {
    constexpr int kDmaOutputRingSize = 8;
    dma_output_ring.resize(kDmaOutputRingSize);
    const std::size_t nv12_bytes = Nv12BufferSize(actual_width, actual_height);
    for (int i = 0; i < kDmaOutputRingSize; ++i) {
      if (!dma_output_ring[i].Allocate(nv12_bytes)) {
        std::cerr << "failed to allocate RTSP DMABUF output ring; falling back to BGR"
                  << std::endl;
        dma_output_enabled = false;
        dma_output_ring.clear();
        break;
      }
    }
    if (dma_output_enabled) {
      std::cout << "rtsp_dmabuf_output=on bytes_per_frame=" << nv12_bytes
                << " heap=" << dma_output_ring.front().heap_name()
                << " ring=" << dma_output_ring.size() << std::endl;
    }
  }
#endif

  bool publisher_started = false;
  if (dma_output_enabled) {
#ifdef HAVE_GST_ALLOCATORS
    dma_publisher = std::make_unique<RtspDmaNv12Publisher>(
        actual_width, actual_height, fps > 0 ? fps : 15, rtsp_port, mount_path);
    publisher_started = dma_publisher->Start();
#endif
  } else {
    bgr_publisher = std::make_unique<RtspPublisher>(
        actual_width, actual_height, fps > 0 ? fps : 15, rtsp_port, mount_path);
    publisher_started = bgr_publisher->Start();
  }
  if (!publisher_started) {
    std::cerr << "failed to start RTSP publisher" << std::endl;
    CleanupCamera(fd, &buffers, streaming);
    return 1;
  }
  std::cout << "RTSP output ready: rtsp://<board-ip>:" << rtsp_port << mount_path
            << " mode=" << (dma_output_enabled ? "dmabuf-nv12-no-box-overlay" : "bgr-boxed")
            << std::endl;
  DisplayBoxStabilizer display_stabilizer(route_b_stable);
  std::unique_ptr<AsyncDmaInferWorker> async_worker;
  bool async_active = false;
  if (async_infer_enabled) {
    async_worker = std::make_unique<AsyncDmaInferWorker>(&detector, conf, nms,
                                                         actual_width, actual_height,
                                                         async_pool_size);
    async_active = async_worker->Start();
    if (!async_active) {
      std::cerr << "async_infer requested but failed to start; falling back to sync inference"
                << std::endl;
      async_worker.reset();
    }
  }
  int visualized_frames = 0;
#endif

  int captured_packets = 0;
  int decoded_frames = 0;
  int inferred_frames = 0;
  int detected_frames = 0;
  std::size_t total_detections = 0;
  double decode_sum = 0.0;
  double prepare_sum = 0.0;
  double run_sum = 0.0;
  double total_sum = 0.0;
  const int packet_limit = max_frames > 0 ? std::max(max_frames * 30, 300) : 0;
  const auto wall_start = Clock::now();

  while (
#ifdef MPP_DMA_RTSP_DEMO
      !g_stop_requested.load() &&
#endif
      (max_frames <= 0 || inferred_frames < max_frames) &&
      (packet_limit <= 0 || captured_packets < packet_limit)) {
    pollfd pfd = {};
    pfd.fd = fd;
    pfd.events = POLLIN;
    const int poll_ret = poll(&pfd, 1, 2000);
    if (poll_ret <= 0) {
      std::cerr << "poll camera timeout or error" << std::endl;
      break;
    }

    v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (Xioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
      if (errno == EAGAIN) {
        continue;
      }
      std::cerr << "VIDIOC_DQBUF failed: " << std::strerror(errno) << std::endl;
      break;
    }

    bool requeue_ok = true;
    if (buf.index >= buffers.size()) {
      std::cerr << "invalid V4L2 buffer index: " << buf.index << std::endl;
      requeue_ok = false;
    } else if (buf.bytesused > 0) {
      ++captured_packets;
      DecodedFrame decoded;
      double decode_ms = 0.0;
      if (decoder.Decode(buffers[buf.index].start, buf.bytesused, &decoded, &decode_ms)) {
        if (decoded.frame != nullptr) {
          ++decoded_frames;
          decode_sum += decode_ms;
          MppFrameGuard frame_guard(decoded.frame);

          InferProfile profile;
          YoloRknnDetector::LetterBoxInfo letterbox;
          std::vector<Detection> detections;
          const bool should_infer =
              detect_every_n <= 1 || ((decoded_frames - 1) % detect_every_n == 0);
          if (should_infer) {
#ifdef MPP_DMA_RTSP_DEMO
            if (async_active) {
              async_worker->SubmitLatest(decoded, static_cast<std::uint64_t>(decoded_frames));
            } else
#endif
            {
            if (detector.PrepareDmaFdToBoundInputStrided(
                    decoded.dma_fd, decoded.width, decoded.height, decoded.hor_stride,
                    decoded.ver_stride, decoded.rga_format, &letterbox, &profile)) {
              detections = detector.InferBoundInput(letterbox, conf, nms, &profile);
              ++inferred_frames;
            } else {
              std::cerr << "RGA -> RKNN bound input preparation failed" << std::endl;
            }
            }
          }

#ifdef MPP_DMA_RTSP_DEMO
          if (async_active) {
            detections = async_worker->LatestDetections();
          }
#endif
          if (should_infer && !detections.empty()
#ifdef MPP_DMA_RTSP_DEMO
              && !async_active
#endif
          ) {
            ++detected_frames;
            total_detections += detections.size();
          }
#ifdef MPP_DMA_RTSP_DEMO
          const std::vector<Detection> visual_source =
              SuppressNearbyDuplicateDetections(
                  FilterOsdDetections(detections, decoded.width, decoded.height,
                                      osd_ignore),
                  decoded.width, decoded.height, route_b_dedup);
          const auto display_detections = display_stabilizer.Update(visual_source);
          UpdateAutoZoom(device, auto_zoom_config, camera_tune_config,
                         &auto_zoom_state,
                         static_cast<std::uint64_t>(decoded_frames),
                         decoded.width, decoded.height, display_detections);
          bool pushed = false;
          if (dma_output_enabled) {
#ifdef HAVE_GST_ALLOCATORS
            DmaHeapBuffer& out =
                dma_output_ring[dma_output_index++ % dma_output_ring.size()];
            if (ConvertDmaFrameToNv12Dma(decoded, display_detections, &out)) {
              pushed = dma_publisher->PushFrame(
                  out.fd(), out.size(), static_cast<std::uint64_t>(decoded_frames));
            }
#endif
          } else {
            cv::Mat bgr;
            if (ConvertDmaFrameToBgr(decoded, &bgr)) {
              DrawDetections(&bgr, display_detections);
              pushed =
                  bgr_publisher->PushFrame(bgr, static_cast<std::uint64_t>(decoded_frames));
            }
          }
          if (pushed) {
              ++visualized_frames;
          } else if (decoded_frames <= 5 || decoded_frames % 60 == 0) {
            std::cout << "RTSP has no active client yet, or current output mode rejected a frame;"
                      << " inference continues" << std::endl;
          }
#endif
          if (
#ifdef MPP_DMA_RTSP_DEMO
              async_active
#else
              false
#endif
          ) {
#ifdef MPP_DMA_RTSP_DEMO
            const AsyncInferStats stats = async_worker->Stats();
            inferred_frames = stats.inferred;
#endif
          }
          if (should_infer
#ifdef MPP_DMA_RTSP_DEMO
              && !async_active
#endif
          ) {
            prepare_sum += profile.prepare_ms;
            run_sum += profile.run_ms;
            total_sum += profile.total_ms;
          }

          if (should_infer &&
#ifdef MPP_DMA_RTSP_DEMO
              !async_active &&
#endif
              (inferred_frames <= 5 || inferred_frames % 30 == 0 ||
               !detections.empty())) {
            std::cout << "frame=" << inferred_frames << " packets=" << captured_packets
                      << " decoded=" << decoded_frames << " size=" << decoded.width
                      << "x" << decoded.height << " stride=" << decoded.hor_stride
                      << "x" << decoded.ver_stride << " det=" << detections.size()
                      << " decode_ms=" << std::fixed << std::setprecision(2)
                      << decode_ms << " prepare_ms=" << profile.prepare_ms
                      << " run_ms=" << profile.run_ms
                      << " total_ms=" << profile.total_ms << std::endl;
          }
        }
      }
    }

    if (requeue_ok && !QueueBuffer(fd, static_cast<int>(buf.index))) {
      break;
    }
  }
  if (packet_limit > 0 && captured_packets >= packet_limit &&
      (max_frames <= 0 || inferred_frames < max_frames)) {
    std::cerr << "packet limit reached before requested inferred frames: packets="
              << captured_packets << " limit=" << packet_limit
              << " inferred=" << inferred_frames << " requested=" << max_frames
              << std::endl;
  }

#ifdef MPP_DMA_RTSP_DEMO
  if (async_active) {
    async_worker->Stop();
    const AsyncInferStats stats = async_worker->Stats();
    inferred_frames = stats.inferred;
    detected_frames = stats.detected;
    total_detections = stats.total_detections;
    prepare_sum = stats.prepare_sum;
    run_sum = stats.run_sum;
    total_sum = stats.total_sum;
    std::cout << "async_summary submitted=" << stats.submitted
              << " skipped_busy=" << stats.skipped_busy
              << " replaced_pending=" << stats.replaced_pending
              << " copy_failed=" << stats.copy_failed
              << " pool_size=" << stats.pool_size
              << " inferred=" << stats.inferred
              << " detected=" << stats.detected
              << " total_detections=" << stats.total_detections << std::endl;
  }
#endif

  const auto wall_end = Clock::now();
  const double wall_s = std::chrono::duration<double>(wall_end - wall_start).count();
  std::cout << "summary packets=" << captured_packets
            << " decoded_frames=" << decoded_frames
            << " inferred_frames=" << inferred_frames
            << " detected_frames=" << detected_frames
            << " total_detections=" << total_detections
#ifdef MPP_DMA_RTSP_DEMO
            << " visualized_frames=" << visualized_frames
#endif
            << " infer_wall_fps=" << (wall_s > 0.0 ? inferred_frames / wall_s : 0.0)
            << " decoded_wall_fps=" << (wall_s > 0.0 ? decoded_frames / wall_s : 0.0)
            << " avg_decode_ms="
            << (decoded_frames > 0 ? decode_sum / decoded_frames : 0.0)
            << " avg_prepare_ms="
            << (inferred_frames > 0 ? prepare_sum / inferred_frames : 0.0)
            << " avg_run_ms=" << (inferred_frames > 0 ? run_sum / inferred_frames : 0.0)
            << " avg_total_ms="
            << (inferred_frames > 0 ? total_sum / inferred_frames : 0.0)
            << std::endl;

  CleanupCamera(fd, &buffers, streaming);
  return inferred_frames > 0 ? 0 : 1;
#endif
}
