# Model Outputs

This document records the RKNN tensor layouts used by the current three-model pipeline and how the C++ post-processing code interprets each output.

## Gesture Model

- File: `model/yolov5_gesture_rv1126b.rknn`
- Code path: `GestureModel::parseOutput()`
- Input: `images`, shape `[1, 3, 224, 224]`
- Input preprocessing embedded in RKNN: RGB, `mean=[123.675, 116.28, 103.53]`, `std=[58.395, 57.12, 57.375]`
- Output count: 1
- Output tensor: `output0`, shape `[1, 15]`

Meaning:

- The 15 output values are gesture class scores.
- The code applies softmax when the values look like logits.
- The highest-probability class becomes `gesture_name = class_x`.
- Only selected class ids are mapped to system actions such as `Start`, `Stop`, `Heart`, and `Like`; all other classes remain `GestureType::None`.

Important:

- If the top-1 `class_x` is correct but the triggered action is wrong, check the class-id mapping in `src/GestureModel.cpp`.
- If the top-1 `class_x` is wrong even on training images, compare `.pt -> .onnx -> .rknn` outputs with the same image and preprocessing.

## Pose Model

- File: `model/yolov8n-pose-rv1126b-i8.rknn`
- Code path: `PoseModel::parseOutput()`
- Input: `images`, shape `[1, 3, 640, 640]`
- Input preprocessing embedded in RKNN: RGB, divide by 255, `mean=[0, 0, 0]`, `std=[255, 255, 255]`
- Output count: 4

Outputs:

```text
0: /model.22/Concat_1_output_0   [1, 65, 80, 80]
1: /model.22/Concat_2_output_0   [1, 65, 40, 40]
2: /model.22/Concat_3_output_0   [1, 65, 20, 20]
3: /model.22/Concat_6_output_0   [1, 17, 3, 8400]
```

Meaning:

- Outputs 0-2 are YOLOv8-pose detection heads at three grid scales.
- Each detection-head cell has `65` channels:
  - `64 = 4 * 16` DFL box-distance channels.
  - `1` person score channel.
- Output 3 contains keypoints:
  - `17` COCO keypoints.
  - `3` values per keypoint: `x`, `y`, `score`.
  - `8400 = 80*80 + 40*40 + 20*20` anchors/cells across all scales.

Post-processing:

- Decode DFL box distances for each grid cell.
- Filter by person score.
- Read the matching 17 keypoints.
- Run NMS.
- Keep the best person result for posture and drinking-distance logic.

## Cup Model

- File: `model/yolov8n_rv1126b_i8.rknn`
- Code path: `CupModel::parseOutput()`
- Input: `images`, shape `[1, 3, 640, 640]`
- Input preprocessing embedded in RKNN: RGB, divide by 255, `mean=[0, 0, 0]`, `std=[255, 255, 255]`
- Target class: COCO `class_id=41`, `cup`
- Output count: 9

Outputs:

```text
Scale 80x80:
0: 318                    [1, 64, 80, 80]  box DFL
1: onnx::ReduceSum_326    [1, 80, 80, 80]  class scores
2: 331                    [1, 1, 80, 80]   objectness

Scale 40x40:
3: 338                    [1, 64, 40, 40]  box DFL
4: onnx::ReduceSum_346    [1, 80, 40, 40]  class scores
5: 350                    [1, 1, 40, 40]   objectness

Scale 20x20:
6: 357                    [1, 64, 20, 20]  box DFL
7: onnx::ReduceSum_365    [1, 80, 20, 20]  class scores
8: 369                    [1, 1, 20, 20]   objectness
```

Meaning:

- The cup model does not output ready-made boxes.
- Each scale is split into three tensors:
  - Box tensor: `64 = 4 * 16` DFL channels for left/top/right/bottom distances.
  - Class tensor: 80 COCO class scores.
  - Objectness tensor: one object confidence value per grid cell.
- Cup confidence is computed as:

```text
cup_score = class_score[class_id=41] * objectness_score
```

Post-processing:

- Match box/class/objectness tensors by the same grid size.
- Decode DFL distances into boxes.
- Keep only `class_id=41`.
- Filter by `cup_score_threshold`.
- Run NMS and publish the remaining cup boxes.

Expected log prefix after the current fix:

```text
cup_yolov8_split_decoder
```

If logs show `cup_postprocessed_boxes` for this RKNN file, the parser did not recognize the real 9-output YOLO layout and likely fell back to an unsafe generic parser. That usually produces huge false candidate counts such as `candidates=58032`.

## Log Fields

- `candidates`: number of boxes above threshold before NMS.
- `kept_after_nms`: number of boxes kept after NMS.
- `best_score`: highest score among kept boxes.
- `coords=original`: boxes/keypoints were mapped back from model input coordinates to original camera-frame coordinates.

Healthy expectations:

- No cup in frame: `candidates=0,kept_after_nms=0`, result should be invalid or no cup boxes.
- One visible cup: usually a small number of candidates and 1 final box after NMS.
- Hundreds of boxes after NMS means the output layout or score interpretation is wrong.
