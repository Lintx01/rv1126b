# 代码审查日志 2026-06-17

## 审查范围

- 项目目录：`G:\rv1126b_vision_app`
- 代码范围：`include/`、`src/`、`CMakeLists.txt`、`README.md`、`docs/`
- 审查重点：启动/停机路径、线程生命周期、RKNN/MPP/MQTT/显示等硬件边界、fallback 行为、JSON 输出、主机侧可编译性。

## 验证记录

- `git status --short`：失败，当前目录不是 Git 仓库。
- `cmake -S . -B build-codex-review ...`：失败，当前环境 PATH 中没有 `cmake`。
- `g++ -std=c++17 -Wall -Wextra -Wpedantic -Iinclude ...`：通过，生成主机 fallback 检查版；仅出现因 RKNN/MPP/RGA/OpenCV 宏未开启导致的未使用函数警告。
- `.\rv1126b_vision_app_codex_review.exe`：运行失败，mock 摄像头跑完后触发 `terminate called without an active exception`。
- 审查生成的临时文件 `rv1126b_vision_app_codex_review.exe` 已删除。

## 主要问题

### P0：工作线程请求退出后，`stop()` 早退导致 `std::thread` 析构时终止进程

- 位置：`src/App.cpp:156-181`、`src/App.cpp:246-253`、`src/App.cpp:212-213`、`src/main.cpp:19-20`
- 现象：默认配置启用 mock 摄像头，并设置 `mock_camera_frame_count = 120`。摄像头线程读完后在 `cameraLoop()` 内把 `exit_requested_` 置为 true 并关闭队列。随后 `run()` 跳出循环调用 `stop()`，但 `stop()` 第一行使用 `exit_requested_.exchange(true)`，发现已经为 true 后直接 `return`，没有 join 任何线程。`VisionApp` 析构时仍有 joinable 的 `std::thread`，进程触发 `std::terminate`。
- 已复现：主机 fallback 检查版运行到 mock 流程中途，最终输出 `terminate called without an active exception`，进程非正常退出。
- 影响：默认自测路径不可稳定退出；任何工作线程先行设置 `exit_requested_` 的路径都可能绕过 join 和资源关闭。
- 建议：把“请求退出”和“资源收尾只执行一次”拆成两个状态。`stop()` 即使发现退出已经被请求，也必须继续执行关闭队列、join 线程和关闭设备；可以新增 `stopped_` 或 `shutdown_started_` 原子量保护资源关闭的幂等性。

### P1：RKNN 输出释放不具备异常安全，拷贝输出时抛异常会泄漏 runtime buffer

- 位置：`src/RknnModel.cpp:158-171`
- 现象：`rknn_outputs_get()` 成功后，代码在 `outputs.emplace_back(values, values + count)` 拷贝所有输出，最后才调用 `rknn_outputs_release()`。如果 vector 分配内存抛出异常，或者未来解析路径增加抛异常逻辑，`rknn_outputs_release()` 不会执行。
- 影响：板端长期运行时可能泄漏 RKNN runtime 输出 buffer，最终导致推理失败或进程内存异常。
- 建议：用局部 RAII guard 包住 `rknn_outputs_release()`，确保 `rknn_outputs_get()` 成功后所有退出路径都会释放。

### P1：App 层的模型加载失败判断实际不会触发，fallback/stub 被当成加载成功

- 位置：`src/App.cpp:49-70`、`src/GestureModel.cpp:60-72`、`src/PerceptionModules.cpp:716-721`、`src/PerceptionModules.cpp:835-840`、`src/PostureDrinkModel.cpp:11-20`
- 现象：`GestureModel::load()`、`PoseModel::load()`、`CupModel::load()`、`PostureDrinkModel::load()` 都先设置 `loaded_ = true`，即使 `model_.load()` 失败也返回 true。于是 `App::start()` 中的“模型加载失败 warning”基本不会走到，真实 RKNN 不可用时系统仍以成功启动状态运行。
- 影响：部署到 RV1126B 时，模型路径错误、SDK 缺失、模型格式不匹配等问题会被 stub/fallback 掩盖。三模型 pipeline 可能持续输出 invalid，但主程序退出码仍可能是成功，容易误判板端 AI 已接入成功。
- 建议：区分开发 fallback 和生产必需模型。至少让 `load()` 返回真实 RKNN 是否加载成功，并在 `AppConfig` 中增加明确的 `allow_model_fallback` 或 `strict_model_load` 开关。

### P1：RKNN 输入格式被硬编码为 UINT8 NHWC，未校验模型输入 tensor

- 位置：`src/RknnModel.cpp:132-134`
- 现象：`RknnModel::run()` 固定设置 `RKNN_TENSOR_UINT8`、`RKNN_TENSOR_NHWC`，并用 `input.data.size()` 作为输入大小，没有对 `inputInfos()` 中的 dtype、format、dims、size 做一致性校验。
- 影响：如果导出的 RKNN 模型是 NCHW、float、int8/uint8 量化参数不同，或输入尺寸和配置不一致，运行可能失败，或者更糟糕的是输出数值不可信但不会立刻暴露。
- 建议：加载后记录输入 tensor 的类型、格式、尺寸；推理前严格校验 `Frame` 尺寸和字节数，不匹配时返回明确错误。必要时按 tensor 属性决定 NHWC/NCHW 和量化输入处理。

### P2：`appStateToJson()` 的转义不完整，控制字符会生成非法 JSON

- 位置：`include/Types.hpp:357-364`、`include/Types.hpp:369-379`
- 现象：`jsonEscape()` 只处理反斜杠和双引号，不处理换行、回车、制表符以及其他控制字符。`WebRtcTransport.cpp` 里的 `escapeJson()` 已处理 `\n`、`\r`、`\t`，但两处实现不一致。
- 影响：如果 `gesture_name` 或后续状态字段包含控制字符，MQTT/Web 输出可能不是合法 JSON。
- 建议：合并成一个统一的 JSON 转义函数，至少覆盖 `\n`、`\r`、`\t` 和 `0x00-0x1F` 控制字符。

### P2：三模型异步节奏下，融合结果可能使用不同帧的 pose/cup，却只报告 pose 的 frame_id

- 位置：`src/App.cpp:301-302`、`src/App.cpp:500-510`、`src/App.cpp:516-532`、`src/App.cpp:666-672`
- 现象：AI 循环缓存 `last_pose_result` 和 `last_cup_result`，pose/cup 按不同 interval 更新。融合时允许拿最近一次 pose 和最近一次 cup 组合，`composeVisionResult()` 又优先使用 pose 的 `frame_id`。运行日志中已经出现 cup tick 使用旧 pose frame_id 发布状态的情况。
- 影响：网页叠框、MQTT 告警、饮水距离判断可能把不同时间点的目标融合，导致结果错位，尤其人在移动或摄像头帧率较高时更明显。
- 建议：融合前检查 pose/cup 的时间戳或 frame_id 差值，超过阈值则不做跨帧融合；结果中同时携带 `pose_frame_id` 和 `cup_frame_id`，避免前端误以为所有结果来自同一帧。

### P3：多线程直接写 `std::cout/std::cerr`，日志行会互相穿插

- 位置：多处，例如 `src/App.cpp:760-766`、`src/DisplayDevice.cpp:149`
- 现象：运行验证中出现一行日志被其他线程插入的情况，例如 `DisplayState` 和 `DisplayMock` 输出交错。
- 影响：定位板端问题时日志可读性下降，自动化解析日志也不可靠。
- 建议：引入轻量日志函数，在内部持有 mutex 后一次性输出完整行；至少对关键状态日志统一加锁。

### P3：中文文档和注释在当前 PowerShell 输出中显示乱码

- 位置：`README.md`、多处源码中文注释、`docs/`
- 现象：终端读取时中文显示为 mojibake。文件本身可能是 UTF-8，但当前控制台编码不匹配；也可能存在部分文件编码不统一。
- 影响：不影响编译，但会影响 Windows 终端审查、搜索和后续维护。
- 建议：统一仓库文本为 UTF-8，并在 README 或开发说明里注明 Windows 终端使用 UTF-8；必要时增加 `.editorconfig`。

## 当前结论

项目主机 fallback 编译能通过，整体模块边界比较清楚，但还不能称为“严丝合缝”。当前最优先修复的是 `VisionApp::stop()` 的幂等停机问题，因为它已经在默认 mock 自测路径复现为进程崩溃。随后应处理 RKNN 输出释放、严格模型加载、输入 tensor 校验和跨帧融合一致性，这几项直接影响板端长期稳定性和 AI 结果可信度。

## 视觉模型前后处理补充梳理

- 摄像头输入：`CameraDevice` 优先请求 V4L2 `RGB24`，失败后退到 `YUYV`，并在 `yuyvToRgb24()` 中转成 RGB888。
- AI 预处理入口：`App::aiLoop()` 统一调用 `ImageProcessor::cropResize()`，当前传入的是整帧 crop，然后分别 resize 到 gesture/pose/cup 的输入尺寸。
- 预处理实现顺序：优先 RGA `cropResizeByRga()`，然后 OpenCV `cropResizeByOpenCv()`，最后软件最近邻 `cropResizeBySoftware()`。
- 颜色空间转换：OpenCV 路径支持 `NV12 -> RGB` 和 `BGR -> RGB`；软件路径支持 `BGR888 -> RGB888`；RGA 路径按 `PixelFormat` 映射到 Rockchip RGA 格式并输出 RGB888。
- RKNN 输入：`RknnModel::run()` 当前固定使用 `RKNN_TENSOR_UINT8` 和 `RKNN_TENSOR_NHWC`，没有按模型 tensor 属性动态适配。
- Gesture 后处理：`GestureModel::parseOutput()` 对分类输出做概率判断/softmax，取最大类并映射 Start/Stop/Heart。
- Pose 后处理：`PoseModel::parseOutput()` 支持 YOLOv8-pose raw DFL 输出和 decoded tensor 输出，做 box 解码、keypoint 解析和 NMS。
- Cup 后处理：`CupModel::parseOutput()` 支持 YOLOv8 detect raw DFL、decoded `[N,84]/[84,N]`、后处理 boxes、无 class 的 flat boxes，并只保留 COCO 饮水容器类别 `39/40/41/45`。
- 坐标映射修正：新增 `PreprocessTransform`，AI 预处理后保存原图尺寸、crop 区域和模型输入尺寸。`PoseModel` / `CupModel` 输出仍先按模型输入坐标解析，随后在 `App::aiLoop()` 内反变换回原始帧坐标。
- 饮水判断坐标系：`DrinkDetector::update()` 现在接收已经反变换后的 pose/cup 结果，因此“杯子中心到鼻子/头点距离”的逻辑判断使用原图坐标；网页叠框也可复用同一批原图坐标。
- 仍需注意：当前预处理是直接 resize，不是 letterbox；因此反变换按 `crop + x/y 独立 scale` 计算。若后续改成 YOLO 常见 letterbox，需要在 `PreprocessTransform` 中增加 padding 信息。
