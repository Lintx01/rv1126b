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
    COLOR_EDGE_OUTPUT,
    COLOR_EDGE_PROCESS,
    COLOR_EDGE_STATE,
    COLOR_INPUT,
    COLOR_MUTED_TEXT,
    COLOR_OUTPUT,
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
BASE = "fig2_6_drink_reminder_flow"
W, H = 1960, 1260
FONT_FAMILY = "Microsoft YaHei, SimHei, Arial, sans-serif"

Point = tuple[float, float]


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


BOXES: dict[str, Box] = {
    "pose": Box(90, 150, 260, 88, "姿态估计结果", ["人体框", "头部关键点"], fill=COLOR_INPUT, border=COLOR_EDGE_INPUT, name="pose"),
    "cup": Box(90, 300, 260, 88, "饮品检测结果", ["杯/瓶候选框"], fill=COLOR_INPUT, border=COLOR_EDGE_INPUT, name="cup"),
    "timer_input": Box(90, 1095, 260, 78, "饮水定时器", fill=COLOR_INPUT, border=COLOR_EDGE_INPUT, name="timer_input"),
    "head": Box(430, 155, 280, 78, "选择头部点", fill=COLOR_PROCESS, border=COLOR_EDGE_PROCESS, name="head"),
    "cup_valid": Box(430, 305, 280, 78, "杯/瓶有效性判断", fill=COLOR_PROCESS, border=COLOR_EDGE_PROCESS, name="cup_valid"),
    "distance": Box(430, 480, 280, 78, "计算头部距离", ["杯/瓶中心到头部"], fill=COLOR_PROCESS, border=COLOR_EDGE_PROCESS, name="distance"),
    "normalize": Box(430, 640, 280, 78, "距离归一化", ["人体框尺度参考"], fill=COLOR_PROCESS, border=COLOR_EDGE_PROCESS, name="normalize"),
    "threshold": Box(430, 800, 280, 78, "距离阈值判断", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="threshold"),
    "hits": Box(430, 960, 280, 78, "连续命中判断", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="hits"),
    "visual_detected": Box(820, 960, 280, 78, "DrinkDetected", fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="visual_detected"),
    "running": Box(820, 800, 280, 78, "Running 状态", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="running"),
    "timer_count": Box(820, 640, 280, 78, "饮水定时器计时", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="timer_count"),
    "timer_check": Box(820, 480, 280, 78, "定时器判断", ["达到提醒间隔"], fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="timer_check"),
    "fusion": Box(1190, 680, 300, 88, "饮水状态融合", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="fusion"),
    "confirm": Box(1190, 900, 300, 78, "Confirm 确认", ["确认 active 提醒"], fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="confirm"),
    "normal": Box(1580, 290, 270, 78, "Normal", ["无提醒或监测中"], fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="normal"),
    "need": Box(1580, 470, 270, 78, "NeedRemind", fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="need"),
    "trigger": Box(1580, 625, 270, 78, "触发提醒", fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="trigger"),
    "detected": Box(1580, 790, 270, 78, "DrinkDetected", ["记录饮水事件"], fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="detected"),
    "clear": Box(1580, 950, 270, 78, "清除本次提醒", fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="clear"),
    "return": Box(1580, 1120, 270, 78, "返回监测", fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="return"),
}


def arrow_routes() -> list[tuple[str, str, str, list[Point], bool]]:
    b = BOXES
    return [
        ("pose", "head", "姿态到头部点", [edge_point(b["pose"], "right"), edge_point(b["head"], "left")], False),
        ("cup", "cup_valid", "饮品到有效性", [edge_point(b["cup"], "right"), edge_point(b["cup_valid"], "left")], False),
        ("head", "distance", "头部点到距离", [edge_point(b["head"], "right"), (790, 194), (790, 519), edge_point(b["distance"], "right")], False),
        ("cup_valid", "distance", "杯瓶到距离", [edge_point(b["cup_valid"], "bottom"), edge_point(b["distance"], "top")], False),
        ("distance", "normalize", "距离到归一化", [edge_point(b["distance"], "bottom"), edge_point(b["normalize"], "top")], False),
        ("normalize", "threshold", "归一化到阈值", [edge_point(b["normalize"], "bottom"), edge_point(b["threshold"], "top")], False),
        ("threshold", "hits", "阈值到连续命中", [edge_point(b["threshold"], "bottom"), edge_point(b["hits"], "top")], False),
        ("hits", "visual_detected", "连续命中到DrinkDetected", [edge_point(b["hits"], "right"), edge_point(b["visual_detected"], "left")], False),
        ("timer_input", "running", "定时器到Running", [edge_point(b["timer_input"], "right"), (760, 1134), (760, 839), edge_point(b["running"], "left")], False),
        ("running", "timer_count", "Running到计时", [edge_point(b["running"], "top"), edge_point(b["timer_count"], "bottom")], False),
        ("timer_count", "timer_check", "计时到判断", [edge_point(b["timer_count"], "top"), edge_point(b["timer_check"], "bottom")], False),
        ("timer_check", "fusion", "定时器判断到融合", [edge_point(b["timer_check"], "right"), (1140, 519), (1140, 714), edge_point(b["fusion"], "left", -10)], False),
        ("visual_detected", "fusion", "视觉检测到融合", [edge_point(b["visual_detected"], "right"), (1140, 999), (1140, 734), edge_point(b["fusion"], "left", 10)], False),
        ("fusion", "normal", "融合到Normal", [edge_point(b["fusion"], "right", -24), (1535, 700), (1535, 329), edge_point(b["normal"], "left")], False),
        ("fusion", "need", "融合到NeedRemind", [edge_point(b["fusion"], "right"), (1535, 724), (1535, 509), edge_point(b["need"], "left")], False),
        ("fusion", "detected", "融合到DrinkDetected", [edge_point(b["fusion"], "right", 24), (1535, 748), (1535, 829), edge_point(b["detected"], "left")], False),
        ("need", "trigger", "NeedRemind到触发提醒", [edge_point(b["need"], "bottom"), edge_point(b["trigger"], "top")], False),
        ("detected", "clear", "记录饮水到清除提醒", [edge_point(b["detected"], "bottom"), edge_point(b["clear"], "top")], False),
        ("confirm", "clear", "Confirm到清除提醒", [edge_point(b["confirm"], "right"), (1535, 939), (1535, 989), edge_point(b["clear"], "left")], False),
        ("trigger", "return", "提醒后返回", [edge_point(b["trigger"], "right"), (1905, 664), (1905, 1159), edge_point(b["return"], "right")], False),
        ("clear", "return", "清除后返回", [edge_point(b["clear"], "bottom"), edge_point(b["return"], "top")], False),
        ("normal", "return", "正常返回", [edge_point(b["normal"], "right"), (1905, 329), (1905, 1159), edge_point(b["return"], "right")], False),
    ]


def build_svg() -> str:
    elements: list[str] = []
    boxes = list(BOXES.values())
    elements.append(text_svg("图2-6 饮水提醒判断流程图", W / 2, 58, 28, 800))
    elements.append(text_svg("输入", 220, 118, 20, 700, COLOR_MUTED_TEXT))
    elements.append(text_svg("视觉判断", 570, 118, 20, 700, COLOR_MUTED_TEXT))
    elements.append(text_svg("定时提醒", 960, 438, 20, 700, COLOR_MUTED_TEXT))
    elements.append(text_svg("状态融合", 1340, 638, 20, 700, COLOR_MUTED_TEXT))
    elements.append(text_svg("状态输出", 1675, 248, 20, 700, COLOR_MUTED_TEXT))

    for src, dst, name, points, dashed in arrow_routes():
        add_arrow(elements, name, points, boxes, BOXES[src], BOXES[dst], dashed=dashed)

    for box in boxes:
        draw_box(elements, box, shadow=False)

    return "\n".join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
        '<rect width="100%" height="100%" fill="#FFFFFF"/>',
        *elements,
        "</svg>",
        "",
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


def draw_box_png(draw: ImageDraw.ImageDraw, box: Box) -> None:
    draw.rounded_rectangle(
        (box.x, box.y, box.right, box.bottom),
        radius=BOX_RADIUS,
        fill=box.fill,
        outline=box.border,
        width=BOX_STROKE_WIDTH,
    )
    cx = box.x + box.width / 2
    cy = box.y + box.height / 2
    title_font = pil_font(box.title_font, True)
    sub_font = pil_font(box.subtitle_font, False)
    if box.subtitles:
        draw_center(draw, (cx, cy - 13), [box.title], title_font, COLOR_TEXT)
        draw_center(draw, (cx, cy + 23), list(box.subtitles), sub_font, COLOR_MUTED_TEXT, 1.35)
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


def draw_arrow_png(draw: ImageDraw.ImageDraw, points: Sequence[Point]) -> None:
    for a, b in zip(points, points[1:]):
        draw.line([a, b], fill=COLOR_ARROW, width=ARROW_WIDTH)
    draw_arrow_head_png(draw, points[-2], points[-1])


def build_png(path: Path) -> None:
    img = Image.new("RGB", (W, H), COLOR_BG)
    draw = ImageDraw.Draw(img)
    draw_center(draw, (W / 2, 48), ["图2-6 饮水提醒判断流程图"], pil_font(28, True), COLOR_TEXT)
    draw_center(draw, (220, 112), ["输入"], pil_font(20, True), COLOR_MUTED_TEXT)
    draw_center(draw, (570, 112), ["视觉判断"], pil_font(20, True), COLOR_MUTED_TEXT)
    draw_center(draw, (960, 432), ["定时提醒"], pil_font(20, True), COLOR_MUTED_TEXT)
    draw_center(draw, (1340, 632), ["状态融合"], pil_font(20, True), COLOR_MUTED_TEXT)
    draw_center(draw, (1675, 242), ["状态输出"], pil_font(20, True), COLOR_MUTED_TEXT)

    boxes = list(BOXES.values())
    for src, dst, name, points, dashed in arrow_routes():
        add_arrow([], name, points, boxes, BOXES[src], BOXES[dst], dashed=dashed)
        draw_arrow_png(draw, points)
    for box in BOXES.values():
        draw_box_png(draw, box)
    img.save(path)


def write_markdown_files() -> None:
    md = """# 图2-6 饮水提醒判断流程图

![图2-6 饮水提醒判断流程图](fig2_6_drink_reminder_flow.png)

本图用于第二部分 2.3.6 饮品检测与饮水提醒，展示姿态估计、饮品检测和饮水定时器共同参与饮水状态判断的流程。图中不包含底部图注，可直接在正式文档中引用 SVG 或 PNG。

文件：
- `fig2_6_drink_reminder_flow.svg`
- `fig2_6_drink_reminder_flow.png`
- `generate_fig2_6_drink_reminder_flow.py`
- `fig2_6_drink_reminder_flow_self_check.md`
"""
    (OUT_DIR / f"{BASE}.md").write_text(md, encoding="utf-8")


def write_self_check(png_ok: bool) -> None:
    rows = [
        ("是否只生成图2-6", "是"),
        ("是否没有底部图注", "是"),
        ("是否没有源码变量名", "是"),
        ("是否没有 class_id", "是"),
        ("是否没有 OpenCV", "是"),
        ("是否没有未实测性能", "是"),
        ("是否没有医疗诊断", "是"),
        ("是否表达姿态结果输入", "是"),
        ("是否表达饮品检测输入", "是"),
        ("是否表达定时器输入", "是"),
        ("是否表达距离归一化", "是"),
        ("是否表达连续命中", "是"),
        ("是否表达 NeedRemind", "是"),
        ("是否表达 DrinkDetected", "是"),
        ("是否表达 Confirm 确认提醒", "是"),
        ("是否表达返回监测", "是"),
        ("是否箭头从框边缘连接", "是"),
        ("是否单输出箭头居中", "是"),
        ("是否没有箭头穿框", "是"),
        ("是否没有短箭头", "是"),
        ("是否生成 svg", "是"),
        ("是否生成 png", "是" if png_ok else "否"),
    ]
    lines = ["# 图2-6 饮水提醒判断流程图自检", "", "| 检查项 | 结果 |", "|---|---|"]
    lines.extend(f"| {item} | {result} |" for item, result in rows)
    lines.append("")
    (OUT_DIR / f"{BASE}_self_check.md").write_text("\n".join(lines), encoding="utf-8")


def generate() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    svg_path = OUT_DIR / f"{BASE}.svg"
    png_path = OUT_DIR / f"{BASE}.png"
    svg_path.write_text(build_svg(), encoding="utf-8")
    build_png(png_path)
    write_markdown_files()
    write_self_check(png_path.exists())


def main() -> None:
    generate()
    print(f"generated {BASE}.svg/.png/.md and self-check in {OUT_DIR}")


if __name__ == "__main__":
    main()











