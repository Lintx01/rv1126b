# 三个模型输出说明

本文档记录当前三模型管线使用的 RKNN 输入/输出 tensor 布局，以及 C++ 后处理代码如何解释这些输出。

## 手势模型

- 模型文件：`model/yolov5_gesture_rv1126b.rknn`
- 代码入口：`GestureModel::parseOutput()`
- 输入 tensor：`images`，形状 `[1, 3, 224, 224]`
- RKNN 内置预处理：RGB，`mean=[123.675, 116.28, 103.53]`，`std=[58.395, 57.12, 57.375]`
- 输出数量：1
- 输出 tensor：`output0`，形状 `[1, 15]`

输出含义：

- 15 个输出值表示 15 个手势类别的分类分数。
- 如果输出看起来是 logits，代码会先做 softmax。
- 最高概率类别会记录为 `gesture_name = class_x`。
- 只有部分 class id 会映射成系统动作，例如 `Start`、`Stop`、`Heart`、`Like`；其他类别保持为 `GestureType::None`。

排查要点：

- 如果 top-1 的 `class_x` 是对的，但触发动作不对，优先检查 `src/GestureModel.cpp` 里的类别 ID 映射。
- 如果训练集图片输入后 top-1 的 `class_x` 本身就不对，需要用同一张图片和同一套预处理对比 `.pt -> .onnx -> .rknn` 输出。

## 姿态模型

- 模型文件：`model/yolov8n-pose-rv1126b-i8.rknn`
- 代码入口：`PoseModel::parseOutput()`
- 输入 tensor：`images`，形状 `[1, 3, 640, 640]`
- RKNN 内置预处理：RGB，除以 255，`mean=[0, 0, 0]`，`std=[255, 255, 255]`
- 输出数量：4

输出 tensor：

```text
0: /model.22/Concat_1_output_0   [1, 65, 80, 80]
1: /model.22/Concat_2_output_0   [1, 65, 40, 40]
2: /model.22/Concat_3_output_0   [1, 65, 20, 20]
3: /model.22/Concat_6_output_0   [1, 17, 3, 8400]
```

输出含义：

- 输出 0-2 是 YOLOv8-pose 的三个检测尺度。
- 每个检测尺度的每个 grid cell 有 `65` 个通道：
  - `64 = 4 * 16`，表示 DFL box 距离分布。
  - `1`，表示 person 置信度。
- 输出 3 是关键点 tensor：
  - `17` 表示 COCO 17 个人体关键点。
  - `3` 表示每个关键点的 `x`、`y`、`score`。
  - `8400 = 80*80 + 40*40 + 20*20`，表示三个检测尺度合并后的总 anchor/grid cell 数。

后处理流程：

- 对每个 grid cell 做 DFL box 解码。
- 根据 person 置信度过滤候选框。
- 读取对应的 17 个关键点。
- 执行 NMS 去重。
- 选择分数最高的人体结果，供坐姿判断和喝水距离判断使用。

## 水杯模型

- 模型文件：`model/yolov8n_rv1126b_i8.rknn`
- 代码入口：`CupModel::parseOutput()`
- 输入 tensor：`images`，形状 `[1, 3, 640, 640]`
- RKNN 内置预处理：RGB，除以 255，`mean=[0, 0, 0]`，`std=[255, 255, 255]`
- 目标类别：COCO 饮水容器相关类别，`39=bottle`、`40=wine glass`、`41=cup`、`45=bowl`
- 输出数量：9

输出 tensor：

```text
80x80 尺度：
0: 318                    [1, 64, 80, 80]  box DFL
1: onnx::ReduceSum_326    [1, 80, 80, 80]  class scores
2: 331                    [1, 1, 80, 80]   objectness

40x40 尺度：
3: 338                    [1, 64, 40, 40]  box DFL
4: onnx::ReduceSum_346    [1, 80, 40, 40]  class scores
5: 350                    [1, 1, 40, 40]   objectness

20x20 尺度：
6: 357                    [1, 64, 20, 20]  box DFL
7: onnx::ReduceSum_365    [1, 80, 20, 20]  class scores
8: 369                    [1, 1, 20, 20]   objectness
```

输出含义：

- 水杯模型没有直接输出已经后处理好的检测框。
- 每个尺度拆成三个 tensor：
  - box tensor：`64 = 4 * 16`，表示 left/top/right/bottom 四个方向的 DFL 距离分布。
  - class tensor：80 个 COCO 类别分数。
  - objectness tensor：每个 grid cell 一个目标置信度。
- 饮水容器最终置信度计算方式：

```text
container_score = max(class_score[class_id in 39,40,41,45]) * objectness_score
```

后处理流程：

- 按相同 grid 尺寸匹配 box、class、objectness 三个 tensor。
- 对 box tensor 做 DFL 解码，得到检测框。
- 只保留 `39/40/41/45` 四类饮水容器分数，并取其中最高类别作为检测框标签。
- 使用 `cup_score_threshold` 过滤低分候选。
- 执行 NMS，输出最终水杯框。

当前修复后，正常应该看到的日志前缀是：

```text
cup_yolov8_split_decoder
```

如果这份 RKNN 仍然打出：

```text
cup_postprocessed_boxes
```

说明代码没有识别到真实的 9 路 YOLO 输出布局，可能又落到了宽松的通用框解析逻辑。那种情况下很容易出现异常大的候选数量，例如 `candidates=58032`。

## 日志字段含义

- `candidates`：NMS 之前、已经过阈值过滤的候选框数量。
- `kept_after_nms`：NMS 之后保留下来的框数量。
- `best_score`：保留下来的框中最高的分数。
- `coords=original`：表示框或关键点已经从模型输入坐标映射回原始摄像头画面坐标。

合理现象：

- 画面里没有饮水容器：应接近 `candidates=0,kept_after_nms=0`，结果无有效水杯框。
- 画面里有一个清晰水杯、水瓶、玻璃杯或碗：通常只有少量候选框，NMS 后一般保留 1 个主框。
- 如果 NMS 后仍然有几百个框，通常说明输出布局或分数解释错了，而不是画面里真的有这么多饮水容器。
