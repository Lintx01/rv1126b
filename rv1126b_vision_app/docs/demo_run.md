# Demo Run Guide

## Final Demo Setup

- Video:
  RTSP + VLC
- Data:
  MQTT
- Local display:
  ST7789 + LVGL

## Build

```bash
cmake -S . -B build-rtsp \
  -DRV1126B_ENABLE_LVGL=ON \
  -DRV1126B_ENABLE_RGA=ON \
  -DRV1126B_ENABLE_RKNN=ON \
  -DRV1126B_ENABLE_OPENCV=ON \
  -DRV1126B_ENABLE_MPP=ON \
  -DRV1126B_ENABLE_MQTT=ON \
  -DRV1126B_ENABLE_GSTREAMER_RTSP=ON

cmake --build build-rtsp -j2
```

## Start

```bash
chmod +x run_demo.sh
./run_demo.sh
```

## VLC Address

```text
rtsp://192.168.137.2:8554/live
```

## HTTP-FLV Address

[http://192.168.137.2:8080/live.flv](http://192.168.137.2:8080/live.flv)

## MQTT Topics

```text
rv1126b/status
rv1126b/event
rv1126b/telemetry
```

## Logs

```bash
tail -f logs/demo_main.log
tail -f logs/rtsp_relay.log
```

Key log lines:

```text
[阈值][总览] gesture_prob=0.6, cup_score=0.5, pose_keypoint=0.35, drink_distance_norm=0.4, drink_consecutive_hits=3
[阈值][手势] 触发动作要求：top1_prob >= 0.6，且 class 属于 10/11/12/13
[阈值][饮品模型] class_ids=39|40|41, score >= 0.5 才进入候选，NMS IoU=0.45
[阈值][坐姿事件] keypoint_score >= 0.35；头前伸角 >= 35 判 HEAD_FORWARD；低头角 >= 15 判 HEAD_DOWN；后仰角 <= -15 判 HEAD_BACKWARD
[阈值][喝水事件] keypoint_score >= 0.35；杯子到头部归一化距离 <= 0.4 连续 3 次判 DRINK_DETECTED；有杯但距离更远判 NEED_REMIND
[AI][调度] 状态机=运行中(Running), 本轮模型=gesture(手势),pose(姿态),cup(饮品), frame=123
[AI][模型] 调用 gesture(手势), frame=123, input=224x224
[手势识别] frame=123, top1=class_13(点赞)
[手势事件] 点赞，class_13(点赞)
[显示状态] frame_id=123, 状态机=运行中(Running), posture_state=good(1), drink_state=normal(0)
```

The logs prefer Chinese for operator-facing text while keeping model names, enum names, tensor messages, and numeric fields in English where they are useful for debugging.

## Board Time Check

The SSH login banner or `uname -a` output, for example `Thu Dec 4 18:49:22 MST 2025`, is the kernel build time, not the current board time.

Check the real system time with:

```bash
date
date -u
timedatectl 2>/dev/null || true
```

The ST7789 idle clock uses the board epoch time plus `display_timezone_offset_minutes=480`, so the screen shows Beijing time (UTC+8). If the screen date is wrong, first synchronize the board system clock.

## Common Issues

If camera is busy:

```bash
pkill -f rv1126b_vision_app
pkill -f rv1126b_rtsp_relay
fuser -k /dev/video23
```

If port `8554` is occupied:

```bash
fuser -k 8554/tcp
```

If VLC cannot open the stream:

1. First confirm Windows can ping `192.168.137.2`
2. Then confirm the main program and relay are both running
