#include "yolo_rknn.h"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#ifdef HAVE_RGA
#include <linux/videodev2.h>
#include <poll.h>
#include <rga/im2d.h>
#include <rga/rga.h>
#endif

namespace {

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
    parm.parm.capture.timeperframe.denominator = static_cast<unsigned int>(requested_fps);
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
    const int box_h = std::min(label_size.height + baseline + 6, frame->rows - box_y + label_size.height + 4);
    if (box_w > 0 && box_h > 0) {
      cv::rectangle(*frame,
                    cv::Rect(box_x, box_y - label_size.height - 4, box_w, box_h),
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
            << " <model.rknn> [device=/dev/video0] [width=640] [height=480]"
            << " [fps=30] [conf=0.24] [nms=0.45] [frames=300] [output_video]" << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
#ifndef HAVE_RGA
  (void)argc;
  (void)argv;
  std::cerr << "rk_yolo_dma_demo requires librga/HAVE_RGA at build time" << std::endl;
  return 2;
#else
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  const std::string model_path = argv[1];
  const std::string device = (argc > 2) ? argv[2] : "/dev/video0";
  const int width = (argc > 3) ? std::stoi(argv[3]) : 640;
  const int height = (argc > 4) ? std::stoi(argv[4]) : 480;
  const int fps = (argc > 5) ? std::stoi(argv[5]) : 30;
  const float conf = (argc > 6) ? std::stof(argv[6]) : 0.24f;
  const float nms = (argc > 7) ? std::stof(argv[7]) : 0.45f;
  const int max_frames = (argc > 8) ? std::stoi(argv[8]) : 300;
  const std::string output_video = (argc > 9) ? argv[9] : "";

  setenv("RK_YOLO_ZERO_COPY_INPUT", "1", 1);
  setenv("RK_YOLO_RGA_LETTERBOX", "1", 0);

  YoloRknnDetector detector;
  if (!detector.Load(model_path)) {
    return 1;
  }
  if (!detector.zero_copy_input_enabled()) {
    std::cerr << "zero-copy input memory was not created; abort DMA demo" << std::endl;
    return 1;
  }

  int fd = -1;
  int actual_width = 0;
  int actual_height = 0;
  std::vector<V4l2Buffer> buffers;
  if (!OpenCamera(device, width, height, fps, &fd, &actual_width, &actual_height, &buffers)) {
    return 1;
  }

  cv::VideoWriter writer;
  if (!output_video.empty()) {
    const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    const double writer_fps = fps > 0 ? static_cast<double>(fps) : 15.0;
    writer.open(output_video, fourcc, writer_fps, cv::Size(actual_width, actual_height));
    if (!writer.isOpened()) {
      std::cerr << "failed to open output video: " << output_video << std::endl;
      CleanupCamera(fd, &buffers, true);
      return 1;
    }
    std::cout << "output_video=" << output_video << std::endl;
  }

  bool streaming = true;
  int frame_count = 0;
  int detected_frames = 0;
  std::size_t total_detections = 0;
  double prepare_sum = 0.0;
  double run_sum = 0.0;
  double total_sum = 0.0;
  const auto wall_start = std::chrono::steady_clock::now();

  while (frame_count < max_frames) {
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

    if (buf.index >= buffers.size()) {
      std::cerr << "invalid V4L2 buffer index: " << buf.index << std::endl;
      break;
    }

    InferProfile profile;
    YoloRknnDetector::LetterBoxInfo letterbox;
    std::vector<Detection> detections;
    if (detector.PrepareDmaFdToBoundInput(buffers[buf.index].dma_fd, actual_width,
                                          actual_height, RK_FORMAT_YUYV_422,
                                          &letterbox, &profile)) {
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

    if (writer.isOpened()) {
      cv::Mat yuyv(actual_height, actual_width, CV_8UC2, buffers[buf.index].start);
      cv::Mat bgr;
      cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);
      DrawDetections(&bgr, detections);
      DrawAlarmOverlay(&bgr, detections);
      writer.write(bgr);
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
  const double wall_s =
      std::chrono::duration<double>(wall_end - wall_start).count();
  std::cout << "summary frames=" << frame_count << " detected_frames=" << detected_frames
            << " total_detections=" << total_detections
            << " wall_fps=" << (wall_s > 0.0 ? frame_count / wall_s : 0.0)
            << " avg_prepare_ms=" << (frame_count > 0 ? prepare_sum / frame_count : 0.0)
            << " avg_run_ms=" << (frame_count > 0 ? run_sum / frame_count : 0.0)
            << " avg_total_ms=" << (frame_count > 0 ? total_sum / frame_count : 0.0)
            << std::endl;
  if (writer.isOpened()) {
    writer.release();
    std::cout << "wrote output_video=" << output_video << std::endl;
  }

  CleanupCamera(fd, &buffers, streaming);
  return 0;
#endif
}
