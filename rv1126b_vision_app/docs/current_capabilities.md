# 当前项目能力说明

## 1. 当前工程路径

当前工程路径：

```bash
C:\Users\AgiUser\Desktop\代码\6.27\rv1126b\rv1126b_vision_app
```

本文件基于该目录下当前最新代码检查生成，不基于旧目录或旧记忆。

## 2. 当前主要功能

当前代码支持的主要能力：

* 摄像头采集：主程序默认使用 `/dev/video23`，默认帧尺寸为 `640x480`，目标帧率 `25fps`。
* 手势模型：已接入手势 RKNN 模型，当前主程序默认模型路径为 `model/yolov5_gesture_rv1126b.rknn`，输入尺寸为 `224x224`。
* 姿态模型：已接入姿态 RKNN 模型，当前主程序默认模型路径为 `model/yolov8n-pose-rv1126b-i8.rknn`，输入尺寸为 `640x640`。
* 水杯模型：已接入水杯/饮品 RKNN 模型，当前主程序默认使用 `CupModelProfile::BottleBoxesOnly`，模型路径为 `model/bottle_rv1126b_i8.rknn`，输入尺寸为 `640x640`。
* MPP H.264 编码：主程序配置中 `enable_mpp_encoder = true`，用于输出 H.264 视频流。
* 视频 overlay：当前使用轻量 NV12 Y 平面画框方式，不依赖 OpenCV，不做全帧颜色空间转换；只画 pose/cup 产生的 oxes，手势分类结果不会画框。
* HTTP-FLV 输出：主程序当前设置 `web_stream_protocol = HttpFlv`，默认 Web 端口为 `8080`，用于输出 `http://<board_ip>:8080/live.flv`。
* RTSP relay + VLC 播放：存在 `tools/rtsp_relay.cpp`，在 `-DRV1126B_ENABLE_GSTREAMER_RTSP=ON` 且依赖满足时生成 `rv1126b_rtsp_relay`，可把 HTTP-FLV 转成 RTSP 给 VLC 播放。
* ST7789/LVGL 显示：主程序默认 `enable_display = true`、`enable_lvgl_display = true`，ST7789 默认设备为 `/dev/spidev1.0`，支持 `--display-only` 测试待机页。
* MQTT：代码支持 MQTT 客户端、状态/遥测/事件发布逻辑；CMake 选项 `RV1126B_ENABLE_MQTT` 默认是 `ON`，但当前 `src/main.cpp` 中运行配置 `config.enable_mqtt = false`，所以主程序默认关闭 MQTT。比赛编译命令也建议先用 `-DRV1126B_ENABLE_MQTT=OFF`。
* 单图测试工具：当前存在 `tools/gesture_image_test.cpp` 和 `tools/single_image_ai_debug.cpp`，CMake 中存在对应 target。

## 3. 当前模型文件

根据当前代码 grep 结果，主程序真实默认模型路径为：

* `gesture_model_path`: `model/yolov5_gesture_rv1126b.rknn`
* `pose_model_path`: `model/yolov8n-pose-rv1126b-i8.rknn`
* `cup_model_path`: 当前由 `applyCupModelProfile(config)` 根据 `cup_model_profile` 设置；比赛默认 `BottleBoxesOnly` 为 `model/bottle_rv1126b_i8.rknn`。

当前 `model/` 目录中可见文件：

* `model/bottle_rv1126b_i8.rknn`
* `model/gesture_mobilenetv3_small_fp16.rknn`
* `model/yolov5_gesture_rv1126b.rknn`
* `model/yolov8n-pose-rv1126b-i8.rknn`
* `model/yolov8n_rv1126b_i8.rknn`

注意：`src/main.cpp` 中还保留 `posture_drink_model_path = "/userdata/models/posture_drink.rknn"`，但当前配置为 `use_three_model_pipeline = true`、`use_legacy_posture_drink_model = false`，比赛主线应以手势、姿态、水杯三模型为准。

饮品检测当前支持两种 config profile：

1. `CupModelProfile::Coco`：使用 `model/yolov8n_rv1126b_i8.rknn`，按 COCO class-aware YOLO 输出解析，只保留 `class_id=39/40/41`，分别对应 `bottle`、`wine_glass`、`cup`。
2. `CupModelProfile::BottleBoxesOnly`：使用 `model/bottle_rv1126b_i8.rknn`，当前输出格式为 `[1,5,8400]`，`anchors=8400`，按 `channel_first_1x5x8400` 解析：channel 0 = `cx`，channel 1 = `cy`，channel 2 = `w`，channel 3 = `h`，channel 4 = `score`，日志中 `score_source=channel4`。该模型不输出类别，不做 class_id 过滤，所有 `score >= cup_score_threshold` 的有效框默认作为 bottle，`label=bottle(box_only)`，`class_id=-1`。NMS 后最多保留 `cup_max_output_boxes` 个框。不要把该模型当 Nx5 flat 输出解析，也不要当 `class_id` 为 `0` 的单类别模型解析。

切换方式是在配置中修改：`config.cup_model_profile = CupModelProfile::Coco;` 或 `config.cup_model_profile = CupModelProfile::BottleBoxesOnly;`，然后调用 `applyCupModelProfile(config)`。当前比赛建议使用 `BottleBoxesOnly`。

## 4. 喝水提醒

当前喝水提醒有两类来源：

1. 视觉提醒：`DrinkDetector` 检测到有人和杯子/瓶子，但杯子/瓶子离头部较远时，视觉状态为 `NeedRemind`。
2. 定时提醒：系统处于 `Running` 状态并持续达到 `drink_timer_interval_ms` 后，App 层触发定时喝水提醒，最终状态合成为 `NeedRemind`。

定时提醒配置位于 `AppConfig`：`drink_timer_reminder_enabled`、`drink_timer_interval_ms`、`drink_timer_repeat_ms`、`drink_timer_reset_on_drink_detected`、`drink_timer_confirm_ack_enabled`。默认首次提醒间隔为 30 分钟，重复提醒间隔为 5 分钟；调试时可在 `src/main.cpp` 中把 `config.drink_timer_interval_ms` 改成 `15000`，把 `config.drink_timer_repeat_ms` 改成 `10000`。

定时提醒只在 `Running` 状态工作，`Idle`/`Stop` 状态不提醒。检测到 `DrinkDetected` 后会清除 active timed reminder 并重新计时。`Confirm` 手势只有在 active timed reminder 存在时才确认并清除本次定时提醒；没有 active timed reminder 时，`Confirm` 仍只保留原有确认反馈，不影响视觉喝水判断。

MQTT status 里的 `drink` 字段仍只使用 `normal` / `need_remind` / `drink_detected`。定时提醒事件名为 `drink_timer_remind`，视觉提醒事件仍为 `drink_remind`，消息文本均为 `Time to drink water`。

ST7789/LVGL 显示上，定时提醒合成为 `NeedRemind` 后显示 `DrinkRemindFace`；`DrinkDetected` 显示 `DrinkOkFace`。Start/Stop/Rock/Confirm 临时表情优先级最高，临时表情期间不会被基础状态强行覆盖；基础状态显示优先级为 `DrinkOkFace` > `BadPostureFace` > `DrinkRemindFace` > `SleepFace` > `NormalFace`，坐姿异常会优先于饮水提醒展示。

## 5. 主程序编译命令

比赛演示编译命令：

```bash
cmake -S . -B build-rtsp \
-DRV1126B_ENABLE_LVGL=ON \
-DRV1126B_ENABLE_RGA=ON \
-DRV1126B_ENABLE_RKNN=ON \
-DRV1126B_ENABLE_OPENCV=ON \
-DRV1126B_ENABLE_MPP=ON \
-DRV1126B_ENABLE_MQTT=OFF \
-DRV1126B_ENABLE_GSTREAMER_RTSP=ON

cmake --build build-rtsp -j2
```

最小编译检查命令：

```bash
cmake -S . -B build-check-off \
-DRV1126B_ENABLE_LVGL=OFF \
-DRV1126B_ENABLE_RGA=OFF \
-DRV1126B_ENABLE_RKNN=OFF \
-DRV1126B_ENABLE_OPENCV=OFF \
-DRV1126B_ENABLE_MPP=OFF \
-DRV1126B_ENABLE_MQTT=OFF \
-DRV1126B_ENABLE_GSTREAMER_RTSP=OFF

cmake --build build-check-off -j2
```

## 6. 主程序运行命令

普通运行：

```bash
./build-rtsp/rv1126b_vision_app
```

当前版本已去掉等比补边，实时主程序统一使用 resize 预处理。
`RV_PREPROCESS_MODE` 只控制预处理后端：

* `rga`：优先 RGA resize
* `opencv`：优先 OpenCV resize

不再支持已移除的预处理几何环境变量。如果后续模型效果变差，需要重新评估模型训练/导出时是否依赖等比补边。

RGA 运行：

```bash
RV_PREPROCESS_MODE=rga ./build-rtsp/rv1126b_vision_app
```

`RV_FORCE_AI_RUNNING` 支持 `1`、`true`、`on`、`yes`。设置后主程序会跳过手势启动，直接进入 Running 状态，方便测试 pose/cup/视频 overlay。

```bash
RV_FORCE_AI_RUNNING=1 \
RV_PREPROCESS_MODE=rga \
./build-rtsp/rv1126b_vision_app
```

## 7. 手势业务映射

当前统一手势阈值：`gesture_score_threshold = 0.6`。

当前四类稳定手势业务映射：

| class | 功能 | 业务行为 |
| --- | --- | --- |
| class_6 | Start / 开始监测 | 进入 Running，发布 `gesture_start`，ST7789/LVGL 显示 `StartFace` / `Monitoring ON` |
| class_5 | Stop / 暂停监测 | 进入 Idle/Standby，发布 `gesture_stop`，ST7789/LVGL 显示 `StopFace` / `Standby` |
| class_10 | Confirm / 确认 | 发布 `gesture_confirm`，ST7789/LVGL 显示 `ConfirmFace` / `Got it`，不改变 Running/Idle 主状态 |
| class_13 | Confirm / 确认 | 发布 `gesture_confirm`，ST7789/LVGL 显示 `ConfirmFace` / `Got it`，不改变 Running/Idle 主状态 |
| class_12 | Rock / 互动反馈 | 发布 `gesture_rock`，ST7789/LVGL 显示 `RockFace` / `Nice!`，不改变 Running/Idle 主状态 |

OK 和点赞容易互相识别错，所以 `class_10` 和 `class_13` 合并为同一类 `Confirm`。所有手势继续统一使用 `gesture_score_threshold`，不设置分手势阈值。喝水定时提醒还没完成，`Confirm` 目前只做确认反馈，不负责复杂提醒清除。
## 手势触发逻辑

当前手势统一阈值：`gesture_score_threshold=0.6`。

当前业务映射：

| class | GestureType | 业务动作 |
| --- | --- | --- |
| class_6 | Start | 开始监测 / 唤醒 |
| class_5 | Stop | 暂停监测 / 回待机 |
| class_10 | Confirm | 确认 / OK / 我知道了 |
| class_13 | Confirm | 确认 / 点赞 / 我知道了 |
| class_12 | Rock | 互动反馈 / Nice |

OK 和点赞容易互相识别错，所以 `class_10` 和 `class_13` 合并为 `Confirm`。

当前触发规则：模型每 `gesture_interval_ms` 运行一次，当前默认 `300ms`。单次识别到有效手势后，不立即触发；必须同一个业务手势连续识别 `gesture_stable_required` 次，当前默认 `2` 次，才触发。触发后进入 `gesture_trigger_cooldown_ms` 冷却，当前默认 `1500ms`。`gesture_require_release=true` 时，同一个手势触发后，用户需要放下手或识别到 `None` / 其他手势，才允许再次触发同一个手势。

日志含义：`[手势识别]` 是模型单次 top1 输出，会打印 `prob`、`threshold`、`pass_threshold`、`mapped`、`valid`；`[手势稳定]` 表示连续确认计数，会打印 `waiting`、`cooldown`、`locked_wait_release` 或 `reset`；`[手势触发]` 才表示真正执行 Start / Stop / Confirm / Rock 业务动作。
## 7. VLC 推流命令

终端 1：启动主程序，生成 HTTP-FLV：

```bash
RV_FORCE_AI_RUNNING=1 \
RV_PREPROCESS_MODE=rga \
./build-rtsp/rv1126b_vision_app
```

终端 2：启动 RTSP relay，把 HTTP-FLV 转成 RTSP：

```bash
./build-rtsp/rv1126b_rtsp_relay \
--input http://127.0.0.1:8080/live.flv \
--port 8554 \
--mount /live
```

Windows VLC 地址：

```text
rtsp://192.168.137.2:8554/live
```

HTTP-FLV 地址：

```text
http://192.168.137.2:8080/live.flv
```

如果 `build-rtsp/rv1126b_rtsp_relay` 不存在，需要打开 `-DRV1126B_ENABLE_GSTREAMER_RTSP=ON` 重新编译，并确认板子有 GStreamer RTSP server 依赖。

## 8. 单图测试功能

当前存在：

* `tools/gesture_image_test.cpp`
* `tools/single_image_ai_debug.cpp`

编译命令：

```bash
cmake -S . -B build-tools \
-DRV1126B_ENABLE_RKNN=ON \
-DRV1126B_ENABLE_OPENCV=ON \
-DRV1126B_ENABLE_RGA=OFF \
-DRV1126B_ENABLE_LVGL=OFF \
-DRV1126B_ENABLE_MPP=OFF \
-DRV1126B_ENABLE_MQTT=OFF \
-DRV1126B_ENABLE_GSTREAMER_RTSP=OFF

cmake --build build-tools -j2 --target gesture_image_test single_image_ai_debug
```

运行命令：

```bash
./build-tools/gesture_image_test /path/to/gesture.jpg

./build-tools/single_image_ai_debug /path/to/test.jpg
```

当前代码中可见的调试输出路径：

* `gesture_image_test`: `/tmp/gesture_input_rgb_debug.jpg`
* `gesture_image_test`: `/tmp/gesture_input_bgr_debug.jpg`
* `single_image_ai_debug`: `/tmp/debug_gesture_input.jpg`
* `single_image_ai_debug`: `/tmp/debug_pose_input.jpg`
* `single_image_ai_debug`: `/tmp/debug_cup_input.jpg`
* `single_image_ai_debug`: `/tmp/single_image_ai_debug_overlay.jpg`

single_image_ai_debug 当前复用实时主程序的 `ImageProcessor::cropResize()` 生成 gesture / pose / cup 三路模型输入。工具会先用 OpenCV 读取单张图片，再构造原始 RGB `Frame`，随后按整图 crop 生成 224x224 或 640x640 的模型输入；这些输入会带 `PreprocessTransform`，因此 Pose/Cup 结果沿用模型内部基于 `Frame.transform` 的坐标反算逻辑。

`single_image_ai_debug` 支持 `RV_PREPROCESS_MODE=rga` 和 `RV_PREPROCESS_MODE=opencv`，用于尽量模拟实时视频主程序的 RGA/OpenCV 预处理后端；未设置时使用默认预处理配置。它仍会输出 `/tmp/debug_gesture_input.jpg`、`/tmp/debug_pose_input.jpg`、`/tmp/debug_cup_input.jpg` 和 `/tmp/single_image_ai_debug_overlay.jpg`，其中 debug 输入图来自 `ImageProcessor::cropResize()` 实际生成的模型输入。

单图调试图仍然保留详细绘制：person box、pose keypoints、skeleton、所有 cup 候选框、score/class_id 以及 gesture/posture/drink 文本。实时视频 overlay 不因此改变，仍只画筛选后的少量框以降低 CPU 压力和画面混乱。喝水连续判断在单图工具中仍是调试用途，不完全等同于实时视频时间序列。
single_image_ai_debug 的 CUP 部分会打印：

* `cup_count`
* `threshold`
* `candidates_before_nms`
* `kept_after_nms`
* `best_score`
* `best_class_id`
* 每个 cup 的 `score` 和 `box`

排查建议：如果 `cup_count=0` 且 `best_raw_score` 较低，说明模型对杯子响应弱；如果 `best_raw_score` 高但 `candidates_before_nms=0`，需要检查阈值或 class_id；如果 `cup_count>0`，再看 `best_score` 和 `box` 是否准确。当前单图工具优先打印后处理结果中的 cup score；低于阈值的 raw score 需要 CupModel 后处理额外暴露后才能显示。

gesture_image_test 是当前推荐的手势模型调试工具，用于确认手势图片输出的 class、top5、概率和映射后的 GestureType。它不等同于视频检测框；手势模型是分类模型，不会输出框。

## 9. ST7789/LVGL 测试

当前支持 `--display-only`：

```bash
./build-rtsp/rv1126b_vision_app --display-only
```

这个命令用于只测试 ST7789/LVGL，不跑摄像头、不跑 AI、不推流。当前 display-only 分支会打开显示设备、调用 `showIdleClock()`，并持续 `tick()`。
## ST7789/LVGL 显示逻辑

正常主程序启动时，`force_ai_running=false` 会显示待机时钟 `IdleClock`，页面为 Happy Life / 北京时间 / Waiting Gesture；`force_ai_running=true` 会显示 `NormalFace`，页面为 RUNNING / Monitoring。

手势触发后的临时表情会自动回退：Start 显示 START / Monitoring ON 3 秒，然后回 RUNNING / Monitoring；Stop 显示 STOP / Standby 3 秒，然后回待机时钟；Confirm 显示 OK / Got it 3 秒，然后回当前主状态；Rock 显示 ROCK / Nice! 20 秒，然后回当前主状态。

当前 ST7789 使用 LVGL 轻量动态 UI：IdleClock 显示 Happy Life、北京时间和秒环；Running 显示 RUNNING / Monitoring，并用扫描圆环和状态点表示监测中；Start 显示 START / Monitoring ON 和启动圆环；Stop 显示 STOP / Standby 和睡眠动态；Confirm 显示 OK / Got it 和确认圆环；Rock 显示 ROCK / Nice!，前约 5 秒做墨镜/星星感的轻量摆动和闪烁，之后静态保持到 20 秒回退。

基础状态表情选择时，喝水成功反馈优先保留；坐姿异常比饮水提醒更紧急；只有没有喝水成功、坐姿异常和饮水提醒时，待机状态才显示 SleepFace。

动画使用 LVGL 原生控件，不依赖图片资源和外部字体；动画通过 `DisplayDevice::tick()` 驱动，不新建线程，不阻塞主流程，不做高频全屏重建。临时表情显示期间，普通状态页不会覆盖它；新的手势临时表情可以刷新临时显示和持续时间。`--display-only` 仍只测试待机时钟，不跑摄像头、AI 或推流。

## 10. 当前已知限制

1. 当前 RTSP/VLC 使用的 H.264 视频流支持低 CPU NV12 Y 平面画框；为了避免 VLC 画面混乱，视频展示层只画最终展示框：最多 1 个最高分 `person` 框，以及最多 1 个最高分 `cup` / `bottle` / `wine_glass` 框。日志、Web 和单图测试仍可保留更多候选框；视频 overlay 不再把所有 cup 候选框都画出来。结果超过 `video_overlay_result_ttl_frames` 会自动过期，避免旧框残留。
2. 手势模型是分类模型，不会在视频上画手势框。
3. 如果只看到 schedule gesture，而 `pose_ms`/`cup_ms` 为 `0`，说明当前没有调度姿态/水杯模型。
4. ST7789 当前待机页可用，Start/Stop/Confirm/Rock 四类手势会推送对应业务表情；`--display-only` 仍只显示待机时钟。
5. 北京时间显示策略需要确认，避免系统时间和代码偏移重复。当前配置里有 `display_timezone_offset_minutes = 480`。
6. 当前代码版本多，命令和文档容易混乱，需要以本文件为当前版本准则。
7. 等比补边 功能后续准备移除或整理，不作为当前比赛主线。
8. `RV_FORCE_AI_RUNNING=1` 会让主程序初始进入 Running 状态；默认不设置时仍然从 Idle 开始，等待手势启动。
9. `已移除的预处理几何环境变量` 本次 grep 未在 `src/`、`include/`、`tools/`、`run_demo.sh` 中发现，当前不应作为主线运行参数。
10. MQTT 代码存在，但当前主程序运行配置默认关闭；如果要作为比赛状态数据链路，需要单独确认 broker、编译选项和运行配置。

## 11. 调试命令

查看实际模型加载：

```bash
grep -E "model loaded|GestureModel|PoseModel|CupModel|read model failed|fallback|stub" logs/run_model.log
```

查看实际 AI 调度：

```bash
grep -E "[AI].*gesture|[AI].*pose|[AI].*cup|gesture_ms|pose_ms|cup_ms" logs/run_model.log
```

查看端口：

```bash
ss -lntp | grep 8080
ss -lntp | grep 8554
```

摄像头占用处理：

```bash
pkill -f rv1126b_vision_app
pkill -f rv1126b_rtsp_relay
fuser -k /dev/video23
```

8554 占用处理：

```bash
fuser -k 8554/tcp
```

## 11. 最终比赛建议

当前比赛主线建议：

* ST7789/LVGL 本地状态显示。
* RTSP + VLC 视频流。
* MQTT 状态数据，如果当前 MQTT 稳定。
* 手势和水杯模型继续校准。

不要把 WebRTC、网页播放、HTTP-FLV 浏览器播放作为当前主线。HTTP-FLV 当前更适合作为主程序到 RTSP relay 的中间输入。

