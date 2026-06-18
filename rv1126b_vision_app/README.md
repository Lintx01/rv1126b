# RV1126B ST7789 MQTT AI 工程

这是一个 RV1126B 端侧视觉项目的 C++17 多线程工程骨架，目标硬件为 RV1126B 开发板、MIPI 摄像头和 ST7789 SPI 显示屏。

当前工程保持原有目录结构：

- `include/`：公共类型、接口和模块声明。
- `src/`：业务主控、设备封装、模型封装和传输封装。
- `docs/`：中文使用说明和 ST7789 实机检查文档。

## 当前模块状态

- `CameraDevice`：V4L2/MMAP 摄像头采集，非 Linux 环境提供 stub。
- `FramePool`：单例帧池，使用 `shared_ptr<Frame>` 管理帧生命周期，避免手动 delete。
- `BlockingQueue`：支持 `BLOCK`、`DROP_OLDEST`、`DROP_NEWEST` 三种溢出策略。
- `LatestFrameBuffer`：AI 输入只保留最新帧，允许跳帧，避免处理旧帧造成延迟。
- `GestureModel`：YOLOv5 手势模型接口，输出 `GestureResult`。
- `PoseModel`：YOLOv8-pose 模型接口，当前只保留 stub 和 TODO。
- `CupModel`：水杯检测模型接口，当前只保留 stub 和 TODO。
- `PostureAnalyzer`：姿态后处理接口，当前不实现具体姿态规则。
- `DrinkDetector`：喝水检测后处理接口，当前不实现具体距离规则。
- `PostureDrinkModel`：旧版姿态+饮水合并模型，保留为 legacy fallback。
- `DisplayDevice`：ST7789 基础驱动，`showFace(DisplayFace)` 是显示统一入口。
- `MppEncoder`：MPP H.264 编码路径；无 MPP SDK 时使用 fallback 伪码流用于自检。
- `ImageProcessor`：优先 RGA，OpenCV fallback，最后软件最近邻 fallback。
- `MqttClient`：Mosquitto MQTT 发布路径；无依赖时使用日志 fallback。
- `WebStreamer`：当前是 WebRTC 适配层，占位生成 H.264 RTP packet 和信令 JSON。

## AI 主流程

当前 AI 调度为单线程串行调度，不引入线程池：

- gesture 默认间隔：`300ms`
- pose 默认间隔：`150ms`
- cup 默认间隔：`500ms`

主链路：

```text
CameraDevice
  -> FramePool
  -> LatestFrameBuffer<FramePtr> 供 AI 取最新帧
  -> Drop-oldest encode_queue 供 encoderLoop 编码

AiScheduler
  -> GestureModel
  -> PoseModel
  -> CupModel
  -> PostureAnalyzer
  -> DrinkDetector
  -> AppState
  -> DisplayFace / MQTT / Web result
```

注意：`PostureAnalyzer` 和 `DrinkDetector` 当前只保留接口和最小状态返回。真实姿态判断、杯子到鼻子/头部归一化距离、连续帧确认、提醒冷却时间等规则，需要后续根据 RV1126B 实机摄像头角度和采样数据完善。

## 显示状态

显示队列使用 `BlockingQueue<DisplayFace>`，不再直接传 `GestureType`。

支持的显示状态：

- `NORMAL_FACE`
- `BAD_POSTURE_FACE`
- `DRINK_REMIND_FACE`
- `DRINK_OK_FACE`
- `GESTURE_OK_FACE`
- `SLEEP_FACE`
- `ERROR_FACE`

当前第一版映射：

- `GESTURE_OK_FACE` 复用 `showHeartExpression()`
- 其他表情先输出日志占位，后续再补真实 RGB565 图案

无屏调试时可设置：

```cpp
config.enable_display = false;
```

## WebRTC 状态说明

本工程选择 WebRTC 作为最终网页端视频协议方向，原因是浏览器原生支持、延迟低，适合 H.264 视频和 AI 结果同步。

当前代码不是完整 WebRTC native 发送实现。现阶段已经包含：

- H.264 Annex-B NALU 到 RTP packet 的打包逻辑
- WebRTC signaling/DataChannel JSON 的适配层日志
- AI 结果 JSON 发送适配接口

尚未实现真实浏览器播放所需的 DTLS/SRTP/ICE 发送层。后续需要接入以下任一方案：

- libdatachannel
- WebRTC native
- GStreamer `webrtcbin`

## 构建

fallback 模式不要求 RGA、OpenCV、RKNN、Mosquitto、MPP 可用：

```bash
cmake -S . -B build-fallback \
  -DRV1126B_ENABLE_RGA=OFF \
  -DRV1126B_ENABLE_OPENCV=OFF \
  -DRV1126B_ENABLE_RKNN=OFF \
  -DRV1126B_ENABLE_MQTT=OFF \
  -DRV1126B_ENABLE_MPP=OFF
cmake --build build-fallback -j
```

RV1126B 实机模式按 SDK 环境逐步打开：

```bash
cmake -S . -B build-rv1126b \
  -DRV1126B_ENABLE_RGA=ON \
  -DRV1126B_ENABLE_OPENCV=ON \
  -DRV1126B_ENABLE_RKNN=ON \
  -DRV1126B_ENABLE_MQTT=ON \
  -DRV1126B_ENABLE_MPP=ON
cmake --build build-rv1126b -j
```

## 运行

```bash
./build-fallback/rv1126b_vision_app
```

关键日志：

```text
[Camera] frame_id=...
[AI] latest frame_id=..., skipped=...
[AI] schedule gesture, frame=...
[AI] schedule pose, frame=...
[AI] schedule cup, frame=...
[AI] AppState posture=..., drink=..., display_face=...
[AI] DisplayFace pushed=..., frame=...
[Encoder] frame_id=..., skipped=...
```

AI 和 Encoder 出现跳帧是正常现象，表示系统正在优先处理实时最新帧，而不是积压旧帧。
