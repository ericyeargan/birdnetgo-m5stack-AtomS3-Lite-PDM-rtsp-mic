#!/usr/bin/env python3
"""List spectral lines in a mono WAV capture that sit exactly on p/q * fs.

Used to survey the clock-locked tones the clock-spur canceller removes; see
docs/CLOCK_SPURS.md. Requires numpy and scipy.

    python3 scripts/find_locked_tones.py capture.wav [--skip 3] [--min-db 12]
"""
import argparse
from fractions import Fraction

import numpy as np
from scipy.io import wavfile
from scipy.signal import medfilt


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("wav")
    ap.add_argument("--skip", type=float, default=3.0, help="seconds to drop from the start")
    ap.add_argument("--min-db", type=float, default=12.0, help="minimum level above the local floor")
    ap.add_argument("--max-den", type=int, default=400, help="largest denominator q to consider")
    ap.add_argument("--tol-hz", type=float, default=0.01, help="max distance from p/q * fs")
    args = ap.parse_args()

    fs, x = wavfile.read(args.wav)
    if x.ndim > 1:
        x = x[:, 0]
    x = x.astype(float)[int(args.skip * fs):]
    n = (len(x) // fs) * fs  # whole seconds, so bins fall on 1/T Hz
    if n < fs:
        raise SystemExit("capture too short after --skip")
    x = x[:n] - x[:n].mean()

    w = np.blackman(n)
    mag = np.abs(np.fft.rfft(x * w)) * 2 / w.sum()
    df = fs / n
    db = 20 * np.log10(mag + 1e-12)
    excess = db - medfilt(db, 401)

    lines = []
    for i in range(int(150 / df), len(mag) - 3):
        if excess[i] < args.min_db or excess[i] != excess[i - 3:i + 4].max():
            continue
        a, b, c = db[i - 1], db[i], db[i + 1]
        f = (i + 0.5 * (a - c) / (a - 2 * b + c)) * df  # parabolic peak interpolation
        r = Fraction(f / fs).limit_denominator(args.max_den)
        if abs(float(r) * fs - f) < args.tol_hz:
            lines.append((excess[i], r, f, mag[i]))

    print(f"fs {fs} Hz, {n / fs:.0f} s analysed")
    if not lines:
        print("no clock-locked lines found")
    for e, r, f, a in sorted(lines, key=lambda t: -t[0]):
        print(f"{str(r):>8} x fs  {f:9.2f} Hz  +{e:4.1f} dB over floor  {a:.3f} counts")


if __name__ == "__main__":
    main()
