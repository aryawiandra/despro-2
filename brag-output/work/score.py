"""Original score for the SAFE-EXIT brag video: D minor, 120 BPM, synced to video.html cues."""
import wave

import numpy as np

# scipy.signal fails to load on this macOS build, so filters are zero-phase
# Butterworth magnitude responses applied in the frequency domain.
SR = 44100
DUR = 23.5
N = int(SR * DUR)
rng = np.random.default_rng(21)


def f(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def _apply(x, gain_fn):
    x = np.asarray(x, dtype=float)
    n = x.shape[-1]
    spec = np.fft.rfft(x, axis=-1)
    freqs = np.fft.rfftfreq(n, 1 / SR)
    return np.fft.irfft(spec * gain_fn(freqs), n=n, axis=-1)


def lp(x, hz, order=2):
    return _apply(x, lambda fr: 1 / np.sqrt(1 + (fr / hz) ** (2 * order)))


def hp(x, hz, order=2):
    return _apply(x, lambda fr: 1 / np.sqrt(1 + (hz / np.maximum(fr, 1e-6)) ** (2 * order)))


def bp(x, lo, hi, order=2):
    return lp(hp(x, lo, order), hi, order)


def fftconvolve(a, b):
    n = len(a) + len(b) - 1
    size = 1 << (n - 1).bit_length()
    return np.fft.irfft(np.fft.rfft(a, size) * np.fft.rfft(b, size), size)[:n]


def place(bus, sig, t, gain=1.0, pan=0.0):
    i = int(t * SR)
    if i >= N:
        return
    sig = sig[: N - i]
    l, r = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
    bus[0, i : i + len(sig)] += sig * gain * l * 1.414
    bus[1, i : i + len(sig)] += sig * gain * r * 1.414


def env_adsr(n, a, r):
    e = np.ones(n)
    na, nr = int(a * SR), int(r * SR)
    e[:na] = np.linspace(0, 1, na)
    e[-nr:] *= np.linspace(1, 0, nr)
    return e


music = np.zeros((2, N))
fx = np.zeros((2, N))
tt = np.arange(N) / SR

# --- drone + smoke bed (0 -> 9s, thins out after reveal) ---
drone = sum(np.sin(2 * np.pi * f(m) * tt + p) * g for m, p, g in [(38, 0, 1), (45, 1.3, 0.6), (50, 2.1, 0.35)])
drone *= (0.25 + 0.75 * np.clip(tt / 5.5, 0, 1)) * np.clip((9.5 - tt) / 3.0, 0.25, 1) * np.clip((23.5 - tt) / 1.5, 0, 1)
place(music, lp(drone, 400) * 0.16, 0)
noise = rng.standard_normal(N)
smoke = lp(noise, 700, 4) * (0.5 + 0.5 * np.sin(2 * np.pi * 0.23 * tt)) ** 2
smoke *= np.clip(tt / 1.5, 0, 1) * np.clip((6.2 - tt) / 0.4, 0, 1)
place(music, smoke * 0.05, 0)

# --- pad chords (from reveal) ---
CHORDS = {
    6: [50, 57, 62, 65, 69], 8: [46, 58, 62, 65, 70], 10: [53, 57, 60, 65, 69], 12: [48, 55, 60, 64, 67],
    14: [50, 57, 62, 65, 69], 16: [46, 58, 62, 65, 70], 18: [53, 57, 60, 65, 69], 20: [45, 57, 61, 64, 69],
    21: [50, 57, 62, 64, 65, 69],
}
ROOTS = {6: 38, 8: 34, 10: 41, 12: 36, 14: 38, 16: 34, 18: 41, 20: 33, 21: 38}
starts = sorted(CHORDS)
for k, s in enumerate(starts):
    end = starts[k + 1] if k + 1 < len(starts) else DUR
    length = end - s + (0.4 if s != 21 else 0)
    n = int(length * SR)
    tl = np.arange(n) / SR
    sig = np.zeros(n)
    for m in CHORDS[s]:
        for det in (-0.07, 0.0, 0.07):
            ph = 2 * np.pi * f(m + det) * tl
            sig += (np.sin(ph) + 0.25 * np.sin(2 * ph) + 0.1 * np.sin(3 * ph)) / 3
    sig = lp(sig, 1500 if s < 21 else 2200)
    sig *= env_adsr(n, 0.35, 0.5 if s != 21 else 1.6)
    if s == 21:
        sig *= np.clip((23.45 - (s + tl)) / 1.2, 0, 1)
    place(music, sig * 0.028, s)

# --- kick + bass + hats, 120 BPM from 6.0 to 21.0 ---
BEAT = 0.5


def kick():
    n = int(0.45 * SR)
    tl = np.arange(n) / SR
    fr = 48 + 90 * np.exp(-tl * 38)
    return np.sin(2 * np.pi * np.cumsum(fr) / SR) * np.exp(-tl * 7) * env_adsr(n, 0.001, 0.04)


duck = np.ones(N)
b = 6.0
while b < 21.0 - 1e-6:
    if not (12.0 <= b < 12.5):  # breath on the fire hit
        place(music, kick(), b, 0.34 if b >= 9.0 else 0.22)
        i = int(b * SR)
        dn = int(0.25 * SR)
        duck[i : i + dn] = np.minimum(duck[i : i + dn], 0.45 + 0.55 * np.linspace(0, 1, dn) ** 0.6)
    b += BEAT
SMOOTH = int(0.012 * SR)  # 12 ms: removes clicks from instant gain steps
duck = np.convolve(duck, np.ones(SMOOTH) / SMOOTH, mode="same")
bass = np.zeros(N)
for s in starts[:-1]:
    end = starts[starts.index(s) + 1]
    e8 = s
    while e8 < min(end, 21.0) - 1e-6:
        n = int(0.24 * SR)
        tl = np.arange(n) / SR
        note = np.tanh(1.8 * np.sin(2 * np.pi * f(ROOTS[s]) * tl)) * np.exp(-tl * 6) * env_adsr(n, 0.004, 0.05)
        i = int(e8 * SR)
        bass[i : i + n] += note[: N - i] if i + n > N else note
        e8 += BEAT / 2
bass = lp(bass, 320) * duck
place(music, bass * 0.13, 0)
b = 9.25
while b < 21.0:
    n = int(0.05 * SR)
    h = hp(rng.standard_normal(n), 7000) * np.exp(-np.arange(n) / SR * 90)
    place(music, h, b, 0.035, pan=0.3)
    b += BEAT
music[:, :] *= duck  # pad breathes with the kick

# --- tonal blips (D minor pentatonic), sit under the music ---


def blip(m, dur=0.35, bright=0.3):
    n = int(dur * SR)
    tl = np.arange(n) / SR
    ph = 2 * np.pi * f(m) * tl
    return (np.sin(ph) + bright * np.sin(2 * ph)) * np.exp(-tl * 11) * np.minimum(1, tl * 400)


# floor LED chase (6.15 -> 7.3)
for k, m in enumerate([62, 65, 67, 69, 72, 74, 77, 79]):
    place(fx, blip(m, 0.25), 6.15 + k * 0.14, 0.05, pan=-0.4 + k * 0.1)
# you are here
place(fx, blip(81, 0.5, 0.1), 9.7, 0.05)
# route A segments + arrival at e2
for k, m in enumerate([74, 77, 79, 81]):
    place(fx, blip(m), 9.9 + k * 0.3, 0.075, pan=0.2)
place(fx, blip(86, 0.7), 11.1, 0.07)
place(fx, blip(81, 0.7), 11.1, 0.05)
# route B segments + arrival at e3
for k, m in enumerate([69, 72, 74, 77]):
    place(fx, blip(m), 12.5 + k * 0.3, 0.075, pan=-0.2)
place(fx, blip(86, 0.8), 13.7, 0.075)
place(fx, blip(81, 0.8), 13.7, 0.055)
place(fx, blip(77, 0.8), 13.7, 0.045)
# hardware spec lines
for k, t in enumerate([15.3, 15.7, 16.1]):
    place(fx, blip([69, 74, 77][k], 0.4, 0.15), t, 0.055)
# outro strip chase
for k, m in enumerate([62, 65, 69, 74, 77, 81]):
    place(fx, blip(m, 0.3), 21.05 + k * 0.14, 0.045, pan=-0.5 + k * 0.2)

# --- impacts, risers, whooshes ---


def boom(dur=1.6, f0=70, f1=38):
    n = int(dur * SR)
    tl = np.arange(n) / SR
    fr = f1 + (f0 - f1) * np.exp(-tl * 6)
    body = np.sin(2 * np.pi * np.cumsum(fr) / SR) * np.exp(-tl * 2.6)
    click = lp(rng.standard_normal(n), 900) * np.exp(-tl * 30)
    return body + 0.25 * click


def riser(dur):
    n = int(dur * SR)
    tl = np.arange(n) / SR
    ramp = (tl / dur) ** 2
    nz = bp(rng.standard_normal(n), 300, 3000) * ramp
    gl = np.sin(2 * np.pi * np.cumsum(f(50) * (1 + 1.0 * ramp)) / SR) * ramp
    return nz * 0.5 + gl * 0.3


def whoosh(dur=0.5):
    n = int(dur * SR)
    tl = np.arange(n) / SR
    return bp(rng.standard_normal(n), 400, 2500) * np.sin(np.pi * tl / dur) ** 2


place(fx, boom(1.2, 60, 40), 3.0, 0.22)  # 90% lands
place(fx, riser(1.0), 5.0, 0.09)
place(fx, boom(), 6.0, 0.38)  # reveal
place(fx, riser(0.5), 11.5, 0.05)
place(fx, boom(1.4, 80, 42), 12.0, 0.34)  # fire detected
fire_n = int(0.6 * SR)
place(fx, lp(rng.standard_normal(fire_n), 1800) * np.exp(-np.arange(fire_n) / SR * 5), 12.0, 0.08)
for c in (9.0, 15.0, 18.5):
    place(fx, whoosh(), c - 0.25, 0.05)
place(fx, boom(1.8, 60, 36), 21.0, 0.26)  # outro

# --- shared space: one reverb for music + fx ---
ir_n = int(1.9 * SR)
ir_t = np.arange(ir_n) / SR
ir = lp(rng.standard_normal((2, ir_n)), 5000) * np.exp(-ir_t * 3.2)
ir /= np.abs(ir).sum(axis=1, keepdims=True) ** 0.5 * 30
bus = music + fx
send = 0.35 * music + 0.6 * fx
wet = np.stack([fftconvolve(send[c], ir[c])[:N] for c in range(2)])
mix = bus + wet * 0.5
mix = hp(mix, 28)
mix = np.tanh(mix * 1.6) / 1.6
fade = np.clip((DUR - tt) / 0.35, 0, 1) * np.clip(tt / 0.05, 0, 1)
mix *= fade
mix /= np.abs(mix).max() / 0.89
with wave.open("score.wav", "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes((mix.T * 32767).astype("<i2").tobytes())
print("peak ok, seconds:", N / SR)
