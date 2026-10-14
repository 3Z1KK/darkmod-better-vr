"""Generates the small VR tool/pouch sound effects (procedural, no external sources)."""
import numpy as np, wave, sys, os
SR = 44100
rng = np.random.default_rng(7)

def bandnoise(n, lo, hi):
    x = rng.standard_normal(n)
    X = np.fft.rfft(x); f = np.fft.rfftfreq(n, 1 / SR)
    X[(f < lo) | (f > hi)] = 0
    y = np.fft.irfft(X, n)
    return y / (np.abs(y).max() + 1e-9)

def sweep_noise(n, f0, f1, width):
    # noise whose band centre moves from f0 to f1 (scrape)
    out = np.zeros(n); seg = 512
    for s in range(0, n, seg):
        t = s / n; fc = f0 + (f1 - f0) * t
        out[s:s + seg] = bandnoise(seg * 4, fc - width, fc + width)[:min(seg, n - s)]
    return out

def env(n, attack, decay_shape=2.0):
    t = np.linspace(0, 1, n)
    a = np.clip(t / max(attack, 1e-4), 0, 1)
    return a * (1 - t) ** decay_shape

def ring(n, freqs, decay):
    t = np.arange(n) / SR
    return sum(np.sin(2 * np.pi * f * t + rng.uniform(0, 6)) * np.exp(-t * decay * (1 + i * 0.4)) / (i + 1) for i, f in enumerate(freqs))

def write(path, x, gain=0.7):
    x = x / (np.abs(x).max() + 1e-9) * gain
    pcm = (np.clip(x, -1, 1) * 32767).astype(np.int16)
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())

def make(outdir):
    os.makedirs(outdir, exist_ok=True)
    # sword out of the scabbard: rising metallic scrape with a ring at the end
    n = int(0.45 * SR)
    x = sweep_noise(n, 2500, 6000, 900) * env(n, 0.15, 1.2) * 0.8
    r = ring(n, [3150, 4720, 6890], 9.0); r[:int(0.30 * SR)] = 0
    write(f'{outdir}/sword_draw.wav', x + r * 0.35, 0.55)
    # sword back in: falling scrape, ends with a dull clack
    n = int(0.38 * SR)
    x = sweep_noise(n, 5500, 2200, 800) * env(n, 0.05, 1.0)
    k = np.zeros(n); kn = int(0.04 * SR); k[-kn - 600:-600] = bandnoise(kn, 150, 900) * np.exp(-np.linspace(0, 6, kn))
    write(f'{outdir}/sword_sheath.wav', x * 0.7 + k * 1.2, 0.5)
    # leather: two soft rustles
    def rustle(length, lo, hi):
        m = int(length * SR)
        return bandnoise(m, lo, hi) * env(m, 0.2, 2.5)
    for name, a, b in (('leather_draw', 0.12, 0.10), ('leather_holster', 0.10, 0.14)):
        x = np.concatenate([rustle(a, 300, 3000), np.zeros(int(0.03 * SR)), rustle(b, 250, 2500) * 0.7])
        write(f'{outdir}/{name}.wav', x, 0.45)
    # lockpick: tiny metal tick + cloth
    n = int(0.12 * SR)
    x = rustle(0.12, 800, 5000) * 0.5 + ring(n, [5400, 8100], 60.0) * 0.6
    write(f'{outdir}/pick_draw.wav', x, 0.35)
    write(f'{outdir}/pick_holster.wav', x[::-1].copy() * env(n, 0.3, 1.0), 0.3)
    # pouch: flap open / closed, item scroll tick
    write(f'{outdir}/pouch_open.wav', np.concatenate([rustle(0.07, 400, 3500), rustle(0.11, 200, 2000) * 0.8]), 0.4)
    write(f'{outdir}/pouch_close.wav', np.concatenate([rustle(0.06, 200, 1800), rustle(0.05, 300, 2500) * 0.6]), 0.4)
    n = int(0.035 * SR)
    write(f'{outdir}/pouch_scroll.wav', bandnoise(n, 1500, 6000) * np.exp(-np.linspace(0, 8, n)), 0.25)

if __name__ == '__main__':
    make(sys.argv[1])
