# 阶段性总审稿报告

## 1. 审稿范围

本轮审查范围包括 `docs/competition/03_abstract_and_part1.md`、`docs/competition/04_part2_system_design.md`、`docs/competition/05_summary_and_references.md`，并结合 `docs/competition/01_project_facts.md`、`docs/competition/02_document_blueprint.md`、`CMakeLists.txt`、`include/Types.hpp`、`src/main.cpp`、`src/App.cpp`、`src/PerceptionModules.cpp`、`src/GestureModel.cpp`、`src/DisplayDevice.cpp`、`src/VideoPipeline.cpp`、`src/WebStreamer.cpp`、`src/MqttClient.cpp`、`tools/rtsp_relay.cpp`、`tools/single_image_ai_debug.cpp` 和 `docs/current_capabilities.md` 进行事实核对。

本轮没有审查第三部分正式结果，因为第三部分所需实物照片、软件截图、功能验证表和性能参数表尚未补齐。

## 2. 总体评价

当前文档已经形成较完整的技术主线：基于 RV1126B 的端侧视觉健康守护系统，通过摄像头采集、RGA/OpenCV 预处理、RKNN/NPU 推理、多模型协同、状态机判断、本地 ST7789/LVGL 显示、HTTP-FLV/RTSP 视频查看和 MQTT 同步构成工程闭环。整体适合继续补第三部分成果材料。最大风险不在主线，而在结果性表述：ST7789 的坏姿势和饮水提醒虽然有代码状态映射，但用户实测反馈屏上未见显示，应在正式文档中保守写为“代码支持/待实机照片验证”；此外 `docs/current_capabilities.md` 中 MQTT 默认关闭的旧描述与当前 `src/main.cpp` 的 `enable_mqtt=true` 冲突，需要后续统一。

## 3. 模板结构检查

| 模板章节 | 当前状态 | 问题 | 建议 |
| ---- | ---- | -- | -- |
| 作品名称 | 已完成 | 无明显问题 | 保留“基于 RV1126B 的端侧视觉健康守护系统” |
| 摘要 | 已完成 | ST7789 坐姿/饮水提醒显示写得偏肯定 | 改为“代码中配置显示状态，实机效果待第三部分照片补充” |
| 第一部分 1.1~1.6 | 已完成 | 1.4 为表格，需最终排版复核字数 | 保留源码确认参数，不补未实测指标 |
| 第二部分 2.1~2.3 | 已完成 | ST7789 坏姿势/饮水提醒实机显示证据不足；“稳定运行所需电源”表述易误解 | 增加显示链路待验证说明；改为“正常运行所需电源” |
| 第三部分 3.1~3.3 | 当前只占位 | 未写正式成果，符合当前阶段 | 待实物照片、软件截图、功能验证表和性能参数补充后再写 |
| 第四部分 4.1~4.2 | 已完成 | 内容克制，符合总结定位 | 保留工程调试细节，避免写长期稳定运行结论 |
| 第五部分参考文献 | 已完成初稿 | 多数条目仍需版本号、访问日期或出版信息 | 保留“待核对”标注，后续人工补全 |

## 4. 字数限制检查

| 小节 | 限制 | 当前判断 | 是否需要压缩 |
| -- | -- | ---- | ------ |
| 摘要 | 800字内 | 近似符合 | 暂不需要，建议最终排版前人工复核 |
| 1.1 | 400字内 | 近似符合 | 暂不需要 |
| 1.2 | 400字内 | 近似符合 | 暂不需要 |
| 1.3 | 400字内 | 近似符合 | 暂不需要 |
| 1.4 | 200字内 | 正文短，表格较长 | 不压缩表格，最终模板中确认计字规则 |
| 1.5 | 200字内 | 近似符合 | 暂不需要 |
| 1.6 | 200字内 | 近似符合 | 暂不需要 |
| 4.1 | 300字内 | 近似符合 | 暂不需要 |
| 4.2 | 1000字内 | 近似符合 | 暂不需要 |

说明：本轮未做严格中文字符统计，建议最终排版前用人工或脚本复核。

## 5. 技术真实性检查

### 5.1 项目定位

文档总体始终写成 RV1126B 端侧视觉健康守护系统，没有写成单模型 demo，也没有写成云端 AI 识别系统。当前表述与源码和事实清单一致。

### 5.2 硬件事实

硬件事实基本准确：当前 `src/main.cpp` 设置摄像头为 `/dev/video23`、`640x480`、`frame_channels=1`，`src/CameraDevice.cpp` 使用 V4L2 MPLANE/MMAP 和 NV12；显示配置为 ST7789 `240x240`，`/dev/spidev1.0`、DC 128、RESET 23、BACKLIGHT -1。上位机用途写为 VLC 播放、MQTT 订阅、日志查看和调试，不承担 AI 推理，表述合理。

需要注意：ST7789 具体接线和实机显示效果仍需照片确认，不能仅凭配置写成已完成成果。

### 5.3 模型事实

手势模型路径 `model/yolov5_gesture_rv1126b.rknn`、姿态模型路径 `model/yolov8n-pose-rv1126b-i8.rknn` 与源码一致。当前主程序在 `src/main.cpp` 中显式设置 `CupModelProfile::BottleBoxesOnly`，并通过 `applyCupModelProfile(config)` 将饮品模型配置为 `model/bottle_rv1126b_i8.rknn`。

文档已区分 COCO class-aware 与 BottleBoxesOnly：COCO 保留 39/40/41，BottleBoxesOnly 不输出类别、`label=bottle(box_only)`、`class_id=-1`。未发现把 BottleBoxesOnly 写成 `class_id=0` 的正式正文问题。

风险点：第二部分写到 `[1,5,8400]` 通道优先解析逻辑，源码确实已有 `channel_first_1x5x8400` 解析；但若没有板端日志或截图，不应写“已上板验证”，建议统一为“当前代码已按模型输出结构适配，实机验证材料待补”。

### 5.4 手势逻辑

Start / Stop / Confirm / Rock 映射与 `src/GestureModel.cpp` 一致：`class_6=Start`、`class_5=Stop`、`class_10/13=Confirm`、`class_12=Rock`。文档中 Confirm 未被写成运行状态切换，而是写为确认定时提醒和交互反馈，符合 `src/App.cpp` 中 `acknowledgeDrinkTimerByConfirm()` 的逻辑。稳定计数、冷却时间、释放锁表述与 `gesture_stable_required=2`、`gesture_trigger_cooldown_ms=1500`、`gesture_require_release=true` 一致。

### 5.5 姿态和饮水逻辑

文档将姿态功能写为坐姿提醒规则，未写医疗诊断。HEAD_FORWARD、HEAD_DOWN、HEAD_BACKWARD 等原因与 `src/PerceptionModules.cpp` 中规则一致。饮水状态 `Normal`、`NeedRemind`、`DrinkDetected` 与 `include/Types.hpp` 一致，归一化距离阈值 0.40、连续命中 3 次、30 分钟首次提醒和 5 分钟重复提醒均有源码依据。

需要注意：用户反馈姿态不良已测过但屏幕未显示，因此第三部分不能把“ST7789 坏姿势提醒实测成功”写成成果；第二部分只能写代码映射和待验证。

### 5.6 显示、推流与通信

HTTP-FLV 和 RTSP relay 区分清楚：主程序输出 `http://<board_ip>:8080/live.flv`，`tools/rtsp_relay.cpp` 单独将 HTTP-FLV 转为 `rtsp://<board_ip>:8554/live`。未发现把主程序写成直接输出 RTSP 的问题。

MQTT 真实发布与 fallback 已区分：`src/MqttClient.cpp` 在有 libmosquitto 时走真实发布路径，没有依赖时为 log fallback。当前 `src/main.cpp` 设置 `enable_mqtt=true`、host `192.168.137.1`、port `1884`，但 `docs/current_capabilities.md` 仍写默认关闭 MQTT，是当前最大文档一致性风险之一。

ST7789/LVGL：`selectDisplayFace()` 的优先级为手势触发 > Standby/SleepFace > DrinkOk > DrinkRemind > BadPosture > Normal。也就是说，在待机/Standby 状态下坏姿势不会优先于待机显示；临时手势表情也会在持续时间内压过基础页面。`DisplayDevice::showFace()` 确实为 BadPosture、DrinkRemind、DrinkOk 设置了简单页面，但实机可见性需要结合 Running 状态、显示队列、LVGL tick、SPI/GPIO 接线和照片继续验证。

## 6. 风格与表达问题

当前正文整体较工程化，宣传性表述较少。建议统一以下术语：

* RV1126B
* 端侧 AI
* RKNN
* NPU
* RGA/OpenCV
* ST7789/LVGL
* HTTP-FLV
* RTSP relay
* MQTT
* BottleBoxesOnly
* COCO class-aware

建议修订点：

* 将“显示待机、运行、手势、坐姿和饮水提醒状态”改为“代码中配置待机、运行、手势、坐姿和饮水提醒等显示状态，实机照片待补”。
* 将“稳定运行所需电源”改为“正常运行所需电源”，避免被误读为已经验证长时间稳定运行。
* 删除阶段性合并稿中 03/04/05 原有的“本轮自查”小节，避免进入正式比赛正文。
* 对 MQTT 默认值采用“当前源码配置”和“演示配置需确认”的写法，避免与旧文档冲突。

## 7. 高风险句子替换建议

| 原句或问题表述 | 风险 | 建议替换 |
| ------- | -- | ---- |
| “通过 ST7789 240x240 屏幕和 LVGL 界面显示待机、运行、手势反馈、坐姿提醒和饮水提醒状态” | 用户实测未看到坏姿势/饮水提醒，实机证据不足 | “代码中通过 ST7789/LVGL 配置待机、运行、手势反馈、坐姿提醒和饮水提醒等显示状态，实际界面效果需在第三部分结合照片验证” |
| “当前代码已包含 `[1,5,8400]` 通道优先输出结构的解析逻辑” | 源码成立，但容易被理解为已上板验证 | “当前代码已按 `[1,5,8400]` 输出结构进行适配，是否已在板端稳定验证需结合运行日志和截图确认” |
| “稳定运行所需电源” | 可能被误解为长期稳定运行测试结论 | “正常运行所需电源” |
| “当前 `src/main.cpp` 中运行配置显式设置 `enable_mqtt=true`...” | 与 `docs/current_capabilities.md` 旧描述冲突 | 正式正文中写“当前源码设置为 enable_mqtt=true；旧文档存在默认关闭描述，演示配置以最终运行版本为准” |
| “ST7789 显示坐姿提醒和饮水提醒” | 缺少实物显示验证，且显示优先级可能导致看不到 | “ST7789 显示模块中存在坐姿和饮水提醒页面映射，实机显示效果、优先级和触发条件需补充验证材料” |

## 8. 第三部分后续补充要求

第三部分必须等待结果材料后再写正式正文。至少需要补充：

* 系统整体正面照片。
* 系统斜 45° 照片。
* 硬件连接照片。
* ST7789 显示照片，尤其需覆盖待机、运行、手势反馈、坏姿势提醒、饮水提醒或无法显示的实际情况。
* VLC RTSP 播放截图。
* MQTT 订阅截图。
* 主程序运行截图。
* 单图调试截图。
* 功能验证表。
* 性能参数表。

没有这些材料前，不要写第三部分成果性正文，不要写 FPS、延迟、准确率、功耗和长期稳定运行时长。

## 9. 参考文献检查

当前参考文献共 13 篇，少于 20 篇；未发现编造 DOI、卷号或页码的问题；多数条目已标注“待核对”。覆盖方向包括 RKNN/RV1126、V4L2、OpenCV、LVGL、GStreamer/RTSP、MQTT、H.264、YOLO 和姿态估计。

后续需要人工补全的信息包括：Rockchip 文档准确名称和版本号，OpenCV/LVGL/GStreamer 文档版本与访问日期，MQTT 标准版本来源，H.264 标准年份，OpenPose/CVPR 论文出版信息。

## 10. 审稿结论

当前文档可以进入阶段性合并稿，并可在第三部分材料补齐后继续整合为完整比赛文档。现阶段不建议生成 Word，因为第三部分成果和测试数据尚未完成。

当前最优先修正的 5 个问题：

1. ST7789 坏姿势/饮水提醒显示从“已显示”改为“代码映射存在，实机照片待验证”。
2. 统一 MQTT 说明，明确当前源码 enable=true，但旧文档存在冲突，真实发布需 broker 和 libmosquitto。
3. BottleBoxesOnly `[1,5,8400]` 只写代码适配，不写已上板验证。
4. 第三部分继续保持占位，不写成果正文。
5. 删除阶段性合并稿中原先每轮自查表，只保留本轮总自查。

# 本轮自查

| 检查项 | 结果 |
| --- | -- |
| 是否写了第三部分正式正文 | 否 |
| 是否编造实物成果 | 否 |
| 是否写未实测性能 | 否 |
| 是否出现学校/老师信息 | 否 |
| 是否出现医疗诊断效果 | 否 |
| 是否混淆 COCO 与 BottleBoxesOnly | 否 |
| 是否写 BottleBoxesOnly 为 class_id=0 | 否 |
| 是否混淆 HTTP-FLV 与 RTSP | 否 |
| 是否混淆 MQTT fallback 与真实发布 | 否 |
| 是否修改代码 | 否 |
