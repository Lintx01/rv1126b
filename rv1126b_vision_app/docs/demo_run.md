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
