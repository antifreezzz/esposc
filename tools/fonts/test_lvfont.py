import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import lvfont  # noqa: E402

FONT = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "design", "fonts", "BarlowCondensed-SemiBold.ttf")


class Pack(unittest.TestCase):
    def test_nibbles_high_first_and_continuous(self):
        # 4 бита на пиксель, без выравнивания строк: так читает lv_font_fmt_txt
        self.assertEqual(lvfont.pack4([15, 0, 1]), bytes([0xF0, 0x10]))
        self.assertEqual(lvfont.pack4([1, 2, 3, 4]), bytes([0x12, 0x34]))

    def test_level_from_8bit(self):
        self.assertEqual(lvfont.level4(0), 0)
        self.assertEqual(lvfont.level4(255), 15)
        self.assertEqual(lvfont.level4(128), 8)


class Ranges(unittest.TestCase):
    def test_ranges_and_chars(self):
        self.assertEqual(lvfont.codepoints("0x20-0x22,0x2039", "×"), [0x20, 0x21, 0x22, 0xD7, 0x2039])

    def test_duplicates_dropped(self):
        self.assertEqual(lvfont.codepoints("0x41-0x42", "A"), [0x41, 0x42])


class Glyphs(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.f = lvfont.convert(FONT, 32, lvfont.codepoints("0x20-0x5F", "‹›"))

    def g(self, ch):
        return self.f.glyphs[self.f.cps.index(ord(ch))]

    def test_space_is_empty_with_advance(self):
        g = self.g(" ")
        self.assertEqual((g.w, g.h), (0, 0))
        self.assertGreater(g.adv16, 0)

    def test_cap_sits_on_baseline(self):
        g = self.g("H")
        self.assertEqual(g.oy, 0)
        self.assertTrue(20 <= g.h <= 24, g.h)  # заглавные Barlow ~0.7 em

    def test_descender_below_baseline(self):
        g = self.g("Q")
        self.assertLess(g.oy, 0)

    def test_bitmap_size_matches_box(self):
        g = self.g("H")
        self.assertEqual(len(g.px), g.w * g.h)
        self.assertEqual(max(g.px), 15)

    def test_line_metrics_hold_all_glyphs(self):
        top = max(g.oy + g.h for g in self.f.glyphs)
        bottom = min(g.oy for g in self.f.glyphs if g.h)
        self.assertGreaterEqual(self.f.line_height, top - bottom)
        self.assertGreaterEqual(self.f.base_line, -bottom)

    def test_kerning_pair_av(self):
        a, v = self.f.cps.index(ord("A")), self.f.cps.index(ord("V"))
        self.assertLess(self.f.kern.get((a, v), 0), 0)


class Emit(unittest.TestCase):
    def test_c_source(self):
        f = lvfont.convert(FONT, 17, lvfont.codepoints("0x20-0x7E", "‹›×±"))
        src = lvfont.emit_c(f, "bc_17", "opts")
        self.assertIn("const lv_font_t bc_17 = {", src)
        self.assertIn(".bpp = 4", src)
        # индексы картинок идут подряд по упакованным байтам
        idx = [int(x) for x in re.findall(r"\.bitmap_index = (\d+)", src)]
        self.assertEqual(idx[0], 0)
        self.assertEqual(idx, sorted(idx))
        nbytes = len(re.findall(r"0x[0-9a-f]{2}", src.split("glyph_bitmap[] = {")[1].split("};")[0]))
        self.assertEqual(nbytes, len(lvfont.pack_all(f)[0]))
        # ASCII одним диапазоном, остальное списком
        self.assertIn(".range_start = 32, .range_length = 95", src)
        self.assertIn("LV_FONT_FMT_TXT_CMAP_SPARSE_TINY", src)
        self.assertIn("kern_pairs", src)


if __name__ == "__main__":
    unittest.main()
