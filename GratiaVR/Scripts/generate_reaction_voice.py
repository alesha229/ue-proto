"""Non-verbal reaction voice for the character (system Python, numpy/scipy): breathy formant synthesis.

Each sound is a source-filter voice: a glottal pulse train (Rosenberg pulse, jitter, shimmer, vibrato)
mixed with aspiration noise, shaped by five time-varying formant resonators (adult female vowel
targets) and lip radiation. Kinds:

  soft    breathy "ah" falling             calm touch
  hum     closed-mouth "mm"                calm / pleasure
  ask     "hm?" rising                     curious, reserved
  startle sharp inhale + short "ah!"       fast touch (any mood)
  giggle  three breathy "he"               cheerful
  sigh    exhaled "haa" with weak voice    reserved / shy
  moan    long "a-ah" with vibrato         penetration channels

Writes Exports/Gratia/Audio/Reactions/<Kind>_<n>.wav (48 kHz mono, peak -3 dBFS)
and reactions.json (kind, variant, duration). Original synthesis, no recordings: free to ship.

    python GratiaVR/Scripts/generate_reaction_voice.py [--plot out.png]
"""
import argparse
import json
import zlib
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter

RATE = 48000
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Exports/Gratia/Audio/Reactions'

# Adult female formant targets (Hz) and bandwidths, after Peterson & Barney / Hillenbrand.
VOWELS = {
    'a': ((850, 1220, 2810, 3800, 4600), (90, 110, 160, 250, 300)),
    'e': ((560, 2100, 2900, 3900, 4700), (80, 110, 160, 250, 300)),
    'i': ((330, 2700, 3300, 4100, 4800), (60, 120, 170, 250, 300)),
    'o': ((560, 920, 2700, 3800, 4600), (80, 90, 150, 250, 300)),
    'u': ((380, 950, 2650, 3800, 4600), (70, 90, 150, 250, 300)),
    # Closed-mouth hum: low nasal murmur, weak upper resonances.
    'm': ((260, 1050, 2500, 3600, 4500), (60, 250, 300, 350, 400)),
}


def track(points, n):
    """Piecewise-linear control curve: points [(t 0..1, value)] sampled at n frames."""
    t = np.linspace(0.0, 1.0, n)
    xs, ys = zip(*points)
    return np.interp(t, xs, ys)


def glottal(f0, rng, open_phase=0.42, close_phase=0.18, jitter=0.010, shimmer=0.05):
    """Rosenberg glottal flow (opening cosine, faster closing quarter-cosine, closed rest) for a per-sample
    f0 curve (Hz); returns (flow, pulse position 0..1 for pulsing the aspiration noise). Lip radiation later
    differentiates it: about -6 dB/octave overall, like a real voice (no buzz)."""
    n = len(f0)
    flow = np.zeros(n)
    position = np.zeros(n)
    phase, scale, amp = 0.0, 1.0, 1.0
    for i in range(n):
        phase += f0[i] * scale / RATE
        if phase >= 1.0:
            phase -= 1.0
            scale = 1.0 + rng.normal(0.0, jitter)
            amp = max(0.2, 1.0 + rng.normal(0.0, shimmer))
        position[i] = phase
        if phase < open_phase:
            flow[i] = amp * 0.5 * (1.0 - np.cos(np.pi * phase / open_phase))
        elif phase < open_phase + close_phase:
            flow[i] = amp * np.cos(0.5 * np.pi * (phase - open_phase) / close_phase)
    return flow, position


def resonator(x, freq, bw):
    """Time-varying two-pole resonator (Klatt), coefficients per block of 64 samples."""
    y = np.zeros_like(x)
    z1 = z2 = 0.0
    block = 64
    for start in range(0, len(x), block):
        f = float(freq[min(start, len(freq) - 1)])
        b = float(bw[min(start, len(bw) - 1)])
        c = -np.exp(-2.0 * np.pi * b / RATE)
        bb = 2.0 * np.exp(-np.pi * b / RATE) * np.cos(2.0 * np.pi * f / RATE)
        a = 1.0 - bb - c
        for i in range(start, min(start + block, len(x))):
            v = a * x[i] + bb * z1 + c * z2
            z2, z1 = z1, v
            y[i] = v
    return y


def voice(duration, f0_points, vowel_points, voicing_points, breath_points, gain_points, rng, vibrato=(0.0, 5.5), breathy=True):
    """One utterance. vowel_points: [(t, vowel)] morphing linearly between targets."""
    n = int(duration * RATE)
    f0 = track(f0_points, n)
    depth, rate = vibrato
    if depth:
        f0 = f0 * (1.0 + depth * np.sin(2.0 * np.pi * rate * np.arange(n) / RATE + rng.uniform(0, 6.28)))
    # Slow random wander makes the pitch less mechanical.
    wander = np.cumsum(rng.normal(0.0, 1.0, n // 480 + 2))
    f0 = f0 * (1.0 + 0.008 * np.interp(np.arange(n), np.arange(len(wander)) * 480, wander - wander.mean()) / max(1.0, np.abs(wander).max()))
    # A breathy (soft) voice keeps the folds open longer: stronger fundamental, weaker upper harmonics.
    flow, position = glottal(f0, rng, *((0.5, 0.25) if breathy else (0.38, 0.14)))
    voiced = (flow - flow.mean()) * track(voicing_points, n)
    noise = lfilter([1.0, -0.7], [1.0], rng.normal(0.0, 1.0, n))
    # Aspiration is pulsed by the opening glottis while voiced and steady while only breathing.
    pulsed = 0.45 + 0.55 * np.clip(np.sin(np.pi * np.clip(position / 0.6, 0, 1)), 0, 1)
    voicing = track(voicing_points, n)
    aspiration = noise * (pulsed * voicing + (1.0 - voicing)) * track(breath_points, n) * 0.08
    signal = voiced + aspiration
    # Cascade F1..F5 (each resonator has unity gain at DC).
    for k in range(5):
        freqs = track([(t, VOWELS[v][0][k]) for t, v in vowel_points], n)
        bws = track([(t, VOWELS[v][1][k]) for t, v in vowel_points], n)
        signal = resonator(signal, freqs, bws)
    radiated = np.diff(signal, prepend=0.0)
    # Breath "air" above the formants (the cascade stops near 5 kHz; a real breath does not).
    air = lfilter([1.0, -1.0], [1.0, -0.2], rng.normal(0.0, 1.0, n)) * track(breath_points, n)
    radiated = radiated + air * 0.06 * np.std(radiated) / max(1e-9, np.std(air))
    env = track(gain_points, n)
    # 4 ms raised-cosine edges against clicks.
    edge = int(0.004 * RATE)
    env[:edge] *= 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, edge))
    env[-edge:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, edge))
    return radiated * env


def silence(seconds):
    return np.zeros(int(seconds * RATE))


def make(kind, variant):
    # A stable seed per sound (Python's hash() of strings changes between runs).
    rng = np.random.default_rng(zlib.crc32(f'{kind}_{variant}'.encode()))
    p = 1.0 + 0.06 * (variant - 1)          # pitch spread between variants
    d = 1.0 + 0.08 * ((variant * 7) % 3 - 1)  # duration spread
    if kind == 'soft':
        return voice(0.52 * d, [(0, 285 * p), (0.3, 270 * p), (1, 225 * p)], [(0, 'a'), (0.7, 'a'), (1, 'o')],
                     [(0, 0), (0.12, 0.7), (0.6, 0.8), (1, 0.2)], [(0, 0.9), (0.15, 0.5), (1, 0.6)],
                     [(0, 0), (0.1, 0.9), (0.45, 1.0), (1, 0)], rng, vibrato=(0.008, 5.0))
    if kind == 'hum':
        return voice(0.55 * d, [(0, 225 * p), (0.45, 255 * p), (1, 235 * p)], [(0, 'm'), (1, 'm')],
                     [(0, 0.2), (0.15, 0.9), (0.8, 0.9), (1, 0.3)], [(0, 0.25), (1, 0.2)],
                     [(0, 0), (0.15, 0.8), (0.7, 1.0), (1, 0)], rng, vibrato=(0.006, 5.0))
    if kind == 'ask':
        return voice(0.42 * d, [(0, 230 * p), (0.55, 240 * p), (1, 330 * p)], [(0, 'm'), (0.6, 'm'), (1, 'e')],
                     [(0, 0.3), (0.15, 0.9), (1, 0.8)], [(0, 0.3), (1, 0.3)],
                     [(0, 0), (0.12, 0.8), (0.8, 1.0), (1, 0)], rng)
    if kind == 'startle':
        gasp = voice(0.16, [(0, 300), (1, 300)], [(0, 'a'), (1, 'e')], [(0, 0), (1, 0)], [(0, 0.2), (0.3, 1.3), (1, 0.6)],
                     [(0, 0), (0.25, 0.9), (1, 0.4)], rng)
        ah = voice(0.24 * d, [(0, 400 * p), (0.25, 380 * p), (1, 300 * p)], [(0, 'a'), (1, 'a')],
                   [(0, 0.9), (0.7, 0.9), (1, 0.3)], [(0, 0.35), (1, 0.5)], [(0, 0), (0.06, 1.0), (0.5, 0.8), (1, 0)], rng, breathy=False)
        return np.concatenate([gasp * 0.7, silence(0.02), ah])
    if kind == 'giggle':
        parts = []
        for i, f in enumerate((340, 365, 350)[: 2 + variant % 2]):
            parts.append(voice(0.085, [(0, f * p), (1, (f - 30) * p)], [(0, 'e'), (1, 'e')], [(0, 0.3), (0.3, 0.8), (1, 0.2)],
                               [(0, 1.0), (0.3, 0.6), (1, 0.7)], [(0, 0), (0.2, 1.0), (1, 0)], rng, breathy=False))
            parts.append(silence(0.055 + 0.01 * i))
        return np.concatenate(parts)
    if kind == 'sigh':
        return voice(0.75 * d, [(0, 230 * p), (1, 185 * p)], [(0, 'a'), (0.6, 'o'), (1, 'u')],
                     [(0, 0), (0.2, 0.35), (0.7, 0.3), (1, 0)], [(0, 0.6), (0.2, 1.0), (1, 0.4)],
                     [(0, 0), (0.15, 0.9), (0.5, 0.8), (1, 0)], rng)
    if kind == 'moan':
        return voice(0.9 * d, [(0, 250 * p), (0.35, 300 * p), (0.75, 285 * p), (1, 230 * p)], [(0, 'a'), (0.6, 'a'), (1, 'o')],
                     [(0, 0.2), (0.15, 0.85), (0.8, 0.8), (1, 0.2)], [(0, 0.6), (0.2, 0.35), (1, 0.5)],
                     [(0, 0), (0.12, 0.85), (0.5, 1.0), (1, 0)], rng, vibrato=(0.012, 5.5))
    raise ValueError(kind)


KINDS = ('soft', 'hum', 'ask', 'startle', 'giggle', 'sigh', 'moan')
VARIANTS = 3


def master(x):
    """DC removed, peak at -3 dBFS."""
    x = x - np.mean(x)
    peak = np.max(np.abs(x)) or 1.0
    return x / peak * 10 ** (-3 / 20)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--plot', help='spectrogram sheet of every sound (PNG)')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    manifest = []
    sounds = {}
    for kind in KINDS:
        for variant in range(1, VARIANTS + 1):
            x = master(make(kind, variant))
            name = f'{kind.capitalize()}_{variant}'
            wavfile.write(OUT / f'{name}.wav', RATE, (x * 32767).astype(np.int16))
            manifest.append({'name': name, 'kind': kind, 'variant': variant, 'seconds': round(len(x) / RATE, 3)})
            sounds[name] = x
    (OUT / 'reactions.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    if args.plot:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        fig, axes = plt.subplots(len(KINDS), VARIANTS, figsize=(15, 2.2 * len(KINDS)))
        for row, kind in enumerate(KINDS):
            for col in range(VARIANTS):
                name = f'{kind.capitalize()}_{col + 1}'
                ax = axes[row][col]
                ax.specgram(sounds[name], NFFT=1024, Fs=RATE, noverlap=768, cmap='magma', vmin=-120)
                ax.set_ylim(0, 6000)
                ax.set_title(name, fontsize=8)
                ax.tick_params(labelsize=6)
        fig.tight_layout()
        fig.savefig(args.plot, dpi=80)
    print(json.dumps(manifest))


if __name__ == '__main__':
    main()
