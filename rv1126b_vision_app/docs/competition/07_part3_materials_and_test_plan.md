# 第三部分素材采集与实测记录模板

## 1. 第三部分写作目标

第三部分“完成情况及性能参数”不是根据代码推断成果，而是用照片、截图、日志和测试表证明系统已经完成到可展示状态。本部分材料需要证明：

- 系统实物已经搭建，RV1126B、摄像头、ST7789、网络连接和上位机关系清楚。
- 摄像头、RV1126B、ST7789、上位机等硬件连接清楚，且不把普通模块连接写成自研 PCB 成果。
- 主程序能够在板端运行，并能加载当前配置中的手势、姿态和 BottleBoxesOnly 饮品检测链路。
- 视频能够通过主程序 HTTP-FLV 输出，并通过独立 RTSP relay 转换为 VLC 可播放的 RTSP 地址。
- MQTT 能够真实发布状态消息，必须用订阅窗口收到 `rv1126b/status`、`rv1126b/event` 或 `rv1126b/telemetry` 作为证据。
- ST7789 能显示待机、运行、手势反馈，以及经过实机验证的提醒状态；坏姿势、饮水提醒和 DrinkOk 当前必须先按“待验证”处理。
- AI 模块能输出手势、姿态、饮品检测和饮水状态，BottleBoxesOnly 的 `[1,5,8400]` 输出解析需要板端日志或单图调试截图支撑。
- 功能验证和性能参数必须来自实测，不能从代码中编造 FPS、延迟、准确率、功耗或连续运行时长。

## 2. 第三部分章节与素材对应关系

### 2.1 3.1 整体介绍

| 素材 | 拍摄对象 | 拍摄角度 | 画面中必须出现什么 | 证明什么 | 建议图题 |
|---|---|---|---|---|---|
| 系统整体正面照片 | 完整桌面系统 | 正面平视 | RV1126B、摄像头、ST7789、桌面摆放关系 | 系统实物已经搭建 | 图3-1 系统整体正面照片 |
| 系统斜 45° 全局照片 | 完整桌面系统 | 斜 45° | 开发板、摄像头、屏幕、连线、上位机局部 | 硬件连接和空间布局 | 图3-2 系统 45° 全局照片 |
| 系统运行时整体照片 | 运行中的完整系统 | 能同时看到板端和上位机 | ST7789 亮屏、上位机 VLC 或终端、摄像头朝向 | 系统处于运行展示状态 | 图3-3 系统运行状态整体照片 |

### 2.2 3.2 工程成果

#### 3.2.1 机械成果

当前资料没有复杂机械外壳、CAD 或机加工结构证据，因此机械成果建议写成“桌面摆放式结构”。需要采集的材料包括：

- 摄像头位置照片：证明摄像头视场覆盖用户上半身和桌面杯/瓶区域。
- ST7789 朝向照片：证明本地反馈屏朝向用户。
- RV1126B 开发板摆放照片：证明主控和外设的桌面布局。
- 整体固定方式照片：如支架、胶带、夹具或临时固定方式，以实物为准。

注意：没有外壳/CAD 时，不要编造机械加工成果，只写模块摆放关系和后续可扩展支架。

#### 3.2.2 电路成果

需要拍摄：

- RV1126B 与摄像头连接。
- RV1126B 与 ST7789 连接。
- 网络连接。
- 电源连接。
- 整体接线。

注意：如果没有自研 PCB，只写“模块连接成果”，不要写自研电路板成果。ST7789 的 `/dev/spidev1.0`、DC=128、RESET=23、BACKLIGHT=-1 需要用实物接线或板端说明核对。

#### 3.2.3 软件成果

需要截图：

- 主程序运行日志。
- VLC 播放 RTSP 画面。
- HTTP-FLV 或 RTSP 地址。
- MQTT 订阅消息。
- ST7789 状态显示。
- `single_image_ai_debug` 输出。
- 如果视频流有人体框或杯/瓶框叠加，截取视频画面。

### 2.3 3.3 特性成果

需要整理：

- 功能验证表。
- 性能参数表。
- ST7789 状态验证表。
- RTSP 推流验证表。
- MQTT 发布验证表。
- BottleBoxesOnly 输出验证记录。
- 现场测试照片。

## 3. 拍照清单

| 编号 | 照片名称 | 拍摄内容 | 拍摄要求 | 用于章节 | 状态 |
|---|---|---|---|---|---|
| P1 | 系统整体正面照片 | 完整桌面系统 | 正面拍摄，清楚看到 RV1126B、摄像头、ST7789 | 3.1 | 待拍摄 |
| P2 | 系统 45° 全局照片 | 完整桌面系统 | 斜 45°，能看到空间布局和连接线 | 3.1 | 待拍摄 |
| P3 | RV1126B 开发板特写 | 主控平台 | 画面清晰，不遮挡主要接口 | 3.2.2 | 待拍摄 |
| P4 | 摄像头安装/摆放照片 | 摄像头和视场方向 | 标明摄像头朝向用户上半身和桌面区域 | 3.2.1 | 待拍摄 |
| P5 | ST7789 显示屏照片 | 屏幕和接线 | 能看到屏幕亮起和朝向用户 | 3.2.2/3.2.3 | 待拍摄 |
| P6 | 硬件整体接线照片 | 摄像头、屏幕、电源、网络 | 连线尽量清晰，必要时分两张拍 | 3.2.2 | 待拍摄 |
| P7 | 网络连接/上位机连接照片 | 板端网络和上位机 | 能说明 VLC/MQTT/调试上位机连接关系 | 3.2.2 | 待拍摄 |
| P8 | 运行状态整体照片 | 主程序运行中的系统 | 同时体现板端运行和上位机画面 | 3.1/3.2.3 | 待拍摄 |
| P9 | 待机界面照片 | ST7789 IdleClock | 使用 `--display-only` 或默认 Idle 状态拍摄 | 3.2.3/3.3 | 待拍摄 |
| P10 | Running 界面照片 | ST7789 NormalFace / Running | 建议 `RV_FORCE_AI_RUNNING=1` 或 Start 后拍摄 | 3.2.3/3.3 | 待拍摄 |
| P11 | Start 手势反馈照片 | StartFace | 触发 Start 后 3 秒内拍摄 | 3.3 | 待拍摄 |
| P12 | Stop 手势反馈照片 | StopFace | 触发 Stop 后 3 秒内拍摄 | 3.3 | 待拍摄 |
| P13 | Rock 手势反馈照片 | RockFace | 触发 Rock 后拍摄，默认持续时间较长 | 3.3 | 待拍摄 |
| P14 | 坏姿势提醒照片 | BadPostureFace | 必须在 Running 状态下验证；当前先不写已完成 | 3.3 | 待验证 |
| P15 | 饮水提醒照片 | DrinkRemindFace | 必须在 Running 状态下验证视觉提醒或定时提醒 | 3.3 | 待验证 |
| P16 | DrinkDetected/DrinkOk 照片 | DrinkOkFace | 杯/瓶靠近头部并连续命中后拍摄 | 3.3 | 待验证 |

## 4. 截图清单

| 编号 | 截图名称 | 截图内容 | 命令或操作 | 用于章节 | 状态 |
|---|---|---|---|---|---|
| S1 | 主程序启动日志截图 | 程序启动、摄像头、模型、显示、推流初始化 | 启动 `rv1126b_vision_app` 后截取终端 | 3.2.3 | 待截图 |
| S2 | Config 日志截图 | `/dev/video23`、640x480、BottleBoxesOnly、MQTT、ST7789 配置 | 截取 `[Config]`、`[Config][Cup]`、`[Config][DrinkTimer]` | 3.2.3 | 待截图 |
| S3 | Gesture 识别日志截图 | Start/Stop/Rock/Confirm 手势日志 | 触发手势并截取 `[手势识别]`、`[手势触发]` | 3.3 | 待截图 |
| S4 | Pose 识别日志截图 | 人体框、关键点、姿态原因 | 截取 Pose/Posture 相关日志 | 3.3 | 待截图 |
| S5 | CupModel/BottleBoxesOnly 日志截图 | BottleBoxesOnly profile、候选框、NMS | 截取 `[CupModel][BottleBoxesOnly]` | 3.3 | 待截图 |
| S6 | DrinkTimer 日志截图 | 30 分钟首次提醒、5 分钟重复提醒或调试间隔 | 截取 `[DrinkTimer]` 相关日志 | 3.3 | 待截图 |
| S7 | VLC RTSP 播放截图 | VLC 正常播放 RTSP 画面 | 打开 `rtsp://<board_ip>:8554/live` 后截图 | 3.2.3/3.3 | 待截图 |
| S8 | RTSP relay 启动日志截图 | relay 输入、pipeline、VLC 地址 | 启动 `rv1126b_rtsp_relay` 后截图 | 3.2.3 | 待截图 |
| S9 | MQTT 订阅窗口截图 | `mosquitto_sub` 订阅 `rv1126b/#` | 上位机订阅窗口截图 | 3.2.3/3.3 | 待截图 |
| S10 | MQTT status 消息截图 | `rv1126b/status` payload | 订阅窗口中截取 status 消息 | 3.3 | 待截图 |
| S11 | MQTT event 消息截图 | `rv1126b/event` payload | 触发手势、坏姿势或饮水提醒后截图 | 3.3 | 待截图 |
| S12 | single_image_ai_debug 运行截图 | GESTURE、POSE、CUP、DRINK 输出 | 运行单图工具并截图 | 3.2.3/3.3 | 待截图 |
| S13 | BottleBoxesOnly 输出格式日志截图 | `[1,5,8400]` 或 `channel_first_1x5x8400` | 截取 CupModel 或单图调试日志 | 3.3 | 待截图/若有 |
| S14 | 摄像头占用排查截图 | `/dev/video23` 占用情况 | `fuser -v /dev/video23` | 3.3 附注 | 可选 |
| S15 | 8554 端口占用排查截图 | RTSP 端口监听或占用 | `ss -lntp | grep 8554` | 3.3 附注 | 可选 |

## 5. 上板实测命令建议

以下命令用于采集材料，不表示已经执行成功。实际构建目录、IP、broker 地址和模型路径以板端当前环境为准。

### 5.1 主程序运行命令

不带 `tee log` 的基础运行命令：

```bash
cd ~/bisai/rv1126b/rv1126b_vision_app

RV_FORCE_AI_RUNNING=1 \
RV_PREPROCESS_MODE=rga \
./build-rtsp/rv1126b_vision_app
```

如果需要 MQTT，可按最终代码支持情况使用：

```bash
RV_ENABLE_MQTT=1 \
RV_MQTT_HOST=192.168.137.1 \
RV_MQTT_PORT=1884 \
RV_FORCE_AI_RUNNING=1 \
RV_PREPROCESS_MODE=rga \
./build-full/rv1126b_vision_app
```

说明：当前 `src/main.cpp` 已确认读取 `RV_FORCE_AI_RUNNING` 和 `RV_PREPROCESS_MODE`；未在当前代码中确认 MQTT host/port 环境变量读取。如果代码不支持 `RV_ENABLE_MQTT`、`RV_MQTT_HOST`、`RV_MQTT_PORT`，则以 `src/main.cpp` 中 `enable_mqtt=true`、`mqtt_host=192.168.137.1`、`mqtt_port=1884` 为准。

### 5.2 摄像头占用排查命令

```bash
ps -ef | grep -E "rv1126b_vision_app|gst-launch|ffmpeg|single_image_ai_debug" | grep -v grep
fuser -v /dev/video23
pkill -9 rv1126b_vision_app
```

说明：`pkill -9` 只在确认旧进程占用摄像头且需要释放资源时使用。

### 5.3 RTSP relay 运行命令

```bash
./build-rtsp/rv1126b_rtsp_relay \
  --input http://127.0.0.1:8080/live.flv \
  --port 8554 \
  --mount /live
```

VLC 地址：

```text
rtsp://<board_ip>:8554/live
```

说明：HTTP-FLV 是主程序输出；RTSP 是 `rv1126b_rtsp_relay` 单独转换，不要写成主程序直接输出 RTSP。

### 5.4 RTSP 端口占用排查命令

```bash
ss -lntp | grep 8554
fuser -v -n tcp 8554
pkill -9 rv1126b_rtsp_relay
```

### 5.5 MQTT 订阅命令

Windows 上位机 Mosquitto 订阅：

```bash
mosquitto_sub -h 127.0.0.1 -p 1884 -t "rv1126b/#" -v
```

如果 broker 在板子：

```bash
mosquitto_sub -h <board_ip> -p 1884 -t "rv1126b/#" -v
```

说明：只有订阅窗口真实收到 `rv1126b/status`、`rv1126b/event` 或 `rv1126b/telemetry`，才能在第三部分写“MQTT 真实发布成功”。如果只看到程序本地打印 `[MQTT] log fallback`，不能证明 MQTT 网络发布成功。

### 5.6 单图调试命令

```bash
./build-tools/single_image_ai_debug ./capture.jpg
```

或按当前实际构建目录填写：

```bash
./<实际构建目录>/single_image_ai_debug ./capture.jpg
```

需要截图：

- GESTURE。
- POSE。
- CUP。
- DRINK。
- BottleBoxesOnly 输出格式相关日志。
- `/tmp/debug_gesture_input.jpg`、`/tmp/debug_pose_input.jpg`、`/tmp/debug_cup_input.jpg`、`/tmp/single_image_ai_debug_overlay.jpg` 如需放入文档，也要保存对应图片。

## 6. ST7789 状态验证计划

| 状态 | 触发方式 | 预期显示 | 是否必须 Running | 需要照片 | 当前状态 |
|---|---|---|---|---|---|
| IdleClock | 默认 Idle 或 `--display-only` | 待机时钟页 | 否 | 是，P9 | 待拍摄 |
| NormalFace / Running | Start 后进入 Running，或 `RV_FORCE_AI_RUNNING=1` | RUNNING / Monitoring | 是 | 是，P10 | 待拍摄 |
| StartFace | Idle 下触发 Start 手势 | START / Monitoring ON | 否，但由 Idle 触发 | 是，P11 | 待拍摄 |
| StopFace | Running 下触发 Stop 手势 | STOP / Standby | 是 | 是，P12 | 待拍摄 |
| ConfirmFace | 触发 Confirm 手势 | OK / Got it | 否；若用于确认定时提醒需 Running 且提醒 active | 可选 | 待拍摄 |
| RockFace | 触发 Rock 手势 | ROCK / Nice! | 否 | 是，P13 | 待拍摄 |
| BadPostureFace | Running 状态下触发 HEAD_DOWN/HEAD_FORWARD/HEAD_BACKWARD | POSTURE / Sit up | 是 | 是，P14 | 待实机验证 |
| DrinkRemindFace | Running 状态下触发视觉 NeedRemind 或定时提醒 active | DRINK / Water time | 是 | 是，P15 | 待实机验证 |
| DrinkOkFace | Running 状态下杯/瓶靠近头部并连续命中 | DRINK OK / Good | 是 | 是，P16 | 待实机验证 |

验证注意事项：

1. BadPostureFace、DrinkRemindFace、DrinkOkFace 当前先标为“待实机验证”，不能写成已完成。
2. `selectDisplayFace()` 中手势临时表情优先于普通状态页，Standby/SleepFace 优先于 DrinkOk、DrinkRemind 和 BadPosture；如果系统处于 Stop/Standby，坏姿势或饮水提醒可能不会显示。
3. 建议在 `RV_FORCE_AI_RUNNING=1` 或 Start 后 Running 状态下验证坏姿势和饮水提醒。
4. 每次验证需要记录触发动作、终端日志时间点、ST7789 照片编号和对应 AppState/MQTT/VLC 证据。
5. 如果姿态或饮水状态日志已经触发但屏幕没有变化，需要单独记录为“显示链路待修复/待核对”，不要在第三部分写成显示成功。

## 7. 功能验证表模板

| 功能项 | 验证方法 | 预期结果 | 实测结果 | 证据编号 | 结论 |
|---|---|---|---|---|---|
| 摄像头采集 | 启动主程序，查看 `/dev/video23` 和视频画面 | 能采集 640x480 NV12 图像 | 待填写 | S1/S2/S7 | 待判定 |
| 手势 Start | Idle 状态下做 Start 手势 | 进入 Running，显示 StartFace 或状态变化 | 待填写 | P11/S3 | 待判定 |
| 手势 Stop | Running 状态下做 Stop 手势 | 进入 Idle/Standby，显示 StopFace | 待填写 | P12/S3 | 待判定 |
| 手势 Rock | 做 Rock 手势 | 显示 RockFace，记录事件 | 待填写 | P13/S3/S11 | 待判定 |
| 姿态识别 | 用户出现在摄像头视野内，查看 Pose 日志 | 输出人体框和关键点 | 待填写 | S4 | 待判定 |
| 坐姿异常提醒 | Running 状态下模拟低头/前伸/后仰 | 输出 bad posture 状态，屏幕显示需单独验证 | 待填写 | P14/S4/S11 | 待判定 |
| 饮品检测 | 桌面放置杯/瓶，查看 CupModel 日志或视频框 | 检测到 `bottle(box_only)` 框 | 待填写 | S5/S13 | 待判定 |
| 视觉饮水判断 | 杯/瓶靠近头部并连续命中 | 输出 DrinkDetected | 待填写 | P16/S5/S11 | 待判定 |
| 定时饮水提醒 | Running 状态持续到提醒时间，或使用调试间隔 | 输出定时提醒事件 | 待填写 | P15/S6/S11 | 待判定 |
| ST7789 本地显示 | 拍摄各状态屏幕 | 待机、运行、手势状态可见；提醒状态按实测填写 | 待填写 | P9-P16 | 待判定 |
| HTTP-FLV 输出 | 浏览器或 relay 输入访问 `/live.flv` | 主程序输出 HTTP-FLV | 待填写 | S1/S8 | 待判定 |
| RTSP relay 输出 | 启动 `rv1126b_rtsp_relay` | 监听 8554，输出 `/live` | 待填写 | S8/S15 | 待判定 |
| VLC 播放 | VLC 打开 `rtsp://<board_ip>:8554/live` | 画面显示正常 | 待填写 | S7 | 待判定 |
| MQTT status 发布 | 订阅 `rv1126b/#` | 收到 `rv1126b/status` | 待填写 | S9/S10 | 待判定 |
| MQTT event 发布 | 触发手势、坏姿势或饮水提醒 | 收到 `rv1126b/event` | 待填写 | S9/S11 | 待判定 |
| single_image_ai_debug 单图调试 | 对测试图片运行单图工具 | 输出 GESTURE/POSE/CUP/DRINK 和叠图 | 待填写 | S12/S13 | 待判定 |

## 8. 性能参数表模板

| 参数类别 | 参数名称 | 当前配置/设计值 | 实测值 | 测试方法 | 证据编号 | 是否可写入正式文档 |
|---|---|---|---|---|---|---|
| 摄像头 | 分辨率 | 640x480 | 待实测确认 | Config 日志、VLC 画面或采集日志 | S2/S7 | 可写配置值；实测待补 |
| 摄像头 | 图像格式 | NV12 | 待实测确认 | CameraDevice 日志或代码配置截图 | S1/S2 | 可写配置值；实测待补 |
| 手势模型 | 输入尺寸 | 224x224 | 待实测确认 | Config 日志或单图工具输出 | S2/S12 | 可写配置值 |
| 姿态模型 | 输入尺寸 | 640x640 | 待实测确认 | Config 日志或单图工具输出 | S2/S12 | 可写配置值 |
| 饮品模型 | 输入尺寸 | 640x640 | 待实测确认 | Config 日志或单图工具输出 | S2/S12 | 可写配置值 |
| 显示 | ST7789 分辨率 | 240x240 | 待实测确认 | 显示配置日志和屏幕照片 | P5/S2 | 可写配置值；显示效果待补 |
| 视频 | HTTP-FLV 地址 | `8080/live.flv` | 待实测确认 | WebStreamer 日志 | S1/S8 | 可写配置值；连通性待补 |
| 视频 | RTSP 地址 | `8554/live` | 待实测确认 | RTSP relay 日志和 VLC 截图 | S7/S8 | 可写配置值；连通性待补 |
| 通信 | MQTT topic | `rv1126b/status`、`rv1126b/event`、`rv1126b/telemetry` | 待实测确认 | `mosquitto_sub` 订阅截图 | S9-S11 | 真实收到后可写 |
| 饮水提醒 | 定时提醒 | 默认 30 分钟，可配置 | 待实测确认 | DrinkTimer 日志 | S6 | 可写配置值；触发结果待补 |
| 性能 | 平均帧率 | 待实测 | 待填写 | 明确测试时长和采样方法 | 待编号 | 无实测前不可写 |
| 性能 | 端到端延迟 | 待实测 | 待填写 | 明确输入动作到显示/视频/MQTT 输出的计时方式 | 待编号 | 无实测前不可写 |
| 性能 | 单次推理耗时 | 待实测 | 待填写 | 使用明确日志或 profiling 工具 | 待编号 | 无实测前不可写 |
| 资源 | CPU 占用 | 待实测 | 待填写 | `top`/`htop`/系统工具截图 | 待编号 | 无实测前不可写 |
| 资源 | 内存占用 | 待实测 | 待填写 | `top`/`free`/系统工具截图 | 待编号 | 无实测前不可写 |
| 资源 | 功耗 | 待实测 | 待填写 | 功率计或电源读数照片 | 待编号 | 无实测前不可写 |
| 稳定性 | 连续运行时长 | 待实测 | 待填写 | 明确开始/结束时间和日志 | 待编号 | 无实测前不可写 |

说明：没有实测值前，不允许把平均帧率、端到端延迟、单次推理耗时、CPU 占用、内存占用、功耗、连续运行时长写入第三部分性能结果。

## 9. MQTT 真实性验证模板

| 验证项 | 预期 | 实测记录 | 证据截图 | 结论 |
|---|---|---|---|---|
| broker 启动 | broker 在上位机或板子监听 1884 | 待填写 | 待编号 | 待判定 |
| 订阅 `rv1126b/#` | 订阅窗口保持打开 | 待填写 | S9 | 待判定 |
| status 收到 | 收到 `rv1126b/status` | 待填写 | S10 | 待判定 |
| event 收到 | 触发手势或提醒后收到 `rv1126b/event` | 待填写 | S11 | 待判定 |
| telemetry 收到 | 收到 `rv1126b/telemetry` | 待填写 | 待编号 | 待判定 |
| 是否出现 log fallback | 若出现 `[MQTT] log fallback`，说明不是网络发布 | 待填写 | 待编号 | 待判定 |
| 板子 host/port 配置 | 当前源码为 `192.168.137.1:1884`，最终以实测为准 | 待填写 | S2 | 待判定 |

明确：如果只看到程序本地打印 `[MQTT] log fallback`，不能证明 MQTT 网络发布成功。必须有 `mosquitto_sub` 或其他 MQTT 客户端订阅窗口收到 topic 和 payload。

## 10. RTSP 真实性验证模板

| 验证项 | 预期 | 实测记录 | 证据截图 | 结论 |
|---|---|---|---|---|
| 主程序 HTTP-FLV 正常 | WebStreamer 输出 `http://<board_ip>:8080/live.flv` | 待填写 | S1/S8 | 待判定 |
| `rtsp_relay` 启动成功 | 显示 input、pipeline、VLC 地址 | 待填写 | S8 | 待判定 |
| 8554 端口监听 | `ss -lntp | grep 8554` 能看到监听 | 待填写 | S15 | 待判定 |
| VLC 打开 RTSP | VLC 打开 `rtsp://<board_ip>:8554/live` | 待填写 | S7 | 待判定 |
| 画面显示正常 | 能看到摄像头画面或叠加框 | 待填写 | S7 | 待判定 |
| 端口占用处理 | 旧 relay 占用时能定位并释放 | 待填写 | S15 | 可选 |

## 11. BottleBoxesOnly 验证模板

| 验证项 | 预期 | 实测日志/截图 | 结论 |
|---|---|---|---|
| 输出 shape 是否为 `[1,5,8400]` | 日志出现 `channel_first_1x5x8400` 或等价输出结构 | 待填写 | 待验证 |
| 是否按 channel-first 解析 | 日志显示 channel 0-4 对应 `cx/cy/w/h/score` 或 `score_source=channel4` | 待填写 | 待验证 |
| class_id 是否为 -1 | 检测结果为 `class_id=-1` | 待填写 | 待验证 |
| label 是否为 `bottle(box_only)` | 检测结果 label 为 `bottle(box_only)` | 待填写 | 待验证 |
| 是否不再出现上千个 cup boxes | NMS 后数量合理，不出现明显爆框 | 待填写 | 待验证 |
| candidates / kept_after_nms / kept_used 是否合理 | 日志数量符合画面中杯/瓶数量级 | 待填写 | 待验证 |
| 单图调试是否能看到正确 bottle 框 | overlay 中杯/瓶框位置正确 | 待填写 | 待验证 |
| 实时视频是否能稳定显示/判断 | 视频或日志中饮品框和饮水状态稳定 | 待填写 | 待验证 |

注意：如果没有上板日志，结论写“待验证”。不要把 BottleBoxesOnly 写成 `class_id=0`，也不要把 COCO 39/40/41 当作当前主程序唯一模式。

## 12. 第三部分正式写作时可使用的证据编号

P 表示照片：

- P1 系统正面照片。
- P2 系统 45° 照片。
- P3 RV1126B 开发板特写。
- P4 摄像头安装/摆放照片。
- P5 ST7789 显示屏照片。
- P6 硬件整体接线照片。
- P7 网络连接/上位机连接照片。
- P8 运行状态整体照片。
- P9 待机界面照片。
- P10 Running 界面照片。
- P11 Start 手势反馈照片。
- P12 Stop 手势反馈照片。
- P13 Rock 手势反馈照片。
- P14 坏姿势提醒照片。
- P15 饮水提醒照片。
- P16 DrinkDetected/DrinkOk 照片。

S 表示截图：

- S1 主程序启动日志。
- S2 Config 日志。
- S3 Gesture 识别日志。
- S4 Pose 识别日志。
- S5 CupModel/BottleBoxesOnly 日志。
- S6 DrinkTimer 日志。
- S7 VLC RTSP 截图。
- S8 RTSP relay 启动日志。
- S9 MQTT 订阅窗口。
- S10 MQTT status 消息。
- S11 MQTT event 消息。
- S12 single_image_ai_debug 运行截图。
- S13 BottleBoxesOnly 输出格式日志。
- S14 摄像头占用排查截图。
- S15 8554 端口占用排查截图。

T 表示测试表：

- T1 功能验证表。
- T2 性能参数表。
- T3 ST7789 状态验证表。
- T4 MQTT 真实性验证表。
- T5 RTSP 真实性验证表。
- T6 BottleBoxesOnly 验证表。

后续第三部分写正文时，要引用这些证据编号；没有证据编号的结论不要写成已完成成果。

## 13. 第三部分写作前检查清单

- [ ] 系统整体照片已拍。
- [ ] 硬件连接照片已拍。
- [ ] ST7789 待机/运行/手势状态已拍。
- [ ] ST7789 坏姿势提醒已验证。
- [ ] ST7789 饮水提醒已验证。
- [ ] VLC RTSP 截图已获取。
- [ ] MQTT 真实订阅截图已获取。
- [ ] 主程序日志截图已获取。
- [ ] 单图调试截图已获取。
- [ ] 功能验证表已填写。
- [ ] 性能参数表已填写。
- [ ] 未实测指标没有写入正式文档。

