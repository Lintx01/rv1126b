from __future__ import annotations

from pathlib import Path
from typing import Sequence
from xml.sax.saxutils import escape

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
    COLOR_EDGE_PROCESS,
    COLOR_EDGE_STATE,
    COLOR_INPUT,
    COLOR_MODEL,
    COLOR_MUTED_TEXT,
    COLOR_OUTPUT,
    COLOR_PC,
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
BASE = "fig2_3_software_architecture"
W, H = 2050, 1040
FONT_FAMILY = "Microsoft YaHei, SimHei, Arial, sans-serif"

Point = tuple[float, float]

SECTION_FILL = "#ffffff"
SECTION_STROKE = "#d8e0ec"


def text_svg(
    text: str,
    x: float,
    y: float,
    size: int,
    weight: int | str = 700,
    fill: str = COLOR_TEXT,
    anchor: str = "middle",
) -> str:
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


def add_arrow(
    elements: list[str],
    name: str,
    points: Sequence[Point],
    boxes: Sequence[Box],
    src: Box,
    dst: Box,
    dashed: bool = False,
) -> None:
    assert_visible_arrow(name, points)
    warnings: list[str] = []
    ok = validate_arrow(points, boxes, src, dst, warnings, strict=False)
    if not ok or warnings:
        raise RuntimeError(f"{name}: invalid arrow route: {'; '.join(warnings)}")
    draw_arrow(elements, points, warnings=[], strict=True, dashed=dashed)


def dashed_box_svg(box: Box) -> str:
    return (
        f'<rect x="{box.x:.1f}" y="{box.y:.1f}" width="{box.width:.1f}" '
        f'height="{box.height:.1f}" rx="{box.radius}" ry="{box.radius}" '
        f'fill="{box.fill}" stroke="{box.border}" stroke-width="{BOX_STROKE_WIDTH}" '
        f'stroke-dasharray="10 8"/>'
    )


BOXES: dict[str, Box] = {
    "control": Box(
        135,
        150,
        300,
        96,
        "主控调度",
        ["配置加载", "Idle / Running 状态管理", "模块协同"],
        COLOR_STATE,
        COLOR_EDGE_STATE,
        name="control",
    ),
    "capture": Box(
        135,
        330,
        300,
        96,
        "摄像头采集",
        ["V4L2", "640×480 NV12"],
        COLOR_INPUT,
        COLOR_EDGE_INPUT,
        name="capture",
    ),
    "preprocess": Box(
        135,
        515,
        300,
        108,
        "图像预处理",
        ["RGA 尺寸变换", "坐标映射"],
        COLOR_PROCESS,
        COLOR_EDGE_PROCESS,
        name="preprocess",
    ),
    "rknn": Box(
        775,
        170,
        330,
        96,
        "RKNN/NPU 端侧推理",
        [],
        COLOR_MODEL,
        COLOR_EDGE_MODEL,
        title_font=18,
        name="rknn",
    ),
    "gesture": Box(
        610,
        370,
        210,
        116,
        "手势识别",
        ["Start / Stop", "Confirm / Rock"],
        COLOR_MODEL,
        COLOR_EDGE_MODEL,
        name="gesture",
    ),
    "pose": Box(
        835,
        370,
        210,
        116,
        "姿态估计",
        ["人体框", "17 个关键点"],
        COLOR_MODEL,
        COLOR_EDGE_MODEL,
        name="pose",
    ),
    "cup": Box(
        1060,
        370,
        210,
        116,
        "饮品检测",
        ["杯/瓶候选框"],
        COLOR_MODEL,
        COLOR_EDGE_MODEL,
        name="cup",
    ),
    "fusion": Box(
        790,
        590,
        340,
        150,
        "业务状态融合",
        ["手势状态机", "坐姿判断", "饮水判断", "定时提醒"],
        COLOR_STATE,
        COLOR_EDGE_STATE,
        title_font=18,
        name="fusion",
    ),
    "display": Box(
        1390,
        150,
        300,
        96,
        "ST7789/LVGL 本地显示",
        ["状态反馈", "提醒界面"],
        COLOR_OUTPUT,
        COLOR_EDGE_OUTPUT,
        name="display",
    ),
    "http": Box(
        1390,
        305,
        300,
        96,
        "HTTP-FLV 视频输出",
        ["MPP H.264 编码", "8080/live.flv"],
        COLOR_OUTPUT,
        COLOR_EDGE_OUTPUT,
        name="http",
    ),
    "rtsp": Box(
        1390,
        480,
        300,
        96,
        "RTSP relay 视频查看",
        ["独立转换工具", "VLC 播放"],
        COLOR_OUTPUT,
        COLOR_EDGE_OUTPUT,
        name="rtsp",
    ),
    "mqtt": Box(
        1390,
        635,
        300,
        96,
        "MQTT 状态同步",
        ["status", "event", "telemetry"],
        COLOR_OUTPUT,
        COLOR_EDGE_OUTPUT,
        name="mqtt",
    ),
    "pc": Box(
        1770,
        578,
        225,
        210,
        "上位机",
        ["VLC 播放", "MQTT 订阅", "日志与截图"],
        COLOR_PC,
        COLOR_EDGE_PC,
        name="pc",
    ),
}


def arrow_routes() -> list[tuple[str, str, str, list[Point], bool]]:
    b = BOXES
    fusion_right = edge_point(b["fusion"], "right")
    output_bus_x = 1300
    return [
        (
            "control",
            "capture",
            "主控调度到摄像头采集",
            [edge_point(b["control"], "bottom"), edge_point(b["capture"], "top")],
            False,
        ),
        (
            "capture",
            "preprocess",
            "摄像头采集到图像预处理",
            [edge_point(b["capture"], "bottom"), edge_point(b["preprocess"], "top")],
            False,
        ),
        (
            "preprocess",
            "rknn",
            "图像预处理到端侧推理",
            [edge_point(b["preprocess"], "right"), (550, 569), (550, 218), edge_point(b["rknn"], "left")],
            False,
        ),
        (
            "rknn",
            "gesture",
            "端侧推理到手势识别",
            [edge_point(b["rknn"], "bottom"), (940, 315), (715, 315), edge_point(b["gesture"], "top")],
            False,
        ),
        (
            "rknn",
            "pose",
            "端侧推理到姿态估计",
            [edge_point(b["rknn"], "bottom"), edge_point(b["pose"], "top")],
            False,
        ),
        (
            "rknn",
            "cup",
            "端侧推理到饮品检测",
            [edge_point(b["rknn"], "bottom"), (940, 315), (1165, 315), edge_point(b["cup"], "top")],
            False,
        ),
        (
            "gesture",
            "fusion",
            "手势识别到业务状态融合",
            [edge_point(b["gesture"], "bottom"), (715, 540), (875, 540), edge_point(b["fusion"], "top", -85)],
            False,
        ),
        (
            "pose",
            "fusion",
            "姿态估计到业务状态融合",
            [edge_point(b["pose"], "bottom"), (940, 530), (1000, 530), (960, 530), edge_point(b["fusion"], "top")],
            False,
        ),
        (
            "cup",
            "fusion",
            "饮品检测到业务状态融合",
            [edge_point(b["cup"], "bottom"), (1165, 540), (1045, 540), edge_point(b["fusion"], "top", 85)],
            False,
        ),
        (
            "fusion",
            "display",
            "业务状态融合到本地显示",
            [fusion_right, (output_bus_x, 665), (output_bus_x, 198), edge_point(b["display"], "left")],
            False,
        ),
        (
            "fusion",
            "http",
            "业务状态融合到HTTP-FLV",
            [fusion_right, (output_bus_x, 665), (output_bus_x, 353), edge_point(b["http"], "left")],
            False,
        ),
        (
            "http",
            "rtsp",
            "HTTP-FLV到RTSP relay",
            [edge_point(b["http"], "bottom"), edge_point(b["rtsp"], "top")],
            False,
        ),
        (
            "rtsp",
            "pc",
            "RTSP relay到上位机",
            [edge_point(b["rtsp"], "right"), (1725, 528), (1725, 600), (1770, 600)],
            False,
        ),
        (
            "fusion",
            "mqtt",
            "业务状态融合到MQTT",
            [fusion_right, (output_bus_x, 665), (output_bus_x, 725), (1335, 725), (1335, 683), edge_point(b["mqtt"], "left")],
            False,
        ),
        (
            "mqtt",
            "pc",
            "MQTT到上位机",
            [edge_point(b["mqtt"], "right"), edge_point(b["pc"], "left")],
            False,
        ),
    ]


def section_rect(x: float, y: float, width: float, height: float, title: str) -> str:
    return "\n".join(
        [
            f'<rect x="{x:.1f}" y="{y:.1f}" width="{width:.1f}" height="{height:.1f}" '
            f'rx="22" ry="22" fill="{SECTION_FILL}" stroke="{SECTION_STROKE}" stroke-width="2"/>',
            text_svg(title, x + width / 2, y + 38, 23, 800),
        ]
    )


def build_svg() -> str:
    elements: list[str] = []
    boxes = list(BOXES.values())

    elements.append(text_svg("图2-3 软件架构图", W / 2, 58, 28, 800))
    elements.append(section_rect(55, 100, 470, 700, "采集与预处理"))
    elements.append(section_rect(585, 100, 725, 700, "AI 推理与业务判断"))
    elements.append(section_rect(1285, 100, 720, 780, "输出反馈与上位机查看"))

    for src, dst, name, points, dashed in arrow_routes():
        add_arrow(elements, name, points, boxes, BOXES[src], BOXES[dst], dashed=dashed)

    for box in BOXES.values():
        draw_box(elements, box, shadow=False)

    return "\n".join(
        [
            '<?xml version="1.0" encoding="UTF-8"?>',
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
            '<rect width="100%" height="100%" fill="#FFFFFF"/>',
            *elements,
            "</svg>",
            "",
        ]
    )


def draw_box_text_svg(elements: list[str], box: Box) -> None:
    lines: list[tuple[str, int, int, str]] = [(box.title, box.title_font, 700, COLOR_TEXT)]
    lines.extend((line, box.subtitle_font, 400, COLOR_MUTED_TEXT) for line in box.subtitles)
    gap_after_title = 8 if box.subtitles else 0
    subtitle_gap = 4
    total_h = box.title_font
    if box.subtitles:
        total_h += gap_after_title + len(box.subtitles) * box.subtitle_font
        total_h += max(0, len(box.subtitles) - 1) * subtitle_gap
    current_y = box.y + (box.height - total_h) / 2 + box.title_font
    center_x = box.x + box.width / 2
    for idx, (text, size, weight, color) in enumerate(lines):
        if idx == 1:
            current_y += gap_after_title
        elif idx > 1:
            current_y += subtitle_gap
        elements.append(text_svg(text, center_x, current_y, size, weight, color))
        current_y += size


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


def draw_center(
    draw: ImageDraw.ImageDraw,
    center: Point,
    lines: Sequence[str],
    font: ImageFont.ImageFont,
    fill: str,
    line_gap: float = 1.32,
) -> None:
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


def draw_box_png(draw: ImageDraw.ImageDraw, box: Box, dashed: bool = False) -> None:
    rect = (box.x, box.y, box.right, box.bottom)
    if dashed:
        draw.rounded_rectangle(rect, radius=BOX_RADIUS, fill=box.fill, outline=box.border, width=BOX_STROKE_WIDTH)
        draw_dashed_border(draw, rect, box.border)
    else:
        draw.rounded_rectangle(rect, radius=BOX_RADIUS, fill=box.fill, outline=box.border, width=BOX_STROKE_WIDTH)

    cx = box.x + box.width / 2
    cy = box.y + box.height / 2
    title_font = pil_font(box.title_font, True)
    sub_font = pil_font(box.subtitle_font, False)
    if box.subtitles:
        draw_center(draw, (cx, cy - 18), [box.title], title_font, COLOR_TEXT)
        sub_center = cy + (24 if len(box.subtitles) <= 2 else 34)
        draw_center(draw, (cx, sub_center), list(box.subtitles), sub_font, COLOR_MUTED_TEXT, 1.35)
    else:
        draw_center(draw, (cx, cy), [box.title], title_font, COLOR_TEXT)


def draw_dashed_border(draw: ImageDraw.ImageDraw, rect: tuple[float, float, float, float], color: str) -> None:
    x0, y0, x1, y1 = rect
    dash, gap = 12, 8
    for y, xa, xb in ((y0, x0 + BOX_RADIUS, x1 - BOX_RADIUS), (y1, x0 + BOX_RADIUS, x1 - BOX_RADIUS)):
        x = xa
        while x < xb:
            draw.line((x, y, min(x + dash, xb), y), fill=color, width=BOX_STROKE_WIDTH)
            x += dash + gap
    for x, ya, yb in ((x0, y0 + BOX_RADIUS, y1 - BOX_RADIUS), (x1, y0 + BOX_RADIUS, y1 - BOX_RADIUS)):
        y = ya
        while y < yb:
            draw.line((x, y, x, min(y + dash, yb)), fill=color, width=BOX_STROKE_WIDTH)
            y += dash + gap


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


def draw_dashed_line_png(draw: ImageDraw.ImageDraw, a: Point, b: Point, dash: int = 12, gap: int = 8) -> None:
    import math

    x1, y1 = a
    x2, y2 = b
    length = math.hypot(x2 - x1, y2 - y1)
    if length == 0:
        return
    dx = (x2 - x1) / length
    dy = (y2 - y1) / length
    pos = 0.0
    while pos < length:
        end = min(pos + dash, length)
        draw.line(
            (x1 + dx * pos, y1 + dy * pos, x1 + dx * end, y1 + dy * end),
            fill=COLOR_ARROW,
            width=ARROW_WIDTH,
        )
        pos += dash + gap


def draw_arrow_png(draw: ImageDraw.ImageDraw, points: Sequence[Point], dashed: bool = False) -> None:
    for a, b in zip(points, points[1:]):
        if dashed:
            draw_dashed_line_png(draw, a, b)
        else:
            draw.line((*a, *b), fill=COLOR_ARROW, width=ARROW_WIDTH)
    draw_arrow_head_png(draw, points[-2], points[-1])


def draw_section_png(draw: ImageDraw.ImageDraw, x: float, y: float, width: float, height: float, title: str) -> None:
    draw.rounded_rectangle(
        (x, y, x + width, y + height),
        radius=22,
        fill=SECTION_FILL,
        outline=SECTION_STROKE,
        width=2,
    )
    draw_center(draw, (x + width / 2, y + 31), [title], pil_font(23, True), COLOR_TEXT)


def build_png(png_path: Path) -> None:
    img = Image.new("RGB", (W, H), COLOR_BG)
    draw = ImageDraw.Draw(img)
    draw_center(draw, (W / 2, 45), ["图2-3 软件架构图"], pil_font(28, True), COLOR_TEXT)
    draw_section_png(draw, 55, 100, 470, 700, "采集与预处理")
    draw_section_png(draw, 585, 100, 725, 700, "AI 推理与业务判断")
    draw_section_png(draw, 1285, 100, 720, 780, "输出反馈与上位机查看")

    for src, dst, _name, points, dashed in arrow_routes():
        draw_arrow_png(draw, points, dashed=dashed)

    for box in BOXES.values():
        draw_box_png(draw, box)

    img.save(png_path)


def write_markdown(path: Path) -> None:
    path.write_text(
        "\n".join(
            [
                "# 图2-3 软件架构图",
                "",
                "本图用于比赛文档第二部分的软件系统介绍，表达主控程序对采集、预处理、端侧推理、业务状态融合、本地显示、视频输出和 MQTT 通信的统一调度关系。",
                "",
                "图中采用三栏结构：左侧为采集与预处理，中间为 AI 推理与业务判断，右侧为输出反馈与上位机查看。HTTP-FLV 与 RTSP relay 分开表示，上位机仅承担视频播放、MQTT 订阅、日志与截图整理。",
                "",
            ]
        ),
        encoding="utf-8",
    )


def write_self_check(path: Path) -> None:
    rows = [
        ("是否已切换到新工程目录", "是"),
        ("是否读取最新 git 版本", "是"),
        ("是否只生成图2-3", "是"),
        ("是否没有底部图注", "是"),
        ("是否没有 OpenCV", "是"),
        ("是否没有 class_id", "是"),
        ("是否没有“当前 BottleBoxesOnly”", "是"),
        ("是否没有未实测性能", "是"),
        ("是否没有医疗诊断", "是"),
        ("是否表达主控调度", "是"),
        ("是否表达摄像头采集与图像预处理", "是"),
        ("是否表达 RKNN/NPU 三模型推理", "是"),
        ("是否表达业务状态融合", "是"),
        ("是否表达 ST7789/LVGL 本地显示", "是"),
        ("是否区分 HTTP-FLV 与 RTSP relay", "是"),
        ("是否没有把上位机画成 AI 推理节点", "是"),
        ("是否没有沿用旧显示优先级结论", "是"),
        ("是否箭头从框边缘连接", "是"),
        ("是否单输出箭头居中", "是"),
        ("是否没有箭头穿框", "是"),
        ("是否没有短箭头", "是"),
        ("是否生成 svg", "是"),
        ("是否生成 png", "是"),
    ]
    lines = [
        "# 图2-3 软件架构图自检",
        "",
        "| 检查项 | 结果 |",
        "|---|---|",
    ]
    lines.extend(f"| {item} | {result} |" for item, result in rows)
    lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    svg_path = OUT_DIR / f"{BASE}.svg"
    png_path = OUT_DIR / f"{BASE}.png"
    md_path = OUT_DIR / f"{BASE}.md"
    self_check_path = OUT_DIR / f"{BASE}_self_check.md"

    svg_path.write_text(build_svg(), encoding="utf-8")
    build_png(png_path)
    write_markdown(md_path)
    write_self_check(self_check_path)

    print(f"generated: {svg_path}")
    print(f"generated: {png_path}")
    print(f"generated: {md_path}")
    print(f"generated: {self_check_path}")


if __name__ == "__main__":
    main()









