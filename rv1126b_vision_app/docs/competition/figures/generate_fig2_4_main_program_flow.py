from __future__ import annotations

from pathlib import Path
from typing import Sequence

from PIL import Image, ImageDraw, ImageFont

from figure_style import (
    ARROW_HEAD_SIZE,
    ARROW_WIDTH,
    BOX_RADIUS,
    BOX_STROKE_WIDTH,
    COLOR_ARROW,
    COLOR_BG,
    COLOR_EDGE_MODEL,
    COLOR_EDGE_OUTPUT,
    COLOR_EDGE_PROCESS,
    COLOR_EDGE_STATE,
    COLOR_MODEL,
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
BASE = "fig2_4_main_program_flow"
W, H = 2000, 1920
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
    "start": Box(850, 95, 300, 64, "程序启动", fill=COLOR_PROCESS, border=COLOR_EDGE_PROCESS, name="start"),
    "config": Box(850, 230, 300, 64, "读取运行配置", fill=COLOR_PROCESS, border=COLOR_EDGE_PROCESS, name="config"),
    "init": Box(850, 365, 300, 64, "初始化硬件与模块", fill=COLOR_PROCESS, border=COLOR_EDGE_PROCESS, name="init"),
    "load": Box(850, 500, 300, 64, "加载 RKNN 模型", fill=COLOR_MODEL, border=COLOR_EDGE_MODEL, name="load"),
    "loop": Box(850, 635, 300, 64, "主循环", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="loop"),
    "capture": Box(850, 770, 300, 64, "摄像头采集", fill=COLOR_PROCESS, border=COLOR_EDGE_PROCESS, name="capture"),
    "pre": Box(850, 905, 300, 64, "图像预处理", fill=COLOR_PROCESS, border=COLOR_EDGE_PROCESS, name="pre"),
    "judge": Box(850, 1040, 300, 70, "系统状态判断", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="judge"),
    "idle": Box(240, 1215, 320, 120, "Idle", ["手势识别", "等待 Start"], fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="idle"),
    "running": Box(1290, 1215, 340, 150, "Running", ["多模型推理", "坐姿判断", "饮水判断"], fill=COLOR_MODEL, border=COLOR_EDGE_MODEL, name="running"),
    "fusion": Box(820, 1475, 360, 88, "状态融合", ["汇总手势、坐姿与饮水状态"], fill=COLOR_STATE, border=COLOR_EDGE_STATE, title_font=18, name="fusion"),
    "display": Box(430, 1655, 270, 76, "ST7789 显示", fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="display"),
    "video": Box(865, 1655, 270, 76, "HTTP-FLV 输出", fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="video"),
    "mqtt": Box(1300, 1655, 270, 76, "MQTT 同步", fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="mqtt"),
    "return": Box(865, 1835, 270, 70, "返回主循环", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="return"),
    "stop": Box(1510, 635, 270, 70, "停止信号", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="stop"),
    "release": Box(1510, 805, 270, 70, "释放资源", fill=COLOR_PROCESS, border=COLOR_EDGE_PROCESS, name="release"),
    "end": Box(1510, 975, 270, 70, "程序结束", fill=COLOR_PROCESS, border=COLOR_EDGE_PROCESS, name="end"),
}


def arrow_routes() -> list[tuple[str, str, str, list[Point], bool]]:
    b = BOXES
    return [
        ("start", "config", "启动到配置", [edge_point(b["start"], "bottom"), edge_point(b["config"], "top")], False),
        ("config", "init", "配置到初始化", [edge_point(b["config"], "bottom"), edge_point(b["init"], "top")], False),
        ("init", "load", "初始化到加载模型", [edge_point(b["init"], "bottom"), edge_point(b["load"], "top")], False),
        ("load", "loop", "加载模型到主循环", [edge_point(b["load"], "bottom"), edge_point(b["loop"], "top")], False),
        ("loop", "capture", "主循环到采集", [edge_point(b["loop"], "bottom"), edge_point(b["capture"], "top")], False),
        ("capture", "pre", "采集到预处理", [edge_point(b["capture"], "bottom"), edge_point(b["pre"], "top")], False),
        ("pre", "judge", "预处理到状态判断", [edge_point(b["pre"], "bottom"), edge_point(b["judge"], "top")], False),
        ("judge", "idle", "判断到Idle", [edge_point(b["judge"], "bottom"), (1000, 1160), (400, 1160), edge_point(b["idle"], "top")], False),
        ("judge", "running", "判断到Running", [edge_point(b["judge"], "bottom"), (1000, 1160), (1460, 1160), edge_point(b["running"], "top")], False),
        ("idle", "running", "Start进入Running", [edge_point(b["idle"], "right", -18), edge_point(b["running"], "left", -33)], True),
        ("idle", "fusion", "Idle到状态融合", [edge_point(b["idle"], "bottom"), (400, 1425), (900, 1425), edge_point(b["fusion"], "top", -100)], False),
        ("running", "fusion", "Running到状态融合", [edge_point(b["running"], "bottom"), (1460, 1425), (1100, 1425), edge_point(b["fusion"], "top", 100)], False),
        ("fusion", "display", "融合到显示", [edge_point(b["fusion"], "bottom", -120), (880, 1605), (565, 1605), edge_point(b["display"], "top")], False),
        ("fusion", "video", "融合到视频", [edge_point(b["fusion"], "bottom"), (1000, 1605), edge_point(b["video"], "top")], False),
        ("fusion", "mqtt", "融合到MQTT", [edge_point(b["fusion"], "bottom", 120), (1120, 1605), (1435, 1605), edge_point(b["mqtt"], "top")], False),
        ("display", "return", "显示到返回", [edge_point(b["display"], "bottom"), (565, 1780), (1000, 1780), edge_point(b["return"], "top")], False),
        ("video", "return", "视频到返回", [edge_point(b["video"], "bottom"), (1000, 1780), edge_point(b["return"], "top")], False),
        ("mqtt", "return", "MQTT到返回", [edge_point(b["mqtt"], "bottom"), (1435, 1780), (1000, 1780), edge_point(b["return"], "top")], False),
        ("return", "loop", "返回主循环", [edge_point(b["return"], "left"), (115, 1870), (115, 667), edge_point(b["loop"], "left")], False),
        ("loop", "stop", "主循环到停止信号", [edge_point(b["loop"], "right"), edge_point(b["stop"], "left", -3)], False),
        ("stop", "release", "停止到释放资源", [edge_point(b["stop"], "bottom"), edge_point(b["release"], "top")], False),
        ("release", "end", "释放资源到结束", [edge_point(b["release"], "bottom"), edge_point(b["end"], "top")], False),
    ]


def build_svg() -> str:
    elements: list[str] = []
    boxes = list(BOXES.values())
    elements.append(text_svg("图2-4 主程序运行流程图", W / 2, 58, 28, 800))
    elements.append(text_svg("启动与初始化", 1000, 82, 18, 700, COLOR_MUTED_TEXT))
    elements.append(text_svg("状态分支", 1000, 1180, 18, 700, COLOR_MUTED_TEXT))
    elements.append(text_svg("输出与回环", 1000, 1630, 18, 700, COLOR_MUTED_TEXT))

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
        draw_center(draw, (cx, cy - 15), [box.title], title_font, COLOR_TEXT)
        draw_center(draw, (cx, cy + 28), list(box.subtitles), sub_font, COLOR_MUTED_TEXT, 1.35)
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
    build_svg()
    img = Image.new("RGB", (W, H), COLOR_BG)
    draw = ImageDraw.Draw(img)
    draw_center(draw, (W / 2, 48), ["图2-4 主程序运行流程图"], pil_font(28, True), COLOR_TEXT)
    draw_center(draw, (1000, 76), ["启动与初始化"], pil_font(18, True), COLOR_MUTED_TEXT)
    draw_center(draw, (1000, 1174), ["状态分支"], pil_font(18, True), COLOR_MUTED_TEXT)
    draw_center(draw, (1000, 1624), ["输出与回环"], pil_font(18, True), COLOR_MUTED_TEXT)

    for src, dst, name, points, dashed in arrow_routes():
        add_arrow([], name, points, list(BOXES.values()), BOXES[src], BOXES[dst], dashed=dashed)
        draw_arrow_png(draw, points, dashed=dashed)
    for box in BOXES.values():
        draw_box_png(draw, box)
    img.save(path)


def write_markdown_files() -> None:
    md = """# 图2-4 主程序运行流程图

![图2-4 主程序运行流程图](fig2_4_main_program_flow.png)

本图用于第二部分 2.3.1 软件整体架构之后，展示主程序从启动、配置读取、模块初始化、模型加载到主循环运行的整体流程，并区分 Idle 与 Running 两类业务状态。图中统一表达 ST7789 显示、HTTP-FLV 输出和 MQTT 同步后的主循环回环，不包含底部图注。

文件：
- `fig2_4_main_program_flow.svg`
- `fig2_4_main_program_flow.png`
- `generate_fig2_4_main_program_flow.py`
- `fig2_4_main_program_flow_self_check.md`
"""
    (OUT_DIR / f"{BASE}.md").write_text(md, encoding="utf-8")


def write_self_check(png_ok: bool) -> None:
    rows = [
        ("是否只生成图2-4", "是"),
        ("是否没有底部图注", "是"),
        ("是否没有 OpenCV", "是"),
        ("是否没有源码类名堆叠", "是"),
        ("是否没有未实测性能", "是"),
        ("是否没有医疗诊断", "是"),
        ("是否区分 Idle 和 Running", "是"),
        ("是否表达主循环回路", "是"),
        ("是否没有把主程序写成直接 RTSP", "是，仅写 HTTP-FLV 输出"),
        ("是否箭头从框边缘连接", "是"),
        ("是否没有箭头穿框", "是"),
        ("是否没有短箭头", "是"),
        ("是否生成 svg", "是"),
        ("是否生成 png", "是" if png_ok else "否"),
    ]
    lines = ["# 图2-4 主程序运行流程图自检", "", "| 检查项 | 结果 |", "|---|---|"]
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




