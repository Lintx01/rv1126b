#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

mkdir -p logs

pkill -f rv1126b_vision_app || true
pkill -f rv1126b_rtsp_relay || true
pkill -f gst-launch-1.0 || true
pkill -f ffmpeg || true
pkill -f ffplay || true
sleep 1

if [ ! -x build-rtsp/rv1126b_vision_app ]; then
    echo "[Demo] missing build-rtsp/rv1126b_vision_app"
    echo "[Demo] please build first"
    exit 1
fi

if [ ! -x build-rtsp/rv1126b_rtsp_relay ]; then
    echo "[Demo] missing build-rtsp/rv1126b_rtsp_relay"
    echo "[Demo] please build first"
    exit 1
fi

if [ ! -e /dev/video23 ]; then
    echo "[Demo][WARN] /dev/video23 not found"
fi

if [ ! -e /dev/spidev1.0 ]; then
    echo "[Demo][WARN] /dev/spidev1.0 not found"
fi

MAIN_PID=""
RELAY_PID=""

cleanup() {
    if [ -n "${RELAY_PID}" ]; then
        kill "${RELAY_PID}" 2>/dev/null || true
    fi
    if [ -n "${MAIN_PID}" ]; then
        kill "${MAIN_PID}" 2>/dev/null || true
    fi
}

trap cleanup INT TERM EXIT

RV_PREPROCESS_MODE=rga RV_FORCE_AI_RUNNING=1 \
./build-rtsp/rv1126b_vision_app > logs/demo_main.log 2>&1 &
MAIN_PID=$!

sleep 3

./build-rtsp/rv1126b_rtsp_relay \
--input http://127.0.0.1:8080/live.flv \
--port 8554 \
--mount /live > logs/rtsp_relay.log 2>&1 &
RELAY_PID=$!

echo "[Demo] HTTP-FLV: http://192.168.137.2:8080/live.flv"
echo "[Demo] RTSP VLC: rtsp://192.168.137.2:8554/live"
echo "[Demo] logs/demo_main.log"
echo "[Demo] logs/rtsp_relay.log"

wait
