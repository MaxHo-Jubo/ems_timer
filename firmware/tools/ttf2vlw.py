#!/usr/bin/env python3
"""
TTF → VLW（TFT_eSPI / LovyanGFX 共用格式）轉檔工具

VLW header（24 bytes，big-endian，對齊 LovyanGFX VLWfont::loadFont）：
  - gCount (4)               ← glyph count
  - version (4)              ← vlw encoder version（discard）
  - yAdvance (4)             ← LGFX 命名為 yAdvance（行高 px）；TFT_eSPI 註解寫
                                "Font size in points"，但 LGFX 實作對 vlw 視為
                                px 行高，且回退邏輯：max(yAdvance, ascent+descent)
                                所以這個 slot 直接給 px 字高即可
  - reserved (4)             ← 0
  - ascent (4)               ← top of "d"
  - descent (4)              ← bottom of "p"

Glyph metadata（28 bytes/glyph，big-endian），按 unicode 升序：
  - unicode (4)
  - height (4)
  - width (4)
  - x_advance (4)
  - dY (4)                   ← top 距離 baseline（向上為正）
  - dX (4)                   ← left bearing
  - reserved (4)             ← 0

Glyph bitmap：8-bit grayscale alpha（0-255），width × height bytes/glyph

用法：
  python3 ttf2vlw.py <ttf_path> <out.vlw> --size 24 --chars-from <source_file>
"""
from __future__ import annotations
import argparse
import struct
import sys
import re
from pathlib import Path
from typing import Optional
import freetype


def collect_chars_from_source(src_path: Path):
    """掃描 main.cpp 字串字面值（剝註解後）抽出非 ASCII 字符。

    涵蓋區塊：
      U+00A0–00FF Latin-1 Supplement（× 等符號）
      U+2000–206F General Punctuation（— em dash 等）
      U+3000–303F CJK Symbols and Punctuation（全形空白、引號）
      U+4E00–9FFF CJK Unified Ideographs（漢字主集）
      U+FF00–FFEF Halfwidth and Fullwidth Forms（全形括號/問號/斜線/直線）
    """
    text = src_path.read_text(encoding="utf-8")
    text = re.sub(r"//.*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    chars = set()
    ranges = [
        (0x00A0, 0x00FF),
        (0x2000, 0x206F),
        (0x3000, 0x303F),
        (0x4E00, 0x9FFF),
        (0xFF00, 0xFFEF),
    ]
    for s in re.findall(r'"([^"]*)"', text):
        for c in s:
            cp = ord(c)
            if any(lo <= cp <= hi for lo, hi in ranges):
                chars.add(c)
    return chars


def build_charset(chars, include_ascii: bool = True):
    """合併 ASCII 列印區（32-126）+ 中文字集，排序去重回 codepoint list"""
    cps = set()
    if include_ascii:
        cps.update(range(0x20, 0x7F))  # ASCII printable
    cps.update(ord(c) for c in chars)
    return sorted(cps)


def render_glyph(face, codepoint: int):
    """渲染單一字符，回傳 metadata + bitmap bytes；找不到字符回 None"""
    glyph_idx = face.get_char_index(codepoint)
    if glyph_idx == 0:
        return None
    face.load_glyph(glyph_idx, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
    g = face.glyph
    bm = g.bitmap
    width = bm.width
    height = bm.rows
    pitch = bm.pitch
    # 確保 bitmap 是 8-bit grayscale
    if bm.pixel_mode != freetype.FT_PIXEL_MODE_GRAY:
        raise RuntimeError(f"unexpected pixel_mode {bm.pixel_mode} for U+{codepoint:04X}")
    # buffer 是 list[int]，每 row pitch bytes，需取 width
    raw = bytes(bm.buffer)
    rows = []
    for y in range(height):
        row = raw[y * pitch : y * pitch + width]
        rows.append(row)
    pixel_data = b"".join(rows)
    return {
        "unicode": codepoint,
        "width": width,
        "height": height,
        "x_advance": g.advance.x // 64,  # 26.6 fixed-point → pixel
        "dY": g.bitmap_top,
        "dX": g.bitmap_left,
        "bitmap": pixel_data,
    }


def write_vlw(glyphs, out_path: Path, font_size: int, ascent: int, descent: int):
    """寫出 vlw 二進位（對齊 LovyanGFX VLWfont::loadFont 的 24-byte header）

    使用 .tmp 中介檔 + os.replace 原子置換，避免寫入中途崩潰留下不完整 .vlw 被韌體載入。
    """
    import os
    glyphs_sorted = sorted(glyphs, key=lambda g: g["unicode"])
    tmp_path = out_path.with_suffix(out_path.suffix + ".tmp")
    with tmp_path.open("wb") as f:
        # Header（24 bytes / 6 個 uint32 BE）
        f.write(struct.pack(">IIIIII",
                            len(glyphs_sorted),  # gCount
                            0,                    # vlw encoder version (discard)
                            font_size,            # yAdvance（行高 px；LGFX max(yAdvance, ascent+descent) 回退）
                            0,                    # reserved
                            ascent,               # top of "d"
                            descent))             # bottom of "p"
        # Metadata block（28 bytes/glyph）
        # unicode/height/width/x_advance 為 unsigned；dY/dX 為 signed bearing
        for g in glyphs_sorted:
            f.write(struct.pack(">IIIIiiI",
                                g["unicode"],
                                g["height"],
                                g["width"],
                                g["x_advance"],
                                g["dY"],
                                g["dX"],
                                0))
        # Bitmap block
        for g in glyphs_sorted:
            f.write(g["bitmap"])
    os.replace(tmp_path, out_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ttf", type=Path, help="來源 TTF/TTC")
    ap.add_argument("out", type=Path, help="輸出 .vlw")
    ap.add_argument("--size", type=int, default=24, help="字符像素高（預設 24）")
    ap.add_argument("--chars-from", type=Path, required=True,
                    help="從此原始檔抽取中文字集（main.cpp）")
    ap.add_argument("--ttc-index", type=int, default=0,
                    help="TTC 多字型索引（預設 0）")
    args = ap.parse_args()

    chars = collect_chars_from_source(args.chars_from)
    cps = build_charset(chars, include_ascii=True)
    print(f"[info] charset: {len(cps)} codepoints (ASCII 95 + CJK {len(chars)})")

    face = freetype.Face(str(args.ttf), index=args.ttc_index)
    face.set_pixel_sizes(0, args.size)

    glyphs = []
    missing = []
    for cp in cps:
        g = render_glyph(face, cp)
        if g is None:
            missing.append(cp)
        else:
            glyphs.append(g)
    if missing:
        print(f"[warn] {len(missing)} codepoints 在字型中找不到: "
              f"{[hex(c) for c in missing[:10]]}{'...' if len(missing)>10 else ''}",
              file=sys.stderr)

    # ascent / descent 取 face 的設計值（pixel-aligned）
    ascent = face.size.ascender // 64
    descent = abs(face.size.descender // 64)
    print(f"[info] font_size={args.size} ascent={ascent} descent={descent} glyphs={len(glyphs)}")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_vlw(glyphs, args.out, args.size, ascent, descent)
    sz_kb = args.out.stat().st_size / 1024
    print(f"[done] wrote {args.out} ({sz_kb:.1f} KB)")


if __name__ == "__main__":
    main()
