# 手势单图测试说明

## 1. 功能说明

`gesture_image_test` 用于单图测试当前手势分类模型，确认 OK、5、大拇指等手势图片分别输出哪个 class、概率是多少，以及是否映射到实时程序会使用的 `GestureType`。

这个工具只验证手势分类结果，不等同于视频检测框。手势模型是分类模型，不会输出框。

## 2. 编译命令

```bash
cmake -S . -B build-tools \
-DRV1126B_ENABLE_RKNN=ON \
-DRV1126B_ENABLE_OPENCV=ON \
-DRV1126B_ENABLE_RGA=OFF \
-DRV1126B_ENABLE_LVGL=OFF \
-DRV1126B_ENABLE_MPP=OFF \
-DRV1126B_ENABLE_MQTT=OFF \
-DRV1126B_ENABLE_GSTREAMER_RTSP=OFF

cmake --build build-tools -j2 --target gesture_image_test
```

如果 OpenCV 或 RKNN SDK 没有找到，需要先修复板端依赖，不能把工具输出当成真实模型结果。

## 3. 运行命令

使用主程序默认手势模型：

```bash
./build-tools/gesture_image_test ./test_images/ok.jpg
./build-tools/gesture_image_test ./test_images/five.jpg
./build-tools/gesture_image_test ./test_images/thumb.jpg
```

也可以指定模型文件：

```bash
./build-tools/gesture_image_test ./test_images/thumb.jpg model/gesture_mobilenetv3_small_fp16.rknn
```

默认模型路径应和 `src/main.cpp` 中的 `gesture_model_path` 保持一致。

## 4. 输出怎么看

重点看这些字段：

* `top1 class`：模型概率最高的类别，例如 `class_13`。
* `prob`：top1 的 softmax 概率。
* `top5`：概率最高的 5 个类别，便于判断模型是否混淆。
* `mapped GestureType`：当前 class 映射到的动作类型，例如 `Start`、`Stop`、`Confirm`、`Rock` 或 `None`。
* `valid`：`GestureModel` 返回的调试字段。当前实时主程序主要依据 `GestureType` 触发动作；如果出现 `mapped=Start/Stop/Confirm/Rock` 但 `valid=false`，说明后续应检查 `GestureModel::parseOutput` 的 valid 赋值逻辑。

工具会分别测试 RGB 和 BGR 输入：

```text
========== RGB_INPUT ==========
========== BGR_INPUT ==========
```

并保存模型输入图：

```text
/tmp/gesture_input_rgb_debug.jpg
/tmp/gesture_input_bgr_debug.jpg
```

## 5. 调参建议

如果 `prob` 低于 `gesture_score_threshold`，实时程序不会触发动作。

如果 `top1 class` 没有映射到 `Start`、`Stop`、`Confirm`、`Rock`，实时程序也不会触发动作。

如果 RGB 和 BGR 的 top1 差异很大，需要确认模型训练/导出时使用的是 RGB 还是 BGR 输入顺序。

## 6. 当前建议记录表

| 手势 | 实测 top1 class | 实测 prob | 当前映射 | 是否触发 |
| --- | ------------- | ------- | ---- | ---- |
| class_6 / 数字6 | 待测试 | 待测试 | Start | prob >= 0.6 |
| class_5 / 数字5 | 待测试 | 待测试 | Stop | prob >= 0.6 |
| class_10 / OK | 待测试 | 待测试 | Confirm | prob >= 0.6 |
| class_13 / 点赞 | 待测试 | 待测试 | Confirm | prob >= 0.6 |
| class_12 / Rock | 待测试 | 待测试 | Rock | prob >= 0.6 |

说明：OK 和点赞容易互相识别错，所以 class_10 和 class_13 在业务上合并为 Confirm。Confirm/Rock 不改变 Running/Idle 主状态，只做反馈；Start/Stop 才改变主状态。
