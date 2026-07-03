"""
Reusable SVG style helpers for competition document figures.

This module defines a small standard-library SVG toolkit for formal
engineering diagrams. It intentionally does not generate any concrete
figure by itself.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from math import hypot
from pathlib import Path
from typing import Iterable, List, Optional, Sequence, Tuple
from xml.sax.saxutils import escape


Point = Tuple[float, float]
Segment = Tuple[Point, Point]


CANVAS_W = 1600
CANVAS_H = 900

TITLE_FONT = 28
SECTION_FONT = 23
BOX_TITLE_FONT = 17
BOX_SUB_FONT = 12

BOX_RADIUS = 16
BOX_STROKE_WIDTH = 2
BOX_PADDING_X = 18
BOX_PADDING_Y = 14

ARROW_WIDTH = 3
ARROW_HEAD_SIZE = 12
MIN_ARROW_SHAFT = 45
MIN_BOX_GAP_FOR_ARROW = 70
MIN_POLYLINE_SEGMENT = 24

COLOR_BG = "#f8fafc"
COLOR_TEXT = "#172033"
COLOR_MUTED_TEXT = "#4b5563"
COLOR_BORDER = "#6b7a90"

COLOR_INPUT = "#dbeafe"
COLOR_EDGE_INPUT = "#7aa7d9"
COLOR_PLATFORM_BG = "#e8f5f1"
COLOR_EDGE_PLATFORM = "#7cb6a6"
COLOR_PROCESS = "#f8fbff"
COLOR_EDGE_PROCESS = "#91a9c6"
COLOR_MODEL = "#ebe9ff"
COLOR_EDGE_MODEL = "#aaa3dd"
COLOR_STATE = "#fff4d6"
COLOR_EDGE_STATE = "#d8b86d"
COLOR_OUTPUT = "#f0e7ff"
COLOR_EDGE_OUTPUT = "#b9a0de"
COLOR_PC = "#e8eef6"
COLOR_EDGE_PC = "#91a1b5"
COLOR_ARROW = "#43556f"


@dataclass
class Box:
    """A rounded rectangle node with fixed ports on its edges."""

    x: float
    y: float
    width: float
    height: float
    title: str
    subtitles: Sequence[str] = field(default_factory=list)
    fill: str = COLOR_PROCESS
    border: str = COLOR_EDGE_PROCESS
    title_font: int = BOX_TITLE_FONT
    subtitle_font: int = BOX_SUB_FONT
    radius: int = BOX_RADIUS
    name: Optional[str] = None

    @property
    def left(self) -> float:
        return self.x

    @property
    def right(self) -> float:
        return self.x + self.width

    @property
    def top(self) -> float:
        return self.y

    @property
    def bottom(self) -> float:
        return self.y + self.height

    @property
    def center(self) -> Point:
        return (self.x + self.width / 2, self.y + self.height / 2)

    @property
    def ports(self) -> dict:
        cx, cy = self.center
        return {
            "left": (self.left, cy),
            "right": (self.right, cy),
            "top": (cx, self.top),
            "bottom": (cx, self.bottom),
        }

    @property
    def label(self) -> str:
        return self.name or self.title


class SvgFigure:
    """SVG document builder with boxes, arrows, and validation warnings."""

    def __init__(
        self,
        width: int = CANVAS_W,
        height: int = CANVAS_H,
        title: Optional[str] = None,
        background: str = COLOR_BG,
        strict: bool = False,
    ) -> None:
        self.width = width
        self.height = height
        self.title = title
        self.background = background
        self.strict = strict
        self.elements: List[str] = []
        self.boxes: List[Box] = []
        self.warnings: List[str] = []

    def warn_or_raise(self, message: str) -> None:
        warn_or_raise(message, self.warnings, self.strict)

    def add_box(self, box: Box) -> Box:
        self.boxes.append(box)
        draw_box(self.elements, box)
        return box

    def add_title(self, title: Optional[str] = None, y: float = 48) -> None:
        text = title or self.title
        if not text:
            return
        self.elements.append(
            f'<text x="{self.width / 2:.1f}" y="{y:.1f}" '
            f'text-anchor="middle" font-family="Microsoft YaHei, SimHei, Arial, sans-serif" '
            f'font-size="{TITLE_FONT}" font-weight="700" fill="{COLOR_TEXT}">'
            f"{escape(text)}</text>"
        )

    def add_section_label(self, text: str, x: float, y: float) -> None:
        self.elements.append(
            f'<text x="{x:.1f}" y="{y:.1f}" text-anchor="middle" '
            f'font-family="Microsoft YaHei, SimHei, Arial, sans-serif" '
            f'font-size="{SECTION_FONT}" font-weight="700" fill="{COLOR_TEXT}">'
            f"{escape(text)}</text>"
        )

    def connect_boxes(
        self,
        src_box: Box,
        src_port: str,
        dst_box: Box,
        dst_port: str,
        route: Optional[Sequence[Point]] = None,
        dashed: bool = False,
    ) -> List[Point]:
        return connect_boxes(
            self.elements,
            src_box,
            src_port,
            dst_box,
            dst_port,
            route=route,
            all_boxes=self.boxes,
            warnings=self.warnings,
            strict=self.strict,
            dashed=dashed,
        )

    def save_svg(self, path: str | Path) -> Path:
        return save_svg(path, self.render())

    def try_export_png(self, svg_path: str | Path, png_path: str | Path) -> bool:
        return try_export_png(svg_path, png_path)

    def render(self) -> str:
        header = [
            '<?xml version="1.0" encoding="UTF-8"?>',
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.width}" '
            f'height="{self.height}" viewBox="0 0 {self.width} {self.height}">',
            "<defs>",
            '<filter id="softShadow" x="-10%" y="-10%" width="120%" height="130%">',
            '<feDropShadow dx="0" dy="3" stdDeviation="3" flood-color="#64748b" flood-opacity="0.13"/>',
            "</filter>",
            "</defs>",
            f'<rect width="100%" height="100%" fill="{self.background}"/>',
        ]
        footer = ["</svg>"]
        return "\n".join(header + self.elements + footer) + "\n"


def warn_or_raise(message: str, warnings: Optional[List[str]] = None, strict: bool = False) -> None:
    if warnings is not None:
        warnings.append(message)
    if strict:
        raise ValueError(message)
    print(f"WARNING: {message}")


def get_port(box: Box, port_name: str) -> Point:
    try:
        return box.ports[port_name]
    except KeyError as exc:
        valid = ", ".join(sorted(box.ports))
        raise ValueError(f"Unknown port {port_name!r}; expected one of: {valid}") from exc


def draw_box(svg: List[str], box: Box, shadow: bool = True) -> None:
    filter_attr = ' filter="url(#softShadow)"' if shadow else ""
    svg.append(
        f'<rect x="{box.x:.1f}" y="{box.y:.1f}" width="{box.width:.1f}" '
        f'height="{box.height:.1f}" rx="{box.radius}" ry="{box.radius}" '
        f'fill="{box.fill}" stroke="{box.border}" stroke-width="{BOX_STROKE_WIDTH}"{filter_attr}/>'
    )

    lines: List[Tuple[str, int, int, str]] = [(box.title, box.title_font, 700, COLOR_TEXT)]
    lines.extend((line, box.subtitle_font, 400, COLOR_MUTED_TEXT) for line in box.subtitles)

    # Estimate visual line boxes so all text is centered as one block.
    gap_after_title = 8 if box.subtitles else 0
    subtitle_line_gap = 4
    total_h = box.title_font
    if box.subtitles:
        total_h += gap_after_title
        total_h += len(box.subtitles) * box.subtitle_font
        total_h += max(0, len(box.subtitles) - 1) * subtitle_line_gap

    current_y = box.y + (box.height - total_h) / 2 + box.title_font
    center_x = box.x + box.width / 2

    for idx, (text, font_size, weight, color) in enumerate(lines):
        if idx == 1:
            current_y += gap_after_title
        elif idx > 1:
            current_y += subtitle_line_gap
        svg.append(
            f'<text x="{center_x:.1f}" y="{current_y:.1f}" text-anchor="middle" '
            f'font-family="Microsoft YaHei, SimHei, Arial, sans-serif" '
            f'font-size="{font_size}" font-weight="{weight}" fill="{color}">'
            f"{escape(str(text))}</text>"
        )
        current_y += font_size


def route_polyline(start: Point, end: Point, via: Optional[Sequence[Point]] = None) -> List[Point]:
    return [start] + list(via or []) + [end]


def connect_boxes(
    svg: List[str],
    src_box: Box,
    src_port: str,
    dst_box: Box,
    dst_port: str,
    route: Optional[Sequence[Point]] = None,
    all_boxes: Optional[Sequence[Box]] = None,
    warnings: Optional[List[str]] = None,
    strict: bool = False,
    dashed: bool = False,
) -> List[Point]:
    start = get_port(src_box, src_port)
    end = get_port(dst_box, dst_port)
    points = route_polyline(start, end, route)

    if route is None:
        straight_gap = segment_length(start, end)
        if straight_gap < MIN_BOX_GAP_FOR_ARROW:
            warn_or_raise(
                f"Short direct connection {src_box.label}->{dst_box.label}: "
                f"{straight_gap:.1f}px < {MIN_BOX_GAP_FOR_ARROW}px. "
                "Use a polyline route or increase spacing.",
                warnings,
                strict,
            )
            return points
        if not is_orthogonal_segment(start, end):
            warn_or_raise(
                f"Direct connection {src_box.label}->{dst_box.label} is diagonal. "
                "Use an orthogonal polyline route.",
                warnings,
                strict,
            )

    validate_arrow(points, all_boxes or [], src_box, dst_box, warnings, strict)
    draw_arrow(svg, points, warnings=warnings, strict=strict, dashed=dashed)
    return points


def draw_arrow(
    svg: List[str],
    points: Sequence[Point],
    warnings: Optional[List[str]] = None,
    strict: bool = False,
    dashed: bool = False,
) -> None:
    if len(points) < 2:
        warn_or_raise("Arrow needs at least two points.", warnings, strict)
        return

    last_len = segment_length(points[-2], points[-1])
    if last_len < MIN_ARROW_SHAFT:
        warn_or_raise(
            f"Last arrow shaft is too short: {last_len:.1f}px < {MIN_ARROW_SHAFT}px.",
            warnings,
            strict,
        )

    point_text = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
    dash_attr = ' stroke-dasharray="8 7"' if dashed else ""
    svg.append(
        f'<polyline points="{point_text}" fill="none" stroke="{COLOR_ARROW}" '
        f'stroke-width="{ARROW_WIDTH}" stroke-linecap="round" stroke-linejoin="round"{dash_attr}/>'
    )

    head = arrow_head_points(points[-2], points[-1], ARROW_HEAD_SIZE)
    head_text = " ".join(f"{x:.1f},{y:.1f}" for x, y in head)
    svg.append(f'<polygon points="{head_text}" fill="{COLOR_ARROW}"/>')


def arrow_head_points(start: Point, tip: Point, size: float) -> List[Point]:
    sx, sy = start
    tx, ty = tip
    dx, dy = tx - sx, ty - sy
    length = hypot(dx, dy)
    if length == 0:
        return [tip, tip, tip]
    ux, uy = dx / length, dy / length
    px, py = -uy, ux
    base_x = tx - ux * size
    base_y = ty - uy * size
    half_w = size * 0.45
    return [
        (tx, ty),
        (base_x + px * half_w, base_y + py * half_w),
        (base_x - px * half_w, base_y - py * half_w),
    ]


def validate_arrow(
    points: Sequence[Point],
    all_boxes: Sequence[Box],
    src_box: Optional[Box] = None,
    dst_box: Optional[Box] = None,
    warnings: Optional[List[str]] = None,
    strict: bool = False,
) -> bool:
    ok = True
    total_len = polyline_length(points)

    if total_len < ARROW_HEAD_SIZE * 3:
        ok = False
        warn_or_raise(
            f"Arrow total length is too short: {total_len:.1f}px < {ARROW_HEAD_SIZE * 3}px.",
            warnings,
            strict,
        )

    for a, b in zip(points, points[1:]):
        seg_len = segment_length(a, b)
        if seg_len < MIN_POLYLINE_SEGMENT:
            ok = False
            warn_or_raise(
                f"Polyline segment is too short: {seg_len:.1f}px < {MIN_POLYLINE_SEGMENT}px.",
                warnings,
                strict,
            )
        if not is_orthogonal_segment(a, b):
            ok = False
            warn_or_raise("Polyline contains a diagonal segment; use 90-degree routing.", warnings, strict)

        for box in all_boxes:
            if box is src_box or box is dst_box:
                continue
            if check_segment_intersects_box((a, b), box, margin=8):
                ok = False
                warn_or_raise(
                    f"Arrow segment intersects non-target box: {box.label}.",
                    warnings,
                    strict,
                )

    if src_box is not None and not point_on_rect_edge(points[0], src_box):
        ok = False
        warn_or_raise(f"Arrow start is not on source box edge: {src_box.label}.", warnings, strict)

    if dst_box is not None and not point_on_rect_edge(points[-1], dst_box):
        ok = False
        warn_or_raise(f"Arrow end is not on target box edge: {dst_box.label}.", warnings, strict)

    if len(points) >= 2 and segment_length(points[-2], points[-1]) < MIN_ARROW_SHAFT:
        ok = False
        warn_or_raise(
            f"Last visible shaft is too short before arrow head: "
            f"{segment_length(points[-2], points[-1]):.1f}px.",
            warnings,
            strict,
        )

    return ok


def check_segment_intersects_box(segment: Segment, box: Box, margin: float = 8) -> bool:
    """Check horizontal/vertical segment intersection with an expanded box interior."""

    (x1, y1), (x2, y2) = segment
    left, top, right, bottom = expanded_rect(box, margin)

    if y1 == y2:
        y = y1
        if not (top < y < bottom):
            return False
        a, b = sorted((x1, x2))
        return max(a, left) < min(b, right)

    if x1 == x2:
        x = x1
        if not (left < x < right):
            return False
        a, b = sorted((y1, y2))
        return max(a, top) < min(b, bottom)

    # Diagonal segments are discouraged. Use bounding-box approximation.
    min_x, max_x = sorted((x1, x2))
    min_y, max_y = sorted((y1, y2))
    return max(min_x, left) < min(max_x, right) and max(min_y, top) < min(max_y, bottom)


def expanded_rect(box: Box, margin: float = 0) -> Tuple[float, float, float, float]:
    return (box.left - margin, box.top - margin, box.right + margin, box.bottom + margin)


def point_on_rect_edge(point: Point, box: Box, eps: float = 0.5) -> bool:
    x, y = point
    on_left_or_right = (abs(x - box.left) <= eps or abs(x - box.right) <= eps) and box.top - eps <= y <= box.bottom + eps
    on_top_or_bottom = (abs(y - box.top) <= eps or abs(y - box.bottom) <= eps) and box.left - eps <= x <= box.right + eps
    return on_left_or_right or on_top_or_bottom


def segment_length(a: Point, b: Point) -> float:
    return hypot(b[0] - a[0], b[1] - a[1])


def polyline_length(points: Sequence[Point]) -> float:
    return sum(segment_length(a, b) for a, b in zip(points, points[1:]))


def is_orthogonal_segment(a: Point, b: Point) -> bool:
    return a[0] == b[0] or a[1] == b[1]


def save_svg(path: str | Path, svg_text: str) -> Path:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(svg_text, encoding="utf-8")
    return output


def try_export_png(svg_path: str | Path, png_path: str | Path) -> bool:
    try:
        import cairosvg  # type: ignore
    except Exception as exc:
        print(f"PNG export skipped: cairosvg is unavailable ({exc}).")
        return False

    try:
        cairosvg.svg2png(url=str(svg_path), write_to=str(png_path))
    except Exception as exc:
        print(f"PNG export failed: {exc}.")
        return False

    print(f"PNG exported: {png_path}")
    return True


if __name__ == "__main__":
    print("figure_style.py provides reusable SVG helpers and does not generate concrete figures.")
