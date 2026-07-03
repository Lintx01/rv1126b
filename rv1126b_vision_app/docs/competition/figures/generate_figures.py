from __future__ import annotations

from pathlib import Path
from xml.sax.saxutils import escape


OUT_DIR = Path(__file__).resolve().parent


FONT = '"Microsoft YaHei","Noto Sans CJK SC","SimHei",Arial,sans-serif'
INK = "#263238"
MUTED = "#64748B"
LINE = "#475569"
BG = "#FBFCFE"
INPUT = "#E8F1FF"
PROCESS = "#EAF7EF"
AI = "#EEF2FF"
STATE = "#FFF4E5"
OUTPUT = "#F3ECFF"
WARN = "#FCE7E7"
WHITE = "#FFFFFF"


def tspan_lines(lines: list[str], x: int, y: int, size: int = 22, color: str = INK,
                weight: str = "500", anchor: str = "middle", line_gap: int = 26) -> str:
    parts = [
        f'<text x="{x}" y="{y}" text-anchor="{anchor}" '
        f'font-family={FONT!r} font-size="{size}" fill="{color}" font-weight="{weight}">'
    ]
    for i, line in enumerate(lines):
        dy = 0 if i == 0 else line_gap
        parts.append(f'<tspan x="{x}" dy="{dy}">{escape(line)}</tspan>')
    parts.append("</text>")
    return "".join(parts)


def rect(x: int, y: int, w: int, h: int, fill: str, stroke: str = "#CBD5E1",
         rx: int = 12, sw: int = 2, extra: str = "") -> str:
    return (
        f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" '
        f'fill="{fill}" stroke="{stroke}" stroke-width="{sw}" {extra}/>'
    )


def label_box(x: int, y: int, w: int, h: int, title: str, lines: list[str],
              fill: str, title_size: int = 22, body_size: int = 18) -> str:
    cx = x + w // 2
    body_y = y + 48
    content = [rect(x, y, w, h, fill)]
    content.append(tspan_lines([title], cx, y + 28, title_size, INK, "700"))
    if lines:
        content.append(tspan_lines(lines, cx, body_y, body_size, INK, "500", line_gap=23))
    return "\n".join(content)


def arrow(x1: int, y1: int, x2: int, y2: int, label: str | None = None,
          color: str = LINE) -> str:
    mid_x = (x1 + x2) // 2
    mid_y = (y1 + y2) // 2
    parts = [
        f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
        f'stroke="{color}" stroke-width="2.4" marker-end="url(#arrow)"/>'
    ]
    if label:
        parts.append(
            f'<rect x="{mid_x - 58}" y="{mid_y - 18}" width="116" height="24" rx="12" '
            f'fill="{WHITE}" stroke="#E2E8F0"/>'
        )
        parts.append(tspan_lines([label], mid_x, mid_y - 1, 15, MUTED, "500"))
    return "\n".join(parts)


def svg_doc(width: int, height: int, title: str, body: str, caption: str) -> str:
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
  <defs>
    <filter id="shadow" x="-10%" y="-10%" width="120%" height="130%">
      <feDropShadow dx="0" dy="4" stdDeviation="5" flood-color="#0F172A" flood-opacity="0.10"/>
    </filter>
    <marker id="arrow" markerWidth="12" markerHeight="12" refX="10" refY="6" orient="auto" markerUnits="strokeWidth">
      <path d="M2,2 L10,6 L2,10 Z" fill="{LINE}"/>
    </marker>
  </defs>
  <rect x="0" y="0" width="{width}" height="{height}" fill="{BG}"/>
  {tspan_lines([title], width // 2, 46, 28, INK, "800")}
  {body}
  {tspan_lines([caption], width // 2, height - 28, 17, MUTED, "500")}
</svg>
'''


def convert_png(svg_path: Path) -> tuple[bool, str]:
    try:
        import cairosvg  # type: ignore
    except Exception as exc:
        return False, f"未生成 PNG：当前 Python 环境未安装 cairosvg（{exc.__class__.__name__}）。"
    png_path = svg_path.with_suffix(".png")
    cairosvg.svg2png(url=str(svg_path), write_to=str(png_path), output_width=None, output_height=None)
    return True, "PNG 已由 SVG 转换生成。"


def write_md(name: str, title: str, caption: str, mermaid: str, png_ok: bool, png_note: str) -> None:
    image_name = f"{name}.png" if png_ok else f"{name}.svg"
    md = f"""# {title}

![{title}]({image_name})

图注：
{caption}

文件说明：
- SVG 矢量图：`{name}.svg`
- PNG 位图：`{name}.png`（{png_note}）
- 图中上位机仅作为视频查看、MQTT 订阅和调试端，不作为 AI 推理节点。

Mermaid 备份：
```mermaid
{mermaid}
```
"""
    (OUT_DIR / f"{name}.md").write_text(md, encoding="utf-8")


def generate_fig1_1() -> None:
    from generate_fig1_1_system_function import generate

    generate()

def generate_fig1_3() -> None:
    from generate_fig1_3_design_flow import generate

    generate()

def generate_fig2_1() -> None:
    from generate_fig2_1_system_overview import generate

    generate()

def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    generate_fig1_1()
    generate_fig1_3()
    generate_fig2_1()
    print("generated figures in", OUT_DIR)


def generate_fig1_1() -> None:
    from generate_fig1_1_system_function import generate

    generate()


if __name__ == "__main__":
    main()
