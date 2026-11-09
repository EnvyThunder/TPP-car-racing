# "Pole Position": an original track for the game's menu and results. Hybrid orchestral in D minor,
# 112 BPM: a quiet pulse, a rising string ostinato, then full brass, drums and a lead. 32 bars, and
# the end runs round into the start, so it loops without a gap. Melody and harmony written here.
import sys
import numpy as np
from scipy.signal import butter, sosfilt, fftconvolve

SR = 44100
BPM = 112.0
BEAT = 60.0 / BPM
STEP = BEAT / 4.0
BARS = 32
N = int(round(BARS * 4 * BEAT * SR))
rng = np.random.default_rng(20261106)


def hz(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)


def saw(f, n, detune=0.0, bright=12000.0):
    t = np.arange(n) / SR
    out = np.zeros(n)
    k = 1
    ph = rng.uniform(0, 6.28)
    while k * f < min(bright, 18000.0) and k <= 40:
        out += np.sin(2 * np.pi * k * f * (1 + detune) * t + k * ph) / k
        k += 1
    return out


def env(n, a, d, s, r):
    e = np.full(n, s, dtype=float)
    na, nd, nr = int(a * SR), int(d * SR), int(r * SR)
    na = min(na, n)
    e[:na] = np.linspace(0, 1, na, endpoint=False)
    nd = min(nd, n - na)
    if nd > 0:
        e[na:na + nd] = np.linspace(1, s, nd)
    nr = min(nr, n)
    if nr > 0:
        e[-nr:] *= np.linspace(1, 0, nr)
    return e


def lp(x, fc, order=2):
    return sosfilt(butter(order, min(fc, 20000.0) / (SR / 2), 'low', output='sos'), x)


def hp(x, fc, order=2):
    return sosfilt(butter(order, fc / (SR / 2), 'high', output='sos'), x)


def put(buf, x, at):
    i = int(at * SR) % N
    end = i + len(x)
    if end <= N:
        buf[i:end] += x
    else:                                # (round into the start: the loop)
        buf[i:] += x[:N - i]
        buf[:end - N] += x[N - i:]


# D minor harmony: a new turn in the second half
CHORDS_A = [[38, 53, 57, 62], [34, 53, 58, 62], [41, 53, 60, 65], [36, 52, 55, 60]]      # Dm Bb F C
CHORDS_B = [[38, 53, 57, 62], [43, 55, 58, 62], [34, 53, 58, 65], [33, 52, 57, 61]]      # Dm Gm Bb A
def chord(bar):
    return (CHORDS_B if 24 <= bar % BARS < 32 else CHORDS_A)[(bar // 2) % 4]

kick_bus = np.zeros(N)
pad = np.zeros(N)
bass = np.zeros(N)
osti = np.zeros(N)
brass = np.zeros(N)
lead = np.zeros(N)
drums = np.zeros(N)
fx = np.zeros(N)

# ---- the pad: held strings under everything, opening up
for bar in range(0, BARS, 2):
    n = int(2 * 4 * BEAT * SR * 1.1)
    bright = 500 + 3500 * min(1.0, bar / 18.0)
    level = 0.05 + 0.10 * min(1.0, bar / 16.0)
    for m in chord(bar)[1:]:
        for det in (-0.004, 0.0, 0.004):
            x = lp(saw(hz(m), n, det, bright * 2), bright) * env(n, 0.5, 0.2, 1.0, 0.6)
            put(pad, x * level / 3.0, bar * 4 * BEAT)

# ---- the low pulse: eighth notes from the first bar
for step in range(0, BARS * 16, 2):
    bar = step // 16
    root = chord(bar)[0]
    n = int(STEP * 2 * 0.85 * SR)
    fc = 300 + 900 * min(1.0, bar / 16.0)
    x = lp(saw(hz(root), n, 0.0, 3000) + 0.6 * saw(hz(root) * 2, n, 0.003, 3000), fc) * env(n, 0.005, 0.1, 0.55, 0.05)
    level = 0.20 + 0.10 * min(1.0, bar / 16.0)
    put(bass, x * level * (1.0 if step % 8 == 0 else 0.75), step * STEP)

# ---- the ostinato: sixteenth-note strings, from bar 8, its own rising shape
SHAPE = [0, 2, 3, 2, 0, 3, 2, 1, 0, 2, 3, 2, 1, 3, 2, 3]
for step in range(8 * 16, BARS * 16):
    bar = step // 16
    tones = chord(bar)[1:] + [chord(bar)[1] + 12]
    m = tones[SHAPE[step % 16]] + 12
    n = int(STEP * 0.8 * SR)
    grow = min(1.0, (bar - 8) / 8.0)
    x = lp(saw(hz(m), n, 0.002, 7000) + saw(hz(m), n, -0.002, 7000), 1500 + 3500 * grow) * env(n, 0.003, 0.05, 0.45, 0.03)
    put(osti, x * (0.035 + 0.045 * grow) * (1.0 if step % 4 == 0 else 0.7), step * STEP)

# ---- brass: long swells and stabs from bar 16
for bar in range(16, BARS, 2):
    n = int(2 * 4 * BEAT * SR * 0.95)
    for m in chord(bar):
        m2 = m if m > 45 else m + 12
        x = lp(saw(hz(m2), n, 0.003, 6000) + saw(hz(m2), n, -0.003, 6000), 1800) * env(n, 0.12, 0.3, 0.7, 0.25)
        put(brass, x * 0.035, bar * 4 * BEAT)
    for beat in (0, 2.5, 6):             # the stabs
        n2 = int(0.28 * SR)
        for m in chord(bar)[1:]:
            x = lp(saw(hz(m + 12), n2, 0.002, 8000), 3200) * env(n2, 0.004, 0.08, 0.3, 0.08)
            put(brass, x * 0.045, bar * 4 * BEAT + beat * BEAT)

# ---- the lead: a melody written for this track (bar, beat, MIDI note, beats)
THEME = [(0, 0, 74, 1.5), (0, 1.5, 77, 0.5), (0, 2, 81, 2), (1, 0, 79, 1), (1, 1, 77, 1), (1, 2, 76, 2),
         (2, 0, 74, 1), (2, 1, 72, 1), (2, 2, 70, 1), (2, 3, 72, 1), (3, 0, 74, 3), (3, 3, 69, 1),
         (4, 0, 70, 1.5), (4, 1.5, 74, 0.5), (4, 2, 77, 2), (5, 0, 79, 1), (5, 1, 81, 1), (5, 2, 82, 2),
         (6, 0, 81, 1), (6, 1, 79, 1), (6, 2, 77, 1), (6, 3, 76, 1), (7, 0, 74, 4)]
for start in (16, 24):
    for b, beat, m, length in THEME:
        if start == 24 and b >= 6:       # the second time ends over the A chord, leading home
            m = {81: 81, 79: 79, 77: 76, 76: 73, 74: 74}.get(m, m)
        n = int(length * BEAT * SR)
        t = np.arange(n) / SR
        vib = 1.0 + 0.003 * np.sin(2 * np.pi * 5.2 * t) * np.minimum(1.0, t / 0.4)
        ph = 2 * np.pi * np.cumsum(hz(m) * vib) / SR
        x = sum(np.sin(k * ph) / k for k in range(1, 9))
        x = lp(x, 3000) * env(n, 0.02, 0.1, 0.8, 0.08)
        put(lead, x * (0.055 if start == 16 else 0.07), (start + b) * 4 * BEAT + beat * BEAT)

# ---- drums
def kick(level):
    n = int(0.4 * SR)
    t = np.arange(n) / SR
    ph = 2 * np.pi * np.cumsum(45 + 110 * np.exp(-t * 30)) / SR
    return np.sin(ph) * np.exp(-t * 9) * level


def snare(level):
    n = int(0.3 * SR)
    t = np.arange(n) / SR
    x = hp(rng.standard_normal(n), 900) * np.exp(-t * 16) * 0.5 + np.sin(2 * np.pi * 190 * t) * np.exp(-t * 22) * 0.5
    return x * level


def taiko(level):
    n = int(0.9 * SR)
    t = np.arange(n) / SR
    ph = 2 * np.pi * np.cumsum(62 + 35 * np.exp(-t * 18)) / SR
    return (np.sin(ph) * np.exp(-t * 4.5) + lp(rng.standard_normal(n), 900) * np.exp(-t * 30) * 0.4) * level


def hat(level):
    n = int(0.06 * SR)
    return hp(rng.standard_normal(n), 7000) * np.exp(-np.arange(n) / SR * 70) * level

for bar in range(4, 16):                 # the build: drums like distant hits, growing
    g = (bar - 4) / 12.0
    put(drums, taiko(0.12 + 0.25 * g), bar * 4 * BEAT)
    if bar >= 8:
        put(drums, taiko(0.08 + 0.2 * g), (bar * 4 + 2.5) * BEAT)
for bar in range(16, BARS):              # full
    for b in (0, 1.5, 2, 3.5):
        k = kick(0.42)
        put(drums, k, (bar * 4 + b) * BEAT)
        put(kick_bus, np.abs(k), (bar * 4 + b) * BEAT)
    for b in (1, 3):
        put(drums, snare(0.20), (bar * 4 + b) * BEAT)
    if bar % 2 == 0:
        put(drums, taiko(0.30), bar * 4 * BEAT)
    for s in range(16):
        put(drums, hat(0.05 if s % 2 else 0.08), (bar * 4 + s / 4.0) * BEAT)

# ---- the rise into the full part, and its answer before the loop
def riser(seconds, level):
    n = int(seconds * SR)
    x = rng.standard_normal(n)
    shaped = hp(x, 600)
    return shaped * np.linspace(0, 1, n) ** 3 * level

put(fx, riser(4 * 4 * BEAT / 2, 0.16), 14 * 4 * BEAT)

# ---- the mix: everything but the drums ducks a little on the kick
pump = np.convolve(kick_bus, np.ones(int(0.02 * SR)) / int(0.02 * SR), mode='same')
pump = 1.0 - 0.35 * np.clip(pump / (pump.max() + 1e-9), 0, 1)
music = (pad + bass + osti + brass + lead) * pump

# a short hall, wrapped round the loop so the start hears the end's tail
ir_n = int(1.5 * SR)
ir = rng.standard_normal(ir_n) * np.exp(-np.arange(ir_n) / SR * 3.2)
ir = lp(ir, 5000)
ir /= np.sqrt(np.sum(ir ** 2))
wet_in = music + fx + 0.25 * drums
wet = fftconvolve(wet_in, ir)
wet[:len(wet) - N] += wet[N:]
wet = wet[:N]
mix = music + fx + drums + 0.35 * wet
mix = hp(np.concatenate((mix[-SR:], mix)), 28)[SR:]     # (filtered round the loop: no click at the join)

mix /= np.max(np.abs(mix))
mix = np.tanh(mix * 1.5) / np.tanh(1.5) * 0.92

out = (mix * 32767).astype(np.int16)
with open(sys.argv[1], 'wb') as f:
    b = out.tobytes()
    import struct
    f.write(b'RIFF' + struct.pack('<I', 36 + len(b)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, SR, SR * 2, 2, 16) + b'data' + struct.pack('<I', len(b)) + b)

print('length %.2f s' % (N / SR))
for s in range(0, int(N / SR), 5):
    w = mix[s * SR:(s + 5) * SR]
    print('%3d s %6.1f dB  brightness %.3f' % (s, 10 * np.log10(np.mean(w ** 2) + 1e-12), np.sqrt(np.sum(np.diff(w) ** 2) / np.sum(w ** 2))))
print('clicks at the loop: end %.3f start %.3f, peak %.2f' % (mix[-1], mix[0], np.max(np.abs(mix))))
