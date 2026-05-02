# 2026-05-02 Codex NPU 驱动问题复核记录

## 结论

Claude 报告中“`rknn_run()` 在某模型上段错误”的现象属实，但“板子 NPU 驱动整体故障”的判断不成立。

本次复核表明：

- RK3588 板端 NPU 驱动可以正常完成 `rknn_run()`。
- 崩溃集中发生在 end2end 输出格式为 `[1,300,6]` 的 RKNN 模型上。
- 项目当前主线使用的非 end2end 模型 `[1,5,8400]` 可以正常推理，实际 `rk_yolo_video` 固定视频验证通过。

因此，当前问题应归类为：

> 特定 end2end RKNN 模型与当前 RKNN Runtime / RKNPU driver 组合存在兼容性问题，而不是 NPU 驱动整体不可用。

## 板端环境

- 板端 IP：`192.168.2.156`
- 用户：`ubuntu`
- 内核：`Linux 5.10.226 #20 SMP Fri May 16 15:40:12 CST 2025 aarch64`
- NPU 驱动：`RKNPU driver: v0.9.8`
- RKNN Runtime：`/lib/librknnrt.so`，md5 `56e425dcc7b59ab0e1aec685b178b99f`
- NPU debugfs：
  - `power=on`
  - `freq=1000000000`
  - `load` 可读

## 设备树与启动日志

设备树中 NPU 节点存在并启用：

```text
/proc/device-tree/npu@fdab0000/status = okay
compatible = rockchip,rk3588-rknpu
```

启动日志中仍存在以下信息：

```text
can't request region for resource [mem 0xfdab0000-0xfdabffff]
failed to find power_model node
failed to initialize power model
```

这些信息需要记录，但不能单独证明 NPU 不可用。因为后续实测表明部分 RKNN 模型可以正常完成 NPU 推理。

## 最小 probe 结果

新增诊断程序：

```text
Thesis Project/tools/diagnostics/rknn_minimal_probe.cpp
```

该程序会：

1. 读取 RKNN 文件到内存；
2. `rknn_init`；
3. 查询 input/output tensor 属性；
4. 按真实 tensor 属性分配输入 buffer；
5. 执行 `rknn_inputs_set`、`rknn_run`、`rknn_outputs_get`。

### 可正常运行的模型

#### 项目主线 FP 模型

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.end2end_false.op12.rk3588.fp.v220.rknn
```

结果：

```text
INPUT[0] dims=1x640x640x3 fmt=NHWC type=float16
OUTPUT[0] dims=1x5x8400 type=float16
RUN ret=0
OUTPUTS_GET ret=0
DONE
EXIT_CODE=0
```

#### full INT8 模型

```text
best.end2end_false.op12.rk3588.int8.v220.rknn
```

结果：

```text
INPUT[0] type=int8
OUTPUT[0] dims=1x5x8400 type=int8
RUN ret=0
OUTPUTS_GET ret=0
EXIT_CODE=0
```

#### hybrid INT8 模型

```text
best.end2end_false.op12.rk3588.int8.hybrid_sigmoid500.v220.rknn
```

结果：

```text
INPUT[0] type=int8
OUTPUT[0] dims=1x5x8400 type=float16
RUN ret=0
OUTPUTS_GET ret=0
EXIT_CODE=0
```

#### 官方 YOLOv10n 模型

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/yolov10n.rknn
```

结果：

```text
OUTPUT[0] dims=1x84x8400 type=float16
RUN ret=0
OUTPUTS_GET ret=0
EXIT_CODE=0
```

### 会崩溃的模型

#### end2end FP 模型

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/training_runs/drone_gpu_50e/weights/best.rk3588.fp.v220.rknn
```

结果：

```text
OUTPUT[0] dims=1x300x6 type=float16
RUN_BEGIN
EXIT_CODE=139
```

#### 512 end2end YOLOv10n 模型

```text
/home/ubuntu/eclipse-workspace/eclipse-workspace/yolov10n.512.rk3588.fp.rknn
```

结果：

```text
OUTPUT[0] dims=1x300x6 type=float16
RUN_BEGIN
EXIT_CODE=139
```

## 实际项目主线验证

使用实际 `rk_yolo_video`，指定项目当前主线模型：

```text
best.end2end_false.op12.rk3588.fp.v220.rknn
```

输入视频：

```text
/home/ubuntu/public_videos/anti_uav_fig1.mp4
```

结果：

```text
done. frames=130,
frames_with_detections=30,
total_detections=30,
alarm_events=14,
avg_infer_ms=109.70
EXIT_CODE=0
```

输出文件：

```text
/tmp/codex_npu_probe_main.mp4
/tmp/codex_npu_probe_main.csv
/tmp/codex_npu_probe_main.jsonl
/tmp/codex_npu_probe_main_alarm.csv
```

这说明项目当前稳定演示路径没有被 NPU 驱动问题阻断。

## 对 Claude 报告的修正判断

Claude 报告中正确的部分：

- `debug_test` 使用 `best.rk3588.fp.v220.rknn` 时确实在 `rknn_run()` 段错误。
- 启动日志中确实存在 NPU 相关 warning/error。

Claude 报告中需要修正的部分：

- 不能据此判断 NPU 驱动整体损坏。
- `rknn_run()` 段错误不是所有模型都会触发。
- 项目主线模型、full INT8、hybrid INT8 和官方 YOLOv10n 非 end2end 模型均可正常运行。

## 后续建议

1. 答辩和论文主线继续使用 `best.end2end_false.op12.rk3588.fp.v220.rknn`。
2. 暂时不要使用 `[1,300,6]` end2end RKNN 模型作为演示或稳定性测试模型。
3. 如果后续必须使用 end2end 模型，应单独研究：
   - RKNN Toolkit 导出版本；
   - RKNN Runtime 版本；
   - end2end/NMS-Free 输出算子兼容性；
   - 是否需要重新导出非 end2end 模型或升级 runtime/BSP。
4. 论文中仍建议采用诚实口径：当前稳定主线是非 end2end FP RKNN；INT8 和 RGA 作为硬件优化实验路径已验证。
