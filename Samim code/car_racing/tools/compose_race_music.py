# "Grid to Glory": an original race track for the game. Hybrid orchestral, D minor, 118 BPM, 92 bars
# (about 3 minutes 7 seconds), driven by an octave-jumping bass and a gritty engine-like synth. Its energy follows a broad map (quiet start, a dark middle-weight
# part, a long full part, a drop, a heavy dark part, a rise) - the melodies and chords are written
# here. Bars 0-4 are an intro heard once at the start of a race; the game loops from bar 5, and the
# last bars lead back into it. Usage: compose_race.py out.wav
import struct
import sys
import numpy as np
from scipy.signal import butter, sosfilt, fftconvolve

SR = 44100
BPM = 118.0
BEAT = 60.0 / BPM
BAR = 4 * BEAT
STEP = BEAT / 4.0
BARS = 92
LOOP_BAR = 5
N = int(round(BARS * BAR * SR))
LOOP = int(round(LOOP_BAR * BAR * SR))
rng = np.random.default_rng(20261109)

# sections: (first bar, name)
INTRO, MID, FULL, DROP, DARK, RISE = 0, 5, 16, 65, 70, 88
def section(bar):
    for start, name in ((RISE, 'rise'), (DARK, 'dark'), (DROP, 'drop'), (FULL, 'full'), (MID, 'mid'), (INTRO, 'intro')):
        if bar >= start:
            return name

def intensity(bar):
    s = section(bar)
    if s == 'intro': return 0.10 + 0.12 * bar / 5.0
    if s == 'mid':   return 0.35 + 0.15 * (bar - MID) / 10.0
    if s == 'full':  return 0.85 if 41 <= bar < 47 else 1.0          # (a lighter stretch in the middle)
    if s == 'drop':  return 0.45 - 0.15 * (bar - DROP) / 5.0
    if s == 'dark':  return 0.60 + 0.25 * (bar - DARK) / 18.0
    return 0.55 + 0.30 * (bar - RISE) / 4.0


def hz(m):
    return 440.0 * 2.0 ** ((m - 69) / 12.0)


def saw(f, n, detune=0.0, bright=12000.0):
    t = np.arange(n) / SR
    out = np.zeros(n)
    ph = rng.uniform(0, 6.28)
    k = 1
    while k * f < min(bright, 18000.0) and k <= 40:
        out += np.sin(2 * np.pi * k * f * (1 + detune) * t + k * ph) / k
        k += 1
    return out


def env(n, a, d, s, r):
    e = np.full(n, s, dtype=float)
    na = min(int(a * SR), n)
    e[:na] = np.linspace(0, 1, na, endpoint=False)
    nd = min(int(d * SR), n - na)
    if nd > 0:
        e[na:na + nd] = np.linspace(1, s, nd)
    nr = min(int(r * SR), n)
    if nr > 0:
        e[-nr:] *= np.linspace(1, 0, nr)
    return e


def lp(x, fc, order=2):
    return sosfilt(butter(order, min(fc, 20000.0) / (SR / 2), 'low', output='sos'), x)


def hp(x, fc, order=2):
    return sosfilt(butter(order, fc / (SR / 2), 'high', output='sos'), x)


def wrap_add(buf, x, i):
    """adds x at sample i; anything past the end goes on from the loop point (bar 5)"""
    while len(x):
        if i >= N:
            i = LOOP + (i - N)
        take = min(len(x), N - i)
        buf[i:i + take] += x[:take]
        x = x[take:]
        i += take


def put(buf, x, at):
    wrap_add(buf, x, int(at * SR))


CH = {'Dm': [38, 53, 57, 62], 'Bb': [34, 53, 58, 62], 'F': [41, 53, 60, 65], 'C': [36, 52, 55, 60],
      'Gm': [43, 55, 58, 62], 'A': [33, 52, 57, 61], 'Eb': [39, 55, 58, 63]}
def chord(bar):
    s = section(bar)
    if s == 'intro': return CH['Dm']
    if s == 'mid':   return CH[['Dm', 'Dm', 'Bb', 'Bb'][(bar - MID) % 4]]
    if s == 'full':
        k = bar - FULL
        prog = ['Dm', 'Bb', 'F', 'C'] if (k // 8) % 2 == 0 else ['Dm', 'Gm', 'Bb', 'A']
        return CH[prog[(k // 2) % 4]]
    if s == 'drop':  return CH[['Bb', 'Bb', 'Gm', 'Gm', 'A'][bar - DROP]]
    if s == 'dark':  return CH[['Dm', 'Eb', 'Dm', 'A'][((bar - DARK) // 4) % 4]]
    return CH['A']                                           # the rise: the A chord pulls back to D minor

pad, bass, osti, brass, lead, drums, fx = (np.zeros(N) for _ in range(7))
kick_bus = np.zeros(N)

# ---- pad: held strings, opening with the energy
for bar in range(BARS):
    I = intensity(bar)
    n = int(BAR * SR * 1.15)
    bright = 400 + 3800 * I
    for m in chord(bar)[1:]:
        for det in (-0.004, 0.0, 0.004):
            x = lp(saw(hz(m), n, det, bright * 2), bright) * env(n, 0.4, 0.2, 1.0, 0.5)
            put(pad, x * (0.03 + 0.06 * I) / 3.0, bar * BAR)

# ---- low pulse: eighth notes (from bar 2)
for step in range(2 * 16, BARS * 16, 2):
    bar = step // 16
    if section(bar) == 'drop':
        continue
    I = intensity(bar)
    root = chord(bar)[0]
    n = int(STEP * 2 * 0.85 * SR)
    f0 = hz(root) * (2.0 if (step // 2) % 2 else 1.0)       # (low, high, low, high: the octave pulse)
    x = lp(saw(f0, n, 0.0, 3000) + 0.6 * saw(f0 * 2, n, 0.003, 3000), 300 + 1300 * I) * env(n, 0.004, 0.08, 0.5, 0.04)
    put(bass, x * (0.15 + 0.17 * I) * (1.0 if step % 8 == 0 else 0.8), step * STEP)

# ---- the engine-like synth: a gritty, distorted sixteenth-note growl on the root (from bar 9)
for step in range(9 * 16, BARS * 16):
    bar = step // 16
    if section(bar) in ('drop',) or (41 <= bar < 45):
        continue
    I = intensity(bar)
    root = chord(bar)[0] + 12
    n = int(STEP * 0.9 * SR)
    x = saw(hz(root), n, 0.006, 5000) + saw(hz(root), n, -0.006, 5000)
    x = np.tanh(x * 3.0)
    sweep = 500 + 1800 * I * (0.6 + 0.4 * np.sin(2 * np.pi * step / 64.0))
    x = lp(x, sweep, 2) * env(n, 0.002, 0.06, 0.6, 0.03)
    put(bass, x * (0.020 + 0.035 * I) * (1.0 if step % 4 == 0 else 0.65), step * STEP)

# ---- ostinato: sixteenths, its own shape (mid from bar 9, full, rise)
SHAPE = [0, 1, 2, 3, 2, 1, 0, 2, 1, 3, 2, 1, 0, 3, 1, 2]
for step in range(9 * 16, BARS * 16):
    bar = step // 16
    if section(bar) in ('drop',) or 41 <= bar < 45:
        continue
    I = intensity(bar)
    tones = chord(bar)[1:] + [chord(bar)[1] + 12]
    m = tones[SHAPE[step % 16]] + 12
    n = int(STEP * 0.8 * SR)
    x = lp(saw(hz(m), n, 0.002, 7000) + saw(hz(m), n, -0.002, 7000), 1200 + 4000 * I) * env(n, 0.003, 0.05, 0.45, 0.03)
    put(osti, x * (0.022 + 0.045 * I) * (1.0 if step % 4 == 0 else 0.7), step * STEP)

# ---- brass: swells and stabs in the full part; low drones in the dark part
for bar in range(FULL, DROP):
    n = int(BAR * SR * 0.95)
    for m in chord(bar):
        m2 = m if m > 45 else m + 12
        x = lp(saw(hz(m2), n, 0.003, 6000) + saw(hz(m2), n, -0.003, 6000), 1700) * env(n, 0.1, 0.3, 0.7, 0.2)
        put(brass, x * 0.03 * intensity(bar), bar * BAR)
    if bar % 2 == 0:
        for beat in (0, 1.5, 3, 6.5):
            n2 = int(0.26 * SR)
            for m in chord(bar)[1:]:
                x = lp(saw(hz(m + 12), n2, 0.002, 8000), 3000) * env(n2, 0.004, 0.08, 0.3, 0.08)
                put(brass, x * 0.04 * intensity(bar), bar * BAR + beat * BEAT)
for bar in range(DARK, RISE, 2):
    n = int(2 * BAR * SR)
    for m in chord(bar)[:2]:
        m2 = m if m < 45 else m - 12
        x = lp(saw(hz(m2), n, 0.004, 3000) + saw(hz(m2), n, -0.004, 3000), 500 + 500 * intensity(bar)) * env(n, 0.8, 0.5, 0.85, 0.8)
        put(brass, x * 0.07, bar * BAR)

# ---- the lead: two themes written for this track (bar, beat, MIDI note, beats)
THEME_C = [(0, 0, 69, 1), (0, 1, 74, 1), (0, 2, 76, 1), (0, 3, 77, 1), (1, 0, 76, 2), (1, 2, 74, 2),
           (2, 0, 72, 1), (2, 1, 74, 1), (2, 2, 77, 2), (3, 0, 76, 4),
           (4, 0, 69, 1), (4, 1, 74, 1), (4, 2, 76, 1), (4, 3, 77, 1), (5, 0, 79, 2), (5, 2, 77, 1), (5, 3, 76, 1),
           (6, 0, 74, 1.5), (6, 1.5, 72, 0.5), (6, 2, 70, 2), (7, 0, 69, 4)]
THEME_D = [(0, 0, 81, 2), (0, 2, 79, 1), (0, 3, 77, 1), (1, 0, 79, 3), (1, 3, 74, 1), (2, 0, 77, 2), (2, 2, 76, 1),
           (2, 3, 74, 1), (3, 0, 76, 4), (4, 0, 81, 2), (4, 2, 82, 1), (4, 3, 81, 1), (5, 0, 79, 2), (5, 2, 77, 2),
           (6, 0, 76, 1), (6, 1, 77, 1), (6, 2, 79, 1), (6, 3, 76, 1), (7, 0, 74, 4)]
def play(theme, start, shift, level):
    for b, beat, m, length in theme:
        n = int(length * BEAT * SR)
        t = np.arange(n) / SR
        vib = 1.0 + 0.003 * np.sin(2 * np.pi * 5.2 * t) * np.minimum(1.0, t / 0.4)
        ph = 2 * np.pi * np.cumsum(hz(m + shift) * vib) / SR
        x = sum(np.sin(k * ph) / k for k in range(1, 9))
        put(lead, lp(x, 3000) * env(n, 0.02, 0.1, 0.8, 0.08) * level, (start + b) * BAR + beat * BEAT)
play(THEME_C, 16, 0, 0.05)
play(THEME_D, 24, 0, 0.055)
play(THEME_C, 32, 12, 0.045)
play(THEME_D, 47, 0, 0.06)
play(THEME_C, 55, 0, 0.06)
play(THEME_D, 74, -12, 0.05)        # the dark part: the same idea, an octave down

# ---- drums
def kick(level):
    n = int(0.4 * SR); t = np.arange(n) / SR
    return np.sin(2 * np.pi * np.cumsum(45 + 110 * np.exp(-t * 30)) / SR) * np.exp(-t * 9) * level

def snare(level):
    n = int(0.3 * SR); t = np.arange(n) / SR
    return (hp(rng.standard_normal(n), 900) * np.exp(-t * 16) * 0.5 + np.sin(2 * np.pi * 190 * t) * np.exp(-t * 22) * 0.5) * level

def taiko(level):
    n = int(0.9 * SR); t = np.arange(n) / SR
    ph = 2 * np.pi * np.cumsum(62 + 35 * np.exp(-t * 18)) / SR
    return (np.sin(ph) * np.exp(-t * 4.5) + lp(rng.standard_normal(n), 900) * np.exp(-t * 30) * 0.4) * level

def hat(level):
    n = int(0.06 * SR)
    return hp(rng.standard_normal(n), 7000) * np.exp(-np.arange(n) / SR * 70) * level

def boom():
    n = int(2.5 * SR); t = np.arange(n) / SR
    return (np.sin(2 * np.pi * np.cumsum(38 + 60 * np.exp(-t * 6)) / SR) * np.exp(-t * 1.6) + lp(rng.standard_normal(n), 400) * np.exp(-t * 3) * 0.3) * 0.5

for bar in range(BARS):
    s = section(bar)
    I = intensity(bar)
    if s == 'mid' or s == 'dark' or s == 'rise':
        put(drums, taiko(0.15 + 0.25 * I), bar * BAR)
        put(drums, taiko(0.10 + 0.20 * I), bar * BAR + 1.5 * BEAT)
        put(drums, taiko(0.10 + 0.20 * I), bar * BAR + 2.5 * BEAT)
        if s != 'mid' or bar >= 10:
            put(drums, taiko(0.08 + 0.15 * I), bar * BAR + 3.5 * BEAT)
        if s == 'dark' or bar >= 10:                     # a steady pulse: never a lull
            for b in range(4):
                k = kick(0.28)
                put(drums, k, bar * BAR + b * BEAT)
                put(kick_bus, np.abs(k), bar * BAR + b * BEAT)
            for st in range(1, 16, 2):
                put(drums, hat(0.035), bar * BAR + st * STEP)
    if s == 'full':
        light = 41 <= bar < 47
        for b in ((0, 2) if light else (0, 1, 1.5, 2, 3, 3.5)):
            k = kick(0.42)
            put(drums, k, bar * BAR + b * BEAT)
            put(kick_bus, np.abs(k), bar * BAR + b * BEAT)
        if not light:
            for b in (1, 3):
                put(drums, snare(0.22), bar * BAR + b * BEAT)
            if bar % 2 == 1:                             # a fill of quick snares into the next bar
                for st in (13, 14, 15):
                    put(drums, snare(0.10), bar * BAR + st * STEP)
            for st in range(16):
                put(drums, hat(0.06 if st % 2 else 0.09), bar * BAR + st * STEP)
        put(drums, taiko(0.30 if bar % 2 == 0 else 0.2), bar * BAR)
    if s == 'rise':
        for st in range(0, 16, max(1, 4 - (bar - RISE))):
            put(drums, snare(0.06 + 0.04 * (bar - RISE)), bar * BAR + st * STEP)
for bar in (FULL, DARK, MID):
    put(fx, boom(), bar * BAR)

def riser(seconds, level):
    n = int(seconds * SR)
    return hp(rng.standard_normal(n), 600) * np.linspace(0, 1, n) ** 3 * level
put(fx, riser(2 * BAR, 0.16), (FULL - 2) * BAR)
for b in range(FULL + 8, DROP, 8):
    put(fx, riser(1 * BAR, 0.10), (b - 1) * BAR)
put(fx, riser(2 * BAR, 0.12), (DARK - 2) * BAR)
put(fx, riser(4 * BAR, 0.14), RISE * BAR)            # (into bar 5 again: the loop)

# ---- the mix
pump = np.convolve(kick_bus, np.ones(int(0.02 * SR)) / int(0.02 * SR), mode='same')
pump = 1.0 - 0.45 * np.clip(pump / (pump.max() + 1e-9), 0, 1)
music = (pad + bass + osti + brass + lead) * pump
ir_n = int(1.6 * SR)
ir = lp(rng.standard_normal(ir_n) * np.exp(-np.arange(ir_n) / SR * 3.0), 5000)
ir /= np.sqrt(np.sum(ir ** 2))
wet = fftconvolve(music + fx + 0.25 * drums, ir)
tail = wet[N:]
wet = wet[:N]
wrap_add(wet, tail, N)                                # (the tail goes on from the loop point)
mix = music + fx + drums + 0.35 * wet
loop_part = np.concatenate((mix[-SR:], mix[LOOP:]))   # (filtered round the loop: no click)
mix[LOOP:] = hp(loop_part, 28)[SR:]
mix[:LOOP] = hp(mix[:LOOP], 28)
mix /= np.max(np.abs(mix))
mix = np.tanh(mix * 1.5) / np.tanh(1.5) * 0.92
out = (mix * 32767).astype(np.int16).tobytes()
with open(sys.argv[1], 'wb') as f:
    f.write(b'RIFF' + struct.pack('<I', 36 + len(out)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 1, SR, SR * 2, 2, 16) + b'data' + struct.pack('<I', len(out)) + out)
print('length %.2f s, loop from %.3f s' % (N / SR, LOOP / SR))
for s in range(0, int(N / SR) - 1, 6):
    w = mix[s * SR:(s + 2) * SR]
    print('%3d s %-5s %6.1f dB  brightness %.3f' % (s, section(int(s / BAR)), 10 * np.log10(np.mean(w ** 2) + 1e-12),
                                                   np.sqrt(np.sum(np.diff(w) ** 2) / np.sum(w ** 2))))
