from __future__ import annotations

import io
import math
import zipfile
import copy
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[4]
SRC = ROOT / "比赛文档_第三部分草稿_可读版_20260704_音频更新.docx"
OUT = ROOT / "比赛文档_第三部分草稿_可读版_20260704_音频更新_前两部分完善.docx"
FIG_DIR = ROOT / "rv1126b_vision_app" / "docs" / "competition" / "figures" / "audio_docx_front_update"


PALETTE = {
    "bg": (255, 255, 255),
    "ink": (31, 41, 55),
    "muted": (75, 85, 99),
    "edge": (51, 65, 85),
    "blue": (219, 234, 254),
    "blue_edge": (147, 197, 253),
    "green": (220, 252, 231),
    "green_edge": (110, 231, 183),
    "yellow": (254, 243, 199),
    "yellow_edge": (245, 158, 11),
    "purple": (237, 233, 254),
    "purple_edge": (167, 139, 250),
    "gray": (241, 245, 249),
    "gray_edge": (148, 163, 184),
}


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    candidates = [
        "C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    ]
    for name in candidates:
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


F_TITLE = font(34, True)
F_HEAD = font(22, True)
F_BOX = font(21, True)
F_SMALL = font(16)
F_TINY = font(14)


def text_size(draw: ImageDraw.ImageDraw, text: str, fnt: ImageFont.ImageFont) -> tuple[int, int]:
    if not text:
        return 0, 0
    box = draw.multiline_textbbox((0, 0), text, font=fnt, spacing=5)
    return box[2] - box[0], box[3] - box[1]


def center_text(
    draw: ImageDraw.ImageDraw,
    rect: tuple[int, int, int, int],
    title: str,
    subtitle: str = "",
    title_font: ImageFont.ImageFont = F_BOX,
    sub_font: ImageFont.ImageFont = F_SMALL,
    fill: tuple[int, int, int] = PALETTE["ink"],
) -> None:
    x, y, w, h = rect
    parts: list[tuple[str, ImageFont.ImageFont, tuple[int, int, int]]] = [(title, title_font, fill)]
    if subtitle:
        parts.append((subtitle, sub_font, PALETTE["muted"]))
    heights = [text_size(draw, t, f)[1] for t, f, _ in parts]
    total = sum(heights) + (10 if subtitle else 0)
    cy = y + (h - total) / 2
    for idx, (t, f, c) in enumerate(parts):
        tw, th = text_size(draw, t, f)
        draw.multiline_text((x + (w - tw) / 2, cy), t, font=f, fill=c, align="center", spacing=5)
        cy += th + (10 if idx == 0 and subtitle else 0)


def box(
    draw: ImageDraw.ImageDraw,
    rect: tuple[int, int, int, int],
    title: str,
    subtitle: str = "",
    fill: tuple[int, int, int] = PALETTE["gray"],
    outline: tuple[int, int, int] = PALETTE["gray_edge"],
    radius: int = 14,
) -> None:
    x, y, w, h = rect
    draw.rounded_rectangle((x, y, x + w, y + h), radius=radius, fill=fill, outline=outline, width=2)
    center_text(draw, rect, title, subtitle)


def arrow(draw: ImageDraw.ImageDraw, start: tuple[int, int], end: tuple[int, int], width: int = 4) -> None:
    draw.line((start, end), fill=PALETTE["edge"], width=width)
    ang = math.atan2(end[1] - start[1], end[0] - start[0])
    size = 13
    p1 = (end[0] - size * math.cos(ang - 0.45), end[1] - size * math.sin(ang - 0.45))
    p2 = (end[0] - size * math.cos(ang + 0.45), end[1] - size * math.sin(ang + 0.45))
    draw.polygon([end, p1, p2], fill=PALETTE["edge"])


def save_png(img: Image.Image, name: str) -> bytes:
    FIG_DIR.mkdir(parents=True, exist_ok=True)
    path = FIG_DIR / name
    img.save(path)
    bio = io.BytesIO()
    img.save(bio, format="PNG")
    return bio.getvalue()


def fig1_1() -> bytes:
    img = Image.new("RGB", (1500, 790), PALETTE["bg"])
    d = ImageDraw.Draw(img)
    d.text((750, 34), "图1-1 系统功能示意图", font=F_TITLE, fill=PALETTE["ink"], anchor="mm")
    headers = [("输入层", 180), ("端侧处理层", 545), ("功能层", 900), ("反馈层", 1255)]
    for t, x in headers:
        d.text((x, 108), t, font=F_HEAD, fill=PALETTE["muted"], anchor="mm")

    rows = [165, 290, 415, 540]
    left = [
        ("摄像头画面", "桌面前方视觉输入"),
        ("用户手势", "Start / Stop / Confirm / Rock"),
        ("人体姿态", "上半身与头肩状态"),
        ("桌面杯/瓶", "饮品容器位置"),
    ]
    mid = [
        ("RV1126B 端侧 AI", "本地感知\n本地判断"),
        ("图像预处理", "RGA 尺寸变换与坐标映射"),
        ("多模型协同", "手势识别\n姿态估计\n饮品检测"),
        ("状态机融合", "防抖、冷却\n提醒事件判断"),
    ]
    func = [
        ("手势启停", "非接触运行控制"),
        ("坐姿提醒", "基于姿态状态判断"),
        ("饮水判断", "杯/瓶与头部距离"),
        ("定时饮水提醒", "默认 30 分钟，可配置"),
    ]
    feedback = [
        ("ST7789 本地屏幕", "LVGL 状态反馈"),
        ("本地语音提示", "坐姿/饮水提示音"),
        ("HTTP-FLV / RTSP", "上位机视频查看"),
        ("MQTT 状态同步", "status / event / telemetry"),
    ]
    for i, y in enumerate(rows):
        box(d, (70, y, 260, 82), *left[i], PALETTE["blue"], PALETTE["blue_edge"])
        box(d, (430, y, 260, 82), *mid[i], PALETTE["green"], PALETTE["green_edge"])
        box(d, (800, y, 260, 82), *func[i], PALETTE["yellow"], PALETTE["yellow_edge"])
        box(d, (1165, y, 260, 82), *feedback[i], PALETTE["purple"], PALETTE["purple_edge"])
        arrow(d, (330, y + 41), (430, y + 41))
        arrow(d, (690, y + 41), (800, y + 41))
        arrow(d, (1060, y + 41), (1165, y + 41))
    d.text(
        (750, 735),
        "本图展示系统从视觉输入、端侧处理、功能判断到屏幕与语音等反馈输出的整体功能关系。",
        font=F_SMALL,
        fill=PALETTE["muted"],
        anchor="mm",
    )
    return save_png(img, "image2.png")


def fig1_3() -> bytes:
    img = Image.new("RGB", (1453, 376), PALETTE["bg"])
    d = ImageDraw.Draw(img)
    d.text((726, 38), "图1-3 系统设计流程图", font=F_TITLE, fill=PALETTE["ink"], anchor="mm")
    steps = [
        ("01", "需求分析", PALETTE["blue"], PALETTE["blue_edge"]),
        ("02", "平台选型", PALETTE["blue"], PALETTE["blue_edge"]),
        ("03", "摄像头采集\n链路搭建", PALETTE["green"], PALETTE["green_edge"]),
        ("04", "RKNN 模型部署", PALETTE["green"], PALETTE["green_edge"]),
        ("05", "图像预处理\n坐标映射", PALETTE["gray"], PALETTE["gray_edge"]),
        ("06", "模型后处理\n状态机设计", PALETTE["yellow"], PALETTE["yellow_edge"]),
        ("07", "本地显示/语音\n视频推流/MQTT", PALETTE["yellow"], PALETTE["yellow_edge"]),
        ("08", "实机验证\n参数调整", PALETTE["purple"], PALETTE["purple_edge"]),
    ]
    small_box_font = font(19, True)
    x, y, w, h, gap = 34, 150, 157, 98, 20
    for i, (num, title, fill, edge) in enumerate(steps):
        rx = x + i * (w + gap)
        d.rounded_rectangle((rx, y, rx + w, y + h), radius=14, fill=fill, outline=edge, width=2)
        center_text(d, (rx, y, w, h), title, "", small_box_font, F_TINY)
        d.ellipse((rx + 10, y + 10, rx + 36, y + 36), fill=PALETTE["bg"], outline=edge, width=2)
        d.text((rx + 23, y + 23), num, font=F_TINY, fill=PALETTE["muted"], anchor="mm")
        if i < len(steps) - 1:
            arrow(d, (rx + w, y + h // 2), (rx + w + gap, y + h // 2), width=3)
    return save_png(img, "image3.png")


def fig2_1() -> bytes:
    img = Image.new("RGB", (2160, 1000), PALETTE["bg"])
    d = ImageDraw.Draw(img)
    d.text((1080, 42), "图2-1 系统整体框图", font=F_TITLE, fill=PALETTE["ink"], anchor="mm")
    d.rounded_rectangle((300, 95, 1430, 900), radius=24, fill=(236, 253, 245), outline=PALETTE["green_edge"], width=3)
    d.text((865, 130), "RV1126B 端侧平台", font=font(30, True), fill=PALETTE["ink"], anchor="mm")

    box(d, (60, 180, 200, 95), "摄像头", "640×480\nNV12", PALETTE["blue"], PALETTE["blue_edge"])
    box(d, (380, 180, 220, 95), "图像采集", "V4L2 输入", PALETTE["gray"], PALETTE["gray_edge"])
    box(d, (680, 180, 240, 95), "图像预处理", "RGA 尺寸变换\n坐标映射", PALETTE["gray"], PALETTE["gray_edge"])
    arrow(d, (260, 228), (380, 228))
    arrow(d, (600, 228), (680, 228))

    models = [
        ((380, 375, 250, 105), "手势识别模型", "224×224\nStart / Stop / Confirm / Rock"),
        ((680, 375, 250, 105), "姿态估计模型", "640×640\n人体框 + 17 关键点"),
        ((980, 375, 250, 105), "饮品检测模型", "640×640\n杯/瓶候选框"),
    ]
    for rect, title, sub in models:
        box(d, rect, title, sub, PALETTE["purple"], PALETTE["purple_edge"])
        arrow(d, (rect[0] + rect[2] // 2, 275), (rect[0] + rect[2] // 2, rect[1]))
    d.line((505, 325, 1105, 325), fill=PALETTE["edge"], width=4)
    d.text((805, 315), "RKNN / NPU 端侧推理", font=F_HEAD, fill=PALETTE["muted"], anchor="mm")

    box(d, (560, 620, 380, 130), "状态融合与事件判断", "手势状态机\n坐姿判断\n饮水判断\n定时提醒", PALETTE["yellow"], PALETTE["yellow_edge"])
    for sx in (505, 805, 1105):
        arrow(d, (sx, 480), (750, 620))
    box(d, (1045, 635, 260, 95), "系统状态", "Idle / Running\nAppState / VisionResult", PALETTE["yellow"], PALETTE["yellow_edge"])
    arrow(d, (940, 685), (1045, 685))

    outputs = [
        ((1560, 175, 250, 85), "MPP H.264 编码", "叠加推理结果"),
        ((1560, 320, 250, 85), "HTTP-FLV", "8080/live.flv"),
        ((1560, 465, 250, 85), "RTSP relay", "8554/live\n独立转换工具"),
        ((1560, 610, 250, 85), "ST7789/LVGL", "本地显示"),
        ((1560, 735, 250, 85), "本地语音提示", "坐姿/饮水提示音"),
        ((1560, 860, 250, 85), "MQTT", "status / event / telemetry"),
    ]
    for rect, title, sub in outputs:
        box(d, rect, title, sub, PALETTE["purple"], PALETTE["purple_edge"])
    arrow(d, (920, 228), (1560, 218))
    arrow(d, (1685, 260), (1685, 320))
    arrow(d, (1685, 405), (1685, 465))
    for y in (652, 777, 902):
        arrow(d, (1305, 685), (1560, y))

    box(d, (1920, 560, 180, 270), "上位机", "VLC 播放\nMQTT 订阅\n调试与截图", PALETTE["blue"], PALETTE["gray_edge"])
    arrow(d, (1810, 507), (1920, 640))
    arrow(d, (1810, 902), (1920, 740))
    return save_png(img, "image4.png")


def fig2_3() -> bytes:
    img = Image.new("RGB", (1452, 736), PALETTE["bg"])
    d = ImageDraw.Draw(img)
    d.text((726, 35), "图2-3 软件架构图", font=F_TITLE, fill=PALETTE["ink"], anchor="mm")
    panels = [
        (40, 70, 320, 585, "采集与预处理"),
        (400, 70, 500, 585, "AI 推理与业务判断"),
        (900, 70, 510, 585, "输出反馈与上位机查看"),
    ]
    for x, y, w, h, title in panels:
        d.rounded_rectangle((x, y, x + w, y + h), radius=16, outline=(203, 213, 225), width=2)
        d.text((x + w / 2, y + 24), title, font=F_HEAD, fill=PALETTE["ink"], anchor="mm")

    box(d, (95, 110, 215, 75), "主控调度", "配置加载\nIdle / Running 状态管理", PALETTE["yellow"], PALETTE["yellow_edge"])
    box(d, (95, 250, 215, 75), "摄像头采集", "V4L2\n640×480 NV12", PALETTE["blue"], PALETTE["blue_edge"])
    box(d, (95, 390, 215, 75), "图像预处理", "RGA 尺寸变换\n坐标映射", PALETTE["gray"], PALETTE["gray_edge"])
    arrow(d, (203, 185), (203, 250))
    arrow(d, (203, 325), (203, 390))

    box(d, (520, 125, 235, 70), "RKNN/NPU 端侧推理", "", PALETTE["purple"], PALETTE["purple_edge"])
    mods = [
        (430, 255, "手势识别", "Start / Stop\nConfirm / Rock"),
        (590, 255, "姿态估计", "人体框\n17个关键点"),
        (750, 255, "饮品检测", "杯/瓶候选框"),
    ]
    for x, y, title, sub in mods:
        box(d, (x, y, 150, 85), title, sub, PALETTE["purple"], PALETTE["purple_edge"])
        arrow(d, (640, 195), (x + 75, y))
    box(d, (560, 440, 240, 105), "业务状态融合", "手势状态机\n坐姿判断\n饮水判断\n定时提醒", PALETTE["yellow"], PALETTE["yellow_edge"])
    for x, *_ in mods:
        arrow(d, (x + 75, 340), (680, 440))
    arrow(d, (310, 428), (520, 160))

    bus_x = 930
    d.line((bus_x, 150, bus_x, 545), fill=PALETTE["edge"], width=4)
    arrow(d, (800, 492), (bus_x, 492), width=4)
    outs = [
        (990, 115, "ST7789/LVGL 本地显示", "状态反馈\n提醒界面"),
        (990, 245, "AudioReminder 语音提示", "坐姿/饮水提示音"),
        (990, 375, "HTTP-FLV 视频输出", "MPP H.264 编码\n8080/live.flv"),
        (990, 505, "RTSP relay / MQTT", "VLC 查看\n状态事件同步"),
    ]
    for x, y, title, sub in outs:
        box(d, (x, y, 245, 80), title, sub, PALETTE["purple"], PALETTE["purple_edge"])
        arrow(d, (bus_x, y + 40), (x, y + 40), width=3)
    box(d, (1265, 440, 110, 150), "上位机", "VLC 播放\nMQTT 订阅\n日志与截图", PALETTE["blue"], PALETTE["gray_edge"])
    arrow(d, (1220, 415), (1265, 480))
    arrow(d, (1220, 545), (1265, 535))
    return save_png(img, "image5.png")


def fig2_4() -> bytes:
    img = Image.new("RGB", (1453, 1395), PALETTE["bg"])
    d = ImageDraw.Draw(img)
    d.text((726, 35), "图2-4 主程序运行流程图", font=F_TITLE, fill=PALETTE["ink"], anchor="mm")
    d.text((726, 63), "启动与初始化", font=F_SMALL, fill=PALETTE["muted"], anchor="mm")
    flow = [
        (620, 85, "程序启动"),
        (620, 185, "读取运行配置"),
        (620, 295, "初始化硬件与模块"),
        (620, 405, "加载 RKNN 模型"),
        (620, 525, "主循环"),
        (620, 635, "摄像头采集"),
        (620, 745, "图像预处理"),
        (620, 855, "系统状态判断"),
    ]
    for i, (x, y, title) in enumerate(flow):
        fill = PALETTE["yellow"] if title in {"加载 RKNN 模型", "主循环", "系统状态判断"} else PALETTE["gray"]
        edge = PALETTE["yellow_edge"] if fill == PALETTE["yellow"] else PALETTE["gray_edge"]
        box(d, (x, y, 220, 55), title, "", fill, edge)
        if i:
            arrow(d, (730, flow[i - 1][1] + 55), (730, y))

    d.text((730, 930), "状态分支", font=F_HEAD, fill=PALETTE["muted"], anchor="mm")
    box(d, (185, 990, 240, 110), "Idle", "手势识别\n等待 Start", PALETTE["yellow"], PALETTE["yellow_edge"])
    box(d, (960, 990, 250, 110), "Running", "多模型推理\n坐姿判断\n饮水判断", PALETTE["purple"], PALETTE["purple_edge"])
    arrow(d, (730, 910), (305, 990))
    arrow(d, (730, 910), (1085, 990))
    d.line((305, 1100, 305, 1170), fill=PALETTE["edge"], width=4)
    d.line((1085, 1100, 1085, 1170), fill=PALETTE["edge"], width=4)
    box(d, (610, 1170, 260, 65), "状态融合", "汇总手势、坐姿与饮水状态", PALETTE["yellow"], PALETTE["yellow_edge"])
    arrow(d, (305, 1170), (610, 1202))
    arrow(d, (1085, 1170), (870, 1202))

    d.text((740, 1242), "输出与回环", font=F_HEAD, fill=PALETTE["muted"], anchor="mm")
    outs = [
        (250, 1268, "ST7789 显示"),
        (520, 1268, "语音提示"),
        (790, 1268, "HTTP-FLV 输出"),
        (1060, 1268, "MQTT 同步"),
    ]
    for x, y, title in outs:
        box(d, (x, y, 180, 50), title, "", PALETTE["purple"], PALETTE["purple_edge"])
        arrow(d, (740, 1235), (x + 90, y))
    box(d, (625, 1338, 230, 40), "返回主循环", "", PALETTE["yellow"], PALETTE["yellow_edge"])
    arrow(d, (740, 1318), (740, 1338), width=3)
    return save_png(img, "image6.png")


def fig2_6() -> bytes:
    img = Image.new("RGB", (1451, 933), PALETTE["bg"])
    d = ImageDraw.Draw(img)
    d.text((725, 35), "图2-6 饮水提醒判断流程图", font=F_TITLE, fill=PALETTE["ink"], anchor="mm")
    d.text((160, 95), "输入", font=F_HEAD, fill=PALETTE["muted"], anchor="mm")
    d.text((430, 95), "视觉判断", font=F_HEAD, fill=PALETTE["muted"], anchor="mm")
    d.text((725, 300), "定时提醒", font=F_HEAD, fill=PALETTE["muted"], anchor="mm")
    d.text((1235, 95), "状态输出", font=F_HEAD, fill=PALETTE["muted"], anchor="mm")

    box(d, (70, 120, 200, 70), "姿态估计结果", "人体框\n头部关键点", PALETTE["blue"], PALETTE["blue_edge"])
    box(d, (70, 245, 200, 70), "饮品检测结果", "杯/瓶候选框", PALETTE["blue"], PALETTE["blue_edge"])
    box(d, (315, 120, 230, 58), "选择头部点", "", PALETTE["gray"], PALETTE["gray_edge"])
    box(d, (315, 245, 230, 58), "杯/瓶有效性判断", "", PALETTE["gray"], PALETTE["gray_edge"])
    box(d, (315, 375, 230, 58), "计算头部距离", "杯/瓶中心到头部", PALETTE["gray"], PALETTE["gray_edge"])
    box(d, (315, 500, 230, 58), "距离归一化", "人体框尺度参考", PALETTE["gray"], PALETTE["gray_edge"])
    box(d, (315, 625, 230, 58), "距离阈值判断", "", PALETTE["yellow"], PALETTE["yellow_edge"])
    box(d, (315, 750, 230, 58), "连续命中判断", "", PALETTE["yellow"], PALETTE["yellow_edge"])
    arrow(d, (270, 155), (315, 149))
    arrow(d, (270, 280), (315, 274))
    arrow(d, (430, 303), (430, 375))
    arrow(d, (430, 433), (430, 500))
    arrow(d, (430, 558), (430, 625))
    arrow(d, (430, 683), (430, 750))
    d.line((575, 149, 575, 404), fill=PALETTE["edge"], width=4)
    arrow(d, (545, 149), (575, 149), width=3)
    arrow(d, (545, 274), (575, 274), width=3)
    arrow(d, (575, 404), (625, 459), width=3)

    box(d, (625, 430, 210, 58), "饮水定时器计时", "", PALETTE["yellow"], PALETTE["yellow_edge"])
    box(d, (625, 555, 210, 58), "定时器判断", "达到提醒间隔", PALETTE["yellow"], PALETTE["yellow_edge"])
    box(d, (625, 680, 210, 58), "Running 状态", "", PALETTE["yellow"], PALETTE["yellow_edge"])
    box(d, (625, 800, 210, 58), "DrinkDetected", "", PALETTE["purple"], PALETTE["purple_edge"])
    arrow(d, (730, 738), (730, 800))
    arrow(d, (730, 680), (730, 613))
    arrow(d, (730, 555), (730, 488))
    arrow(d, (545, 779), (625, 829))

    box(d, (900, 410, 230, 70), "饮水状态融合", "", PALETTE["yellow"], PALETTE["yellow_edge"])
    arrow(d, (835, 584), (900, 445), width=3)
    arrow(d, (835, 829), (900, 445), width=3)
    outputs = [
        (1190, 140, "Normal", "无提醒或监测中"),
        (1190, 270, "NeedRemind", ""),
        (1190, 390, "屏幕/语音提醒", "ST7789 + 提示音"),
        (1190, 540, "DrinkDetected", "记录饮水事件"),
        (1190, 690, "清除本次提醒", ""),
        (1190, 815, "返回监测", ""),
    ]
    for x, y, title, sub in outputs:
        box(d, (x, y, 200, 65), title, sub, PALETTE["purple"], PALETTE["purple_edge"])
    d.line((1160, 170, 1160, 845), fill=PALETTE["edge"], width=4)
    arrow(d, (1130, 445), (1160, 445), width=3)
    arrow(d, (1160, 172), (1190, 172), width=3)
    arrow(d, (1160, 302), (1190, 302), width=3)
    arrow(d, (1290, 335), (1290, 390))
    arrow(d, (1160, 572), (1190, 572), width=3)
    arrow(d, (1290, 605), (1290, 690))
    arrow(d, (1030, 730), (1190, 722))
    box(d, (900, 700, 200, 58), "Confirm 确认", "确认 active 提醒", PALETTE["yellow"], PALETTE["yellow_edge"])
    arrow(d, (1290, 755), (1290, 815))
    return save_png(img, "image8.png")


TEXT_REPLACEMENTS = {
    "在端侧完成图像采集、图像预处理、RKNN/NPU推理、状态判断和多端反馈，用于提供坐姿提醒、饮水提醒和非接触式交互辅助。":
        "在端侧完成图像采集、图像预处理、RKNN/NPU推理、状态判断和多端反馈，用于提供坐姿提醒、饮水提醒、语音提示和非接触式交互辅助。",
    "本作品面向桌面学习和办公环境，构建从视觉采集、端侧推理、状态判断到本地反馈的完整链路。":
        "本作品面向桌面学习和办公环境，构建从视觉采集、端侧推理、状态判断到本地声画反馈的完整链路。",
    "多端反馈：通过 ST7789/LVGL 本地显示、HTTP-FLV 视频输出、RTSP relay 视频查看和 MQTT 状态同步形成可观察的反馈链路。":
        "多端反馈：通过 ST7789/LVGL 本地显示、语音提示、HTTP-FLV 视频输出、RTSP relay 视频查看和 MQTT 状态同步形成可观察的反馈链路。",
    "多端反馈链路：结合本地屏幕、视频流、RTSP relay和MQTT状态同步，形成端侧可观察的反馈闭环。":
        "多端反馈链路：结合本地屏幕、语音提示、视频流、RTSP relay和MQTT状态同步，形成端侧可观察的反馈闭环。",
    "结合 ST7789 本地显示、视频查看和 MQTT 状态同步，提升系统演示和调试的可观察性。":
        "结合 ST7789 本地显示、语音提示、视频查看和 MQTT 状态同步，提升系统演示和调试的可观察性。",
    "ST7789 显示、视频输出和 MQTT 通信联调":
        "ST7789 显示、语音提示、视频输出和 MQTT 通信联调",
    "本地与上位机反馈":
        "本地声画反馈与上位机反馈",
    "业务层将模型结果融合为系统状态，并分发给本地显示、视频输出和通信同步链路。":
        "业务层将模型结果融合为系统状态，并分发给本地显示、语音提示、视频输出和通信同步链路。",
    "主程序提供HTTP-FLV视频输出，RTSP relay 作为独立转换工具供 VLC 查看，MQTT用于状态、事件和遥测同步":
        "主程序提供本地显示和语音提示，同时输出HTTP-FLV视频；RTSP relay 作为独立转换工具供 VLC 查看，MQTT用于状态、事件和遥测同步",
    "硬件系统由RV1126B主控平台、摄像头、ST7789显示屏、网络连接和上位机构成。":
        "硬件系统由RV1126B主控平台、摄像头、ST7789显示屏、音频输出设备、网络连接和上位机构成。",
    "ST7789 显示屏用于本地状态反馈；网络连接承载视频查看和状态同步；":
        "ST7789 显示屏用于本地状态反馈；音频输出设备用于播放坐姿和饮水提示音；网络连接承载视频查看和状态同步；",
    "ST7789 显示屏面向用户，便于在不查看上位机的情况下观察运行状态；":
        "ST7789 显示屏面向用户，便于在不查看上位机的情况下观察运行状态；音频输出设备面向用户，用于在坐姿或饮水提醒触发时给出提示音；",
    "ST7789 显示链路通过 SPI/GPIO 完成本地界面输出；网络通信链路承载视频访问、RTSP relay 和 MQTT 状态同步；":
        "ST7789 显示链路通过 SPI/GPIO 完成本地界面输出；音频提示链路播放本地 wav 提示音；网络通信链路承载视频访问、RTSP relay 和 MQTT 状态同步；",
    "业务状态融合、显示、视频和通信链路。":
        "业务状态融合、显示、语音、视频和通信链路。",
    "输出层负责本地显示、视频输出和 MQTT 状态同步。":
        "输出层负责本地显示、语音提示、视频输出和 MQTT 状态同步。",
    "并将结果输出至显示、视频和通信链路。":
        "并将结果输出至显示、语音、视频和通信链路。",
    "负责将采集、AI 推理、显示、视频和通信模块组织为统一运行链路。":
        "负责将采集、AI 推理、显示、语音、视频和通信模块组织为统一运行链路。",
    "Running状态执行姿态估计、饮品检测、饮水判断和提醒输出；":
        "Running状态执行姿态估计、饮品检测、饮水判断、屏幕提醒和语音提示输出；",
    "定时饮水提醒默认首次间隔为 30 分钟，重复提醒间隔为 5 分钟；":
        "定时饮水提醒默认首次间隔为 30 分钟，重复提醒间隔为 5 分钟，到点后可触发屏幕与语音提示；",
    "姿态结果可进一步参与本地显示、视频叠加和MQTT事件输出。":
        "姿态结果可进一步参与本地显示、语音提示、视频叠加和MQTT事件输出。",
    "系统进入NeedRemind；当用户通过Confirm确认提醒或检测到饮水动作后":
        "系统进入NeedRemind并触发屏幕与语音提醒；当用户通过Confirm确认提醒或检测到饮水动作后",
    "2.3.7 本地显示、视频与通信输出":
        "2.3.7 本地显示、语音、视频与通信输出",
    "本地显示链路通过ST7789和LVGL将系统状态反馈给用户，可展示待机、运行、手势反馈、坐姿提醒、饮水提醒和饮水检测等界面状态。显示模块接收业务层输出的状态结果，并负责将其转化为本地屏幕界面。":
        "本地显示链路通过ST7789和LVGL将系统状态反馈给用户，可展示待机、运行、手势反馈、坐姿提醒、饮水提醒和饮水检测等界面状态。语音提示链路与屏幕提醒配合，在坐姿异常或饮水提醒触发时播放本地提示音，增强桌面场景下的即时反馈。显示模块接收业务层输出的状态结果，并负责将其转化为本地屏幕界面。",
}


NS = {
    "w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main",
}


def cell_text(tc: ET.Element) -> str:
    return "".join(t.text or "" for t in tc.findall(".//w:t", NS))


def set_cell_text(tc: ET.Element, value: str) -> None:
    texts = tc.findall(".//w:t", NS)
    if not texts:
        p = tc.find("w:p", NS)
        if p is None:
            p = ET.SubElement(tc, f"{{{NS['w']}}}p")
        r = ET.SubElement(p, f"{{{NS['w']}}}r")
        t = ET.SubElement(r, f"{{{NS['w']}}}t")
        t.text = value
        return
    texts[0].text = value
    for extra in texts[1:]:
        extra.text = ""


def row_cells(tr: ET.Element) -> list[ET.Element]:
    return tr.findall("w:tc", NS)


def insert_row_after(table: ET.Element, anchor_first_cell: str, values: list[str]) -> bool:
    rows = table.findall("w:tr", NS)
    for idx, tr in enumerate(rows):
        cells = row_cells(tr)
        if cells and cell_text(cells[0]) == anchor_first_cell:
            new_tr = copy.deepcopy(tr)
            new_cells = row_cells(new_tr)
            for i, value in enumerate(values):
                if i < len(new_cells):
                    set_cell_text(new_cells[i], value)
            table.insert(idx + 1, new_tr)
            return True
    return False


def reorder_table_rows(table: ET.Element, desired_first_cells: list[str]) -> None:
    rows = table.findall("w:tr", NS)
    if not rows:
        return
    by_key: dict[str, ET.Element] = {}
    extras: list[ET.Element] = []
    for tr in rows:
        cells = row_cells(tr)
        key = cell_text(cells[0]) if cells else ""
        if key in desired_first_cells and key not in by_key:
            by_key[key] = tr
        else:
            extras.append(tr)
    ordered = [by_key[key] for key in desired_first_cells if key in by_key]
    for tr in rows:
        table.remove(tr)
    for tr in ordered + [tr for tr in extras if tr not in ordered]:
        table.append(tr)


def rebuild_table_body(table: ET.Element, body_values: list[list[str]]) -> None:
    rows = table.findall("w:tr", NS)
    if not rows:
        return
    header_template = copy.deepcopy(rows[0])
    body_template = copy.deepcopy(rows[1] if len(rows) > 1 else rows[0])
    for tr in rows:
        table.remove(tr)
    table.append(header_template)
    for values in body_values:
        new_tr = copy.deepcopy(body_template)
        cells = row_cells(new_tr)
        for i, value in enumerate(values):
            if i < len(cells):
                set_cell_text(cells[i], value)
        table.append(new_tr)


def replace_paragraph(root: ET.Element, old: str, new: str) -> bool:
    for p in root.findall(".//w:p", NS):
        text = "".join(t.text or "" for t in p.findall(".//w:t", NS))
        if text == old:
            texts = p.findall(".//w:t", NS)
            if texts:
                texts[0].text = new
                for extra in texts[1:]:
                    extra.text = ""
            return True
    return False


def structured_doc_updates(doc_xml: str) -> tuple[str, list[str]]:
    ET.register_namespace("w", NS["w"])
    root = ET.fromstring(doc_xml.encode("utf-8"))
    missing: list[str] = []

    if not replace_paragraph(
        root,
        "输出侧采用本地显示、视频查看和状态同步相结合的方式：ST7789/LVGL用于状态反馈与提醒界面，HTTP-FLV用于主程序视频输出，RTSP relay用于上位机VLC查看，MQTT用于状态、事件和遥测信息同步。作品不依赖云端识别，重点体现端侧 AI 推理、多模型协同、状态机融合和嵌入式系统集成能力。",
        "输出侧采用本地显示、语音提示、视频查看和状态同步相结合的方式：ST7789/LVGL用于状态反馈与提醒界面，本地音频用于坐姿和饮水提示音播报，HTTP-FLV用于主程序视频输出，RTSP relay用于上位机VLC查看，MQTT用于状态、事件和遥测信息同步。作品不依赖云端识别，重点体现端侧 AI 推理、多模型协同、状态机融合和嵌入式系统集成能力。",
    ):
        missing.append("摘要输出侧段落")

    tables = root.findall(".//w:tbl", NS)
    table_updates = [
        (1, "本地显示", ["语音提示", "音频输出", "本地 wav 音频", "坐姿和饮水提示音播报"], "表1-1 语音提示行"),
        (3, "ST7789 显示屏", ["音频输出设备", "播放坐姿和饮水提示音", "本地 wav 音频，ALSA/aplay 播放", "与屏幕提醒形成声画反馈"], "表2-1 音频输出行"),
        (4, "ST7789显示链路", ["音频提示链路", "提醒事件、本地 wav 文件", "坐姿和饮水提示音", "通过系统音频设备播放本地提示音"], "表2-2 音频提示行"),
        (5, "本地显示", ["语音提示", "播放坐姿和饮水提示音", "提醒事件、音频文件", "本地语音反馈"], "表2-3 语音提示行"),
    ]
    for table_idx, anchor, values, label in table_updates:
        if table_idx >= len(tables) or not insert_row_after(tables[table_idx], anchor, values):
            missing.append(label)

    table_rebuilds = {
        1: [
            ["摄像头输入", "分辨率与格式", "640×480，NV12", "视频采集配置"],
            ["手势模型", "输入尺寸", "224×224", "模型输入配置"],
            ["姿态模型", "输入尺寸", "640×640", "模型输入配置"],
            ["饮品模型", "输入尺寸", "640×640", "模型输入配置"],
            ["本地显示", "屏幕规格", "ST7789，240×240", "本地状态反馈"],
            ["语音提示", "音频输出", "本地 wav 音频", "坐姿和饮水提示音播报"],
            ["视频输出", "HTTP-FLV", "8080/live.flv", "主程序视频输出接口"],
            ["视频查看", "RTSP relay", "8554/live", "独立转换后供 VLC 查看"],
            ["状态同步", "MQTT 主题", "rv1126b/status；rv1126b/event；rv1126b/telemetry", "状态、事件和遥测同步"],
            ["饮水提醒", "定时间隔", "默认 30 分钟，重复提醒 5 分钟", "可配置提醒参数"],
        ],
        3: [
            ["RV1126B 平台", "系统主控、NPU 推理、视频编码与网络通信", "板载 NPU、MPP、RGA、V4L2 等能力", "完成端侧 AI 与多链路输出"],
            ["摄像头", "采集用户桌面场景图像", "640×480，NV12", "为手势识别、姿态估计和饮品检测提供输入"],
            ["ST7789 显示屏", "本地显示系统状态与提醒界面", "240×240，SPI/GPIO 连接", "提供待机、运行、提醒等状态反馈"],
            ["音频输出设备", "播放坐姿和饮水提示音", "本地 wav 音频，ALSA/aplay 播放", "与屏幕提醒形成声画反馈"],
            ["网络连接", "承载视频访问、RTSP relay 和 MQTT 状态同步", "以太网或无线网络", "支持上位机查看与远程状态同步"],
            ["上位机", "用于 VLC 查看 RTSP 视频、调试 MQTT 消息和观察运行结果", "Windows/Linux 终端设备", "完成展示、调试与验证"],
        ],
        4: [
            ["摄像头采集链路", "摄像头、V4L2、图像缓冲区", "NV12 图像帧", "为预处理与模型推理提供原始图像"],
            ["ST7789显示链路", "SPI/GPIO、LVGL 显示模块", "状态界面与提醒界面", "完成本地可视化反馈"],
            ["音频提示链路", "提醒事件、本地 wav 文件", "坐姿和饮水提示音", "通过系统音频设备播放本地提示音"],
            ["网络通信链路", "HTTP-FLV、RTSP relay、MQTT", "视频流、状态、事件和遥测数据", "支持上位机查看和远程同步"],
            ["电源与公共连接", "开发板、电源适配器、外设连接线", "稳定供电和公共接口", "保证系统持续运行"],
        ],
        5: [
            ["主控与状态融合", "负责将采集、AI 推理、显示、语音、视频和通信模块组织为统一运行链路", "系统状态、模型结果、提醒事件", "统一状态机与事件分发"],
            ["视觉采集", "通过摄像头获取桌面场景图像", "NV12 图像帧", "提供实时视觉输入"],
            ["图像预处理", "完成图像尺寸转换、格式适配和坐标映射", "模型输入张量、映射参数", "为多模型推理准备输入"],
            ["手势识别", "识别 Start、Stop、Confirm 和 Rock 等控制手势", "手势类别与置信度", "用于非接触式交互控制"],
            ["姿态估计", "检测人体框和关键点，判断坐姿状态", "人体框、关键点、姿态状态", "用于坐姿提醒和状态融合"],
            ["饮品检测", "检测桌面杯子或瓶子等饮品目标", "饮品框与置信度", "用于饮水判断"],
            ["饮水判断", "结合头部与杯瓶位置、定时器和状态机判断饮水提醒", "Normal、NeedRemind、DrinkDetected 等状态", "触发饮水提醒或清除提醒"],
            ["本地显示", "在 ST7789/LVGL 上显示运行状态、手势反馈和提醒界面", "状态结果与提醒事件", "本地可视化反馈"],
            ["语音提示", "播放坐姿和饮水提示音", "提醒事件、音频文件", "本地语音反馈"],
            ["视频输出与查看", "通过 HTTP-FLV 输出主程序视频，并通过 RTSP relay 供 VLC 查看", "叠加检测结果的视频流", "支持展示和调试"],
            ["MQTT 通信", "同步状态、事件和遥测信息", "status、event、telemetry 主题", "支持远程观察与数据记录"],
        ],
    }
    for table_idx, rows in table_rebuilds.items():
        if table_idx < len(tables):
            rebuild_table_body(tables[table_idx], rows)

    return ET.tostring(root, encoding="unicode"), missing


def update_docx() -> None:
    replacements = {
        "word/media/image2.png": fig1_1(),
        "word/media/image3.png": fig1_3(),
        "word/media/image4.png": fig2_1(),
        "word/media/image5.png": fig2_3(),
        "word/media/image6.png": fig2_4(),
        "word/media/image8.png": fig2_6(),
    }

    with zipfile.ZipFile(SRC, "r") as zin:
        doc_xml = zin.read("word/document.xml").decode("utf-8")
        missing = []
        for old, new in TEXT_REPLACEMENTS.items():
            if old not in doc_xml:
                missing.append(old)
            doc_xml = doc_xml.replace(old, new)
        doc_xml, structured_missing = structured_doc_updates(doc_xml)
        missing.extend(structured_missing)

        with zipfile.ZipFile(OUT, "w", compression=zipfile.ZIP_DEFLATED) as zout:
            for item in zin.infolist():
                data = zin.read(item.filename)
                if item.filename == "word/document.xml":
                    data = doc_xml.encode("utf-8")
                elif item.filename in replacements:
                    data = replacements[item.filename]
                zout.writestr(item, data)

    print(f"written: {OUT}")
    if missing:
        print("missing replacements:")
        for old in missing:
            print("-", old[:100])


if __name__ == "__main__":
    update_docx()
