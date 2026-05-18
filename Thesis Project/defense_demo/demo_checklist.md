# Defense Demo Checklist

## One Day Before

- Copy the latest project folder to the RK3588 board.
- Build `rk_yolo_video` and `rk_yolo_live_rtsp` on the board.
- Put at least one public drone video under `/home/ubuntu/public_videos/`.
- Test `defense_demo/scripts/run_video_backup.sh` once and keep the output video.
- Test `defense_demo/scripts/run_live_safe.sh` with the USB camera.
- Confirm the PC can open `rtsp://<board-ip>:8554/drone`.
- Prepare the phone image/video target with high brightness and no screen timeout.

## Before Entering The Classroom

- Use wired Ethernet if possible. If not, use one phone hotspot for both PC and board.
- Use the original board power adapter.
- Fix the camera on the desk; avoid holding the camera by hand.
- Run:

  ```bash
  bash defense_demo/scripts/check_board_ready.sh
  ```

## Five-Minute Demo Plan

1. Show the live RTSP stream on the PC.
2. Hold the phone-screen drone target at a moderate distance.
3. Move the phone slowly left/right/up/down first.
4. Move it forward/backward to show the zoom response.
5. Point out the green detection box and red `UAV ALERT` banner.
6. If live detection becomes unstable, switch to the fixed-video backup.
7. If asked about the best hardware path, explain route B as a technical backup
   after the stable demo, not as the first live path.

## Do Not Enable First

- Do not enable dynamic ROI in the main demo. It can introduce crop drift when the target moves quickly.
- Do not enable multi-context in the main demo. It is better for throughput experiments than for low-latency viewing.
- Do not enable RGA or zero-copy first. They are experimental comparison paths, not the safest defense demo path.
- Do not enable continuous autofocus unless the image is obviously blurred. Continuous AF can cause focus breathing.
- Do not switch to route B before the stable RTSP demo has been shown. Route B
  is useful for explaining MPP/RGA/RKNN memory optimization, but the classroom
  live demo should first prove the stable end-to-end chain.

## If Something Goes Wrong

- No PC video: check board IP, RTSP URL, firewall, and whether a client has connected.
- Box jumps: confirm `RK_YOLO_DYNAMIC_ROI=0` and `RK_YOLO_BOX_SMOOTH=1`.
- Few detections: use `run_live_sensitive.sh`, increase phone brightness, and reduce motion speed.
- Camera keeps refocusing: keep `RK_YOLO_CAMERA_FOCUS_AUTO=0`; use fixed focus for the demo.
- Network fails: play the fixed-video backup output locally.
