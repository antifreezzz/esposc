#!/usr/bin/python3
"""TTF -> C-файл шрифта LVGL (lv_font_fmt_txt, 4 bpp, без сжатия, кернинг парами).

Замена lv_font_conv (npm). Глифы рисует FreeType через PIL, кернинг - разница ширины пары и
одиночных глифов при раскладке raqm (GPOS kern). Запуск системным /usr/bin/python3:
    lvfont.py --font X.ttf --size 17 --range 0x20-0x7E --chars '‹›' --name bc_17 -o bc_17.c
"""
import argparse
from dataclasses import dataclass, field

from PIL import Image, ImageDraw, ImageFont

ASCII_LO, ASCII_HI = 0x20, 0x7E


@dataclass
class Glyph:
    w: int
    h: int
    ox: int
    oy: int  # низ картинки над базовой линией, вверх положительно
    adv16: int  # ширина шага в 1/16 px
    px: list  # уровни 0..15 построчно


@dataclass
class Font:
    size: int
    cps: list
    glyphs: list
    line_height: int
    base_line: int
    kern: dict = field(default_factory=dict)  # (индекс левого, индекс правого) -> 1/16 px


def level4(v):
    return (v * 15 + 127) // 255


def pack4(levels):
    out = bytearray()
    for i in range(0, len(levels), 2):
        hi = levels[i]
        lo = levels[i + 1] if i + 1 < len(levels) else 0
        out.append(hi << 4 | lo)
    return bytes(out)


def codepoints(ranges, chars=""):
    cps = set()
    for part in filter(None, ranges.split(",")):
        a, _, b = part.partition("-")
        lo = int(a, 0)
        cps.update(range(lo, int(b, 0) + 1) if b else [lo])
    cps.update(ord(c) for c in chars)
    return sorted(cps)


def convert(path, size, cps):
    font = ImageFont.truetype(path, size, layout_engine=ImageFont.Layout.RAQM)
    ascent, descent = font.getmetrics()
    pad = size * 2
    glyphs = []
    for cp in cps:
        ch = chr(cp)
        img = Image.new("L", (size * 4, size * 4))
        ImageDraw.Draw(img).text((size, pad), ch, font=font, fill=255, anchor="ls")
        adv16 = round(font.getlength(ch) * 16)
        box = img.getbbox()
        if box is None:
            glyphs.append(Glyph(0, 0, 0, 0, adv16, []))
            continue
        crop = img.crop(box)
        px = [level4(v) for v in crop.tobytes()]
        glyphs.append(Glyph(crop.width, crop.height, box[0] - size, pad - box[3], adv16, px))
    # Высота строки по нарисованным глифам, как у lv_font_conv: запас шрифта под диакритику не нужен.
    top = max([g.oy + g.h for g in glyphs if g.h] or [ascent])
    bottom = min([g.oy for g in glyphs if g.h] + [0])
    f = Font(size, list(cps), glyphs, top - bottom, -bottom)
    f.kern = kerning(font, cps)
    return f


def kerning(font, cps):
    text = [chr(c) for c in cps]
    single = [font.getlength(c) for c in text]
    kern = {}
    for i, a in enumerate(text):
        if a == " ":
            continue
        for j, b in enumerate(text):
            if b == " ":
                continue
            v = round((font.getlength(a + b) - single[i] - single[j]) * 16)
            if v:
                kern[(i, j)] = max(-128, min(127, v))
    return kern


def pack_all(f):
    data, index = bytearray(), []
    for g in f.glyphs:
        index.append(len(data))
        data += pack4(g.px)
    return bytes(data), index


def cmaps(cps):
    """Подряд идущие ASCII - диапазоном, прочее - разреженным списком (id глифов подряд)."""
    out, i = [], 0
    while i < len(cps):
        j = i
        while j + 1 < len(cps) and cps[j + 1] == cps[j] + 1:
            j += 1
        if j - i + 1 >= 4:
            out.append(("range", i, cps[i:j + 1]))
            i = j + 1
            continue
        k = i
        while k < len(cps):  # копим одиночки до следующего длинного ряда
            r = k
            while r + 1 < len(cps) and cps[r + 1] == cps[r] + 1:
                r += 1
            if r - k + 1 >= 4:
                break
            k = r + 1
        out.append(("sparse", i, cps[i:k]))
        i = k
    return out


def hexrows(data, indent="    ", per=16):
    return "\n".join(indent + ", ".join(f"0x{b:02x}" for b in data[i:i + per]) + "," for i in range(0, len(data), per))


def emit_c(f, name, opts):
    data, index = pack_all(f)
    L = [f"/* {name}: {f.size} px, 4 bpp. Сгенерировано tools/fonts/lvfont.py, не править руками.",
         f" * {opts} */", "", '#include "lvgl.h"', "",
         "static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {", hexrows(data), "};", "",
         "static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {",
         "    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,"]
    for cp, g, ix in zip(f.cps, f.glyphs, index):
        L.append(f"    {{.bitmap_index = {ix}, .adv_w = {g.adv16}, .box_w = {g.w}, .box_h = {g.h}, "
                 f".ofs_x = {g.ox}, .ofs_y = {g.oy}}}, /* U+{cp:04X} */")
    L += ["};", ""]
    maps = cmaps(f.cps)
    for n, (kind, first, cps) in enumerate(maps):
        if kind == "sparse":
            L.append(f"static const uint16_t unicode_list_{n}[] = {{{', '.join(str(c - cps[0]) for c in cps)}}};")
    L.append("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    for n, (kind, first, cps) in enumerate(maps):
        rl = cps[-1] - cps[0] + 1
        if kind == "range":
            L.append(f"    {{.range_start = {cps[0]}, .range_length = {rl}, .glyph_id_start = {first + 1}, "
                     ".unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, "
                     ".type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},")
        else:
            L.append(f"    {{.range_start = {cps[0]}, .range_length = {rl}, .glyph_id_start = {first + 1}, "
                     f".unicode_list = unicode_list_{n}, .glyph_id_ofs_list = NULL, .list_length = {len(cps)}, "
                     ".type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY},")
    L += ["};", ""]
    pairs = sorted(f.kern.items())
    wide = len(f.glyphs) + 1 > 255
    if pairs:
        L.append(f"static const {'uint16_t' if wide else 'uint8_t'} kern_pair_glyph_ids[] = {{")
        L.append("    " + ", ".join(str(v) for (a, b), _ in pairs for v in (a + 1, b + 1)) + ",")
        L += ["};", "static const int8_t kern_pair_values[] = {",
              "    " + ", ".join(str(v) for _, v in pairs) + ",", "};",
              "static const lv_font_fmt_txt_kern_pair_t kern_pairs = {",
              "    .glyph_ids = kern_pair_glyph_ids, .values = kern_pair_values,",
              f"    .pair_cnt = {len(pairs)}, .glyph_ids_size = {1 if wide else 0},", "};", ""]
    L += ["static const lv_font_fmt_txt_dsc_t font_dsc = {",
          "    .glyph_bitmap = glyph_bitmap,", "    .glyph_dsc = glyph_dsc,", "    .cmaps = cmaps,",
          f"    .kern_dsc = {'&kern_pairs' if pairs else 'NULL'},", "    .kern_scale = 16,",
          f"    .cmap_num = {len(maps)},", "    .bpp = 4,", "    .kern_classes = 0,", "    .bitmap_format = 0,", "};", "",
          f"const lv_font_t {name} = {{",
          "    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,",
          "    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,",
          f"    .line_height = {f.line_height},", f"    .base_line = {f.base_line},",
          f"    .cap_height = {cap(f)},", f"    .x_height = {xh(f)},",
          "    .subpx = LV_FONT_SUBPX_NONE,", "    .underline_position = -1,", "    .underline_thickness = 1,",
          "    .static_bitmap = 1,", "    .dsc = &font_dsc,", "    .fallback = NULL,", "    .user_data = NULL,", "};", ""]
    return "\n".join(L)


def _h(f, ch):
    return f.glyphs[f.cps.index(ord(ch))].h if ord(ch) in f.cps else 0


def cap(f):
    return _h(f, "H")


def xh(f):
    return _h(f, "x")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", required=True)
    ap.add_argument("--size", type=int, required=True)
    ap.add_argument("--range", default="")
    ap.add_argument("--chars", default="")
    ap.add_argument("--name", required=True)
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    f = convert(a.font, a.size, codepoints(a.range, a.chars))
    opts = f"--size {a.size} --range {a.range} --chars '{a.chars}' --font {a.font.split('/')[-1]}"
    with open(a.out, "w") as fp:
        fp.write(emit_c(f, a.name, opts))


if __name__ == "__main__":
    main()
