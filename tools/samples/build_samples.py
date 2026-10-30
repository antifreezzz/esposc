#!/usr/bin/env python3
"""Сборка образа набора сэмплов (тарелки) для прошивки, формат - components/acid/include/acid_bank.h.

  build_samples.py sfz <config.json> <library_dir> <out.bin> [--preview DIR] [--budget BYTES]
  build_samples.py manifest <manifest.json> <out.bin> [--preview DIR] [--budget BYTES]
  build_samples.py vector <out.h>

Манифест - для своих записей: {"notes": [{"note": 49, "layers": [{"vel_hi": 40, "files": ["a.wav"]}]}]},
пути файлов от папки манифеста. Конфиг SFZ: {"notes": {"49": {"sfz": "x.sfz", "key": 49}}, "mics": {"_OH": 1.0}} -
какие ноты библиотеки брать и с каким весом сводить микрофоны (подстрока пути сэмпла -> вес; без "mics" - все с весом 1).
Аудио читается через ffmpeg (FLAC, WAV, любая частота), пересчёт в 48 кГц стерео.
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys
import wave
import zlib

import numpy as np

SR = 48000
MAGIC = 0x4B4E4241
VERSION = 1
BLOCK = 256
BLOCK_HDR = 8
HDR = 28
DEFAULT_BUDGET = 0x5F0000

LEAD_DB = -50.0  # атака: первый отсчёт выше пика файла на столько
LEAD_KEEP = int(0.001 * SR)  # до атаки оставляем 1 мс
TAIL_DB = -66.0  # хвост: ниже пика инструмента на столько - срез
FADE = int(0.05 * SR)  # уход в конце
PEAK_DB = -1.0  # пик самого громкого слоя инструмента

STEPS = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767]
INDEX_ADJ = [-1, -1, -1, -1, 2, 4, 6, 8]


# --- IMA ADPCM, один канал ---

def _step(pred, index, nib):
    step = STEPS[index]
    diff = step >> 3
    if nib & 4:
        diff += step
    if nib & 2:
        diff += step >> 1
    if nib & 1:
        diff += step >> 2
    pred = pred - diff if nib & 8 else pred + diff
    pred = 32767 if pred > 32767 else -32768 if pred < -32768 else pred
    index += INDEX_ADJ[nib & 7]
    index = 0 if index < 0 else 88 if index > 88 else index
    return pred, index


def adpcm_encode(x, state):
    """x - int16 массив, state - (pred, index). Возврат: (полубайты uint8, конечное состояние)."""
    pred, index = state
    out = np.empty(len(x), dtype=np.uint8)
    for i, v in enumerate(x.tolist()):
        step = STEPS[index]
        diff = v - pred
        nib = 0
        if diff < 0:
            nib = 8
            diff = -diff
        if diff >= step:
            nib |= 4
            diff -= step
        if diff >= step >> 1:
            nib |= 2
            diff -= step >> 1
        if diff >= step >> 2:
            nib |= 1
        pred, index = _step(pred, index, nib)
        out[i] = nib
    return out, (pred, index)


def adpcm_decode(nibs, state):
    pred, index = state
    out = np.empty(len(nibs), dtype=np.int16)
    for i, nib in enumerate(nibs.tolist()):
        pred, index = _step(pred, index, nib)
        out[i] = pred
    return out


def rr_bytes(frames):
    return (frames + BLOCK - 1) // BLOCK * BLOCK_HDR + frames


def encode_rr(x):
    """x - float (frames, 2) в -1..1. Блоки: состояние до блока L и R, байт на кадр (L младший полубайт)."""
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype(np.int16)
    st = [(0, 0), (0, 0)]
    out = bytearray()
    for b in range(0, len(pcm), BLOCK):
        for c in range(2):
            out += struct.pack('<hBB', st[c][0], st[c][1], 0)
        lo, st[0] = adpcm_encode(pcm[b:b + BLOCK, 0], st[0])
        hi, st[1] = adpcm_encode(pcm[b:b + BLOCK, 1], st[1])
        out += (lo | (hi << 4)).astype(np.uint8).tobytes()
    return bytes(out)


def decode_rr(img, offset, frames):
    """Декод повтора из образа в float (frames, 2)."""
    out = np.empty((frames, 2))
    pos = offset
    for b in range(0, frames, BLOCK):
        n = min(BLOCK, frames - b)
        st = [struct.unpack_from('<hB', img, pos + 4 * c) for c in range(2)]
        pos += BLOCK_HDR
        data = np.frombuffer(img, dtype=np.uint8, count=n, offset=pos)
        pos += n
        out[b:b + n, 0] = adpcm_decode(data & 15, st[0]) / 32767.0
        out[b:b + n, 1] = adpcm_decode(data >> 4, st[1]) / 32767.0
    return out


# --- аудио ---

def read_audio(path):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'f32le', '-ac', '2', '-ar', str(SR), '-'],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype='<f4').reshape(-1, 2).astype(np.float64)


def write_wav(path, x):
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def mix(parts):
    """Сумма (вес, сигнал) разной длины."""
    n = max(len(x) for _, x in parts)
    out = np.zeros((n, 2))
    for w, x in parts:
        out[:len(x)] += w * x
    return out


def fade_end(x):
    x = x.copy()
    n = min(FADE, len(x))
    x[len(x) - n:] *= np.linspace(1.0, 0.0, n)[:, None]
    return x


def trim(x, tail_thresh):
    """Срез тишины до атаки (оставляя 1 мс) и хвоста ниже порога, уход в конце."""
    a = np.max(np.abs(x), axis=1)
    peak = np.max(a)
    if peak <= 0:
        return x[:1]
    start = max(0, int(np.argmax(a > peak * 10 ** (LEAD_DB / 20))) - LEAD_KEEP)
    above = np.nonzero(a > tail_thresh)[0]
    end = int(above[-1]) + 1 if len(above) else start + 1
    return fade_end(x[start:max(end, start + 1)])


def process(notes):
    """notes: {note: [(vel_hi, [сигнал, ...]), ...]}. Срезы и нормализация на инструмент целиком."""
    out = {}
    for note, layers in notes.items():
        peak = max(np.max(np.abs(x)) for _, rrs in layers for x in rrs)
        thresh = peak * 10 ** (TAIL_DB / 20)
        gain = 10 ** (PEAK_DB / 20) / peak if peak > 0 else 1.0
        out[note] = [(v, [gain * trim(x, thresh) for x in rrs]) for v, rrs in layers]
    return out


# --- бюджет и образ ---

def image_size(notes):
    nl = sum(len(ls) for ls in notes.values())
    nr = sum(len(r) for ls in notes.values() for _, r in ls)
    size = (HDR + 4 * len(notes) + 4 * nl + 8 * nr + 3) & ~3
    for ls in notes.values():
        for _, rrs in ls:
            for x in rrs:
                size += (rr_bytes(len(x)) + 3) & ~3
    return size


def fit_budget(notes, budget):
    if image_size(notes) <= budget:
        return notes
    dropped = 0
    while image_size(notes) > budget:
        layer = max((rrs for ls in notes.values() for _, rrs in ls), key=len)
        if len(layer) <= 1:
            break
        layer.pop()
        dropped += 1
    if dropped:
        print(f'budget: round robin dropped {dropped}, size {image_size(notes)}')
    scale = 1.0
    orig = {n: [(v, [x for x in rrs]) for v, rrs in ls] for n, ls in notes.items()}
    while image_size(notes) > budget:
        scale -= 0.1
        if scale <= 0.05:
            sys.exit(f'budget {budget} too small')
        notes = {n: [(v, [fade_end(x[:max(1, int(len(x) * scale))]) for x in rrs]) for v, rrs in ls]
                 for n, ls in orig.items()}
    if scale < 1.0:
        print(f'budget: tails shortened to {scale * 100:.0f}%, size {image_size(notes)}')
    return notes


def build_image(notes):
    order = sorted(notes)
    layers, rrs, data = [], [], bytearray()
    tables = HDR + 4 * len(order) + 4 * sum(len(notes[n]) for n in order) + \
        8 * sum(len(r) for n in order for _, r in notes[n])
    base = (tables + 3) & ~3
    note_tab = []
    for n in order:
        note_tab.append(struct.pack('<BBH', n, len(notes[n]), len(layers)))
        for v, xs in sorted(notes[n], key=lambda l: l[0]):
            layers.append(struct.pack('<BBH', v, len(xs), len(rrs)))
            for x in xs:
                enc = encode_rr(x)
                rrs.append(struct.pack('<II', base + len(data), len(x)))
                data += enc + b'\0' * (-len(enc) % 4)
    body = b''.join(note_tab) + b''.join(layers) + b''.join(rrs)
    body += b'\0' * (base - HDR - len(body)) + bytes(data)
    size = HDR + len(body)
    hdr = struct.pack('<5I4H', MAGIC, VERSION, size, zlib.crc32(body), SR, len(order), len(layers), len(rrs), 0)
    return hdr + body


# --- входы ---

def load_manifest(path):
    root = os.path.dirname(os.path.abspath(path))
    with open(path) as f:
        m = json.load(f)
    notes = {}
    for n in m['notes']:
        notes[int(n['note'])] = [(int(l['vel_hi']), [read_audio(os.path.join(root, p)) for p in l['files']])
                                 for l in n['layers']]
    return notes


NOTE_NAMES = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}


def parse_key(v):
    v = v.strip().lower()
    if re.fullmatch(r'-?\d+', v):
        return int(v)
    m = re.fullmatch(r'([a-g])(#|b)?(-?\d+)', v)
    if not m:
        raise ValueError(f'key {v}')
    k = NOTE_NAMES[m.group(1)] + (1 if m.group(2) == '#' else -1 if m.group(2) == 'b' else 0)
    return k + 12 * (int(m.group(3)) + 1)


def sfz_text(path, defines):
    """Текст SFZ с раскрытыми #include и #define, без комментариев."""
    root = os.path.dirname(path)
    out = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.split('//', 1)[0]
            m = re.match(r'\s*#define\s+(\$\w+)\s+(\S+)', line)
            if m:
                defines[m.group(1)] = m.group(2)
                continue
            m = re.match(r'\s*#include\s+"([^"]+)"', line)
            if m:
                out.append(sfz_text(os.path.join(root, m.group(1)), defines))
                continue
            for k in sorted(defines, key=len, reverse=True):
                line = line.replace(k, defines[k])
            out.append(line)
    return '\n'.join(out)


def sfz_regions(path):
    """Регионы SFZ с унаследованными опкодами control/global/master/group."""
    text = sfz_text(path, {})
    tokens = list(re.finditer(r'<(\w+)>|(\w+)=', text))
    scopes = {'control': {}, 'global': {}, 'master': {}, 'group': {}}
    cur, regions = None, []
    reset = {'control': ['global', 'master', 'group'], 'global': ['master', 'group'], 'master': ['group'],
             'group': []}
    for i, t in enumerate(tokens):
        if t.group(1):
            name = t.group(1)
            if name == 'region':
                cur = {}
                regions.append((cur, {k: dict(v) for k, v in scopes.items()}))
            elif name in scopes:
                for r in reset[name]:
                    scopes[r] = {}
                scopes[name] = {}
                cur = scopes[name]
            else:
                cur = None
            continue
        end = tokens[i + 1].start() if i + 1 < len(tokens) else len(text)
        if cur is not None:
            cur[t.group(2)] = text[t.end():end].strip()
    out = []
    for reg, sc in regions:
        op = {}
        for k in ('global', 'master', 'group'):
            op.update(sc[k])
        op.update(reg)
        op['default_path'] = sc['control'].get('default_path', '')
        out.append(op)
    return out


def load_sfz(cfg_path, lib):
    with open(cfg_path) as f:
        cfg = json.load(f)
    mics = cfg.get('mics')
    notes = {}
    for note, spec in cfg['notes'].items():
        key = int(spec['key'])
        groups = {}
        for op in sfz_regions(os.path.join(lib, spec['sfz'])):
            lo = parse_key(op.get('lokey', op.get('key', '-1')))
            hi = parse_key(op.get('hikey', op.get('key', '-1')))
            if not lo <= key <= hi or 'sample' not in op:
                continue
            sample = (op['default_path'] + op['sample']).replace('\\', '/')
            w = 1.0
            if mics:
                hit = [v for k, v in mics.items() if k in sample]
                if not hit:
                    continue
                w = hit[0]
            if w == 0:
                continue
            w *= 10 ** (float(op.get('volume', 0)) / 20) * float(op.get('amplitude', 100)) / 100
            rr = (op.get('seq_position', '1'), op.get('lorand', '0'))
            vel = (int(op.get('lovel', 1)), int(op.get('hivel', 127)))
            groups.setdefault(vel, {}).setdefault(rr, []).append((w, os.path.join(os.path.dirname(
                os.path.join(lib, spec['sfz'])), sample)))
        if not groups:
            sys.exit(f'note {note}: no regions for key {key}')
        layers = []
        for (lo, hi), rrs in sorted(groups.items(), key=lambda g: g[0][1]):
            layers.append((hi, [mix([(w, read_audio(p)) for w, p in parts]) for _, parts in sorted(rrs.items())]))
        notes[int(note)] = layers
    return notes


def preview(notes, img, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    bank = {}
    magic, version, size, crc, rate, nnotes, nlayers, nrr, _ = struct.unpack_from('<5I4H', img, 0)
    pos = HDR
    note_tab = [struct.unpack_from('<BBH', img, pos + 4 * i) for i in range(nnotes)]
    pos += 4 * nnotes
    layer_tab = [struct.unpack_from('<BBH', img, pos + 4 * i) for i in range(nlayers)]
    pos += 4 * nlayers
    rr_tab = [struct.unpack_from('<II', img, pos + 8 * i) for i in range(nrr)]
    for note, nl, fl in note_tab:
        firsts = []
        for li in range(nl):
            vel_hi, nr, fr = layer_tab[fl + li]
            for r in range(nr):
                x = decode_rr(img, *rr_tab[fr + r])
                write_wav(os.path.join(out_dir, f'{note}_{li}_{r}.wav'), x)
                if r == 0:
                    firsts.append(x)
        gap = np.zeros((SR // 2, 2))
        write_wav(os.path.join(out_dir, f'{note}_all.wav'), np.concatenate([p for x in firsts for p in (x, gap)]))
        bank[note] = len(firsts)
    return bank


def write_vector(path):
    """Тестовый вектор для C: полубайты и ожидаемый декод (host/test_bank.c)."""
    n = 300
    t = np.arange(n)
    rng = np.random.default_rng(7)
    x = (20000 * np.sin(2 * np.pi * (200 + 30 * t) * t / SR) + rng.uniform(-3000, 3000, n))
    x = np.clip(np.round(x), -32768, 32767).astype(np.int16)
    x[150:160] = 32767  # упор вверх
    x[160:170] = -32768  # и вниз
    nibs, _ = adpcm_encode(x, (0, 0))
    y = adpcm_decode(nibs, (0, 0))
    with open(path, 'w') as f:
        f.write('// Сгенерировано tools/samples/build_samples.py vector, не править руками.\n#pragma once\n#include <stdint.h>\n\n')
        f.write(f'#define ADPCM_VECTOR_N {n}\n')
        f.write('static const uint8_t ADPCM_VECTOR_NIB[] = {' + ', '.join(map(str, nibs.tolist())) + '};\n')
        f.write('static const int16_t ADPCM_VECTOR_OUT[] = {' + ', '.join(map(str, y.tolist())) + '};\n')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    for name in ('sfz', 'manifest'):
        p = sub.add_parser(name)
        if name == 'sfz':
            p.add_argument('config')
            p.add_argument('library')
        else:
            p.add_argument('manifest')
        p.add_argument('out')
        p.add_argument('--preview')
        p.add_argument('--budget', type=lambda s: int(s, 0), default=DEFAULT_BUDGET)
    p = sub.add_parser('vector')
    p.add_argument('out')
    a = ap.parse_args(argv)
    if a.cmd == 'vector':
        write_vector(a.out)
        return
    notes = load_sfz(a.config, a.library) if a.cmd == 'sfz' else load_manifest(a.manifest)
    notes = fit_budget(process(notes), a.budget)
    img = build_image(notes)
    with open(a.out, 'wb') as f:
        f.write(img)
    for n in sorted(notes):
        print(f'note {n}: ' + ', '.join(f'vel<={v} x{len(r)} {max(len(x) for x in r) / SR:.2f}s' for v, r in notes[n]))
    print(f'image {len(img)} bytes ({len(img) / 1048576:.2f} MB) of budget {a.budget}')
    if a.preview:
        preview(notes, img, a.preview)


if __name__ == '__main__':
    main()
