# 第四部分  总结

## 4.1 可扩展之处

本作品后续可从以下方向扩展：一是扩展模型能力，在现有手势、姿态和饮品检测基础上增加疲劳检测、离座检测、视距估计和更多坐姿类别；二是扩展硬件交互，加入蜂鸣器、语音播报、触摸按键或更大尺寸屏幕；三是扩展数据服务，通过 MQTT 接入上位机或服务器进行长期习惯统计，但不替代本地 AI 推理；四是完善结构产品化设计，增加固定支架、外壳和摄像头角度调节；五是继续优化端侧性能，包括模型量化、推理调度、后处理和日志等级管理，并适配教室、自习室、办公工位等更多桌面环境。

## 4.2 心得体会

在开发过程中可以明显看到，端侧 AI 系统并不是把模型文件放到板端运行即可。一个可演示、可维护的作品需要同时打通摄像头采集、图像预处理、RKNN/NPU 推理、模型后处理、业务状态判断、本地显示、视频推流和通信同步等环节。本项目围绕 RV1126B 平台，将手势识别、姿态估计和饮品检测接入同一条运行链路，再通过 App 状态机把模型输出转化为启停控制、坐姿提醒、饮水判断和多端反馈，研发重点逐渐从“模型是否能跑”转向“系统是否能稳定形成闭环”。

模型部署和后处理是本项目中较关键的经验。手势模型、姿态模型和饮品检测模型的输入尺寸、输出含义和业务用途不同，不能只按统一模板处理。图像预处理需要为不同模型生成 224x224 或 640x640 输入，并记录坐标映射关系，便于人体框、关键点和杯/瓶框反算到原始画面。饮品检测还需要区分 COCO class-aware 与 BottleBoxesOnly 两种 profile；当前主程序使用 BottleBoxesOnly，模型不输出类别，结果应按 `bottle(box_only)` 和 `class_id=-1` 处理。类似 `[1,5,8400]` 这样的输出张量结构，必须结合 tensor shape 正确解析，不能简单按单类别 `class_id=0` 理解。

嵌入式联调还暴露出许多模型之外的问题。摄像头 `/dev/video23` 可能被旧进程占用，RTSP relay 的 8554 端口也可能因为旧进程未退出而无法重新绑定；MQTT 既有 libmosquitto 的真实发布路径，也有缺少依赖时的日志 fallback，文档和演示中必须区分二者；ST7789/LVGL 显示链路还需要结合实际 SPI 设备和 GPIO 接线核对。视频链路同样需要明确边界：主程序输出 HTTP-FLV，RTSP relay 是单独工具，用于把 HTTP-FLV 转为 VLC 可播放的 RTSP 地址。

状态机和交互体验决定了系统是否适合现场演示和实际使用。手势识别如果只依赖单帧结果，容易出现误触发或重复触发，因此当前系统加入稳定计数、触发冷却和释放锁机制；Start/Stop 用于切换运行状态，Confirm 和 Rock 更偏向提醒确认和互动反馈。饮水提醒也不是简单检测到杯子就报警，而是结合头部关键点、杯/瓶位置、归一化距离、连续命中次数和定时提醒进行综合判断。本地 ST7789 屏幕、HTTP-FLV/RTSP 视频查看和 MQTT 状态同步共同提高了系统可观察性。

总体来看，本项目形成了从端侧视觉感知、模型推理、状态判断到本地显示、视频查看和 MQTT 同步的完整工程闭环。后续工作应继续围绕实物材料、运行截图、功能验证表和性能测试记录进行补充，在不夸大未实测指标的前提下，完善第三部分成果展示，并为模型能力扩展、端侧性能优化和结构产品化设计打下基础。

# 第五部分  参考文献

[1] Rockchip. RKNN Toolkit User Guide[EB/OL]. 待核对版本号与访问日期.

[2] Rockchip. RKNN API Reference[EB/OL]. 待核对版本号与访问日期.

[3] Rockchip. RV1126/RV1109 Developer Guide[EB/OL]. 待核对文档名称、版本号与访问日期.

[4] Linux Media Infrastructure developers. Video4Linux2 API Specification[EB/OL]. 待核对访问日期.

[5] OpenCV Team. OpenCV Documentation[EB/OL]. 待核对版本号与访问日期.

[6] LVGL contributors. LVGL Documentation[EB/OL]. 待核对版本号与访问日期.

[7] GStreamer Project. GStreamer Application Development Manual[EB/OL]. 待核对版本号与访问日期.

[8] GStreamer Project. GStreamer RTSP Server Documentation[EB/OL]. 待核对版本号与访问日期.

[9] OASIS. MQTT Version 3.1.1 Standard[EB/OL]. 待核对访问日期.

[10] ITU-T. Recommendation H.264: Advanced video coding for generic audiovisual services[S]. 待核对版本年份.

[11] Ultralytics. YOLOv5 GitHub Repository[EB/OL]. 待核对访问日期.

[12] Ultralytics. Ultralytics YOLO Documentation[EB/OL]. 待核对版本号与访问日期.

[13] Cao Z, Simon T, Wei S E, et al. Realtime Multi-Person 2D Pose Estimation using Part Affinity Fields[C]. IEEE Conference on Computer Vision and Pattern Recognition, 2017. 待核对出版信息.

# 本轮自查

| 检查项 | 结果 |
| --- | -- |
| 4.1 是否 300字内 | 是 |
| 4.2 是否 1000字内 | 是 |
| 参考文献是否 20 篇以内 | 是，共 13 篇 |
| 是否没有写第三部分正文 | 是 |
| 是否没有编造实物照片/测试结果 | 是 |
| 是否没有写未实测 FPS/延迟/准确率/功耗 | 是 |
| 是否没有写医疗诊断效果 | 是 |
| 是否没有学校/老师信息 | 是 |
| 是否区分 COCO 与 BottleBoxesOnly | 是 |
| 是否没有把 BottleBoxesOnly 写成 class_id=0 | 是，写为 `class_id=-1` |
| 是否区分 HTTP-FLV 与 RTSP relay | 是 |
| 是否区分 MQTT 真实发布和 fallback | 是 |
| 是否修改代码 | 否 |
