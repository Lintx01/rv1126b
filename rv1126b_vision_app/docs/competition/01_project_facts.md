# 项目事实清单

## 1. 项目定位

当前项目是面向桌面学习/办公场景的 RV1126B 端侧视觉健康守护系统。代码以 RV1126B 平台为主控，使用 V4L2 摄像头采集桌面前方画面，通过 RKNN 模型在本地执行手势识别、人体姿态估计和饮品/水杯检测，并在 App 状态机中组合出开始/暂停、坐姿提醒、饮水判断、定时喝水提醒、本地屏幕反馈、视频推流和 MQTT 状态同步。

系统主线是端侧 AI，不是云端识别服务；当前代码没有把云端识别作为主要能力。项目也不是单一模型 Demo，而是包含采集、预处理、三模型推理、业务状态机、ST7789/LVGL 显示、HTTP-FLV 推流、RTSP relay 和 MQTT 通信的完整链路系统。

## 2. 作品名称建议

1. RV1126B 端侧视觉健康守护系统
   - 优点：直接体现平台、端侧 AI 和健康守护主题，适合作为正式标题。
   - 缺点：桌面场景表达不够突出。
2. 基于 RV1126B 的桌面视觉健康助手
   - 优点：突出桌面场景，标题清楚，不虚。
   - 缺点：端侧 AI 和多模型协同需要在副标题或正文中补充。
3. RV1126B 多模型协同桌面健康监测系统
   - 优点：突出多模型协同和系统属性，符合当前代码事实。
   - 缺点：“健康监测”容易被误解为医疗诊断，正式文档中需限定为坐姿和饮水提醒。

## 3. 硬件组成事实

* RV1126B 开发板或平台：项目名、RKNN、RGA、MPP、RV1126B 相关 CMake 选项和代码注释均指向 RV1126B/Rockchip 平台。
* 摄像头输入：`src/main.cpp` 当前设置 `camera_device=/dev/video23`、`frame_width=640`、`frame_height=480`、`frame_channels=1`、`target_fps=25`。`src/CameraDevice.cpp` 使用 V4L2 `VIDEO_CAPTURE_MPLANE`、MMAP、NV12 格式采集，输出 `PixelFormat::NV12`。
* ST7789 显示屏：`src/main.cpp` 当前启用 `enable_display=true`、`enable_lvgl_display=true`，SPI 设备为 `/dev/spidev1.0`，分辨率 `240x240`，SPI 频率 `8000000`，GPIO 配置为 DC `128`、RESET `23`、BACKLIGHT `-1`。文档提示这些板端接线仍需实机核对。
* 网络连接：用于 HTTP-FLV 推流、RTSP relay、MQTT 发布。当前 `device_ip=192.168.137.2`，HTTP-FLV 端口 `8080`，RTSP relay 默认端口 `8554`。
* 上位机用途：通过 VLC 播放 RTSP 流、查看 HTTP-FLV 地址、订阅 MQTT topic、保存运行截图和调试截图。上位机不是 AI 推理主机。

## 4. 软件系统组成事实

| 模块 | 模块职责 | 关键输入 | 关键输出 | 对应代码文件 |
| -- | -- | -- | -- | -- |
| App 主控模块 | 启动各硬件/模型/线程，维护 Idle/Running/Stopping 状态机，组合显示、推流和 MQTT 输出 | `AppConfig`、摄像头帧、模型结果、手势事件 | `AppState`、`VisionResult`、显示事件、MQTT 消息 | `include/App.hpp`、`src/App.cpp`、`src/main.cpp` |
| CameraDevice | 打开 V4L2 摄像头，采集 NV12 帧 | `/dev/video23`、640x480、NV12 | `Frame`，格式 `PixelFormat::NV12` | `include/Interfaces.hpp`、`src/CameraDevice.cpp` |
| ImageProcessor | 裁剪整帧并 resize 到模型输入尺寸，支持 RGA/OpenCV/软件兜底，记录 `PreprocessTransform` | 原始 `Frame`、crop、目标宽高 | RGB888 模型输入帧 | `include/VideoPipeline.hpp`、`src/VideoPipeline.cpp` |
| GestureModel | 手势分类，映射 Start/Stop/Confirm/Rock | 224x224 RGB 帧 | `GestureResult` | `src/GestureModel.cpp` |
| PoseModel | YOLOv8-Pose 后处理，输出人体框和 17 个关键点 | 640x640 RGB 帧 | `PoseResult` | `src/PerceptionModules.cpp` |
| CupModel | 饮品/水杯检测，支持 COCO class-aware 和 BottleBoxesOnly 两种 profile | 640x640 RGB 帧 | `CupResult` | `include/Types.hpp`、`src/PerceptionModules.cpp` |
| DrinkDetector | 根据人体头部点和杯/瓶框距离判断饮水状态 | `PoseResult`、`CupResult` | `DrinkState` | `src/PerceptionModules.cpp` |
| DisplayDevice / LVGL / ST7789 | 本地状态界面、待机时钟和手势/提醒表情 | `DisplayFace`、`AppConfig` | ST7789 屏幕刷新 | `include/Interfaces.hpp`、`src/DisplayDevice.cpp` |
| VideoPipeline / WebStreamer / MPP | MPP H.264 编码、轻量 overlay、HTTP-FLV 输出 | 原始帧、AI 结果、H.264 包 | `http://<board_ip>:8080/live.flv` | `include/VideoPipeline.hpp`、`src/VideoPipeline.cpp`、`src/WebStreamer.cpp`、`src/FlvMuxer.cpp` |
| RTSP relay | 把主程序 HTTP-FLV 转成 RTSP，供 VLC 播放 | `http://127.0.0.1:8080/live.flv` | `rtsp://<board_ip>:8554/live` | `tools/rtsp_relay.cpp` |
| MqttClient | 可选 MQTT 连接、发布状态/事件/遥测，缺少 mosquitto 时日志 fallback | `MqttMessage`、broker 配置 | MQTT topic 或日志输出 | `include/Interfaces.hpp`、`src/MqttClient.cpp`、`src/App.cpp` |
| single_image_ai_debug | 单图调试三模型输入、输出、叠图 | 图片文件、模型配置 | 调试图和终端结果 | `tools/single_image_ai_debug.cpp`、`CMakeLists.txt` |

## 5. 模型与输入输出事实

### 5.1 手势模型

* 模型路径：`model/yolov5_gesture_rv1126b.rknn`，在 `src/main.cpp` 设置。
* 输入尺寸：`224x224`。
* 输出类别数量：`GestureModel::parseOutput()` 按 15 类分类输出解析。
* 当前类别映射关系：`class_6 -> Start`，`class_5 -> Stop`，`class_10 -> Confirm`，`class_13 -> Confirm`，`class_12 -> Rock`。
* 置信度阈值：`gesture_score_threshold=0.60`。
* 防抖和触发机制：`gesture_stable_required=2`，`gesture_trigger_cooldown_ms=1500`，`gesture_require_release=true`。日志关键字包括 `[手势识别]`、`[手势稳定]`、`[手势触发]`。
* Confirm 作用：`Confirm` 会调用 `acknowledgeDrinkTimerByConfirm()`，仅在定时提醒 active 时确认并清除本次定时提醒；同时显示 ConfirmFace 和发布事件。它不作为 Running/Idle 主状态切换。
* 对应代码文件：`src/GestureModel.cpp`、`src/App.cpp`、`include/Types.hpp`、`src/main.cpp`。

### 5.2 姿态模型

* 模型路径：`model/yolov8n-pose-rv1126b-i8.rknn`。
* 输入尺寸：`640x640`。
* 是否输出人体框：是，`PoseResult` 包含 `person_box` 和 `person_score`。
* 是否输出 17 个关键点：是，`PoseResult` 包含 `std::array<PoseKeypoint, 17>`。
* person_score 阈值：`kPoseScoreThreshold=0.25`，用于进入人体候选；NMS IoU 为 `0.45`。
* keypoint_score 阈值：`pose_keypoint_score_threshold=0.35`。
* 支持的姿态状态：枚举为 `Unknown`、`Good`、`BadPending`、`BadAlert`；规则原因包括 `HEAD_FORWARD`、`HEAD_DOWN`、`HEAD_BACKWARD`，头前伸叠加低头时记录 `CHEST_COLLAPSE_RISK`。
* 坐姿异常判断依据：使用鼻尖、耳朵、肩膀计算 front45 指标；头前伸角 `>=35` 判头前伸，低头角 `>=15` 判低头，后仰角 `<=-15` 判后仰。
* 对应代码文件：`src/PerceptionModules.cpp`、`include/Types.hpp`、`src/main.cpp`。

### 5.3 饮品检测模型

1. COCO / class-aware 模式：

* 模型路径：`model/yolov8n_rv1126b_i8.rknn`。
* `output_mode`：`CupOutputMode::CocoClassAware`。
* `class_count`：`80`。
* `class_ids`：`39|40|41`。
* 类别含义：`39=bottle`，`40=wine_glass`，`41=cup`。
* label 规则：`bottle(class_39)`、`wine_glass(class_40)`、`cup(class_41)`。
* 说明：该模式保留 COCO 饮品容器类，不等同于 BottleBoxesOnly。

2. BottleBoxesOnly 模式：

* 模型路径：`model/bottle_rv1126b_i8.rknn`。
* `output_mode`：`CupOutputMode::BottleBoxesOnly`。
* 是否输出类别：代码按无类别框解析，不做 class_id 过滤。
* `class_id`：`-1`，不是 `0`。
* label：`bottle(box_only)`。
* 当前解析：优先识别 RKNN output attr 为 `[1,5,8400]` 的 `channel_first_1x5x8400`，channel 0-4 分别为 `cx/cy/w/h/score`，`score_source=channel4`。若 output attr 不可用，才 fallback 到 flat parser。
* 当前代码已经有 channel-first 修复逻辑；正式文档中仍建议说明以实测日志确认 `format=channel_first_1x5x8400` 后再写“已验证”。
* 对应代码文件：`include/Types.hpp`、`src/PerceptionModules.cpp`、`src/main.cpp`。

### 5.4 模型部署方式

* 是否使用 RKNN：是，`RknnModel` 封装 RKNN 加载、输入、运行、输出读取。
* 是否通过 RV1126B NPU 端侧推理：代码目标是 RKNN 端侧推理；实际是否跑到 NPU 需以上板运行日志和 SDK 环境为准。
* 是否支持 fallback：支持。没有 RKNN SDK 或模型运行失败时，手势会进入 fallback，姿态和饮品会进入 stub 无有效输出。
* CMake RKNN 开关：`RV1126B_ENABLE_RKNN`，默认 `ON`；找到 `rknn_api.h` 和 `librknnrt` 后定义 `RV1126B_HAS_RKNN=1`。
* 对应代码文件：`CMakeLists.txt`、`include/RknnModel.hpp`、`src/RknnModel.cpp`、`src/GestureModel.cpp`、`src/PerceptionModules.cpp`。

## 6. 图像采集与预处理事实

* 摄像头采集格式：V4L2 MPLANE/MMAP，强制请求 `V4L2_PIX_FMT_NV12`，输出 `PixelFormat::NV12`。
* 摄像头分辨率：`640x480`。
* 模型输入尺寸：手势 `224x224`，姿态 `640x640`，饮品 `640x640`。
* 是否使用 RGA：代码支持 RGA，CMake 开关 `RV1126B_ENABLE_RGA` 默认 `ON`；运行时 `RV_PREPROCESS_MODE=rga` 时启用 RGA 且不走 OpenCV fallback。
* 是否支持 OpenCV fallback：支持，CMake 开关 `RV1126B_ENABLE_OPENCV` 默认 `ON`；当前 `src/main.cpp` 未设置环境变量时默认 `preprocess_mode=opencv(default)`，即 `use_rga_preprocess=false`、`fallback_to_opencv=true`。
* 预处理是否记录 transform：是，`Frame::transform` 记录原图尺寸、crop 区域和模型尺寸，用于 Pose/Cup 坐标反算。
* 单图工具与实时视频是否共用 ImageProcessor：`single_image_ai_debug` 使用 `ImageProcessor::cropResize()` 生成三路模型输入，和实时主程序共用预处理入口。
* 当前预处理几何：代码中 `cropResize()` 对整帧直接 resize 到模型尺寸；本清单不编造 letterbox。
* 对应代码文件：`src/CameraDevice.cpp`、`src/VideoPipeline.cpp`、`tools/single_image_ai_debug.cpp`、`include/Types.hpp`。

## 7. 状态机与业务逻辑事实

### 7.1 系统运行状态

* 状态枚举：`Idle`、`Running`、`Stopping`。
* `force_ai_running` 配置存在，由环境变量 `RV_FORCE_AI_RUNNING` 控制。
* 默认状态：未设置 `RV_FORCE_AI_RUNNING` 时初始 `Idle`，只调度手势；设置后初始 `Running`。
* Start 手势：在 Idle 时调用 `requestStartByGesture()`，进入 Running，重置 `DrinkDetector`、`AiScheduler` 和饮水定时器。
* Stop 手势：在 Running 时进入 `Stopping` 后回 `Idle`，暂停饮水定时器。
* Confirm：确认 active 定时提醒，显示确认反馈，不改变主状态。
* Rock：显示互动反馈，不改变主状态。
* 对应代码文件：`include/Types.hpp`、`src/App.cpp`、`src/main.cpp`。

### 7.2 手势稳定机制

* `gesture_stable_required=2`：同一业务手势连续识别达到次数后才触发。
* `gesture_trigger_cooldown_ms=1500`：触发后进入冷却。
* `gesture_require_release=true`：同一手势触发后，需要释放或识别到 None/其他手势才允许再次触发。
* 日志关键字：`[手势识别]`、`[手势稳定]`、`[手势触发]`。
* 作用：减少单帧误触发、连续重复触发和手势保持时的抖动。

### 7.3 坐姿状态逻辑

* Normal/Good：front45 规则可计算且无异常原因时返回 `PostureState::GOOD`。
* HEAD_DOWN：头部 pitch 角 `>=15`。
* HEAD_FORWARD：头前伸角 `>=35`。
* HEAD_BACKWARD：头部 pitch 角 `<=-15`。
* 事件输出方式：`VisionResult.bad_posture=true` 后可发布 MQTT `bad_posture` 事件，状态中 `posture_state=bad_alert`。
* 显示反馈方式：`selectDisplayFace()` 将异常姿态映射到 `BadPostureFace`。

### 7.4 饮水状态逻辑

* `DrinkState` 枚举：`Normal`、`NeedRemind`、`DrinkDetected`。
* Normal：无人、无杯/瓶、头部关键点不可用，或杯/瓶接近头部但连续命中次数未达标。
* NeedRemind：有人且有杯/瓶，但杯/瓶到头部的归一化距离大于阈值；或定时提醒 active。
* DrinkDetected：杯/瓶中心到头部点归一化距离 `<=0.40` 且连续命中 `3` 次。
* 视觉判断依据：优先鼻尖作为头部点，鼻尖不可见时用左右耳平均点；距离除以人体框对角线归一化。
* timer reminder：存在，`drink_timer_reminder_enabled=true`。
* timer interval 默认值：`30 * 60 * 1000` ms。
* repeat interval 默认值：`5 * 60 * 1000` ms。
* Confirm 是否可确认定时提醒：可以，但只有 `drink_timer_active_` 为 true 时才清除本次定时提醒。
* 对应代码文件：`src/PerceptionModules.cpp`、`src/App.cpp`、`include/Types.hpp`、`src/main.cpp`。

## 8. 本地显示事实

* 显示屏类型：ST7789，Linux `spidev` + GPIO 控制。
* 分辨率：`240x240`。
* 是否使用 LVGL：当前 `src/main.cpp` 设置 `enable_lvgl_display=true`，CMake 需 `RV1126B_ENABLE_LVGL=ON` 才编入 LVGL 后端。
* 界面/表情状态枚举：`IdleClock`、`NormalFace`、`BadPostureFace`、`DrinkRemindFace`、`DrinkOkFace`、`GestureOkFace`、`StartFace`、`StopFace`、`ConfirmFace`、`RockFace`、`SleepFace`、`ErrorFace`。
* IdleClock：待机时钟页，显示 Happy Life、北京时间和 Waiting Gesture。
* NormalFace：Running/Monitoring 状态。
* StartFace / StopFace / ConfirmFace / RockFace：手势触发后的临时反馈。
* DrinkRemindFace / DrinkOkFace / BadPostureFace：由饮水状态或坐姿状态映射。
* transient 表情回退：存在。Start/Stop 默认 3 秒，Confirm 默认 3 秒，Rock 默认 20 秒，之后回当前主状态对应基础表情。
* 对应代码文件：`include/Types.hpp`、`include/Interfaces.hpp`、`src/DisplayDevice.cpp`、`src/App.cpp`、`src/main.cpp`。

## 9. 视频推流事实

* 是否使用 MPP 编码：代码支持 MPP H.264 编码，`src/main.cpp` 当前设置 `enable_mpp_encoder=true`；CMake 需找到 MPP 头文件和库才定义 `RV1126B_HAS_MPP=1`。
* 是否输出 HTTP-FLV：是。主程序设置 `web_stream_protocol=HttpFlv`，`enable_web_stream=true`，由 `WebStreamer` 输出。
* HTTP-FLV 默认端口和地址：端口 `8080`，路径 `/live.flv`，地址格式 `http://<board_ip>:8080/live.flv`。
* 是否有 RTSP relay：有，`tools/rtsp_relay.cpp`。它是单独可执行程序，不是主程序内置 RTSP server。
* RTSP 默认端口和路径：端口 `8554`，mount `/live`。
* VLC 播放地址格式：`rtsp://<board_ip>:8554/live`。
* 运行方式：先启动主程序产生 HTTP-FLV，再启动 `rv1126b_rtsp_relay --input http://127.0.0.1:8080/live.flv --port 8554 --mount /live`。
* CMake 开关：`RV1126B_ENABLE_MPP`、`RV1126B_ENABLE_GSTREAMER_RTSP`。
* 对应代码文件：`CMakeLists.txt`、`src/VideoPipeline.cpp`、`src/WebStreamer.cpp`、`src/FlvMuxer.cpp`、`tools/rtsp_relay.cpp`。

## 10. MQTT 通信事实

* MQTT 是否为可选功能：是，受 CMake `RV1126B_ENABLE_MQTT` 和运行时 `config.enable_mqtt` 共同影响。
* CMake 开关：`RV1126B_ENABLE_MQTT`，默认 `ON`。
* 是否使用 libmosquitto：找到 `mosquitto.h` 和 `libmosquitto` 时定义 `RV1126B_HAS_MOSQUITTO=1` 并使用真实发布路径。
* 是否存在 log fallback：存在，未编入 mosquitto 时 `MqttClient` 打印 topic/payload 日志。
* 当前 `AppConfig` 默认 host/port：结构体默认 `127.0.0.1:1883`、`enable_mqtt=false`。
* 当前主程序实际 host/port：`src/main.cpp` 设置 `enable_mqtt=true`、`mqtt_host=192.168.137.1`、`mqtt_port=1884`、`mqtt_base_topic=rv1126b`。
* 环境变量是否支持：当前 grep 只发现 `RV_PREPROCESS_MODE` 和 `RV_FORCE_AI_RUNNING`，未发现 MQTT host/port 环境变量读取。
* 发布 topic：`rv1126b/status`、`rv1126b/event`、`rv1126b/telemetry`。
* status 内容：`device`、`state`、`posture`、`drink`、`gesture`、`timestamp`。
* event 内容：`event`、`message`、`timestamp`；事件包括 `gesture_start`、`gesture_stop`、`gesture_confirm`、`gesture_rock`、`bad_posture`、`drink_remind`、`drink_timer_remind`。
* telemetry 内容：代码当前固定组包 `fps=0`、`ai_fps=0`、`encoder`、`stream=rtsp`、`http_flv_port`、`rtsp_port`。这些不是实测性能指标。
* 待核对：`docs/current_capabilities.md` 仍写主程序默认 `enable_mqtt=false`，与当前 `src/main.cpp` 不一致。
* 对应代码文件：`CMakeLists.txt`、`include/Types.hpp`、`src/MqttClient.cpp`、`src/App.cpp`、`src/main.cpp`。

## 11. 编译与运行事实

* 全功能构建选项可包含：`-DRV1126B_ENABLE_LVGL=ON`、`-DRV1126B_ENABLE_RGA=ON`、`-DRV1126B_ENABLE_RKNN=ON`、`-DRV1126B_ENABLE_OPENCV=ON`、`-DRV1126B_ENABLE_MPP=ON`、`-DRV1126B_ENABLE_MQTT=ON`、`-DRV1126B_ENABLE_GSTREAMER_RTSP=ON`。
* 文档中出现过 `build-rtsp` 和 `build-full` 相关说法；当前代码层面只确认 CMake target 和选项，具体构建目录是否存在需现场构建确认。
* CMake 开关：`RV1126B_ENABLE_RGA`、`RV1126B_ENABLE_OPENCV`、`RV1126B_ENABLE_RKNN`、`RV1126B_ENABLE_MQTT`、`RV1126B_ENABLE_MPP`、`RV1126B_ENABLE_GSTREAMER_RTSP`、`RV1126B_ENABLE_LVGL`。
* 主程序运行方式：`./build-rtsp/rv1126b_vision_app` 或对应构建目录下的 `rv1126b_vision_app`；可用 `RV_FORCE_AI_RUNNING=1` 强制进入 Running。
* RTSP relay 运行方式：`./build-rtsp/rv1126b_rtsp_relay --input http://127.0.0.1:8080/live.flv --port 8554 --mount /live`。
* display-only：`./rv1126b_vision_app --display-only` 只测 ST7789/LVGL 待机页，不跑摄像头、AI 或推流。
* 常见资源占用问题：摄像头 `/dev/video23` 可能被旧主程序占用；RTSP `8554` 端口可能被旧 relay 占用。
* 不应把临时调试命令、fallback 模拟输出、telemetry 中固定 `fps=0` 当作正式产品性能。

## 12. 当前已实现功能清单

| 功能 | 当前状态 | 证据文件/代码位置 | 文档中是否可写 |
| -- | ---- | --------- | ------- |
| 摄像头采集 | 代码已实现，待实机截图/日志补材料 | `src/CameraDevice.cpp`、`src/main.cpp` | 可写代码实现；实测效果待补 |
| 手势 Start/Stop | 代码已实现，含稳定计数和冷却 | `src/GestureModel.cpp`、`src/App.cpp` | 可写 |
| 姿态识别 | 代码已实现 YOLOv8-Pose 后处理，待结果材料补实测 | `src/PerceptionModules.cpp` | 可写代码实现；效果待补 |
| 坐姿提醒 | 代码已实现规则判断和事件/显示映射 | `src/PerceptionModules.cpp`、`src/App.cpp` | 可写逻辑；准确性待补 |
| 饮品检测 | 代码已实现，当前主程序为 BottleBoxesOnly profile | `include/Types.hpp`、`src/PerceptionModules.cpp`、`src/main.cpp` | 可写，需区分两种 profile |
| 视觉饮水判断 | 代码已实现归一化距离和连续命中 | `src/PerceptionModules.cpp` | 可写逻辑；实测待补 |
| 定时饮水提醒 | 代码已实现 30 分钟首次、5 分钟重复、Confirm 确认 | `src/App.cpp`、`src/main.cpp` | 可写 |
| ST7789显示 | 代码已实现 LVGL/ST7789 路径，接线和实机显示待截图 | `src/DisplayDevice.cpp`、`src/main.cpp` | 可写代码实现；照片待补 |
| HTTP-FLV推流 | 代码已实现，主程序输出 `/live.flv` | `src/WebStreamer.cpp`、`src/FlvMuxer.cpp` | 可写 |
| RTSP relay | 代码已实现，CMake 开关打开且依赖满足时生成 | `tools/rtsp_relay.cpp`、`CMakeLists.txt` | 可写；构建产物待核对 |
| MQTT状态同步 | 代码已实现，当前主程序设置启用；broker 联通待截图 | `src/MqttClient.cpp`、`src/App.cpp`、`src/main.cpp` | 可写可选能力；默认状态需说明当前代码 |
| 单图AI调试工具 | 代码和 CMake target 已存在 | `tools/single_image_ai_debug.cpp`、`tools/gesture_image_test.cpp`、`CMakeLists.txt` | 可写调试工具，不写成产品核心能力 |

## 13. 当前不能乱写的内容

* 准确率、召回率、误报率。
* 实测 FPS、AI FPS、端到端延迟。
* 功耗、电流、温度。
* 长时间稳定运行时长。
* 模型大小、NPU 占用、CPU 占用、内存占用，除非后续有明确测试记录。
* 具体健康改善效果。
* 医疗诊断、疾病预防或治疗效果。
* 第三部分实物结果和性能参数正文。
* “MQTT 默认关闭”或“MQTT 默认开启”这类结论需按当前 `main.cpp` 和实际演示配置核对后写。
* BottleBoxesOnly 不能写成 `class_id=0` 单类别模型。
* 不能把 COCO 饮品模型和 BottleBoxesOnly 模型混写。

## 14. 第三部分待补充材料清单

第三部分暂不写正文，只列材料需求。

### 3.1 整体介绍需要补充

* 整体正面照片。
* 斜 45° 全局照片。

### 3.2 工程成果需要补充

* 机械/桌面摆放照片。
* 硬件连接照片。
* ST7789界面照片。
* VLC推流截图。
* MQTT订阅截图。
* 主程序运行截图。
* 单图调试截图。

### 3.3 特性成果需要补充

* 功能验证表。
* 性能参数表。
* 现场测试照片。
* 如果有实测 FPS / 延迟 / 稳定运行时间，再补充。

## 15. 建议用于正式文档的技术主线

本项目以 RV1126B 为端侧主控，围绕桌面学习/办公场景构建视觉健康守护链路：本地采集摄像头画面，经 RGA/OpenCV 预处理后运行手势、姿态和饮品检测模型，通过状态机完成启停、坐姿提醒、饮水判断和定时提醒，并结合 ST7789 本地显示、HTTP-FLV/RTSP 视频输出与 MQTT 状态同步形成闭环。

## 16. 发现的问题与待核对项

* `src/main.cpp` 当前默认 cup profile 是 `BottleBoxesOnly`；`include/Types.hpp` 结构体默认值仍是 `Coco`，但主程序会覆盖并调用 `applyCupModelProfile()`。
* BottleBoxesOnly decoder 已有 `[1,5,8400]` channel-first 解析；正式写“已验证”前需要保留板端日志或单图工具输出证据。
* MQTT：`src/main.cpp` 当前 `enable_mqtt=true`、`192.168.137.1:1884`，而 `docs/current_capabilities.md` 仍写默认关闭，文档待更新或演示配置待确认。
* MQTT 环境变量：当前代码未发现 host/port 环境变量读取，不能写“支持通过环境变量配置 MQTT”。
* RTSP relay target：`CMakeLists.txt` 只有 `RV1126B_ENABLE_GSTREAMER_RTSP=ON` 且依赖满足时才生成 `rv1126b_rtsp_relay`，当前 build 目录是否已有产物待核对。
* `docs/current_capabilities.md` 与当前 `main.cpp` 在 MQTT 默认状态上不一致。
* ST7789 设备节点、GPIO 编号和 8 MHz SPI 配置需要实机接线确认。
* 第三部分图片、实测性能、功能验证表均待结果材料同学补充。
