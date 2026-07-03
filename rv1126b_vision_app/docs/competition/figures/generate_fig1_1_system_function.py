from __future__ import annotations

from pathlib import Path
from xml.sax.saxutils import escape

from PIL import Image, ImageDraw, ImageFont

OUT_DIR = Path(__file__).resolve().parent
BASE = "fig1_1_system_function"

W, H = 1500, 790
FONT_STACK = '"Microsoft YaHei","Noto Sans CJK SC","SimHei",Arial,sans-serif'

BG = "#FBFCFE"
INK = "#263238"
MUTED = "#52606D"
ARROW = "#334A5F"
ARROW_W = 3
ARROW_HEAD = 12

COLORS = {
    "input": ("#E8F1FF", "#9CBCEB"),
    "process": ("#E7F7F2", "#8BCDBB"),
    "function": ("#FFF4D8", "#D8BD6E"),
    "feedback": ("#F2ECFF", "#B8A2E6"),
}

Point = tuple[float, float]


def svg_text(lines: list[str], x: float, center_y: float, size: int, weight: int | str,
             color: str = INK, gap: int | None = None) -> str:
    if gap is None:
        gap = round(size * 1.32)
    total = size + (len(lines) - 1) * gap
    start_y = center_y - total / 2 + size * 0.74
    out = [
        f'<text x="{x:.1f}" y="{start_y:.1f}" text-anchor="middle" '
        f'font-family={FONT_STACK!r} font-size="{size}" font-weight="{weight}" fill="{color}">'
    ]
    for i, line in enumerate(lines):
        dy = 0 if i == 0 else gap
        out.append(f'<tspan x="{x:.1f}" dy="{dy}">{escape(line)}</tspan>')
    out.append("</text>")
    return "".join(out)


def text_centers(cy: float, subtitle_count: int) -> tuple[float, float]:
    if subtitle_count <= 0:
        return cy, cy
    if subtitle_count == 1:
        return cy - 14, cy + 20
    if subtitle_count == 2:
        return cy - 22, cy + 18
    return cy - 26, cy + 18


def svg_box(x: int, y: int, w: int, h: int, fill: str, stroke: str,
            title: str, subtitle: list[str]) -> str:
    cx = x + w / 2
    cy = y + h / 2
    title_y, sub_center = text_centers(cy, len(subtitle))
    parts = [
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="12" fill="{fill}" '
        f'stroke="{stroke}" stroke-width="2"/>',
        svg_text([title], cx, title_y, 16, 700),
    ]
    if subtitle:
        parts.append(svg_text(subtitle, cx, sub_center, 12, 400, MUTED, 16))
    return "\n".join(parts)


def unit_vector(start: Point, end: Point) -> Point:
    import math

    sx, sy = start
    ex, ey = end
    length = math.hypot(ex - sx, ey - sy)
    if length == 0:
        return 0.0, 0.0
    return (ex - sx) / length, (ey - sy) / length


def shaft_points(points: list[Point]) -> list[Point]:
    ux, uy = unit_vector(points[-2], points[-1])
    base = (points[-1][0] - ux * ARROW_HEAD, points[-1][1] - uy * ARROW_HEAD)
    return points[:-1] + [base]


def arrow_head_points(start: Point, tip: Point) -> list[Point]:
    ux, uy = unit_vector(start, tip)
    if ux == 0 and uy == 0:
        return [tip, tip, tip]
    px, py = -uy, ux
    base_x = tip[0] - ux * ARROW_HEAD
    base_y = tip[1] - uy * ARROW_HEAD
    half_w = ARROW_HEAD * 0.45
    return [
        tip,
        (base_x + px * half_w, base_y + py * half_w),
        (base_x - px * half_w, base_y - py * half_w),
    ]


def svg_arrow(points: list[Point]) -> str:
    shaft = shaft_points(points)
    point_text = " ".join(f"{x:.1f},{y:.1f}" for x, y in shaft)
    head = arrow_head_points(points[-2], points[-1])
    head_text = " ".join(f"{x:.1f},{y:.1f}" for x, y in head)
    return (
        f'<polyline points="{point_text}" fill="none" stroke="{ARROW}" stroke-width="{ARROW_W}" '
        f'stroke-linecap="butt" stroke-linejoin="round"/>'
        f'<polygon points="{head_text}" fill="{ARROW}"/>'
    )


def build_data():
    columns = [
        ("输入层", 70, "input"),
        ("端侧处理层", 430, "process"),
        ("功能层", 800, "function"),
        ("反馈层", 1165, "feedback"),
    ]
    box_w = 260
    box_h = 104
    y_rows = [145, 270, 395, 520]
    feedback_y = [190, 342, 494]
    boxes_by_kind = {
        "input": [
            ("摄像头画面", ["桌面前方视觉输入"]),
            ("用户手势", ["Start / Stop / Confirm / Rock"]),
            ("人体姿态", ["上半身与头肩状态"]),
            ("桌面杯/瓶", ["饮品容器位置"]),
        ],
        "process": [
            ("RV1126B 端侧 AI", ["本地感知", "本地判断"]),
            ("图像预处理", ["RGA 尺寸变换与坐标映射"]),
            ("多模型协同", ["手势识别", "姿态估计", "饮品检测"]),
            ("状态机融合", ["防抖、冷却", "提醒事件判断"]),
        ],
        "function": [
            ("手势启停", ["非接触运行控制"]),
            ("坐姿提醒", ["基于姿态状态判断"]),
            ("饮水判断", ["杯/瓶与头部距离"]),
            ("定时饮水提醒", ["默认 30 分钟，可配置"]),
        ],
        "feedback": [
            ("ST7789 本地屏幕", ["LVGL 状态反馈"]),
            ("HTTP-FLV / RTSP", ["上位机视频查看"]),
            ("MQTT 状态同步", ["status / event / telemetry"]),
        ],
    }
    return columns, box_w, box_h, y_rows, feedback_y, boxes_by_kind


def arrow_routes(box_w: int, box_h: int, y_rows: list[int], feedback_y: list[int]) -> list[list[Point]]:
    centers = [y + box_h / 2 for y in y_rows]
    feedback_centers = [y + box_h / 2 for y in feedback_y]
    routes: list[list[Point]] = []
    for y in centers:
        routes.append([(70 + box_w, y), (430, y)])
        routes.append([(430 + box_w, y), (800, y)])
    mid_x = 1110
    for i in range(3):
        routes.append([(800 + box_w, centers[i]), (mid_x, centers[i]), (mid_x, feedback_centers[i]), (1165, feedback_centers[i])])
    routes.append([(800 + box_w, centers[3]), (mid_x, centers[3]), (mid_x, feedback_centers[2]), (1165, feedback_centers[2])])
    return routes


def build_svg() -> str:
    columns, box_w, box_h, y_rows, feedback_y, boxes_by_kind = build_data()
    body: list[str] = []
    body.append(svg_text(["图1-1 系统功能示意图"], W / 2, 50, 28, 800))
    for title, x, kind in columns:
        body.append(svg_text([title], x + box_w / 2, 112, 18, 600, MUTED))

    for title, x, kind in columns:
        fill, stroke = COLORS[kind]
        ys = feedback_y if kind == "feedback" else y_rows
        for (head, sub), y in zip(boxes_by_kind[kind], ys):
            body.append(svg_box(x, y, box_w, box_h, fill, stroke, head, sub))

    for route in arrow_routes(box_w, box_h, y_rows, feedback_y):
        body.append(svg_arrow(route))

    caption = "本图展示系统从视觉输入、RV1126B 端侧处理、功能判断到多端反馈的整体功能关系。"
    body.append(svg_text([caption], W / 2, 750, 15, 400, MUTED))

    return f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
  <rect x="0" y="0" width="{W}" height="{H}" fill="{BG}"/>
  {' '.join(body)}
</svg>
'''


def find_font(bold: bool = False) -> str | None:
    candidates = [
        "C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf",
    ]
    for path in candidates:
        if path and Path(path).exists():
            return path
    return None


def pil_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    path = find_font(bold)
    if path:
        return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def draw_centered_text(draw: ImageDraw.ImageDraw, xy: Point, lines: list[str],
                       font: ImageFont.ImageFont, fill: str, line_gap: float = 1.3) -> None:
    x, cy = xy
    line_heights = []
    widths = []
    for line in lines:
        bbox = draw.textbbox((0, 0), line, font=font)
        widths.append(bbox[2] - bbox[0])
        line_heights.append(bbox[3] - bbox[1])
    gap_px = int(getattr(font, "size", 12) * (line_gap - 1.0))
    total_h = sum(line_heights) + gap_px * (len(lines) - 1)
    y = cy - total_h / 2
    for line, width, height in zip(lines, widths, line_heights):
        draw.text((x - width / 2, y), line, font=font, fill=fill)
        y += height + gap_px


def draw_round_rect(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int],
                    fill: str, outline: str) -> None:
    draw.rounded_rectangle(box, radius=12, fill=fill, outline=outline, width=2)


def draw_arrow(draw: ImageDraw.ImageDraw, points: list[Point]) -> None:
    shaft = shaft_points(points)
    for a, b in zip(shaft, shaft[1:]):
        draw.line([a, b], fill=ARROW, width=ARROW_W)
    draw.polygon(arrow_head_points(points[-2], points[-1]), fill=ARROW)


def build_png() -> None:
    img = Image.new("RGB", (W, H), BG)
    draw = ImageDraw.Draw(img)
    title_font = pil_font(28, True)
    col_font = pil_font(18, True)
    box_title_font = pil_font(16, True)
    body_font = pil_font(12, False)
    caption_font = pil_font(15, False)

    columns, box_w, box_h, y_rows, feedback_y, boxes_by_kind = build_data()

    draw_centered_text(draw, (W / 2, 50), ["图1-1 系统功能示意图"], title_font, INK)
    for label, x, kind in columns:
        draw_centered_text(draw, (x + box_w / 2, 112), [label], col_font, MUTED)

    for _, x, kind in columns:
        fill, stroke = COLORS[kind]
        ys = feedback_y if kind == "feedback" else y_rows
        for (head, sub), y in zip(boxes_by_kind[kind], ys):
            draw_round_rect(draw, (x, y, x + box_w, y + box_h), fill, stroke)
            cy = y + box_h / 2
            title_center, sub_center = text_centers(cy, len(sub))
            draw_centered_text(draw, (x + box_w / 2, title_center), [head], box_title_font, INK)
            if sub:
                draw_centered_text(draw, (x + box_w / 2, sub_center), sub, body_font, MUTED, 1.28)

    for route in arrow_routes(box_w, box_h, y_rows, feedback_y):
        draw_arrow(draw, route)

    caption = "本图展示系统从视觉输入、RV1126B 端侧处理、功能判断到多端反馈的整体功能关系。"
    draw_centered_text(draw, (W / 2, 750), [caption], caption_font, MUTED)
    img.save(OUT_DIR / f"{BASE}.png")


def write_md() -> None:
    caption = "本图展示系统从视觉输入、RV1126B 端侧处理、功能判断到多端反馈的整体功能关系。"
    mermaid = """flowchart LR
    A["输入层<br/>摄像头画面 / 用户手势 / 人体姿态 / 桌面杯瓶"] --> B["端侧处理层<br/>RV1126B 端侧 AI / 图像预处理 / 多模型协同 / 状态机融合"]
    B --> C["功能层<br/>手势启停 / 坐姿提醒 / 饮水判断 / 定时饮水提醒"]
    C --> D["反馈层<br/>ST7789 本地屏幕 / HTTP-FLV 与 RTSP relay / MQTT 状态同步"]
"""
    md = f"""# 图1-1 系统功能示意图

![图1-1 系统功能示意图]({BASE}.png)

图注：
{caption}

文件说明：
- SVG 矢量图：`{BASE}.svg`
- PNG 位图：`{BASE}.png`
- 图中上位机仅作为视频查看和 MQTT 订阅端，不作为 AI 推理节点。

Mermaid 备份：
```mermaid
{mermaid}
```
"""
    (OUT_DIR / f"{BASE}.md").write_text(md, encoding="utf-8")


def generate() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_DIR / f"{BASE}.svg").write_text(build_svg(), encoding="utf-8")
    build_png()
    write_md()


def main() -> None:
    generate()
    print(f"generated {BASE}.svg/.png/.md in {OUT_DIR}")


if __name__ == "__main__":
    main()
