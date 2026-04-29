# INT8 RKNN 转换验证记录

## 验证目标

在没有 RK3588 开发板的情况下，先完成 YOLOv10 单类无人机模型的 INT8 RKNN 离线转换，使后续上板时可以直接开展 FP 与 INT8 的固定视频对比实验。

## 输入材料

| 项目 | 路径 |
| --- | --- |
| ONNX 模型 | `Thesis Project/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.onnx` |
| 校准列表 | `Thesis Project/training_runs/drone_gpu_50e/calibration/drone_calib_200_wsl_nospace.txt` |
| 校准图片数量 | 200 |
| RKNN Toolkit2 环境 | `C:/Users/Tony/Desktop/eclipse-workspace-codex/_deps/rknn-toolkit2-files/.venv_rknn` |
| RKNN Toolkit2 版本 | 2.3.2 |

## 路径问题与处理

首次转换失败，原因不是模型结构错误，而是 RKNN Toolkit2 在读取校准列表时不能正确处理项目路径 `Thesis Project` 中的空格，日志中出现：

```text
Unsupport file /mnt/c/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis!
```

处理方式是在 WSL 中创建无空格软链接：

```bash
ln -sfn "/mnt/c/Users/Tony/Desktop/eclipse-workspace-codex/eclipse-workspace/Thesis Project" /tmp/thesis_project
```

随后通过 `make_calibration_list.py --rewrite-prefix` 将校准列表路径改写为 `/tmp/thesis_project/...`，避免 RKNN Toolkit2 解析路径时被空格截断。

## 转换命令

```bash
cd /tmp/thesis_project
source "/mnt/c/Users/Tony/Desktop/eclipse-workspace-codex/_deps/rknn-toolkit2-files/.venv_rknn/bin/activate"
python tools/int8_rga/convert_onnx_to_rknn.py \
  --onnx training_runs/drone_gpu_50e/weights/best.end2end_false.op12.onnx \
  --output training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.v220.rknn \
  --dtype int8 \
  --dataset training_runs/drone_gpu_50e/calibration/drone_calib_200_wsl_nospace.txt
```

## 转换结果

| 项目 | 结果 |
| --- | --- |
| 转换状态 | 成功 |
| 输出模型 | `Thesis Project/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.int8.v220.rknn` |
| 模型大小 | 4,372,047 bytes |
| FP v220 模型大小 | 6,784,245 bytes |
| 大小变化 | INT8 模型约为 FP 模型的 64.4% |

RKNN Toolkit2 日志提示：

```text
The default input dtype of 'images' is changed from 'float32' to 'int8' in rknn model for performance.
The default output dtype of 'output0' is changed from 'float32' to 'int8' in rknn model for performance.
```

现有 `rk_yolo_video` 后处理路径在 `rknn_outputs_get` 前设置了 `want_float = 1`，因此 Runtime 会请求输出转为 float，后处理逻辑理论上可以继续兼容当前解码代码。最终仍需在 RK3588 开发板上做加载和固定视频验证。

## 后续验证计划

下一次连接开发板后，执行 `2026-04-29-int8-rga-board-command-matrix.md` 中的命令，重点比较：

1. FP OpenCV baseline 与 INT8 OpenCV baseline 的检测稳定性；
2. FP 与 INT8 在 `rknn_run_ms` 上的差异；
3. RGA cvt+resize、RGA letterbox 与 OpenCV 预处理在 `prepare_ms` 上的差异；
4. 输出视频中误检、漏检和检测框位置是否出现明显退化。

## 论文表述边界

当前可以如实写为：已完成 INT8 RKNN 模型离线量化转换，并建立 FP/INT8/RGA 固定视频对比实验流程。由于暂未完成板端实测，不能直接写成“INT8 已提升实时性能”或“INT8 已成为最终部署模型”。
