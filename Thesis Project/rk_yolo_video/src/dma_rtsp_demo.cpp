#include "yolo_rknn.h"

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

#ifdef HAVE_RGA
#include <linux/videodev2.h>
#include <poll.h>
#include <rga/im2d.h>
#include <rga/rga.h>
#endif

namespace {

std::atomic_bool g_stop_requested{false};

void HandleSignal(int) {
  g_stop_requested.store(true);
}

#ifdef HAVE_RGA

struct V4l2Buffer {
  void* start = nullptr;
  std::size_t length = 0;
  int dma_fd = -1;
};

int Xioctl(int fd, unsigned long request, void* arg) {
  int ret = 0;
  do {
    ret = ioctl(fd, request, arg);
  } while (ret == -1 && errno == EINTR);
  return ret;
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
      if (buffer.dma_fd >= 0) {
        close(buffer.dma_fd);
        buffer.dma_fd = -1;
      }
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

bool OpenCamera(const std::string& device, int requested_width, int requested_height,
                int requested_fps, int* fd, int* actual_width, int* actual_height,
                std::vector<V4l2Buffer>* buffers) {
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
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;
  if (Xioctl(*fd, VIDIOC_S_FMT, &fmt) == -1) {
    std::cerr << "VIDIOC_S_FMT YUYV failed: " << std::strerror(errno) << std::endl;
    CleanupCamera(*fd, buffers, false);
    *fd = -1;
    return false;
  }
  if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
    std::cerr << "camera did not accept YUYV format" << std::endl;
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

    v4l2_exportbuffer expbuf = {};
    expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    expbuf.index = i;
    expbuf.flags = O_CLOEXEC;
    if (Xioctl(*fd, VIDIOC_EXPBUF, &expbuf) == -1) {
      std::cerr << "VIDIOC_EXPBUF failed: " << std::strerror(errno) << std::endl;
      CleanupCamera(*fd, buffers, false);
      *fd = -1;
      return false;
    }
    (*buffers)[i].dma_fd = expbuf.fd;

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
            << *actual_height << " YUYV buffers=" << buffers->size() << std::endl;
  return true;
}

#endif

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
        std::to_string(width_) + ",height=" + std::to_string(height_) +
        ",framerate=" + std::to_string(fps_) + "/1"
        " ! videoconvert ! mpph264enc ! h264parse config-interval=1 ! "
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
    if (appsrc_ == nullptr || frame.empty()) {
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

std::string BuildLabel(const Detection& det) {
  std::ostringstream oss;
  oss << "drone " << std::fixed << std::setprecision(2) << det.score;
  return oss.str();
}

float MaxScore(const std::vector<Detection>& detections) {
  float max_score = 0.0f;
  for (const Detection& det : detections) {
    max_score = std::max(max_score, det.score);
  }
  return max_score;
}

void DrawDetections(cv::Mat* frame, const std::vector<Detection>& detections) {
  if (frame == nullptr || frame->empty()) {
    return;
  }
  const cv::Scalar color(0, 255, 0);
  for (const Detection& det : detections) {
    cv::rectangle(*frame, det.box, color, 2);
    const std::string label = BuildLabel(det);
    int baseline = 0;
    const cv::Size label_size =
        cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.55, 2, &baseline);
    const int box_x = std::max(0, det.box.x);
    const int box_y = std::max(label_size.height + 4, det.box.y);
    const int box_w = std::min(label_size.width + 8, frame->cols - box_x);
    const int box_h =
        std::min(label_size.height + baseline + 6, frame->rows - box_y + label_size.height + 4);
    if (box_w > 0 && box_h > 0) {
      cv::rectangle(*frame, cv::Rect(box_x, box_y - label_size.height - 4, box_w, box_h),
                    color, cv::FILLED);
    }
    cv::putText(*frame, label, cv::Point(box_x + 4, box_y - 4),
                cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 0, 0), 2);
  }
}

void DrawAlarmOverlay(cv::Mat* frame, const std::vector<Detection>& detections) {
  if (frame == nullptr || frame->empty()) {
    return;
  }
  const bool active = !detections.empty();
  const int bar_h = std::max(28, frame->rows / 14);
  const cv::Scalar bg = active ? cv::Scalar(0, 0, 220) : cv::Scalar(0, 120, 0);
  cv::rectangle(*frame, cv::Rect(0, 0, frame->cols, bar_h), bg, cv::FILLED);
  std::ostringstream oss;
  if (active) {
    oss << "UAV ALERT | targets=" << detections.size() << " | max_score="
        << std::fixed << std::setprecision(2) << MaxScore(detections);
  } else {
    oss << "NORMAL | targets=0";
  }
  cv::putText(*frame, oss.str(), cv::Point(12, std::min(bar_h - 8, 26)),
              cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
}

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " <model.rknn> [device=/dev/video48] [width=640] [height=480]"
            << " [fps=15] [conf=0.24] [nms=0.45] [port=8561] [mount=/yolo_dma]"
            << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
#ifndef HAVE_RGA
  (void)argc;
  (void)argv;
  std::cerr << "rk_yolo_dma_rtsp_demo requires librga/HAVE_RGA at build time"
            << std::endl;
  return 2;
#else
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);
  gst_init(&argc, &argv);

  const std::string model_path = argv[1];
  const std::string device = (argc > 2) ? argv[2] : "/dev/video48";
  const int width = (argc > 3) ? std::stoi(argv[3]) : 640;
  const int height = (argc > 4) ? std::stoi(argv[4]) : 480;
  const int fps = (argc > 5) ? std::stoi(argv[5]) : 15;
  const float conf = (argc > 6) ? std::stof(argv[6]) : 0.24f;
  const float nms = (argc > 7) ? std::stof(argv[7]) : 0.45f;
  const int port = (argc > 8) ? std::stoi(argv[8]) : 8561;
  const std::string mount = (argc > 9) ? argv[9] : "/yolo_dma";

  setenv("RK_YOLO_ZERO_COPY_INPUT", "1", 1);
  setenv("RK_YOLO_RGA_LETTERBOX", "1", 0);

  YoloRknnDetector detector;
  if (!detector.Load(model_path)) {
    return 1;
  }
  if (!detector.zero_copy_input_enabled()) {
    std::cerr << "zero-copy input memory was not created; abort DMA RTSP demo"
              << std::endl;
    return 1;
  }

  int fd = -1;
  int actual_width = 0;
  int actual_height = 0;
  std::vector<V4l2Buffer> buffers;
  if (!OpenCamera(device, width, height, fps, &fd, &actual_width, &actual_height, &buffers)) {
    return 1;
  }
  bool streaming = true;

  RtspPublisher publisher(actual_width, actual_height, fps, port, mount);
  if (!publisher.Start()) {
    std::cerr << "failed to start RTSP publisher" << std::endl;
    CleanupCamera(fd, &buffers, streaming);
    return 1;
  }

  std::cout << "aggressive experimental path enabled" << std::endl;
  std::cout << "path=V4L2 YUYV DMA fd -> RGA letterbox -> RKNN input memory -> NPU"
            << std::endl;
  std::cout << "display path=YUYV mmap -> BGR overlay -> RTSP appsrc" << std::endl;
  std::cout << "rtsp path=rtsp://<board-ip>:" << port << mount << std::endl;
  std::cout << "waiting for RTSP client connection..." << std::endl;

  int frame_count = 0;
  int detected_frames = 0;
  std::size_t total_detections = 0;
  double prepare_sum = 0.0;
  double run_sum = 0.0;
  double total_sum = 0.0;
  auto last_client_notice = std::chrono::steady_clock::now();
  const auto wall_start = std::chrono::steady_clock::now();

  while (!g_stop_requested.load()) {
    if (!publisher.HasClient()) {
      const auto now = std::chrono::steady_clock::now();
      if (now - last_client_notice > std::chrono::seconds(5)) {
        std::cout << "still waiting for RTSP client..." << std::endl;
        last_client_notice = now;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }

    pollfd pfd = {};
    pfd.fd = fd;
    pfd.events = POLLIN;
    const int poll_ret = poll(&pfd, 1, 2000);
    if (poll_ret <= 0) {
      std::cerr << "poll camera timeout or error" << std::endl;
      continue;
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

    if (buf.index >= buffers.size()) {
      std::cerr << "invalid V4L2 buffer index: " << buf.index << std::endl;
      break;
    }

    InferProfile profile;
    YoloRknnDetector::LetterBoxInfo letterbox;
    std::vector<Detection> detections;
    if (detector.PrepareDmaFdToBoundInput(buffers[buf.index].dma_fd, actual_width,
                                          actual_height, RK_FORMAT_YUYV_422, &letterbox,
                                          &profile)) {
      detections = detector.InferBoundInput(letterbox, conf, nms, &profile);
    }

    ++frame_count;
    if (!detections.empty()) {
      ++detected_frames;
      total_detections += detections.size();
    }
    prepare_sum += profile.prepare_ms;
    run_sum += profile.run_ms;
    total_sum += profile.total_ms;

    cv::Mat yuyv(actual_height, actual_width, CV_8UC2, buffers[buf.index].start);
    cv::Mat bgr;
    cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
    DrawDetections(&bgr, detections);
    DrawAlarmOverlay(&bgr, detections);

    if (!publisher.PushFrame(bgr, static_cast<std::uint64_t>(frame_count))) {
      std::cout << "rtsp client disconnected, waiting for the next client..." << std::endl;
    }

    if (!detections.empty() || frame_count % 30 == 0) {
      std::cout << "frame=" << frame_count << " detections=" << detections.size()
                << " prepare_ms=" << profile.prepare_ms << " run_ms=" << profile.run_ms
                << " total_ms=" << profile.total_ms << std::endl;
    }

    if (!QueueBuffer(fd, static_cast<int>(buf.index))) {
      break;
    }
  }

  const auto wall_end = std::chrono::steady_clock::now();
  const double wall_s = std::chrono::duration<double>(wall_end - wall_start).count();
  std::cout << "summary frames=" << frame_count << " detected_frames=" << detected_frames
            << " total_detections=" << total_detections
            << " wall_fps=" << (wall_s > 0.0 ? frame_count / wall_s : 0.0)
            << " avg_prepare_ms=" << (frame_count > 0 ? prepare_sum / frame_count : 0.0)
            << " avg_run_ms=" << (frame_count > 0 ? run_sum / frame_count : 0.0)
            << " avg_total_ms=" << (frame_count > 0 ? total_sum / frame_count : 0.0)
            << std::endl;

  CleanupCamera(fd, &buffers, streaming);
  return 0;
#endif
}
