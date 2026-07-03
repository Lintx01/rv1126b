# 第二部分  系统组成及功能说明

## 2.1 整体介绍

本系统面向桌面学习和办公场景，由 RV1126B 主控平台、摄像头输入、端侧 AI 推理、业务状态融合、本地显示、视频推流和 MQTT 通信模块组成。系统的核心数据流从摄像头采集开始，CameraDevice 通过 V4L2 读取 `/dev/video23` 的 640x480 NV12 视频帧，ImageProcessor 将原始帧按模型输入尺寸进行 resize 预处理，并记录坐标映射关系。预处理后的图像分别送入 GestureModel、PoseModel 和 CupModel，完成手势识别、人体姿态估计和饮品检测。

控制流由 App 主控模块统一调度。系统处于 Idle 状态时主要运行手势识别，用于等待 Start 手势；进入 Running 状态后，姿态模型、饮品检测模型和饮水状态判断模块协同工作。App 将手势事件、坐姿状态、饮水状态和定时提醒结果融合为统一的 AppState 与 VisionResult，再分发到 ST7789/LVGL 本地屏幕、HTTP-FLV 视频输出、独立 RTSP relay 以及 MQTT 状态同步链路。该系统不是单一模型演示，而是包含采集、预处理、RKNN 推理、状态机判断和多端输出的端侧视觉健康守护系统。

图2-1 系统整体框图如下。

```mermaid
flowchart LR
    Camera["摄像头 /dev/video23<br/>640x480 NV12"] --> Capture["CameraDevice<br/>图像采集"]
    Capture --> Preprocess["ImageProcessor<br/>RGA/OpenCV预处理"]
    Preprocess --> Gesture["GestureModel<br/>手势识别"]
    Preprocess --> Pose["PoseModel<br/>姿态估计"]
    Preprocess --> Cup["CupModel<br/>饮品检测"]
    Gesture --> App["App状态融合与事件判断"]
    Pose --> App
    Cup --> Drink["DrinkDetector<br/>饮水状态判断"]
    Drink --> App
    App --> Display["ST7789/LVGL<br/>本地显示"]
    App --> Video["MPP/WebStreamer<br/>HTTP-FLV"]
    Video --> Rtsp["RTSP relay<br/>rtsp://板卡IP:8554/live"]
    App --> Mqtt["MQTT<br/>status/event/telemetry"]
    Rtsp --> PC["上位机 VLC"]
    Mqtt --> Sub["上位机 MQTT订阅/调试"]
```

## 2.2 硬件系统介绍

### 2.2.1 硬件整体介绍

硬件系统以 RV1126B 平台为主控，负责摄像头采集、图像预处理、RKNN 模型推理、业务逻辑判断以及显示和网络输出。摄像头作为主要视觉输入，当前代码配置的设备节点为 `/dev/video23`，输入分辨率为 640x480，图像格式为 NV12。ST7789 显示屏用于本地状态反馈，当前配置分辨率为 240x240，并通过 LVGL 绘制待机、运行、手势反馈、坐姿提醒和饮水提醒等界面状态。网络连接用于 HTTP-FLV 视频访问、RTSP relay 播放链路以及 MQTT 状态同步。上位机主要承担 VLC 播放、MQTT 订阅、运行日志查看、截图整理和调试验证等工作，不承担 AI 推理。

图2-2 硬件系统组成图建议后续使用实物连接照片或连接示意图补充。

表2-1 硬件组成表

| 硬件模块 | 作用 | 关键参数/接口 | 备注 |
| ---- | -- | ------- | -- |
| RV1126B 平台 | 系统主控，负责采集、端侧推理、状态判断和多端输出 | RKNN、RGA、MPP、网络、SPI/GPIO 等平台能力 | 具体开发板型号以实物材料为准 |
| 摄像头模块 | 获取用户上半身和桌面饮品区域画面 | `/dev/video23`，640x480，NV12 | 代码使用 V4L2 MPLANE/MMAP 采集 |
| ST7789 显示屏 | 本地显示待机、运行、手势和提醒状态 | 240x240，当前配置 `/dev/spidev1.0` | DC=128、RESET=23、背光=-1 需结合实机接线核对 |
| 网络连接 | 视频访问、RTSP relay 和 MQTT 通信 | HTTP-FLV 8080，RTSP relay 8554，MQTT 可配置 | 真实 MQTT 发布需要 broker 和 libmosquitto 支持 |
| 上位机 | VLC 播放、MQTT 订阅、日志查看和截图整理 | VLC、MQTT 客户端、终端工具 | 不作为云端识别或推理节点 |

### 2.2.2 机械设计介绍

当前项目以桌面摆放式结构为主要形态，没有在代码或资料中体现复杂机械结构、CAD 外壳或自研机加工部件。因此本节按模块摆放关系进行说明：RV1126B 开发板、摄像头、ST7789 显示屏和网络连接组成桌面演示系统，摄像头需要朝向用户，使视场覆盖用户头肩区域以及桌面上的杯子或水瓶区域；ST7789 显示屏应朝向用户，便于在不查看上位机的情况下获得当前系统状态和提醒反馈。

这种桌面摆放式结构便于比赛演示和功能调试，也便于根据不同桌面环境调整摄像头角度和显示屏朝向。后续如进入产品化或长期部署阶段，可进一步补充固定支架、屏幕外壳、摄像头支架和理线结构，但在当前文档中不应编造已经完成的外壳、CAD 或机械加工成果。

建议配图：桌面摆放结构照片、摄像头视场示意图。

### 2.2.3 电路各模块介绍

当前项目资料未体现完整自研 PCB 或原理图，因此电路部分按模块连接关系进行说明。摄像头采集链路将图像输入 RV1126B，由 V4L2 接口读取视频帧；ST7789 显示链路通过 SPI 与 GPIO 控制屏幕命令、数据和复位等信号；网络通信链路承载 HTTP-FLV、RTSP relay 和 MQTT 数据；电源与公共连接为开发板、摄像头和显示屏提供运行基础。

表2-2 电路/连接模块说明表

| 电路/连接模块 | 输入 | 输出 | 功能说明 |
| ------- | -- | -- | ---- |
| 摄像头采集链路 | 摄像头视频信号 | 640x480 NV12 图像帧 | RV1126B 通过 `/dev/video23` 和 V4L2 MPLANE/MMAP 获取图像 |
| ST7789 显示链路 | App 显示状态、LVGL 绘制结果 | 240x240 本地屏幕画面 | 当前代码配置 SPI 设备 `/dev/spidev1.0`，并使用 GPIO 控制 DC、RESET 等信号 |
| 网络通信链路 | 编码视频、状态消息、事件消息 | HTTP-FLV、RTSP relay、MQTT topic | 用于上位机查看画面、订阅状态和保存调试材料 |
| 电源与公共连接 | 开发板和外设供电 | 稳定运行所需电源 | 具体供电方式和接线照片需由实物材料补充 |

## 2.3 软件系统介绍

### 2.3.1 软件整体介绍

软件系统采用模块化设计，App 主控模块负责读取配置、初始化摄像头、模型、显示、推流和 MQTT 模块，并启动 camera、encoder、ai、mqtt、display 等运行线程。CameraDevice 将摄像头帧送入帧缓冲和视频队列；ImageProcessor 为不同模型生成对应尺寸的 RGB 输入；GestureModel、PoseModel 和 CupModel 输出结构化识别结果；PostureAnalyzer 和 DrinkDetector 将模型结果转换为坐姿状态和饮水状态；App 再根据系统状态机、手势触发规则和定时饮水提醒机制生成最终输出。

软件输出链路分为三类：本地显示链路将 AppState 映射为 DisplayFace，通过 ST7789/LVGL 显示状态；视频链路将原始帧和识别结果进行轻量叠加，经过 MPP H.264 编码后由 WebStreamer 输出 HTTP-FLV；通信链路通过 MqttClient 发布状态、事件和遥测消息。RTSP relay 是独立可执行程序，接收主程序的 HTTP-FLV 并转换为 VLC 可播放的 RTSP 地址。

图2-3 软件架构图建议如下。

```mermaid
flowchart TB
    Main["src/main.cpp<br/>配置与启动"] --> App["VisionApp<br/>主控与线程调度"]
    App --> CameraThread["cameraLoop<br/>CameraDevice"]
    App --> AiThread["aiLoop<br/>ImageProcessor + 三模型"]
    App --> EncoderThread["encoderLoop<br/>Overlay + MPP"]
    App --> MqttThread["mqttLoop<br/>MqttClient"]
    App --> DisplayThread["displayLoop<br/>DisplayDevice"]
    AiThread --> State["PostureAnalyzer / DrinkDetector<br/>业务状态判断"]
    State --> App
    EncoderThread --> Web["WebStreamer<br/>HTTP-FLV / state.json"]
    DisplayThread --> Panel["ST7789/LVGL"]
    MqttThread --> Broker["MQTT broker 或 log fallback"]
```

图2-4 主程序运行流程图如下。

```mermaid
flowchart TD
    Start["程序启动"] --> Config["加载配置与模型路径"]
    Config --> InitHW["初始化摄像头/显示/网络模块"]
    InitHW --> InitAI["加载RKNN模型"]
    InitAI --> Loop["循环采集视频帧"]
    Loop --> Pre["图像预处理"]
    Pre --> Infer["手势/姿态/饮品模型推理"]
    Infer --> Post["后处理与坐标反算"]
    Post --> State["状态机与事件融合"]
    State --> Output["显示/推流/MQTT输出"]
    Output --> Loop
```

表2-3 软件模块职责表

| 软件模块 | 主要职责 | 关键输入 | 关键输出 | 对应代码文件 |
| ---- | ---- | ---- | ---- | ------ |
| App 主控模块 | 配置加载、线程调度、状态机融合、事件发布 | `AppConfig`、摄像头帧、模型结果、手势事件 | `AppState`、`VisionResult`、显示事件、MQTT 消息 | `src/App.cpp`、`include/App.hpp`、`src/main.cpp` |
| CameraDevice | 摄像头打开、V4L2 参数设置、帧读取 | `/dev/video23`、640x480、NV12 | `Frame` | `src/CameraDevice.cpp`、`include/Interfaces.hpp` |
| ImageProcessor | 模型输入预处理、resize、颜色格式转换、坐标映射记录 | 原始 `Frame`、crop、目标尺寸 | RGB888 模型输入、`PreprocessTransform` | `src/VideoPipeline.cpp`、`include/VideoPipeline.hpp` |
| GestureModel | 手势分类和业务手势映射 | 224x224 RGB 图像 | `GestureResult` | `src/GestureModel.cpp`、`include/Types.hpp` |
| PoseModel | 人体框和 17 个关键点解析 | 640x640 RGB 图像 | `PoseResult` | `src/PerceptionModules.cpp`、`include/Types.hpp` |
| CupModel | 饮品/水瓶检测，支持两种 profile | 640x640 RGB 图像 | `CupResult` | `src/PerceptionModules.cpp`、`include/Types.hpp` |
| DrinkDetector | 根据头部点与杯/瓶位置判断饮水状态 | `PoseResult`、`CupResult` | `DrinkState` | `src/PerceptionModules.cpp` |
| DisplayDevice | ST7789/LVGL 本地显示 | `DisplayFace`、显示配置 | 屏幕界面 | `src/DisplayDevice.cpp`、`include/Interfaces.hpp` |
| VideoPipeline / WebStreamer | MPP 编码、HTTP-FLV 服务、状态 JSON | 视频帧、H.264 包、AI 结果 | `/live.flv`、`/state.json` | `src/VideoPipeline.cpp`、`src/WebStreamer.cpp`、`src/FlvMuxer.cpp` |
| RTSP relay | 将 HTTP-FLV 转为 RTSP | `http://127.0.0.1:8080/live.flv` | `rtsp://<board_ip>:8554/live` | `tools/rtsp_relay.cpp` |
| MqttClient | MQTT 连接、重连和发布，或日志 fallback | `MqttMessage`、broker 配置 | status/event/telemetry topic 或日志 | `src/MqttClient.cpp`、`src/App.cpp` |
| single_image_ai_debug | 单图模型调试、后处理验证和叠图输出 | 图片文件、模型配置 | 终端结果和调试图片 | `tools/single_image_ai_debug.cpp` |

### 2.3.2 软件各模块介绍

#### 2.3.2.1 App 主控与状态融合模块

App 主控模块是软件系统的调度中心，负责初始化 CameraDevice、GestureModel、PoseModel、CupModel、PostureAnalyzer、DrinkDetector、DisplayDevice、MqttClient、WebStreamer 和 MPP 编码模块，并管理采集、AI、编码、显示和 MQTT 线程。其关键输入包括 `AppConfig`、摄像头帧、手势识别结果、姿态结果和饮品检测结果；关键输出包括 `AppState`、`VisionResult`、显示状态、视频叠加结果和 MQTT 消息。

系统主状态包括 Idle、Running 和 Stopping。默认情况下系统进入 Idle，主要调度手势模型等待 Start；`RV_FORCE_AI_RUNNING` 配置可用于调试时直接进入 Running。Start 手势在 Idle 状态下触发运行，Stop 手势在 Running 状态下暂停并回到 Idle。Confirm 不作为主状态切换手势，当前用于确认 active 定时饮水提醒并提供交互反馈；Rock 用于互动反馈。App 同时管理饮水定时器，默认首次提醒间隔为 30 分钟，重复提醒间隔为 5 分钟，检测到 DrinkDetected 或 Confirm 确认后会按配置重新处理定时状态。

对应代码文件：`src/App.cpp`、`include/App.hpp`、`src/main.cpp`。

#### 2.3.2.2 摄像头采集模块

CameraDevice 负责将摄像头画面接入系统。当前主程序配置摄像头设备节点为 `/dev/video23`，分辨率为 640x480，图像格式为 NV12。`src/CameraDevice.cpp` 中使用 V4L2 `VIDEO_CAPTURE_MPLANE`、MMAP 缓冲和流式采集方式打开设备，并在读取时将帧封装为 `Frame`，其中 `format=PixelFormat::NV12`、`channels=1`。

采集到的帧会进入 `LatestFrameBuffer` 供 AI 线程读取，同时进入编码队列供视频链路使用。App 启动时还会初始化 FramePool，用于复用帧缓冲，减少运行过程中的重复分配。该模块只负责采集和帧封装，不在 CameraDevice 内部完成 NV12 到 RGB 的模型输入转换。

对应代码文件：`src/CameraDevice.cpp`、`include/Interfaces.hpp`、`src/App.cpp`。

#### 2.3.2.3 图像预处理模块

ImageProcessor 负责将原始视频帧转换为各模型所需的 RGB 输入。当前主程序对整帧进行 crop 后 resize，不使用未在代码中体现的 letterbox。手势模型输入为 224x224，姿态模型输入为 640x640，饮品模型输入为 640x640。预处理结果会记录 `PreprocessTransform`，包括原图尺寸、crop 区域和模型尺寸，供 PoseModel 和 CupModel 将模型坐标反算到原始画面坐标。

预处理后端支持 RGA、OpenCV 和软件兜底。CMake 中 `RV1126B_ENABLE_RGA` 和 `RV1126B_ENABLE_OPENCV` 控制对应依赖是否编入；运行时 `RV_PREPROCESS_MODE=rga` 可优先使用 RGA，默认配置当前走 OpenCV fallback 路径。单图调试工具 `single_image_ai_debug` 也复用 `ImageProcessor::cropResize()`，因此单图调试和实时视频主链路在预处理接口上保持一致。

对应代码文件：`src/VideoPipeline.cpp`、`include/VideoPipeline.hpp`、`tools/single_image_ai_debug.cpp`。

#### 2.3.2.4 手势识别模块

GestureModel 用于实现非接触式启停和交互反馈。当前模型路径为 `model/yolov5_gesture_rv1126b.rknn`，输入尺寸为 224x224。模型输出按 15 类分类结果解析，先进行 softmax，再取 top1 类别并与阈值比较，当前置信度阈值为 0.60。业务映射关系为 `class_6 -> Start`、`class_5 -> Stop`、`class_10 -> Confirm`、`class_13 -> Confirm`、`class_12 -> Rock`。

为了降低误触发，App 层对手势结果加入稳定计数、冷却和释放锁机制。当前 `gesture_stable_required=2`，表示同一业务手势需连续满足条件后才触发；`gesture_trigger_cooldown_ms=1500`，用于限制重复触发；`gesture_require_release=true`，表示同一手势触发后需要释放或识别到其他状态后才允许再次触发。手势输出为 `GestureResult`，随后由 App 转换为状态切换、显示表情和 MQTT 事件。

图2-5 手势状态机流程图如下。

```mermaid
flowchart TD
    Frame["224x224 手势输入"] --> Infer["GestureModel 推理"]
    Infer --> Top1["softmax + top1 类别"]
    Top1 --> Map["class 映射 Start/Stop/Confirm/Rock"]
    Map --> Threshold["置信度阈值判断"]
    Threshold --> Stable["连续稳定计数"]
    Stable --> Cooldown["冷却与释放锁判断"]
    Cooldown --> Action["触发业务动作或交互反馈"]
```

对应代码文件：`src/GestureModel.cpp`、`src/App.cpp`、`include/Types.hpp`。

#### 2.3.2.5 姿态估计与坐姿判断模块

PoseModel 负责解析人体框和关键点。当前模型路径为 `model/yolov8n-pose-rv1126b-i8.rknn`，输入尺寸为 640x640。模型后处理支持 YOLOv8-Pose 输出解析，结果包括人体框、`person_score` 和 17 个关键点。代码中人体候选阈值为 `person_score >= 0.25`，NMS IoU 阈值为 0.45；关键点判断使用 `pose_keypoint_score_threshold=0.35`。

PostureAnalyzer 基于鼻尖、耳朵和肩膀关键点计算头前伸角和低头/后仰角，并映射为系统姿态状态。当前规则包括 HEAD_FORWARD、HEAD_DOWN、HEAD_BACKWARD 等原因：头前伸角大于等于 35 度时判断为头前伸，头部 pitch 大于等于 15 度时判断为低头，pitch 小于等于 -15 度时判断为后仰。文档中应将其表述为坐姿提醒规则，不应写成医疗诊断或医学评估。

对应代码文件：`src/PerceptionModules.cpp`、`include/Types.hpp`、`src/main.cpp`。

#### 2.3.2.6 饮品检测模块

CupModel 用于检测画面中的饮品容器，为饮水状态判断提供杯/瓶位置。系统支持 COCO class-aware 与 BottleBoxesOnly 两种饮品模型配置，二者不能混淆。COCO 模式使用 `model/yolov8n_rv1126b_i8.rknn`，输出模式为 `CocoClassAware`，只保留 COCO 类别 39、40、41，分别对应 bottle、wine_glass 和 cup，标签形式如 `bottle(class_39)`、`wine_glass(class_40)`、`cup(class_41)`。

当前 `src/main.cpp` 中默认设置 `CupModelProfile::BottleBoxesOnly`，并通过 `applyCupModelProfile(config)` 配置模型路径为 `model/bottle_rv1126b_i8.rknn`。该模式按无类别框输出处理，不输出 COCO 类别，不做 class_id 过滤，检测结果标签为 `bottle(box_only)`，`class_id=-1`，不能写成 `class_id=0`。当前代码已包含 `[1,5,8400]` 通道优先输出结构的解析逻辑，channel 0 到 4 分别作为 `cx/cy/w/h/score`，并结合阈值过滤、NMS 和坐标反算得到最终杯/瓶框。正式结果材料中如需写“已上板验证”，仍应结合实际运行日志和截图确认。

对应代码文件：`src/PerceptionModules.cpp`、`include/Types.hpp`、`src/main.cpp`。

#### 2.3.2.7 饮水状态判断与提醒模块

DrinkDetector 将姿态模型和饮品检测模型的结果融合为饮水状态。其输入为 `PoseResult` 和 `CupResult`，输出为 `DrinkState::Normal`、`DrinkState::NeedRemind` 或 `DrinkState::DrinkDetected`。视觉判断优先使用鼻尖作为头部点；鼻尖不可见时，使用左右耳可见点的平均值作为头部位置。系统计算杯/瓶中心到头部点的距离，并用人体框对角线进行归一化。

当前归一化距离阈值为 0.40，连续命中次数为 3。杯/瓶靠近头部且连续满足阈值时判断为 DrinkDetected；有人且检测到杯/瓶但距离较远时可进入 NeedRemind；无人、无有效人体、无有效杯/瓶或关键点不可用时保持 Normal。App 层还叠加定时饮水提醒，默认首次提醒 30 分钟、重复提醒 5 分钟；定时提醒只在 Running 状态工作，Confirm 可在 active 定时提醒存在时确认并清除本次提醒。该模块用于提醒和交互，不用于声明健康疗效。

图2-6 饮水提醒判断流程图如下。

```mermaid
flowchart TD
    PoseCup["PoseResult + CupResult"] --> Valid["检查人体/杯瓶/头部点有效性"]
    Valid -->|无效| Normal["Normal"]
    Valid -->|有效| Distance["计算杯瓶中心到头部点距离"]
    Distance --> Norm["按人体框对角线归一化"]
    Norm --> Hit{"距离 <= 0.40"}
    Hit -->|是| Count["连续命中计数"]
    Count -->|达到3次| Drink["DrinkDetected"]
    Count -->|未达到| Normal
    Hit -->|否| Remind["NeedRemind"]
    Timer["30分钟定时提醒/5分钟重复"] --> Remind
    Confirm["Confirm确认"] --> Clear["清除active定时提醒"]
```

对应代码文件：`src/PerceptionModules.cpp`、`src/App.cpp`、`include/Types.hpp`。

#### 2.3.2.8 本地显示模块

DisplayDevice 负责 ST7789 屏幕输出，当前屏幕配置为 240x240，主程序启用 `enable_display=true` 和 `enable_lvgl_display=true`。代码支持 Linux `spidev` 和 GPIO 控制 ST7789，同时在编译启用 `RV1126B_ENABLE_LVGL` 时使用 LVGL 绘制页面。当前主程序配置的 SPI 设备为 `/dev/spidev1.0`，SPI 频率为 8 MHz，GPIO 配置为 DC 128、RESET 23、BACKLIGHT -1；这些硬件接线参数需要结合实物照片和板端测试继续核对。

显示状态由 `DisplayFace` 枚举表示，包括 IdleClock、NormalFace、StartFace、StopFace、ConfirmFace、RockFace、DrinkRemindFace、DrinkOkFace、BadPostureFace、SleepFace 和 ErrorFace 等。IdleClock 用于待机时钟页，NormalFace 用于运行监测页，Start/Stop/Confirm/Rock 用于手势触发后的临时反馈，DrinkRemindFace、DrinkOkFace 和 BadPostureFace 分别对应饮水提醒、饮水检测和坐姿异常提醒。App 中存在 transient 表情回退机制，Start/Stop 和 Confirm 默认显示 3 秒，Rock 默认显示 20 秒，之后回到当前主状态对应的基础表情。

对应代码文件：`src/DisplayDevice.cpp`、`include/Interfaces.hpp`、`include/Types.hpp`、`src/App.cpp`。

#### 2.3.2.9 视频编码与 HTTP-FLV 推流模块

视频链路由 VideoPipeline、MppEncoder、WebStreamer 和 FlvMuxer 组成。摄像头帧进入编码队列后，App 可将最近的 AI 结果以轻量 NV12 Y 平面画框方式叠加到视频帧上，主要绘制人体框和杯/瓶框。随后 MPP 编码器在依赖可用时进行 H.264 编码，WebStreamer 将编码包封装为 HTTP-FLV 输出。

当前主程序设置 `web_stream_protocol=HttpFlv`，默认 Web 端口为 8080，主程序输出地址格式为 `http://<board_ip>:8080/live.flv`。WebStreamer 还提供 `/state.json` 用于查看当前 AppState。需要注意，HTTP-FLV 是主程序直接输出的视频链路，主程序本身不直接提供 RTSP 服务；RTSP 播放能力由独立 relay 程序完成转换。

对应代码文件：`src/VideoPipeline.cpp`、`src/WebStreamer.cpp`、`src/FlvMuxer.cpp`、`src/App.cpp`。

#### 2.3.2.10 RTSP relay 模块

RTSP relay 是独立可执行程序 `rv1126b_rtsp_relay`，用于把主程序输出的 HTTP-FLV 转换成 VLC 常用的 RTSP 地址。其默认输入为 `http://127.0.0.1:8080/live.flv`，默认端口为 8554，默认 mount 为 `/live`，上位机 VLC 播放地址格式为 `rtsp://<board_ip>:8554/live`。

该工具基于 GStreamer RTSP server，内部管线使用 `souphttpsrc` 读取 HTTP-FLV，经过 `flvdemux`、`h264parse` 和 `rtph264pay` 后挂载到 RTSP server。CMake 中需要启用 `RV1126B_ENABLE_GSTREAMER_RTSP=ON` 且依赖满足时才会生成该 target。工程调试中如果 VLC 无法连接，需要检查主程序 HTTP-FLV 是否已启动，以及 8554 端口是否被旧 relay 进程占用。

对应代码文件：`tools/rtsp_relay.cpp`、`CMakeLists.txt`。

#### 2.3.2.11 MQTT 状态同步模块

MqttClient 负责将系统状态、事件和遥测信息同步到 MQTT topic。当前代码支持 `rv1126b/status`、`rv1126b/event` 和 `rv1126b/telemetry` 三类消息：status 包含设备状态、坐姿状态、饮水状态、手势和时间戳；event 包含手势事件、坐姿提醒、视觉饮水提醒和定时饮水提醒；telemetry 包含编码器、视频端口等运行配置字段。遥测中的固定字段不应当作为实测 FPS 或性能指标使用。

MQTT 是可选功能，受 CMake `RV1126B_ENABLE_MQTT`、libmosquitto 依赖和运行时 `config.enable_mqtt` 共同影响。编译时找到 libmosquitto 后走真实网络发布路径；未编入 libmosquitto 时，代码提供 log fallback，用于在普通开发环境查看 topic 和 payload，但该 fallback 不是实际 MQTT 网络发布。当前 `src/main.cpp` 中运行配置显式设置 `enable_mqtt=true`，host 为 `192.168.137.1`，port 为 `1884`；但 `docs/current_capabilities.md` 中仍存在默认关闭 MQTT 的旧描述，正式文档宜采用“系统支持 MQTT 状态同步，默认值以当前源码和演示配置为准”的稳妥表述。

对应代码文件：`src/MqttClient.cpp`、`src/App.cpp`、`src/main.cpp`、`include/Types.hpp`。

#### 2.3.2.12 单图 AI 调试工具

`single_image_ai_debug` 是离线单图调试工具，不属于主程序实时运行链路，但对模型输出格式、预处理、坐标反算和后处理阈值验证有帮助。该工具使用 OpenCV 读取单张图片，构造原始 RGB `Frame`，再复用 `ImageProcessor::cropResize()` 分别生成手势、姿态和饮品模型输入。工具会加载 GestureModel、PoseModel 和 CupModel，输出手势类别、人体框、17 个关键点、杯/瓶候选框、饮水状态和调试叠图。

当前单图工具默认饮品 profile 同样为 BottleBoxesOnly，并会输出 `/tmp/debug_gesture_input.jpg`、`/tmp/debug_pose_input.jpg`、`/tmp/debug_cup_input.jpg` 和 `/tmp/single_image_ai_debug_overlay.jpg` 等调试文件。单图工具中的连续饮水判断用于验证规则和阈值，不等同于实时视频序列的实测结果。

对应代码文件：`tools/single_image_ai_debug.cpp`、`CMakeLists.txt`。

# 本轮自查

| 检查项 | 结果 |
| --- | -- |
| 是否只写了第二部分 | 是 |
| 是否没有写第三部分正文 | 是 |
| 是否没有编造实物照片/CAD/PCB | 是 |
| 是否没有写未实测 FPS/延迟/准确率/功耗 | 是 |
| 是否没有写医疗诊断效果 | 是 |
| 是否没有学校/老师信息 | 是 |
| 是否区分 COCO 与 BottleBoxesOnly | 是 |
| 是否没有把 BottleBoxesOnly 写成 class_id=0 | 是，写为 `class_id=-1` |
| 是否区分 HTTP-FLV 和 RTSP relay | 是 |
| 是否区分 MQTT 真实发布和 fallback | 是 |
| 是否修改代码 | 否 |
