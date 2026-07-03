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
    COLOR_EDGE_INPUT,
    COLOR_EDGE_MODEL,
    COLOR_EDGE_OUTPUT,
    COLOR_EDGE_PROCESS,
    COLOR_EDGE_STATE,
    COLOR_INPUT,
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
BASE = "fig2_5_gesture_state_machine"
W, H = 1900, 1360
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
    "frame": Box(90, 150, 280, 78, "摄像头帧", fill=COLOR_INPUT, border=COLOR_EDGE_INPUT, name="frame"),
    "infer": Box(90, 300, 280, 78, "手势模型识别", fill=COLOR_MODEL, border=COLOR_EDGE_MODEL, name="infer"),
    "map": Box(90, 450, 280, 78, "业务手势映射", fill=COLOR_PROCESS, border=COLOR_EDGE_PROCESS, name="map"),
    "confidence": Box(540, 450, 300, 78, "置信度判断", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="confidence"),
    "stable": Box(540, 605, 300, 78, "稳定计数", ["连续命中后继续"], fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="stable"),
    "cooldown_check": Box(540, 760, 300, 78, "冷却判断", ["避免重复触发"], fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="cooldown_check"),
    "release_check": Box(540, 915, 300, 78, "释放锁判断", ["等待手势离开"], fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="release_check"),
    "trigger": Box(930, 915, 300, 78, "触发手势事件", fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="trigger"),
    "start": Box(1320, 160, 330, 88, "Start", ["Idle -> Running"], fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="start"),
    "stop": Box(1320, 315, 330, 88, "Stop", ["Running -> Idle"], fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="stop"),
    "confirm": Box(1320, 470, 330, 88, "Confirm", ["确认提醒", "不切换主状态"], fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="confirm"),
    "rock": Box(1320, 625, 330, 88, "Rock", ["互动反馈", "不切换主状态"], fill=COLOR_OUTPUT, border=COLOR_EDGE_OUTPUT, name="rock"),
    "enter_cooldown": Box(1320, 1145, 300, 78, "进入冷却", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="enter_cooldown"),
    "wait_release": Box(870, 1145, 300, 78, "等待释放", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="wait_release"),
    "return_loop": Box(420, 1145, 300, 78, "返回识别循环", fill=COLOR_STATE, border=COLOR_EDGE_STATE, name="return_loop"),
}


def arrow_routes() -> list[tuple[str, str, str, list[Point], bool]]:
    b = BOXES
    return [
        ("frame", "infer", "帧到识别", [edge_point(b["frame"], "bottom"), edge_point(b["infer"], "top")], False),
        ("infer", "map", "识别到映射", [edge_point(b["infer"], "bottom"), edge_point(b["map"], "top")], False),
        ("map", "confidence", "映射到置信度", [edge_point(b["map"], "right"), edge_point(b["confidence"], "left")], False),
        ("confidence", "stable", "置信度到稳定计数", [edge_point(b["confidence"], "bottom"), edge_point(b["stable"], "top")], False),
        ("stable", "cooldown_check", "稳定计数到冷却判断", [edge_point(b["stable"], "bottom"), edge_point(b["cooldown_check"], "top")], False),
        ("cooldown_check", "release_check", "冷却到释放锁", [edge_point(b["cooldown_check"], "bottom"), edge_point(b["release_check"], "top")], False),
        ("release_check", "trigger", "释放锁到触发", [edge_point(b["release_check"], "right"), edge_point(b["trigger"], "left")], False),
        ("trigger", "start", "触发到Start", [edge_point(b["trigger"], "right"), (1260, 954), (1260, 204), edge_point(b["start"], "left")], False),
        ("trigger", "stop", "触发到Stop", [edge_point(b["trigger"], "right"), (1260, 954), (1260, 359), edge_point(b["stop"], "left")], False),
        ("trigger", "confirm", "触发到Confirm", [edge_point(b["trigger"], "right"), (1260, 954), (1260, 514), edge_point(b["confirm"], "left")], False),
        ("trigger", "rock", "触发到Rock", [edge_point(b["trigger"], "right"), (1260, 954), (1260, 669), edge_point(b["rock"], "left")], False),
        ("start", "enter_cooldown", "Start到冷却", [edge_point(b["start"], "right"), (1745, 204), (1745, 1184), edge_point(b["enter_cooldown"], "right")], False),
        ("stop", "enter_cooldown", "Stop到冷却", [edge_point(b["stop"], "right"), (1745, 359), (1745, 1184), edge_point(b["enter_cooldown"], "right")], False),
        ("confirm", "enter_cooldown", "Confirm到冷却", [edge_point(b["confirm"], "right"), (1745, 514), (1745, 1184), edge_point(b["enter_cooldown"], "right")], False),
        ("rock", "enter_cooldown", "Rock到冷却", [edge_point(b["rock"], "right"), (1745, 669), (1745, 1184), edge_point(b["enter_cooldown"], "right")], False),
        ("enter_cooldown", "wait_release", "进入冷却到等待释放", [edge_point(b["enter_cooldown"], "left"), edge_point(b["wait_release"], "right")], False),
        ("wait_release", "return_loop", "等待释放到返回", [edge_point(b["wait_release"], "left"), edge_point(b["return_loop"], "right")], False),
        ("return_loop", "frame", "返回识别循环", [edge_point(b["return_loop"], "left"), (20, 1184), (20, 189), edge_point(b["frame"], "left")], False),
    ]


def build_svg() -> str:
    elements: list[str] = []
    boxes = list(BOXES.values())
    elements.append(text_svg("图2-5 手势状态机流程图", W / 2, 58, 28, 800))
    elements.append(text_svg("识别链路", 230, 118, 20, 700, COLOR_MUTED_TEXT))
    elements.append(text_svg("防抖状态机", 690, 418, 20, 700, COLOR_MUTED_TEXT))
    elements.append(text_svg("业务动作", 1485, 118, 20, 700, COLOR_MUTED_TEXT))
    elements.append(text_svg("触发后处理", 1020, 1095, 20, 700, COLOR_MUTED_TEXT))

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
        draw_center(draw, (cx, cy + 22), list(box.subtitles), sub_font, COLOR_MUTED_TEXT, 1.35)
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


def draw_arrow_png(draw: ImageDraw.ImageDraw, points: Sequence[Point], dashed: bool = False) -> None:
    for a, b in zip(points, points[1:]):
        draw.line([a, b], fill=COLOR_ARROW, width=ARROW_WIDTH)
    draw_arrow_head_png(draw, points[-2], points[-1])


def build_png(path: Path) -> None:
    build_svg()
    img = Image.new("RGB", (W, H), COLOR_BG)
    draw = ImageDraw.Draw(img)
    draw_center(draw, (W / 2, 48), ["图2-5 手势状态机流程图"], pil_font(28, True), COLOR_TEXT)
    draw_center(draw, (230, 112), ["识别链路"], pil_font(20, True), COLOR_MUTED_TEXT)
    draw_center(draw, (690, 412), ["防抖状态机"], pil_font(20, True), COLOR_MUTED_TEXT)
    draw_center(draw, (1485, 112), ["业务动作"], pil_font(20, True), COLOR_MUTED_TEXT)
    draw_center(draw, (1020, 1089), ["触发后处理"], pil_font(20, True), COLOR_MUTED_TEXT)

    for src, dst, name, points, dashed in arrow_routes():
        add_arrow([], name, points, list(BOXES.values()), BOXES[src], BOXES[dst], dashed=dashed)
        draw_arrow_png(draw, points, dashed=dashed)
    for box in BOXES.values():
        draw_box_png(draw, box)
    img.save(path)


def write_markdown_files() -> None:
    md = """# 图2-5 手势状态机流程图

![图2-5 手势状态机流程图](fig2_5_gesture_state_machine.png)

本图用于第二部分 2.3.4 手势识别与交互控制，展示手势识别结果经过业务映射、置信度判断、稳定计数、冷却判断和释放锁判断后，才触发 Start、Stop、Confirm 或 Rock 事件。图中不包含底部图注，可直接在正式文档中引用 SVG 或 PNG。

文件：
- `fig2_5_gesture_state_machine.svg`
- `fig2_5_gesture_state_machine.png`
- `generate_fig2_5_gesture_state_machine.py`
- `fig2_5_gesture_state_machine_self_check.md`
"""
    (OUT_DIR / f"{BASE}.md").write_text(md, encoding="utf-8")


def write_self_check(png_ok: bool) -> None:
    rows = [
        ("是否只生成图2-5", "是"),
        ("是否没有底部图注", "是"),
        ("是否没有源码类名", "是"),
        ("是否没有 class_x 细节", "是，图中未出现"),
        ("是否没有 OpenCV", "是"),
        ("是否表达 Start：Idle 到 Running", "是"),
        ("是否表达 Stop：Running 到 Idle", "是"),
        ("是否表达 Confirm 不切换主状态", "是"),
        ("是否表达 Rock 不切换主状态", "是"),
        ("是否表达稳定计数", "是"),
        ("是否表达冷却判断", "是"),
        ("是否表达释放锁", "是"),
        ("是否表达返回识别循环", "是"),
        ("是否箭头从框边缘连接", "是"),
        ("是否单输出箭头居中", "是"),
        ("是否没有箭头穿框", "是"),
        ("是否没有短箭头", "是"),
        ("是否生成 svg", "是"),
        ("是否生成 png", "是" if png_ok else "否"),
    ]
    lines = ["# 图2-5 手势状态机流程图自检", "", "| 检查项 | 结果 |", "|---|---|"]
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


