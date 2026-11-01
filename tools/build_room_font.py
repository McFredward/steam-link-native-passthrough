#!/usr/bin/env python3
"""Build a Latin/German font atlas for the owned software overlay renderer."""
import argparse
import hashlib
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


def build(font_path, output):
    font = ImageFont.truetype(str(font_path), 48)
    codes = [*range(32, 127), 196, 214, 220, 223, 228, 246, 252]
    atlas = Image.new("L", (1024, 448))
    draw = ImageDraw.Draw(atlas)
    glyphs = []
    for i, code in enumerate(codes):
        x, y = (i % 16)*64, (i//16)*64
        draw.text((x, y), chr(code), font=font, fill=255)
        glyphs.append((code, x, y, round(font.getlength(chr(code)))))
    raw = atlas.tobytes()
    runs = []
    for value in raw:
        if runs and runs[-1][0] == value and runs[-1][1] < 65535:
            runs[-1][1] += 1
        else:
            runs.append([value, 1])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("// Generated build input; DejaVu font attribution accompanies the package.\n"
        "#pragma once\n#include <cstdint>\nnamespace room_font {\n"
        "struct Glyph {unsigned code,x,y,advance;};\ninline constexpr Glyph glyphs[]={"
        +",".join("{%d,%d,%d,%d}" % g for g in glyphs)+"};\n"
        "struct Run {std::uint8_t value;std::uint16_t count;};\ninline constexpr Run runs[]={"
        +",".join("{%d,%d}" % (v, n) for v, n in runs)+"};\n}\n")
    print("Font source SHA256 "+hashlib.sha256(font_path.read_bytes()).hexdigest())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build(args.font, args.output)
