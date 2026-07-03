# 第二部分重写报告

## 1. 本轮读取文件

本轮按要求读取并核对了以下文件：

- `docs/competition/比赛文档_v0.1_文字版.md`
- `docs/competition/final_readable_draft.md`
- `docs/competition/09_abstract_part1_rewritten.md`
- `docs/competition/08_full_rewrite_plan.md`
- `docs/competition/01_project_facts.md`
- `docs/competition/02_document_blueprint.md`
- `include/Types.hpp`
- `src/main.cpp`
- `src/App.cpp`
- `src/PerceptionModules.cpp`
- `src/GestureModel.cpp`
- `src/DisplayDevice.cpp`
- `src/VideoPipeline.cpp`
- `src/WebStreamer.cpp`
- `src/MqttClient.cpp`
- `tools/rtsp_relay.cpp`

其中 `final_readable_draft.md`、`09_abstract_part1_rewritten.md` 和 `08_full_rewrite_plan.md` 首次默认读取时出现终端编码显示乱码，随后以 UTF-8 方式重新读取了当前比赛文档、项目事实清单、蓝图和源码，确保重写依据以可读中文和源码事实为准。

## 2. 重写章节

本轮只重写了“第二部分  系统组成及功能说明”中的 2.1、2.2 和 2.3，输出到 `docs/competition/11_part2_rewritten.md`。

具体包括：

- 2.1 整体介绍
- 2.2 硬件系统介绍
- 2.2.1 硬件整体组成
- 2.2.2 机械结构与摆放方式
- 2.2.3 模块连接关系
- 2.3 软件系统介绍
- 2.3.1 软件整体架构
- 2.3.2 主控与运行状态
- 2.3.3 视觉采集与图像预处理
- 2.3.4 手势识别与交互控制
- 2.3.5 姿态估计与坐姿提醒
- 2.3.6 饮品检测与饮水提醒
- 2.3.7 本地显示、视频与通信输出
- 2.3.8 单图调试工具

未修改摘要、第一部分、第三部分、第四部分和参考文献。

## 3. 删除或压缩的源码说明

本轮将原文中过细的源码说明压缩为系统设计语言，主要处理如下：

- 压缩了大量类名、函数名、线程名和文件路径堆叠，不再把 2.1 写成 `CameraDevice`、`ImageProcessor`、`GestureModel` 等源码调用链说明。
- 将软件模块职责表中的“对应代码文件”改为更概括的“实现位置”，避免文件名列表过长。
- 删除了过多临时调试路径、日志字段和输出 tensor 细节，只保留与比赛文档有关的工程边界。
- 将手势 class 映射细节压缩为 Start、Stop、Confirm、Rock 四类业务手势，不在正文大量展开 `class_x`。
- 将 ST7789 页面枚举、动画细节和显示优先级压缩为“本地状态反馈”和“临时反馈/基础状态回退”。
- 将 HTTP-FLV、RTSP relay、MQTT 输出合并为 2.3.7，减少重复说明。
- 将单图调试工具压缩为 2.3.8 的简短说明，不再罗列多个 `/tmp` 调试文件路径。

## 4. 保留的关键技术事实

重写稿保留了以下关键事实：

- 系统基于 RV1126B 主控平台。
- 摄像头输入为 640×480 NV12 视频帧。
- 预处理为不同模型生成输入，并保留坐标映射用于结果反算。
- 系统使用 RKNN/NPU 端侧推理链路。
- 模型包括手势识别、姿态估计和饮品检测三类。
- 业务层融合手势事件、坐姿状态、饮水状态和定时提醒。
- 手势包括 Start、Stop、Confirm、Rock，并保留稳定计数、冷却和释放锁机制。
- 姿态估计输出人体框和 17 个关键点，坐姿提醒规则包括低头、前伸和后仰。
- 饮品检测支持 COCO class-aware 和 BottleBoxesOnly 两类配置。
- BottleBoxesOnly 按无类别框处理，没有写成 `class_id=0`。
- 饮水提醒融合视觉判断和定时提醒。
- 定时饮水提醒保留默认 30 分钟、重复提醒 5 分钟。
- ST7789/LVGL 用于本地状态反馈，接线参数需实物核对。
- 主程序输出 HTTP-FLV。
- RTSP relay 是独立工具，用于把 HTTP-FLV 转为 RTSP。
- MQTT 支持真实发布和日志 fallback，二者在文中明确区分。
- 上位机只用于 VLC、MQTT 订阅、日志和截图整理，不作为 AI 推理节点。

## 5. 仍需照片或实测结果支撑的内容

以下内容仍需第三部分或后续材料支撑：

- RV1126B、摄像头、ST7789、网络连接和上位机的实物摆放照片。
- 摄像头视场是否覆盖用户头肩区域和桌面杯/瓶区域。
- ST7789 实际接线、SPI 设备、GPIO 编号和屏幕显示效果。
- HTTP-FLV 是否在目标板上稳定输出。
- RTSP relay 是否成功转换并被 VLC 播放。
- MQTT 是否通过真实 broker 发布，并被订阅端接收。
- 手势 Start、Stop、Confirm、Rock 的现场触发截图或日志。
- 姿态提醒和饮水提醒的实机运行截图或验证记录。
- BottleBoxesOnly 输出格式和检测效果的板端日志或单图调试截图。

## 6. 未实测性能与医疗诊断检查

本轮没有写入未实测 FPS、延迟、准确率、功耗、CPU 占用、内存占用或连续运行时长。

本轮没有写医疗诊断、治疗效果、疾病预防效果或长期健康改善结论。坐姿和饮水相关内容均按“提醒规则”和“交互辅助”表述。

## 7. 代码与其他文件修改情况

本轮没有修改代码，没有修改 docx，没有生成 Word，没有画图。

本轮新增两个 Markdown 文件：

- `docs/competition/11_part2_rewritten.md`
- `docs/competition/11_part2_rewrite_report.md`

## 8. 下一步建议

下一步建议先补齐图2-1到图2-6的正式图片或可转 Word 的图源，再进入第三部分实物照片、运行截图和功能验证表整理。尤其建议优先补充 ST7789 屏幕照片、VLC RTSP 播放截图、MQTT 订阅截图和主程序运行日志，这些材料能支撑第二部分中“本地显示、视频输出和状态同步”的工程闭环描述。

## 9. 自查表

| 检查项 | 结果 |
|---|---|
| 是否只重写第二部分 | 是 |
| 是否保留图2-1到图2-6占位 | 是 |
| 是否减少源码类名和文件路径 | 是 |
| 是否区分 HTTP-FLV 与 RTSP relay | 是 |
| 是否区分 MQTT 真实发布与 fallback | 是 |
| 是否未把上位机写成 AI 推理节点 | 是 |
| 是否未把 BottleBoxesOnly 写成 class_id=0 | 是 |
| 是否未写医疗诊断 | 是 |
| 是否未写未实测 FPS/延迟/准确率/功耗 | 是 |
| 是否未编造硬件外壳/PCB | 是 |
| 是否修改代码 | 否 |
