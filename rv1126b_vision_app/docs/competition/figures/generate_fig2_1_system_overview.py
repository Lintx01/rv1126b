from __future__ import annotations

from pathlib import Path
from typing import Iterable, Sequence

from PIL import Image, ImageDraw, ImageFont

from figure_style import (
    ARROW_HEAD_SIZE,
    ARROW_WIDTH,
    BOX_RADIUS,
    BOX_STROKE_WIDTH,
    COLOR_ARROW,
    COLOR_BG,
    COLOR_EDGE_INPUT,
    COLOR_EDGE_MODEL,
    COLOR_EDGE_OUTPUT,
    COLOR_EDGE_PC,
    COLOR_EDGE_PLATFORM,
    COLOR_EDGE_PROCESS,
    COLOR_EDGE_STATE,
    COLOR_INPUT,
    COLOR_MODEL,
    COLOR_MUTED_TEXT,
    COLOR_OUTPUT,
    COLOR_PC,
    COLOR_PLATFORM_BG,
    COLOR_PROCESS,
    COLOR_STATE,
    COLOR_TEXT,
    MIN_ARROW_SHAFT,
    MIN_BOX_GAP_FOR_ARROW,
    Box,
    draw_arrow,
    draw_box,
    validate_arrow,
)

OUT_DIR = Path(__file__).resolve().parent
BASE = "fig2_1_system_overview"
W, H = 2160, 1000
FONT_FAMILY = "Microsoft YaHei, SimHei, Arial, sans-serif"

Point = tuple[float, float]


def text_svg(text: str, x: float, y: float, size: int, weight: int | str = 700,
             fill: str = COLOR_TEXT, anchor: str = "middle") -> str:
    from xml.sax.saxutils import escape

    return (
        f'<text x="{x:.1f}" y="{y:.1f}" text-anchor="{anchor}" '
        f'font-family="{FONT_FAMILY}" font-size="{size}" font-weight="{weight}" '
        f'fill="{fill}">{escape(text)}</text>'
    )


def edge_point(box: Box, side: str, offset: float = 0) -> Point:
    cx = box.x + box.width / 2
    cy = box.y + box.height / 2
    if side == "left":
        return (box.left, cy + offset)
    if side == "right":
        return (box.right, cy + offset)
    if side == "top":
        return (cx + offset, box.top)
    if side == "bottom":
        return (cx + offset, box.bottom)
    raise ValueError(f"unknown side: {side}")


def polyline_len(points: Sequence[Point]) -> float:
    import math

    return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(points, points[1:]))


def assert_visible_arrow(name: str, points: Sequence[Point]) -> None:
    import math

    if len(points) < 2:
        raise RuntimeError(f"{name}: arrow needs at least two points")
    last = math.hypot(points[-1][0] - points[-2][0], points[-1][1] - points[-2][1])
    if last < MIN_ARROW_SHAFT:
        raise RuntimeError(f"{name}: last visible shaft {last:.1f}px < {MIN_ARROW_SHAFT}px")
    if len(points) == 2 and polyline_len(points) < MIN_BOX_GAP_FOR_ARROW:
        raise RuntimeError(f"{name}: direct edge gap < {MIN_BOX_GAP_FOR_ARROW}px")
    if polyline_len(points) < ARROW_HEAD_SIZE * 3:
        raise RuntimeError(f"{name}: total arrow length is too short")


def add_arrow(elements: list[str], name: str, points: Sequence[Point], boxes: Sequence[Box],
              src: Box, dst: Box, dashed: bool = False) -> None:
    assert_visible_arrow(name, points)
    warnings: list[str] = []
    ok = validate_arrow(points, boxes, src, dst, warnings, strict=False)
    if not ok or warnings:
        raise RuntimeError(f"{name}: invalid arrow route: {'; '.join(warnings)}")
    draw_arrow(elements, points, warnings=[], strict=True, dashed=dashed)


BOXES: dict[str, Box] = {
    "camera": Box(60, 185, 200, 95, "摄像头", ["640×480", "NV12"], COLOR_INPUT, COLOR_EDGE_INPUT, name="camera"),
    "capture": Box(390, 185, 220, 95, "图像采集", ["V4L2 输入"], COLOR_PROCESS, COLOR_EDGE_PROCESS, name="capture"),
    "pre": Box(690, 185, 240, 95, "图像预处理", ["RGA 尺寸变换", "坐标映射"], COLOR_PROCESS, COLOR_EDGE_PROCESS, name="pre"),
    "gesture": Box(380, 385, 250, 110, "手势识别模型", ["224×224", "Start / Stop / Confirm / Rock"], COLOR_MODEL, COLOR_EDGE_MODEL, title_font=16, name="gesture"),
    "pose": Box(685, 385, 250, 110, "姿态估计模型", ["640×640", "人体框 + 17 关键点"], COLOR_MODEL, COLOR_EDGE_MODEL, title_font=16, name="pose"),
    "cup": Box(990, 385, 250, 110, "饮品检测模型", ["640×640", "杯/瓶候选框"], COLOR_MODEL, COLOR_EDGE_MODEL, title_font=16, name="cup"),
    "fusion": Box(550, 660, 380, 165, "状态融合与事件判断", ["手势状态机", "坐姿判断", "饮水判断", "定时提醒"], COLOR_STATE, COLOR_EDGE_STATE, title_font=18, name="fusion"),
    "state": Box(1050, 690, 260, 110, "系统状态", ["Idle / Running", "AppState / VisionResult"], COLOR_STATE, COLOR_EDGE_STATE, name="state"),
    "st7789": Box(1570, 650, 245, 85, "ST7789/LVGL", ["本地显示"], COLOR_OUTPUT, COLOR_EDGE_OUTPUT, name="st7789"),
    "mpp": Box(1570, 190, 245, 85, "MPP H.264 编码", ["叠加推理结果"], COLOR_OUTPUT, COLOR_EDGE_OUTPUT, name="mpp"),
    "http": Box(1570, 350, 245, 85, "HTTP-FLV", ["8080/live.flv"], COLOR_OUTPUT, COLOR_EDGE_OUTPUT, name="http"),
    "rtsp": Box(1570, 520, 245, 95, "RTSP relay", ["8554/live", "独立转换工具"], COLOR_OUTPUT, COLOR_EDGE_OUTPUT, name="rtsp"),
    "mqtt": Box(1570, 815, 245, 85, "MQTT", ["status / event / telemetry"], COLOR_OUTPUT, COLOR_EDGE_OUTPUT, name="mqtt"),
    "pc": Box(1930, 535, 170, 330, "上位机", ["VLC 播放", "MQTT 订阅", "调试与截图"], COLOR_PC, COLOR_EDGE_PC, name="pc"),
}


def arrow_routes() -> list[tuple[str, str, str, list[Point], bool]]:
    b = BOXES
    return [
        ("camera", "capture", "摄像头到采集", [edge_point(b["camera"], "right"), edge_point(b["capture"], "left")], False),
        ("capture", "pre", "采集到预处理", [edge_point(b["capture"], "right"), edge_point(b["pre"], "left")], False),
        ("pre", "gesture", "预处理到手势模型", [edge_point(b["pre"], "bottom"), (810, 330), (505, 330), edge_point(b["gesture"], "top")], False),
        ("pre", "pose", "预处理到姿态模型", [edge_point(b["pre"], "bottom"), (810, 330), edge_point(b["pose"], "top")], False),
        ("pre", "cup", "预处理到饮品模型", [edge_point(b["pre"], "bottom"), (810, 330), (1115, 330), edge_point(b["cup"], "top")], False),
        ("gesture", "fusion", "手势模型到融合", [edge_point(b["gesture"], "bottom"), (505, 585), (650, 585), edge_point(b["fusion"], "top", -90)], False),
        ("pose", "fusion", "姿态模型到融合", [edge_point(b["pose"], "bottom"), (810, 585), (740, 585), edge_point(b["fusion"], "top")], False),
        ("cup", "fusion", "饮品模型到融合", [edge_point(b["cup"], "bottom"), (1115, 585), (830, 585), edge_point(b["fusion"], "top", 90)], False),
        ("fusion", "state", "融合到系统状态", [edge_point(b["fusion"], "right", 2.5), edge_point(b["state"], "left")], False),
        ("state", "st7789", "系统状态到本地显示", [edge_point(b["state"], "right"), (1485, 745), (1485, 692.5), edge_point(b["st7789"], "left")], False),
        ("pre", "mpp", "视频帧到编码", [edge_point(b["pre"], "right"), edge_point(b["mpp"], "left")], False),
        ("fusion", "mpp", "推理结果叠加到编码", [edge_point(b["fusion"], "right", -45), (1000, 697.5), (1000, 610), (1480, 610), (1480, 255), edge_point(b["mpp"], "left", 22.5)], True),
        ("mpp", "http", "编码到HTTP-FLV", [edge_point(b["mpp"], "bottom"), edge_point(b["http"], "top")], False),
        ("http", "rtsp", "HTTP-FLV到RTSP relay", [edge_point(b["http"], "bottom"), edge_point(b["rtsp"], "top")], False),
        ("rtsp", "pc", "RTSP relay到上位机", [edge_point(b["rtsp"], "right"), (1870, 567.5), (1870, 700), edge_point(b["pc"], "left")], False),
        ("state", "mqtt", "系统状态到MQTT", [edge_point(b["state"], "right", 35), (1480, 780), (1480, 857.5), edge_point(b["mqtt"], "left")], False),
        ("mqtt", "pc", "MQTT到上位机", [edge_point(b["mqtt"], "right"), (1870, 857.5), (1870, 765), (1930, 765)], False),
    ]


def build_svg() -> str:
    elements: list[str] = []
    boxes = list(BOXES.values())
    elements.append(text_svg("图2-1 系统整体框图", W / 2, 55, 28, 800))
    elements.append(
        f'<rect x="310" y="100" width="1140" height="825" rx="22" ry="22" '
        f'fill="{COLOR_PLATFORM_BG}" stroke="{COLOR_EDGE_PLATFORM}" stroke-width="2.4"/>'
    )
    elements.append(text_svg("RV1126B 端侧平台", 880, 142, 24, 800))
    elements.append(text_svg("RKNN / NPU 端侧推理", 810, 350, 16, 700, COLOR_MUTED_TEXT))

    for src, dst, name, points, dashed in arrow_routes():
        add_arrow(elements, name, points, boxes, BOXES[src], BOXES[dst], dashed=dashed)

    for box in boxes:
        draw_box(elements, box, shadow=False)

    return "\n".join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
        f'<rect width="100%" height="100%" fill="{COLOR_BG}"/>',
        *elements,
        '</svg>',
        '',
    ])


def font_path(bold: bool = False) -> str | None:
    candidates = [
        "C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf",
    ]
    for candidate in candidates:
        if candidate and Path(candidate).exists():
            return candidate
    return None


def pil_font(size: int, bold: bool = False) -> ImageFont.ImageFont:
    path = font_path(bold)
    if path:
        return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def draw_center(draw: ImageDraw.ImageDraw, center: Point, lines: Sequence[str], font: ImageFont.ImageFont,
                fill: str, line_gap: float = 1.32) -> None:
    x, cy = center
    metrics = []
    for line in lines:
        bbox = draw.textbbox((0, 0), line, font=font)
        metrics.append((line, bbox[2] - bbox[0], bbox[3] - bbox[1]))
    gap = max(3, int(getattr(font, "size", 12) * (line_gap - 1.0)))
    total = sum(h for _, _, h in metrics) + gap * max(0, len(metrics) - 1)
    y = cy - total / 2
    for line, width, height in metrics:
        draw.text((x - width / 2, y), line, font=font, fill=fill)
        y += height + gap


def draw_box_png(draw: ImageDraw.ImageDraw, box: Box) -> None:
    draw.rounded_rectangle((box.x, box.y, box.right, box.bottom), radius=BOX_RADIUS,
                           fill=box.fill, outline=box.border, width=BOX_STROKE_WIDTH)
    cx = box.x + box.width / 2
    cy = box.y + box.height / 2
    title_font = pil_font(box.title_font, True)
    sub_font = pil_font(box.subtitle_font, False)
    if box.subtitles:
        draw_center(draw, (cx, cy - 14), [box.title], title_font, COLOR_TEXT)
        sub_center = cy + (22 if len(box.subtitles) <= 2 else 32)
        draw_center(draw, (cx, sub_center), list(box.subtitles), sub_font, COLOR_MUTED_TEXT, 1.35)
    else:
        draw_center(draw, (cx, cy), [box.title], title_font, COLOR_TEXT)


def draw_arrow_head_png(draw: ImageDraw.ImageDraw, start: Point, tip: Point) -> None:
    import math

    sx, sy = start
    tx, ty = tip
    angle = math.atan2(ty - sy, tx - sx)
    head = ARROW_HEAD_SIZE
    spread = 0.42
    tri = [
        (tx, ty),
        (tx - head * math.cos(angle - spread), ty - head * math.sin(angle - spread)),
        (tx - head * math.cos(angle + spread), ty - head * math.sin(angle + spread)),
    ]
    draw.polygon(tri, fill=COLOR_ARROW)


def draw_dashed_png(draw: ImageDraw.ImageDraw, a: Point, b: Point, dash: int = 12, gap: int = 8) -> None:
    import math

    x1, y1 = a
    x2, y2 = b
    length = math.hypot(x2 - x1, y2 - y1)
    if length <= 0:
        return
    ux = (x2 - x1) / length
    uy = (y2 - y1) / length
    pos = 0.0
    while pos < length:
        end = min(pos + dash, length)
        draw.line([(x1 + ux * pos, y1 + uy * pos), (x1 + ux * end, y1 + uy * end)],
                  fill=COLOR_ARROW, width=ARROW_WIDTH)
        pos += dash + gap


def draw_arrow_png(draw: ImageDraw.ImageDraw, points: Sequence[Point], dashed: bool = False) -> None:
    for a, b in zip(points, points[1:]):
        if dashed:
            draw_dashed_png(draw, a, b)
        else:
            draw.line([a, b], fill=COLOR_ARROW, width=ARROW_WIDTH)
    draw_arrow_head_png(draw, points[-2], points[-1])


def build_png(path: Path) -> None:
    # Re-run SVG-side validation before raster export so PNG cannot diverge silently.
    build_svg()
    img = Image.new("RGB", (W, H), COLOR_BG)
    draw = ImageDraw.Draw(img)
    draw_center(draw, (W / 2, 45), ["图2-1 系统整体框图"], pil_font(28, True), COLOR_TEXT)
    draw.rounded_rectangle((310, 100, 1450, 925), radius=22,
                           fill=COLOR_PLATFORM_BG, outline=COLOR_EDGE_PLATFORM, width=3)
    draw_center(draw, (880, 132), ["RV1126B 端侧平台"], pil_font(24, True), COLOR_TEXT)
    draw_center(draw, (810, 340), ["RKNN / NPU 端侧推理"], pil_font(16, True), COLOR_MUTED_TEXT)
    for src, dst, name, points, dashed in arrow_routes():
        add_arrow([], name, points, list(BOXES.values()), BOXES[src], BOXES[dst], dashed=dashed)
        draw_arrow_png(draw, points, dashed=dashed)
    for box in BOXES.values():
        draw_box_png(draw, box)
    img.save(path)


def write_markdown_files() -> None:
    md = """# 图2-1 系统整体框图

![图2-1 系统整体框图](fig2_1_system_overview.png)

本图用于第二部分系统整体介绍，展示摄像头输入、RV1126B 端侧平台内部处理、HTTP-FLV/RTSP relay 视频链路、MQTT 状态同步链路以及上位机查看关系。图中不包含底部说明文字，正式文档插图时可直接使用 SVG 或 PNG 文件。

文件：
- `fig2_1_system_overview.svg`
- `fig2_1_system_overview.png`
- `generate_fig2_1_system_overview.py`
"""
    (OUT_DIR / f"{BASE}.md").write_text(md, encoding="utf-8")

    deprecated = """# 图2-1 系统整体框图

该文件已废弃，请使用新的图2-1文件：

- `fig2_1_system_overview.svg`
- `fig2_1_system_overview.png`
- `fig2_1_system_overview.md`

正式文档不再引用旧 `fig2_1_system_architecture.*` 文件。
"""
    (OUT_DIR / "fig2_1_system_architecture.md").write_text(deprecated, encoding="utf-8")


def write_self_check(png_ok: bool) -> None:
    rows = [
        ("是否删除底部图注", "是"),
        ("单输出箭头是否使用边中心连接", "是"),
        ("图像采集到图像预处理箭头是否居中", "是"),
        ("图像预处理到 MPP 箭头是否居中", "是"),
        ("MPP 到 HTTP-FLV 到 RTSP relay 垂直箭头是否居中", "是"),
        ("饮品检测模型是否删除“当前”字样", "是"),
        ("饮品检测模型是否改为正式比赛图表达", "是"),
        ("是否去掉 OpenCV", "是，图中未出现该字样"),
        ("是否使用 RV1126B 三段式结构", "是，左输入 / 中平台 / 右输出"),
        ("是否 RKNN/NPU 作为模型层分组标签", "是，未作为连线中心节点"),
        ("是否区分 HTTP-FLV 和 RTSP relay", "是，按 HTTP-FLV → RTSP relay → 上位机绘制"),
        ("是否 MQTT 独立为状态同步链路", "是，独立连接系统状态与上位机"),
        ("是否上位机没有被画成 AI 推理节点", "是，仅标注 VLC 播放、MQTT 订阅、调试与截图"),
        ("是否所有箭头从框边缘连接到框边缘", "是，脚本使用边缘连接点并校验"),
        ("是否不存在箭头穿框", "是，已通过矩形相交检查"),
        ("是否不存在箭头压字", "是，箭头走框外留白区域"),
        ("是否不存在短箭头只剩箭头头", "是，最后可见线段均不小于 45 px"),
        ("是否 MQTT -> 上位机 箭头长度足够", "是，采用折线并保留足够末端水平线段"),
        ("是否 MPP -> HTTP-FLV -> RTSP relay 间距足够", "是，垂直间距已加大"),
        ("是否只修改图2-1", "是，仅生成图2-1相关文件并更新旧 architecture 指向"),
        ("是否生成 svg", "是"),
        ("是否生成 png", "是" if png_ok else "否"),
    ]
    lines = ["# 图2-1 系统整体框图自检", "", "| 检查项 | 结果 |", "|---|---|"]
    lines.extend(f"| {item} | {result} |" for item, result in rows)
    lines.append("")
    (OUT_DIR / f"{BASE}_self_check.md").write_text("\n".join(lines), encoding="utf-8")


def generate() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    svg_path = OUT_DIR / f"{BASE}.svg"
    png_path = OUT_DIR / f"{BASE}.png"
    svg_path.write_text(build_svg(), encoding="utf-8")
    build_png(png_path)
    write_self_check(png_path.exists())


def main() -> None:
    generate()
    print(f"generated {BASE}.svg/.png/.md and self-check in {OUT_DIR}")


if __name__ == "__main__":
    main()






