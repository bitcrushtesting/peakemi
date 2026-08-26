#!/usr/bin/env python3
"""Generate the PeakEmi application icon.

The icon is drawn rather than stored so it can be re-rendered crisply at any
size: a spectrum with a stepped limit line and one flagged emission over it,
which is what the application is about.

    python3 scripts/make-icon.py

Writes resources/peakemi.png (256, used by the Linux desktop entry) and
resources/peakemi-1024.png (the source for the macOS .icns).
"""

import math
import pathlib
import struct
import zlib

BACKGROUND = (0x0F, 0x17, 0x22)
GRID = (0x1E, 0x2B, 0x3B)
TRACE = (0x4A, 0xA3, 0xFF)
LIMIT = (0xC0, 0x39, 0x2B)
PEAK = (0xF9, 0xAB, 0x00)


def render(size: int) -> bytes:
    """Draw the icon at `size` pixels square and return PNG bytes."""
    unit = size / 256.0
    pixels = [[BACKGROUND for _ in range(size)] for _ in range(size)]

    def scale(value: float) -> int:
        return int(round(value * unit))

    def horizontal(y: float, x0: float, x1: float, colour, thickness: float = 1.0) -> None:
        for t in range(max(1, scale(thickness))):
            row = scale(y) + t
            if 0 <= row < size:
                for x in range(max(0, scale(x0)), min(size, scale(x1))):
                    pixels[row][x] = colour

    def vertical(x: float, y0: float, y1: float, colour, thickness: float = 1.0) -> None:
        for t in range(max(1, scale(thickness))):
            column = scale(x) + t
            if 0 <= column < size:
                for y in range(max(0, scale(y0)), min(size, scale(y1))):
                    pixels[y][column] = colour

    for i in range(1, 6):
        horizontal(256 * i / 6, 24, 240, GRID)
        vertical(24 + (216 * i / 6), 20, 232, GRID)
    horizontal(232, 24, 240, GRID, 2)
    vertical(24, 20, 232, GRID, 2)

    step_x, low_y, high_y = 132.0, 96.0, 72.0
    horizontal(low_y, 24, step_x, LIMIT, 3)
    horizontal(high_y, step_x, 240, LIMIT, 3)
    vertical(step_x, high_y, low_y + 3, LIMIT, 3)

    emitters = [(52.0, 84.0), (96.0, 148.0), (168.0, 30.0), (208.0, 66.0)]
    baseline = 204.0
    for column in range(scale(24), scale(240)):
        x = column / unit
        height = 6 + 4 * abs(math.sin(x * 0.7))
        for centre, amplitude in emitters:
            height = max(height, amplitude * math.exp(-(((x - centre) / 3.4) ** 2)))
        vertical(x, baseline - height, baseline, TRACE)

    for centre, amplitude in emitters:
        top = baseline - amplitude
        if top < (low_y if centre < step_x else high_y):
            for row in range(7):
                horizontal(top - 12 + row, centre - (6 - row), centre + (6 - row) + 1, PEAK)

    raw = b"".join(
        b"\x00" + b"".join(struct.pack("BBB", *pixels[y][x]) for x in range(size))
        for y in range(size)
    )

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def main() -> None:
    resources = pathlib.Path(__file__).resolve().parent.parent / "resources"
    for size, name in ((256, "peakemi.png"), (1024, "peakemi-1024.png")):
        path = resources / name
        path.write_bytes(render(size))
        print(f"wrote {path.relative_to(path.parents[1])} ({size}x{size})")


if __name__ == "__main__":
    main()
