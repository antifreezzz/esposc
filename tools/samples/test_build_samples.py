"""Тесты сборщика набора сэмплов: python3 -m unittest discover -s tools/samples"""
import io
import json
import os
import struct
import tempfile
import unittest
import wave
import zlib
from contextlib import redirect_stdout

import numpy as np

import build_samples as bs

SR = 48000


def write_wav(path, x, sr=SR):
    """x: (frames,) или (frames, 2) float -1..1."""
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())


def hit(amp, secs=0.5, lead=0.0, hz=3000.0):
    """Удар: тишина lead, потом шумоватый синус с экспоненциальным спадом."""
    n = int(secs * SR)
    t = np.arange(n) / SR
    rng = np.random.default_rng(1)
    x = amp * np.exp(-t / 0.15) * (0.6 * np.sin(2 * np.pi * hz * t) + 0.4 * rng.uniform(-1, 1, n))
    return np.concatenate([np.zeros(int(lead * SR)), x])


def parse_image(img):
    """Разбор образа питоном по формату acid_bank.h: заголовок, таблицы, декод повторов."""
    magic, version, size, crc, rate, nnotes, nlayers, nrr, _ = struct.unpack_from('<5I4H', img, 0)
    assert magic == 0x4B4E4241 and version == 1 and rate == 48000
    assert size == len(img)
    assert zlib.crc32(img[28:size]) == crc
    pos = 28
    notes = [struct.unpack_from('<BBH', img, pos + 4 * i) for i in range(nnotes)]
    pos += 4 * nnotes
    layers = [struct.unpack_from('<BBH', img, pos + 4 * i) for i in range(nlayers)]
    pos += 4 * nlayers
    rrs = [struct.unpack_from('<II', img, pos + 8 * i) for i in range(nrr)]
    out = {}
    for note, nl, fl in notes:
        out[note] = []
        for li in range(fl, fl + nl):
            vel_hi, nr, fr = layers[li]
            out[note].append((vel_hi, [bs.decode_rr(img, *rrs[ri]) for ri in range(fr, fr + nr)]))
    return out


def peak_db(x):
    return 20 * np.log10(np.max(np.abs(x)) + 1e-12)


class BuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def wav(self, name, x, sr=SR):
        p = os.path.join(self.dir, name)
        write_wav(p, x, sr)
        return p

    def manifest(self, notes):
        p = os.path.join(self.dir, 'm.json')
        with open(p, 'w') as f:
            json.dump({'notes': notes}, f)
        return p

    def build(self, mpath, budget=None):
        out = os.path.join(self.dir, 'out.bin')
        args = ['manifest', mpath, out]
        if budget is not None:
            args += ['--budget', str(budget)]
        buf = io.StringIO()
        with redirect_stdout(buf):
            bs.main(args)
        with open(out, 'rb') as f:
            return f.read(), buf.getvalue()

    def test_image_roundtrip(self):
        a = self.wav('a.wav', hit(0.3))
        b = self.wav('b.wav', hit(0.3, hz=2000))
        c = self.wav('c.wav', hit(0.9))
        img, _ = self.build(self.manifest([
            {'note': 49, 'layers': [{'vel_hi': 60, 'files': ['a.wav', 'b.wav']}, {'vel_hi': 127, 'files': ['c.wav']}]},
            {'note': 51, 'layers': [{'vel_hi': 127, 'files': ['a.wav']}]},
        ]))
        bank = parse_image(img)
        self.assertEqual(sorted(bank), [49, 51])
        self.assertEqual([v for v, _ in bank[49]], [60, 127])
        self.assertEqual([len(r) for _, r in bank[49]], [2, 1])
        # Громкий слой нормализован к -1 dBFS, форма сохранена (корреляция с исходником).
        loud = bank[49][1][1][0]
        self.assertAlmostEqual(peak_db(loud), -1.0, delta=0.5)
        src = hit(0.9)[:len(loud)]
        corr = np.corrcoef(loud[:, 0][:len(src)], src[:len(loud)])[0, 1]
        self.assertGreater(corr, 0.98)
        del a, b, c

    def test_layers_keep_relative_level(self):
        self.wav('soft.wav', hit(10 ** (-20 / 20)))
        self.wav('loud.wav', hit(10 ** (-6 / 20)))
        img, _ = self.build(self.manifest([
            {'note': 49, 'layers': [{'vel_hi': 64, 'files': ['soft.wav']}, {'vel_hi': 127, 'files': ['loud.wav']}]},
        ]))
        bank = parse_image(img)
        soft, loud = bank[49][0][1][0], bank[49][1][1][0]
        self.assertAlmostEqual(peak_db(loud) - peak_db(soft), 14.0, delta=0.5)

    def test_trim_leading_silence(self):
        self.wav('late.wav', hit(0.8, lead=0.1))
        img, _ = self.build(self.manifest([{'note': 49, 'layers': [{'vel_hi': 127, 'files': ['late.wav']}]}]))
        x = parse_image(img)[49][0][1][0]
        thresh = np.max(np.abs(x)) * 10 ** (-50 / 20)
        onset = np.argmax(np.abs(x[:, 0]) > thresh)
        self.assertLessEqual(onset, int(0.0015 * SR))

    def test_tail_trimmed_and_faded(self):
        # Длинный хвост под -66 dB срезается, конец плавный.
        self.wav('long.wav', np.concatenate([hit(0.8, secs=0.5), np.full(SR, 1e-5)]))
        img, _ = self.build(self.manifest([{'note': 49, 'layers': [{'vel_hi': 127, 'files': ['long.wav']}]}]))
        x = parse_image(img)[49][0][1][0]
        self.assertLess(len(x), int(0.9 * SR))
        self.assertLess(np.max(np.abs(x[-50:])), 0.01)

    def test_resample_to_48k(self):
        self.wav('k44.wav', hit(0.8, secs=0.5), sr=44100)
        img, _ = self.build(self.manifest([{'note': 49, 'layers': [{'vel_hi': 127, 'files': ['k44.wav']}]}]))
        x = parse_image(img)[49][0][1][0]
        # Спад 0.15 с должен остаться 0.15 с: находим уровень -1/e по огибающей.
        env = np.abs(x[:, 0])
        k = int(0.15 * SR)
        early = np.max(env[:k // 4])
        late = np.max(env[k - 400:k + 400])
        self.assertAlmostEqual(late / early, np.exp(-0.85), delta=0.08)

    def test_budget_drops_round_robin_first(self):
        for i in range(3):
            self.wav(f'r{i}.wav', hit(0.8, secs=1.0, hz=2000 + 500 * i))
        m = self.manifest([{'note': 49, 'layers': [{'vel_hi': 127, 'files': ['r0.wav', 'r1.wav', 'r2.wav']}]}])
        full, _ = self.build(m)
        img, log = self.build(m, budget=len(full) // 2)
        bank = parse_image(img)
        self.assertEqual(len(bank[49][0][1]), 1)
        self.assertLessEqual(len(img), len(full) // 2)
        self.assertIn('round robin', log)
        # Хвост не трогали: длина как в полном образе.
        self.assertEqual(len(bank[49][0][1][0]), len(parse_image(full)[49][0][1][0]))

    def test_budget_shortens_tails_after_round_robin(self):
        self.wav('one.wav', hit(0.8, secs=1.0))
        m = self.manifest([{'note': 49, 'layers': [{'vel_hi': 127, 'files': ['one.wav']}]}])
        full, _ = self.build(m)
        img, log = self.build(m, budget=len(full) * 6 // 10)
        self.assertLessEqual(len(img), len(full) * 6 // 10)
        self.assertIn('tails', log)
        x = parse_image(img)[49][0][1][0]
        self.assertLess(np.max(np.abs(x[-50:])), 0.01)  # укороченный хвост тоже с уходом

    def test_sfz_regions(self):
        os.makedirs(os.path.join(self.dir, 'lib', 'Samples'))
        for name, amp in [('c_v1_rr1', 0.2), ('c_v1_rr2', 0.25), ('c_v2_rr1', 0.8), ('r_v1', 0.5)]:
            for mic in ('OH', 'Room'):
                write_wav(os.path.join(self.dir, 'lib', 'Samples', f'{name}_{mic}.wav'), hit(amp))
        sfz = """// тест
#define $ext wav
<control> default_path=Samples/
<group> key=49 lovel=1 hivel=64 seq_length=2
<region> seq_position=1 sample=c_v1_rr1_OH.$ext
<region> seq_position=1 sample=c_v1_rr1_Room.$ext
<region> seq_position=2 sample=c_v1_rr2_OH.$ext
<region> seq_position=2 sample=c_v1_rr2_Room.$ext
<group> key=49 lovel=65 hivel=127
<region> sample=c_v2_rr1_OH.$ext
<region> sample=c_v2_rr1_Room.$ext
<group> lokey=51 hikey=51
<region> sample=r_v1_OH.$ext
<region> sample=r_v1_Room.$ext
"""
        with open(os.path.join(self.dir, 'lib', 'kit.sfz'), 'w') as f:
            f.write(sfz)
        cfg = os.path.join(self.dir, 'cfg.json')
        with open(cfg, 'w') as f:
            json.dump({'notes': {'49': {'sfz': 'kit.sfz', 'key': 49}, '51': {'sfz': 'kit.sfz', 'key': 51}},
                       'mics': {'_OH': 1.0}}, f)
        out = os.path.join(self.dir, 'out.bin')
        with redirect_stdout(io.StringIO()):
            bs.main(['sfz', cfg, os.path.join(self.dir, 'lib'), out])
        with open(out, 'rb') as f:
            bank = parse_image(f.read())
        self.assertEqual([v for v, _ in bank[49]], [64, 127])
        self.assertEqual([len(r) for _, r in bank[49]], [2, 1])
        self.assertEqual(len(bank[51]), 1)
        # Только OH (комната с весом 0): громкий слой -1 dBFS, тихий на 12 dB ниже (0.2 против 0.8).
        self.assertAlmostEqual(peak_db(bank[49][1][1][0]) - peak_db(bank[49][0][1][0]), 12.0, delta=0.6)

    def test_sfz_skips_choke_and_switch_regions(self):
        # Глушение (trigger=release), переключатели (sw_down) и поздние #define как в Naked Drums.
        os.makedirs(os.path.join(self.dir, 'lib', 'S'))
        for name in ('hit1', 'hit2', 'choke', 'sw'):
            write_wav(os.path.join(self.dir, 'lib', 'S', f'OH_{name}.wav'), hit(0.5))
        sfz = """#define $MIC OH
<control> default_path=S/
<global> seq_length=2
<group> key=55
#define $POS 1
<region> seq_position=$POS sample=$MIC_hit1.wav
#define $POS 2
<region> seq_position=$POS sample=$MIC_hit2.wav
<group> key=55 trigger=release
<region> sample=$MIC_choke.wav
<group> sw_down=61 key=55
<region> sample=$MIC_sw.wav
"""
        with open(os.path.join(self.dir, 'lib', 'kit.sfz'), 'w') as f:
            f.write(sfz)
        regs = [r for r in bs.sfz_regions(os.path.join(self.dir, 'lib', 'kit.sfz')) if bs.sfz_playable(r)]
        self.assertEqual(sorted(r['sample'] for r in regs), ['OH_hit1.wav', 'OH_hit2.wav'])
        self.assertEqual(sorted(r['seq_position'] for r in regs), ['1', '2'])

    def test_sfz_include_relative_to_main_file(self):
        # Как в SFZ: путь #include из вложенного файла считается от главного .sfz.
        os.makedirs(os.path.join(self.dir, 'lib', 'Stereo'))
        os.makedirs(os.path.join(self.dir, 'lib', 'Data', 'group'))
        os.makedirs(os.path.join(self.dir, 'lib', 'Data', 'stereo'))
        with open(os.path.join(self.dir, 'lib', 'Stereo', 'kit.sfz'), 'w') as f:
            f.write('#include "../Data/stereo/a.txt"\n')
        with open(os.path.join(self.dir, 'lib', 'Data', 'stereo', 'a.txt'), 'w') as f:
            f.write('<group> key=55\n#include "../Data/group/b.txt"\n')
        with open(os.path.join(self.dir, 'lib', 'Data', 'group', 'b.txt'), 'w') as f:
            f.write('<region> sample=x.wav\n')
        regs = bs.sfz_regions(os.path.join(self.dir, 'lib', 'Stereo', 'kit.sfz'))
        self.assertEqual([r['sample'] for r in regs], ['x.wav'])

    def test_sfz_defines_on_one_line_and_volumes(self):
        os.makedirs(os.path.join(self.dir, 'lib'))
        for name in ('a', 'b'):
            write_wav(os.path.join(self.dir, 'lib', f'{name}.wav'), hit(0.5))
        sfz = """#define $V1L 1            #define $V1H 60
#define $V2L 61   #define $V2H 127
<master> master_volume=-3
<group> key=55 group_volume=-6
<region> lovel=$V1L hivel=$V1H sample=a.wav
<group> key=55
<region> lovel=$V2L hivel=$V2H sample=b.wav volume=0
"""
        with open(os.path.join(self.dir, 'lib', 'kit.sfz'), 'w') as f:
            f.write(sfz)
        cfg = os.path.join(self.dir, 'cfg.json')
        with open(cfg, 'w') as f:
            json.dump({'notes': {'49': {'sfz': 'kit.sfz', 'key': 55}}}, f)
        out = os.path.join(self.dir, 'out.bin')
        with redirect_stdout(io.StringIO()):
            bs.main(['sfz', cfg, os.path.join(self.dir, 'lib'), out])
        with open(out, 'rb') as f:
            bank = parse_image(f.read())
        self.assertEqual([v for v, _ in bank[49]], [60, 127])
        # group_volume -6 у тихого слоя, master_volume -3 у обоих: разница 6 dB.
        self.assertAlmostEqual(peak_db(bank[49][1][1][0]) - peak_db(bank[49][0][1][0]), 6.0, delta=0.5)

    def test_preview_files(self):
        self.wav('a.wav', hit(0.5))
        m = self.manifest([{'note': 49, 'layers': [{'vel_hi': 127, 'files': ['a.wav']}]}])
        prev = os.path.join(self.dir, 'prev')
        with redirect_stdout(io.StringIO()):
            bs.main(['manifest', m, os.path.join(self.dir, 'o.bin'), '--preview', prev])
        self.assertTrue(os.path.exists(os.path.join(prev, '49_0_0.wav')))
        self.assertTrue(os.path.exists(os.path.join(prev, '49_all.wav')))

    def test_adpcm_matches_reference_decoder(self):
        x = (np.sin(np.arange(1000) * 0.07) * 20000).astype(np.int16)
        nibs = bs.adpcm_encode(x, (0, 0))[0]
        y = bs.adpcm_decode(nibs, (0, 0))
        err = np.sqrt(np.mean((x[100:].astype(float) - y[100:]) ** 2)) / 32768
        self.assertLess(err, 0.01)


if __name__ == '__main__':
    unittest.main()
