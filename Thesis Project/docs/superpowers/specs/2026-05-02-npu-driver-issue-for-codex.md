# RK3588 板端 NPU 驱动故障诊断报告

> 日期：2026-05-02
> 目的：提供给 Codex 进行诊断和修复

## 问题描述

板端 NPU 在 `rknn_run()` 调用时触发段错误（SIGSEGV），无法执行任何模型推理。

## 时间线

- **2026-04-28**：NPU 正常工作，10 分钟 RTSP 稳定性测试成功运行（4100 次 NPU 推理全部成功）
- **2026-05-01**：两个二进制文件被重新编译（rk_yolo_video、rk_yolo_live_rtsp）
- **2026-05-02**：板子重启后，NPU 驱动初始化异常，`rknn_run()` 段错误

## 环境信息

```
板端地址：192.168.2.156（SSH, ubuntu 用户, 密码: ubuntu）
内核：Linux 5.10.226 #20 SMP Fri May 16 15:40:12 CST 2025 (aarch64)
NPU 驱动：rknpu 0.9.8 (20240828) — 内核内置（builtin），非模块
RKNN Runtime：librknnrt.so 2.3.0 (c949ad889d@2024-11-07T11:35:33)
RGA：librga.so.2.1.0
CPU：RK3588 8核 (4×A76 + 4×A55)
内存：8GB
OS：Ubuntu 20.04 LTS (aarch64)
```

## 故障现象

### 1. 内核启动时报错（dmesg）

```
[    2.443598] RKNPU fdab0000.npu: RKNPU: rknpu iommu is enabled, using iommu mode
[    2.443871] RKNPU fdab0000.npu: can't request region for resource [mem 0xfdab0000-0xfdabffff]
[    2.443892] RKNPU fdab0000.npu: can't request region for resource [mem 0xfdac0000-0xfdacffff]
[    2.443908] RKNPU fdab0000.npu: can't request region for resource [mem 0xfdad0000-0xfdadffff]
[    2.444459] [drm] Initialized rknpu 0.9.8 20240828 for fdab0000.npu on minor 1
[    2.445346] RKNPU fdab0000.npu: RKNPU: bin=0
[    2.445528] RKNPU fdab0000.npu: leakage=8
[    2.447277] RKNPU fdab0000.npu: pvtm=886
[    2.447764] RKNPU fdab0000.npu: pvtm-volt-sel=4
[    2.448832] RKNPU fdab0000.npu: rockchip_pvtpll_set_volt_sel: error cfg clk_id=6 voltsel (-1)
[    2.449021] RKNPU fdab0000.npu: failed to find power_model node
[    2.449029] RKNPU fdab0000.npu: RKNPU: failed to initialize power model
[    2.449034] RKNPU fdab0000.npu: RKNPU: failed to get dynamic-coefficient
```

**关键错误**：
- `can't request region for resource [mem 0xfdab0000-0xfdabffff]`（三个 NPU MMIO 区域全部请求失败）
- `failed to find power_model node`（设备树缺少 power_model 节点）
- `rockchip_pvtpll_set_volt_sel: error cfg clk_id=6 voltsel (-1)`（电压选择失败）

### 2. /proc/iomem 中 NPU 区域缺失

NPU 的三个 MMIO 区域（fdab0000、fdac0000、fdad0000）**不在 /proc/iomem 中**，只有 IOMMU 寄存器存在：

```
fdab9000-fdab90ff : fdab9000.iommu iommu@fdab9000
fdaba000-fdaba0ff : fdab9000.iommu iommu@fdab9000
fdaca000-fdaca0ff : fdab9000.iommu iommu@fdab9000
fdada000-fdada0ff : fdab9000.iommu iommu@fdab9000
```

### 3. 用户空间测试结果

用以下程序在板端直接测试：

```cpp
// 1. rknn_init → 成功 (ret=0)
// 2. rknn_query(RKNN_QUERY_IN_OUT_NUM) → 成功 (n_input=1, n_output=1)
// 3. rknn_inputs_set → 成功 (ret=0)
// 4. rknn_run → 段错误 (SIGSEGV)
```

**rknn_init 和 rknn_query 正常**（这两个只操作用户空间内存），**rknn_run 崩溃**（这个才真正访问 NPU 硬件寄存器）。

### 4. 已尝试的修复措施

| 操作 | 命令 | 结果 |
|------|------|------|
| NPU 软复位 | `echo 1 > /sys/kernel/debug/rknpu/reset` | 显示 "soft reset, num: N"，但 rknn_run 仍然段错误 |
| NPU 上电 | `echo on > /sys/kernel/debug/rknpu/power` | power 显示 on，但 rknn_run 仍然段错误 |
| 设置频率 | `echo 1000000000 > /sys/kernel/debug/rknpu/freq` | freq 显示 1000000000，但 rknn_run 仍然段错误 |
| 板子物理重启 | 拔电重插 | 驱动错误依旧，rknn_run 仍然段错误 |

### 5. DRM 设备映射

```
/dev/dri/card0 + renderD128 → rockchip-drm（GPU/显示）
/dev/dri/card1 + renderD129 → RKNPU（NPU）
```

两个设备均有 ACL 授权 `user:ubuntu:rw-`，权限无问题。

## 重要线索

### 之前是正常的

**2026-04-28 的 10 分钟稳定性测试完整日志**证明 NPU 当时工作正常：

```
文件位置：/home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp/artifacts/stability_10min_20260428_195508_with_resource/
- live.log：666 行，包含完整的帧级性能数据
- process_samples.csv：118 个采样点（每 5 秒），内存/CPU 数据
- computed_summary.txt：汇总指标
- 总运行 597 秒，4100 次 NPU 推理，0 崩溃
```

### 内核没有变化

当前内核 `5.10.226 #20 SMP Fri May 16 15:40:12 CST 2025` 是 2025 年 5 月编译的，4 月 28 日测试和 5 月 2 日测试用的是同一个内核。**内核本身没有更新。**

### 二进制文件被重编译

```
rk_yolo_video：    2026-05-01 10:24（4月28日测试之后重编译）
rk_yolo_live_rtsp：2026-05-02 00:02（4月28日测试之后重编译）
```

但二进制重编译不应该影响 NPU 驱动行为。且在板端直接编译的最小测试程序也复现了同样的段错误，说明问题在内核层面。

## 可能的原因

1. **设备树缺失 power_model 节点**：NPU 驱动报 `failed to find power_model node`，可能导致 NPU 初始化不完整
2. **MMIO 内存区域映射失败**：`can't request region` 意味着 NPU 寄存器空间未被驱动独占映射，可能被其他驱动占用或设备树未正确声明
3. **硬件版本/设备树不匹配**：当前内核可能不是板子原厂配套的，NPU 设备树节点可能缺少必要的资源声明
4. **RKNN Runtime 与 NPU 驱动版本兼容性**：Runtime 2.3.0 + Driver 0.9.8 的组合需要确认是否兼容

## 建议排查方向

1. **检查设备树中 NPU 节点配置**：
   ```
   cat /proc/device-tree/npu@fdab0000/status
   ls /proc/device-tree/npu@fdab0000/
   ```
   确认是否缺少 `reg`、`power-model`、`clocks` 等必要属性

2. **对比 4 月 28 日能正常工作时的内核状态**：
   - 4 月 28 日和 5 月 2 日用的是同一个内核（没有更新）
   - 但 NPU 的行为不同，可能与启动时的设备初始化顺序有关

3. **尝试更新 RKNN Runtime**：
   - 当前版本 2.3.0，可以尝试升级到更新版本看是否兼容

4. **尝试重新编译内核模块**：
   - 如果设备树中确实缺少 NPU 的资源声明，需要修改设备树并重新编译内核

5. **联系板子厂商**：
   - 内核编译时间是 2025-05-16，可能不是官方稳定版本
   - 需要确认是否有更新版本的 BSP（Board Support Package）

## 文件路径（板端）

```
测试程序源码（在板端编译的最小复现程序）：
  /home/ubuntu/stability_test_20260502/debug_test.cpp

4月28日正常工作的稳定性测试数据：
  /home/ubuntu/eclipse-workspace/eclipse-workspace/rk_yolo_live_rtsp/artifacts/stability_10min_20260428_195508_with_resource/

RKNN 头文件（板端）：
  /home/ubuntu/eclipse-workspace/eclipse-workspace/encoder/include/rknn_api.h

RKNN Runtime 库：
  /usr/lib/librknnrt.so (7,259,064 bytes, 2024-11-15)
  /lib/librknnrt.so (同文件，md5: 56e425dcc7b59ab0e1aec685b178b99f)

NPU 驱动 debug 信息：
  /sys/kernel/debug/rknpu/ (freq, volt, power, load, reset, version, delayms)
```
