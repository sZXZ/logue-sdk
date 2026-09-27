#!/usr/bin/env python3
"""
Bake a wav file into a single-cycle, mip-mapped int16 wavetable for the
NTS-3 kaoss pad "WT" wavetable oscillator unit.

    wav2table.py INPUT.wav OUTPUT.c [--name STEM] [--base N]
                [--start SEC] [--len SEC] [--quiet]

Writes OUTPUT.c plus the sibling header wt_data.h. Files are only rewritten
when their contents actually change, so a rebuild does not cascade into
recompiles. Pure Python 3 stdlib: no numpy, no ffmpeg/sox, and no dependency
on the `wave` module (which rejects WAVE_FORMAT_IEEE_FLOAT).

Pipeline
    1. parse RIFF by hand (PCM 8/16/24/32, IEEE float 32/64, any channel
       count, any sample rate), down-mix to mono
    2. pick the analysis window: highest RMS --len window from 10% into the
       file, in 0.05 s hops (or use --start/--len explicitly)
    3. estimate f0 by normalized autocorrelation on a box-decimated copy,
       take the first prominent peak, refine at full rate with parabolic
       interpolation
    4. fold the window into exactly one cycle of --base points, averaging over
       every whole period so inharmonic content cancels
    5. remove DC, normalize, quantize to int16
    6. build the mip chain (2:1 box average, RMS matched to the base level)
    7. emit wt_data.c / wt_data.h and a two line report
"""

import argparse
import math
import os
import sys
from array import array

WT_MIN_POINTS = 8  # smallest mip level, in samples per cycle
WT_PEAK = 0.95     # headroom for the interpolator's overshoot
WAVE_FORMATS = {1: "PCM", 3: "IEEE_FLOAT"}


# --------------------------------------------------------------------------- io


def read_wav(path):
    """Return (samples, samplerate); samples is a list of mono floats in [-1, 1]."""
    with open(path, "rb") as f:
        d = f.read()

    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise ValueError("%s: not a RIFF/WAVE file" % path)

    fmt = channels = rate = bits = None
    body = None
    pos = 12
    while pos + 8 <= len(d):
        cid = d[pos : pos + 4]
        size = int.from_bytes(d[pos + 4 : pos + 8], "little")
        start = pos + 8
        if cid == b"fmt " and size >= 16:
            fmt = int.from_bytes(d[start : start + 2], "little")
            channels = int.from_bytes(d[start + 2 : start + 4], "little")
            rate = int.from_bytes(d[start + 4 : start + 8], "little")
            bits = int.from_bytes(d[start + 14 : start + 16], "little")
        elif cid == b"data":
            body = d[start : start + size]
            break
        pos = start + size + (size & 1)

    if fmt is None or body is None:
        raise ValueError("%s: missing fmt or data chunk" % path)
    if fmt not in WAVE_FORMATS:
        raise ValueError("%s: unsupported wave format %d" % path)
    if rate <= 0 or channels <= 0:
        raise ValueError("%s: bad rate / channel count" % path)

    frames = len(body) // (channels * max(1, bits // 8))
    big = sys.byteorder == "big"

    if fmt == 3 and bits == 32:
        flat = array("f")
        flat.frombytes(body[: 4 * frames * channels])
        if big:
            flat.byteswap()
        scale = 1.0
    elif fmt == 3 and bits == 64:
        flat = array("d")
        flat.frombytes(body[: 8 * frames * channels])
        if big:
            flat.byteswap()
        scale = 1.0
    elif fmt == 1 and bits == 16:
        flat = array("h")
        flat.frombytes(body[: 2 * frames * channels])
        if big:
            flat.byteswap()
        scale = 1.0 / 32768.0
    elif fmt == 1 and bits == 32:
        flat = array("i")
        flat.frombytes(body[: 4 * frames * channels])
        if big:
            flat.byteswap()
        scale = 1.0 / 2147483648.0
    elif fmt == 1 and bits == 8:
        # 8 bit wave PCM is unsigned with 128 as zero
        flat = [v - 128 for v in body[: frames * channels]]
        scale = 1.0 / 128.0
    elif fmt == 1 and bits == 24:
        raw = body[: 3 * frames * channels]
        flat = []
        for i in range(0, len(raw), 3):
            v = int.from_bytes(raw[i : i + 3], "little")
            flat.append(v - 0x1000000 if v & 0x800000 else v)
        scale = 1.0 / 8388608.0
    else:
        raise ValueError(
            "%s: unsupported %d-bit %s" % (path, bits, WAVE_FORMATS.get(fmt, fmt))
        )

    if channels == 1:
        return [float(v) * scale for v in flat], rate

    inv = 1.0 / channels
    mono = [0.0] * frames
    for c in range(channels):
        for i in range(frames):
            mono[i] += float(flat[i * channels + c]) * scale
    return [v * inv for v in mono], rate


# --------------------------------------------------------------------------- analysis


def pick_window(x, sr, length, start_min=0.10, hop=0.05):
    """Highest RMS window of `length` samples, at or after 10% into the file."""
    win = int(length * sr)
    n = len(x)
    if n <= win:
        return 0, n

    step = max(1, int(hop * sr))
    lo = int(start_min * n)
    best_start, best = lo, -1.0
    cur = sum(v * v for v in x[lo : lo + win])
    st = lo
    while True:
        if cur > best:
            best, best_start = cur, st
        nxt = st + step
        if nxt + win > n:
            break
        cur += sum(v * v for v in x[nxt : nxt + step])
        cur -= sum(v * v for v in x[st : st + step])
        st = nxt
    return best_start, best_start + win


def normalized_acf(seg, energy, lags):
    """Autocorrelation of `seg` at each lag, normalized to 1.0 at lag 0."""
    n = len(seg)
    out = []
    for lag in lags:
        s = 0.0
        for i in range(n - lag):
            s += seg[i] * seg[i + lag]
        out.append((lag, s / (energy * (n - lag) / n)))
    return out


def parabolic(lag, ym, y0, yp):
    den = ym - 2.0 * y0 + yp
    return lag + 0.5 * (ym - yp) / den if den != 0.0 else float(lag)


def detect_f0(x, sr, start, end, fmin=20.0, fmax=500.0):
    """Fundamental period of x[start:end], in samples. Returns (period, corr)."""
    # Box decimate to ~4 kHz: cheap, and plenty for a 20..500 Hz search.
    d = max(1, int(round(sr / 4000.0)))
    n = (end - start) // d
    if n < 64:
        return None, None
    y = [sum(x[start + i * d : start + i * d + d]) / d for i in range(n)]
    fs = float(sr) / d
    mu = sum(y) / n
    y = [v - mu for v in y]

    win = y[: min(n, 8192)]
    energy = sum(v * v for v in win)
    if energy <= 0.0:
        return None, None

    lmin = max(2, int(fs / fmax))
    lmax = min(len(win) - 2, int(fs / fmin))
    if lmax <= lmin:
        return None, None
    acf = normalized_acf(win, energy, range(lmin, lmax + 1))

    top = max(v for _, v in acf)
    if top < 0.2:  # no periodicity at all
        return None, None

    # From the global maximum walk back to the preceding valley, then forward
    # to the first peak above half of it: that lands on the fundamental
    # instead of one of its multiples.
    i = max(range(len(acf)), key=lambda k: acf[k][1])
    while i > 0 and acf[i - 1][1] < acf[i][1]:
        i -= 1
    while i < len(acf) - 1 and not (
        acf[i][1] >= acf[i + 1][1] and acf[i][1] > 0.5 * top
    ):
        i += 1
    if 0 < i < len(acf) - 1:
        lag = parabolic(acf[i][0], acf[i - 1][1], acf[i][1], acf[i + 1][1])
    else:
        lag = float(acf[i][0])

    # refine at full rate over +-3%
    period = lag * (float(sr) / fs)
    seg = x[start : min(end, start + 4096)]
    n2 = len(seg)
    mu2 = sum(seg) / n2
    seg = [v - mu2 for v in seg]
    energy2 = sum(v * v for v in seg)
    lo = max(2, int(period * 0.97))
    hi = min(n2 - 2, int(period * 1.03))
    if energy2 <= 0.0 or hi <= lo:
        return period, None

    acf2 = normalized_acf(seg, energy2, range(lo, hi + 1))
    j = max(range(len(acf2)), key=lambda k: acf2[k][1])
    if 0 < j < len(acf2) - 1:
        period = parabolic(acf2[j][0], acf2[j - 1][1], acf2[j][1], acf2[j + 1][1])
    return period, acf2[j][1]


def fold_cycle(x, start, length, period, base):
    """Average `length` samples into exactly one cycle of `base` points."""
    cycles = max(1, int(length / period))
    step = period / base
    # Stay inside the analysis window: the linear tap needs sample i + 1.
    last = length - 1
    out = [0.0] * base
    for p in range(base):
        pos = p * step
        acc = 0.0
        for k in range(cycles):
            t = pos + k * period
            i = int(t)
            if i > last:
                i = last
            fr = t - i
            a = x[start + i]
            if fr > 0.0 and i < last:
                acc += a + fr * (x[start + i + 1] - a)
            else:
                acc += a
        out[p] = acc / cycles
    return out, cycles


def rms(v):
    return math.sqrt(sum(a * a for a in v) / len(v))


def lowpass_periodic(src, cutoff, taps=63):
    """Hamming windowed sinc low-pass, `cutoff` in cycles per sample.

    Indices wrap: `src` is exactly one cycle, so the filter sees a periodic
    signal and needs no edge handling.
    """
    n = len(src)
    m = taps | 1
    h = []
    for i in range(m):
        k = i - (m // 2)
        x = 2.0 * cutoff * k
        sinc = 1.0 if k == 0 else math.sin(math.pi * x) / (math.pi * k)
        h.append(sinc * (0.54 - 0.46 * math.cos(2.0 * math.pi * i / (m - 1))))
    norm = sum(h)
    h = [v / norm for v in h]

    out = [0.0] * n
    half = m // 2
    for i in range(n):
        acc = 0.0
        for j in range(m):
            acc += h[j] * src[(i + j - half) % n]
        out[i] = acc
    return out


def build_mips(base_samples, min_points=WT_MIN_POINTS):
    """2:1 pyramid, every level low-passed to half of its new rate.

    Filtering before each decimation is what makes the chain a spectral tilt:
    level L keeps the harmonics up to npts(L) / 2 and nothing above, so each
    level is an octave darker than the one before and is band-limited at its
    own Nyquist.  A plain box average would only null partials near npts / 2,
    which leaves the levels spectrally identical.
    """
    target = rms(base_samples)
    levels = [base_samples]
    cur = base_samples
    while len(cur) > min_points:
        # new rate is half of the old one, so the new Nyquist sits at fs / 4
        filtered = lowpass_periodic(cur, 0.25)
        nxt = [
            (filtered[2 * i] + filtered[2 * i + 1]) * 0.5
            for i in range(len(filtered) // 2)
        ]
        if len(filtered) & 1:
            nxt.append(filtered[-1])
        level_rms = rms(nxt)
        if level_rms > 0.0:
            g = target / level_rms
            peak = max(abs(v) for v in nxt) * g
            if peak > WT_PEAK:  # headroom for the interpolator's overshoot
                g *= WT_PEAK / peak
            nxt = [v * g for v in nxt]
        levels.append(nxt)
        cur = nxt
    return levels


# --------------------------------------------------------------------------- emit


def fnv1a(text):
    h = 0x811C9DC5
    for ch in text.encode("ascii", "replace"):
        h = ((h ^ ch) * 0x01000193) & 0xFFFFFFFF
    return h


def unit_id(stem):
    """9 bit, never zero: unique per wav name within a dev_id namespace."""
    return (fnv1a(stem) & 0x1FF) | 0x100


def sanitize(text, limit):
    keep = [
        c
        for c in text.upper()
        if c.isascii() and (c.isalnum() or c in "-_ ")
    ]
    return "".join(keep).strip()[:limit]


def quantize(samples):
    out = []
    for v in samples:
        i = int(math.floor(v * 32767.0 + 0.5))
        out.append(32767 if i > 32767 else (-32768 if i < -32768 else i))
    return out


def write_if_changed(path, text):
    try:
        with open(path, "r") as f:
            if f.read() == text:
                return False
    except IOError:
        pass
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write(text)
    os.replace(tmp, path)
    return True


def render_c(src, levels):
    flat = [s for lvl in levels for s in quantize(lvl)]
    offsets = [0]
    for lvl in levels:
        offsets.append(offsets[-1] + len(lvl))

    out = [
        "/*",
        " *  File: %s" % os.path.basename(src),
        " *",
        " *  Generated by tools/wav2table.py -- do not edit.",
        " *  Mip levels (points per cycle): %s"
        % " / ".join(str(len(l)) for l in levels),
        " *  Total: %d int16 samples (%d bytes)" % (len(flat), len(flat) * 2),
        " */",
        "",
        '#include "wt_data.h"',
        "",
        "const int16_t wt_table[WT_TOTAL] =",
        "{",
    ]
    for i in range(0, len(flat), 12):
        row = ", ".join("%6d" % v for v in flat[i : i + 12])
        out.append("  %s," % row)
    out += [
        "};",
        "",
        "const uint16_t wt_level_offset[WT_LEVEL_COUNT + 1] =",
        "{",
        "  " + ", ".join("%d" % v for v in offsets),
        "};",
        "",
    ]
    return "\n".join(out), offsets, len(flat)


def render_h(src, stem, base, level_count, total, rate, f0):
    return "\n".join(
        [
            "/*",
            " *  File: wt_data.h",
            " *",
            " *  Generated by tools/wav2table.py -- do not edit.",
            " *  Source: %s" % os.path.basename(src),
            " */",
            "",
            "#pragma once",
            "",
            "#include <stdint.h>",
            "",
            '#define WT_NAME_STR     "%s"' % sanitize(stem, 21),
            '#define WT_DISPLAY_NAME "WT %s"' % sanitize(stem, 16),
            # Shorter stem for the build option variants: header.c appends
            # " Evo L" etc, and the name field holds 19 characters.
            '#define WT_NAME_BASE    "WT %s"' % sanitize(stem, 10),
            "#define WT_UNIT_ID      0x%04XU" % unit_id(stem),
            "#define WT_F0_HZ        %.3ff" % f0,
            "#define WT_RATE         %.1ff" % float(rate),
            "#define WT_BASE         %d" % base,
            "#define WT_LEVEL_COUNT  %d" % level_count,
            "#define WT_TOTAL        %d" % total,
            "#define WT_MIN_POINTS   %d" % WT_MIN_POINTS,
            "",
            "extern const int16_t wt_table[WT_TOTAL];",
            "extern const uint16_t wt_level_offset[WT_LEVEL_COUNT + 1];",
            "",
        ]
    )


def main(argv):
    ap = argparse.ArgumentParser(description="bake a wav into a mip-mapped wavetable")
    ap.add_argument("wav")
    ap.add_argument("out", help="generated .c path (wt_data.h lands next to it)")
    ap.add_argument("--name", default=None, help="wavetable name (default: file stem)")
    ap.add_argument("--base", type=int, default=2048, help="points per cycle")
    ap.add_argument("--start", type=float, default=None, help="analysis start, sec")
    ap.add_argument("--len", type=float, default=1.0, help="analysis length, sec")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    stem = args.name or os.path.splitext(os.path.basename(args.wav))[0]
    base = 1 << max(3, (max(WT_MIN_POINTS * 2, args.base) - 1).bit_length())

    x, rate = read_wav(args.wav)
    if len(x) < base:
        raise SystemExit("%s: only %d samples" % (args.wav, len(x)))

    if args.start is not None:
        start = int(args.start * rate)
        end = min(len(x), start + int(args.len * rate))
    else:
        start, end = pick_window(x, rate, args.len)
    if end - start < base:
        raise SystemExit("%s: analysis window too short" % args.wav)

    period, corr = detect_f0(x, rate, start, end)
    if period is None or not 1.0 < period <= end - start:
        print("wav2table: %s: no pitch found, folding the window as-is" % stem)
        period = float(end - start)
        corr = None
    cycle, cycles = fold_cycle(x, start, end - start, period, base)
    f0 = rate / period

    # residual DC from the fold, then normalize with headroom
    mu = sum(cycle) / base
    cycle = [v - mu for v in cycle]
    peak = max(abs(v) for v in cycle)
    if peak > 0.0:
        cycle = [v * (WT_PEAK / peak) for v in cycle]

    levels = build_mips(cycle, WT_MIN_POINTS)
    c_text, _, total = render_c(args.wav, levels)
    h_text = render_h(args.wav, stem, base, len(levels), total, rate, f0)

    c_path = args.out
    h_path = os.path.join(os.path.dirname(c_path) or ".", "wt_data.h")
    changed = write_if_changed(c_path, c_text) | write_if_changed(h_path, h_text)

    if not args.quiet:
        print(
            "  f0 %.2f Hz (midi %.1f), %d cycles over %.2f-%.2f s%s"
            % (
                f0,
                69.0 + 12.0 * math.log(f0 / 440.0, 2.0),
                cycles,
                start / float(rate),
                end / float(rate),
                "" if corr is None else ", acf %.2f" % corr,
            )
        )
        print(
            "  %d levels %s = %d samples (%d bytes)%s"
            % (
                len(levels),
                "/".join(str(len(l)) for l in levels),
                total,
                total * 2,
                "" if changed else "  (unchanged)",
            )
        )

    # unit_id collisions between sibling wavs would confuse the librarian
    d = os.path.dirname(os.path.abspath(args.wav))
    mine = unit_id(stem)
    for other in sorted(os.listdir(d)):
        if not other.lower().endswith(".wav"):
            continue
        other_stem = os.path.splitext(other)[0]
        if other_stem != stem and unit_id(other_stem) == mine:
            print(
                "wav2table: WARNING unit_id 0x%04X collides between '%s' and '%s'"
                % (mine, stem, other_stem)
            )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
