from __future__ import annotations

from pathlib import Path
from xml.sax.saxutils import escape

from PIL import Image, ImageDraw, ImageFont

OUT_DIR = Path(__file__).resolve().parent
BASE = "fig1_3_design_flow"

W, H = 1660, 430
FONT_STACK = '"Microsoft YaHei","Noto Sans CJK SC","SimHei",Arial,sans-serif'

BG = "#FBFCFE"
INK = "#263238"
MUTED = "#52606D"
LINE = "#334A5F"
ARROW_W = 2.6

STAGES = [
    ("01", ["需求分析"], "#E8F1FF", "#9CBCEB"),
    ("02", ["平台选型"], "#E8F1FF", "#9CBCEB"),
    ("03", ["摄像头采集", "链路搭建"], "#E7F7F2", "#8BCDBB"),
    ("04", ["RKNN 模型部署"], "#E7F7F2", "#8BCDBB"),
    ("05", ["图像预处理", "坐标映射"], "#EDF6F8", "#9BBFC8"),
    ("06", ["模型后处理", "状态机设计"], "#FFF1D6", "#D8B463"),
    ("07", ["本地显示", "视频推流", "MQTT 联动"], "#FFF1D6", "#D8B463"),
    ("08", ["实机验证", "参数调整"], "#F2ECFF", "#B8A2E6"),
]

Point = tuple[float, float]


def svg_text(lines: list[str], x: float, cy: float, size: int, weight: int | str,
             color: str = INK, gap: int | None = None) -> str:
    if gap is None:
        gap = round(size * 1.28)
    total = size + (len(lines) - 1) * gap
    y = cy - total / 2 + size * 0.74
    parts = [
        f'<text x="{x:.1f}" y="{y:.1f}" text-anchor="middle" '
        f'font-family={FONT_STACK!r} font-size="{size}" font-weight="{weight}" fill="{color}">'
    ]
    for i, line in enumerate(lines):
        dy = 0 if i == 0 else gap
        parts.append(f'<tspan x="{x:.1f}" dy="{dy}">{escape(line)}</tspan>')
    parts.append("</text>")
    return "".join(parts)


def arrow_head_size(gap: float) -> float:
    return max(6.5, min(8.0, gap * 0.34))


def arrow_head_points(start: Point, tip: Point, head: float) -> list[Point]:
    import math

    sx, sy = start
    tx, ty = tip
    dx, dy = tx - sx, ty - sy
    length = math.hypot(dx, dy)
    if length == 0:
        return [tip, tip, tip]
    ux, uy = dx / length, dy / length
    px, py = -uy, ux
    base_x = tx - ux * head
    base_y = ty - uy * head
    half_w = head * 0.42
    return [
        (tx, ty),
        (base_x + px * half_w, base_y + py * half_w),
        (base_x - px * half_w, base_y - py * half_w),
    ]


def svg_arrow(x1: float, y1: float, x2: float, y2: float) -> str:
    gap = abs(x2 - x1) if y1 == y2 else ((x2 - x1) ** 2 + (y2 - y1) ** 2) ** 0.5
    head = arrow_head_size(gap)
    shaft_end = (x2 - head, y2)
    head_text = " ".join(f"{x:.1f},{y:.1f}" for x, y in arrow_head_points((x1, y1), (x2, y2), head))
    return (
        f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{shaft_end[0]:.1f}" y2="{shaft_end[1]:.1f}" '
        f'stroke="{LINE}" stroke-width="{ARROW_W}" stroke-linecap="butt"/>'
        f'<polygon points="{head_text}" fill="{LINE}"/>'
    )


def svg_stage(x: int, y: int, w: int, h: int, number: str, lines: list[str],
              fill: str, stroke: str) -> str:
    cx, cy = x + w / 2, y + h / 2
    return "\n".join([
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="14" fill="{fill}" '
        f'stroke="{stroke}" stroke-width="2.2"/>',
        f'<circle cx="{x + 26}" cy="{y + 25}" r="15" fill="#FFFFFF" '
        f'stroke="{stroke}" stroke-width="1.8"/>',
        svg_text([number], x + 26, y + 25, 12, 700, MUTED),
        svg_text(lines, cx, cy + 5, 17, 700, INK, 22),
    ])


def layout():
    box_w, box_h = 178, 112
    gap = 23
    start_x, y = 38, 172
    center_y = y + box_h / 2
    return box_w, box_h, gap, start_x, y, center_y


def build_svg() -> str:
    box_w, box_h, gap, start_x, y, center_y = layout()
    body: list[str] = []
    body.append(svg_text(["图1-3 系统设计流程图"], W / 2, 50, 28, 800))
    for i, (num, lines, fill, stroke) in enumerate(STAGES):
        x = start_x + i * (box_w + gap)
        body.append(svg_stage(x, y, box_w, box_h, num, lines, fill, stroke))
        if i < len(STAGES) - 1:
            start = x + box_w
            end = x + box_w + gap
            body.append(svg_arrow(start, center_y, end, center_y))

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


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    path = find_font(bold)
    if path:
        return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def draw_center(draw: ImageDraw.ImageDraw, center: Point, lines: list[str],
                fnt: ImageFont.ImageFont, fill: str, line_gap: float = 1.28) -> None:
    x, cy = center
    metrics = []
    for line in lines:
        box = draw.textbbox((0, 0), line, font=fnt)
        metrics.append((line, box[2] - box[0], box[3] - box[1]))
    gap = int(getattr(fnt, "size", 12) * (line_gap - 1.0))
    total = sum(h for _, _, h in metrics) + gap * (len(metrics) - 1)
    y = cy - total / 2
    for line, w, h in metrics:
        draw.text((x - w / 2, y), line, font=fnt, fill=fill)
        y += h + gap


def draw_arrow(draw: ImageDraw.ImageDraw, x1: float, y1: float, x2: float, y2: float) -> None:
    gap = abs(x2 - x1)
    head = arrow_head_size(gap)
    shaft_end = (x2 - head, y2)
    draw.line([(x1, y1), shaft_end], fill=LINE, width=3)
    draw.polygon(arrow_head_points((x1, y1), (x2, y2), head), fill=LINE)


def build_png() -> None:
    img = Image.new("RGB", (W, H), BG)
    draw = ImageDraw.Draw(img)
    title_font = font(28, True)
    stage_font = font(17, True)
    number_font = font(12, True)

    draw_center(draw, (W / 2, 50), ["图1-3 系统设计流程图"], title_font, INK)

    box_w, box_h, gap, start_x, y, center_y = layout()

    for i, (num, lines, fill, stroke) in enumerate(STAGES):
        x = start_x + i * (box_w + gap)
        if i < len(STAGES) - 1:
            draw_arrow(draw, x + box_w, center_y, x + box_w + gap, center_y)

    for i, (num, lines, fill, stroke) in enumerate(STAGES):
        x = start_x + i * (box_w + gap)
        draw.rounded_rectangle((x, y, x + box_w, y + box_h), radius=14, fill=fill, outline=stroke, width=2)
        draw.ellipse((x + 11, y + 10, x + 41, y + 40), fill="#FFFFFF", outline=stroke, width=2)
        draw_center(draw, (x + 26, y + 25), [num], number_font, MUTED)
        draw_center(draw, (x + box_w / 2, y + box_h / 2 + 5), lines, stage_font, INK, 1.28)

    img.save(OUT_DIR / f"{BASE}.png")


def write_md() -> None:
    md = f"""# 图1-3 系统设计流程图

![图1-3 系统设计流程图]({BASE}.png)

文件说明：
- SVG 矢量图：`{BASE}.svg`
- PNG 位图：`{BASE}.png`
- 本图表示从需求分析到实机验证与参数调整的设计路线，不表示性能测试已经完成。

Mermaid 备份：
```mermaid
flowchart LR
    A["需求分析"] --> B["平台选型"]
    B --> C["摄像头采集链路搭建"]
    C --> D["RKNN 模型部署"]
    D --> E["图像预处理与坐标映射"]
    E --> F["模型后处理与状态机设计"]
    F --> G["本地显示、视频推流与 MQTT 联调"]
    G --> H["实机验证与参数调整"]
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
