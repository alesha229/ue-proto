"""Prepare the player's own music for the scenes: smart EQ, loudness, spatial channels, analysis.

System Python (numpy/scipy, ffmpeg from imageio-ffmpeg); development tooling only.
For each source file:
  * smart EQ: the long-term 1/3-octave spectrum is compared with a reference mix slope and a
    gentle linear-phase correction (half the difference, at most +-4 dB) is applied;
  * loudness: ITU-R BS.1770 gated loudness to TARGET_LUFS, look-ahead limiter at -1 dBFS;
  * spatial: left/right mono files for two speakers in the environment (true stereo in space);
  * analysis at 30 frames/s: bass, mids, highs (0..1) and a beat pulse from tracked beats
    (spectral-flux onsets, autocorrelation tempo, dynamic-programming beat tracking).

The tracks are the player's files: outputs stay out of git, personal use only.

    python prepare_user_music.py <out_dir> <slug>=<file.mp3> ...
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from scipy import ndimage, signal
from scipy.io import wavfile

SR = 44100
TARGET_LUFS = -16.0
CEILING = 10 ** (-1.0 / 20)


def ffmpeg_exe():
    import imageio_ffmpeg
    return imageio_ffmpeg.get_ffmpeg_exe()


def decode(path):
    raw = subprocess.run([ffmpeg_exe(), '-v', 'error', '-i', str(path), '-f', 'f32le', '-ac', '2', '-ar', str(SR), '-'],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32).reshape(-1, 2).T.astype(np.float64)


def k_weight(x):
    # BS.1770 pre-filter: high shelf (+4 dB above ~1.5 kHz) then a 38 Hz high-pass.
    fs = SR
    gain, q, fc = 4.0, 1 / np.sqrt(2), 1500.0
    a = 10 ** (gain / 40)
    w0 = 2 * np.pi * fc / fs
    alpha = np.sin(w0) / (2 * q)
    b = [a * ((a + 1) + (a - 1) * np.cos(w0) + 2 * np.sqrt(a) * alpha), -2 * a * ((a - 1) + (a + 1) * np.cos(w0)),
         a * ((a + 1) + (a - 1) * np.cos(w0) - 2 * np.sqrt(a) * alpha)]
    aa = [(a + 1) - (a - 1) * np.cos(w0) + 2 * np.sqrt(a) * alpha, 2 * ((a - 1) - (a + 1) * np.cos(w0)),
          (a + 1) - (a - 1) * np.cos(w0) - 2 * np.sqrt(a) * alpha]
    x = signal.lfilter(b, aa, x, axis=-1)
    w0 = 2 * np.pi * 38.0 / fs
    alpha = np.sin(w0) / (2 * 0.5)
    b = [(1 + np.cos(w0)) / 2, -(1 + np.cos(w0)), (1 + np.cos(w0)) / 2]
    aa = [1 + alpha, -2 * np.cos(w0), 1 - alpha]
    return signal.lfilter(b, aa, x, axis=-1)


def loudness(x):
    y = k_weight(x)
    block, step = int(0.4 * SR), int(0.1 * SR)
    powers = []
    for start in range(0, y.shape[1] - block, step):
        powers.append(np.sum(np.mean(y[:, start:start + block] ** 2, axis=1)))
    powers = np.array(powers)
    levels = -0.691 + 10 * np.log10(np.maximum(powers, 1e-12))
    gated = powers[levels > -70]
    relative = -0.691 + 10 * np.log10(np.mean(gated)) - 10
    gated = powers[levels > max(-70, relative)]
    return float(-0.691 + 10 * np.log10(np.mean(gated)))


def third_octaves():
    return 1000 * 2 ** (np.arange(-14, 13) / 3)  # 62.5 Hz .. 16 kHz


def smart_eq(x):
    # Long-term average spectrum in 1/3-octave bands (Welch), level relative to 1 kHz.
    freqs, psd = signal.welch(x.mean(axis=0), SR, nperseg=8192)
    centres = third_octaves()
    measured = []
    for centre in centres:
        band = (freqs >= centre / 2 ** (1 / 6)) & (freqs < centre * 2 ** (1 / 6))
        measured.append(10 * np.log10(np.mean(psd[band]) + 1e-20))
    measured = np.array(measured)
    # Reference: a balanced modern mix falls about 4.5 dB per octave across the band.
    target = -4.5 * np.log2(centres / 1000)
    offset = np.median(measured - target)
    difference = (target + offset) - measured
    correction = np.clip(ndimage.uniform_filter1d(difference, 3, mode='nearest') * 0.5, -4.0, 4.0)
    # Linear-phase FIR through the correction (flat outside the measured range).
    points = np.concatenate([[0], centres, [SR / 2]])
    gains_db = np.concatenate([[correction[0]], correction, [correction[-1]]])
    taps = signal.firwin2(4097, points / (SR / 2), 10 ** (gains_db / 20))
    delay = len(taps) // 2
    out = np.stack([signal.fftconvolve(channel, taps)[delay:delay + x.shape[1]] for channel in x])
    return out, dict(zip([round(float(c)) for c in centres], [round(float(g), 2) for g in correction]))


def limit(x):
    # Look-ahead peak limiter: gain from the 5 ms peak window, released over 80 ms.
    lookahead = int(0.005 * SR)
    peak = ndimage.maximum_filter1d(np.max(np.abs(x), axis=0), lookahead * 2 + 1)
    gain = np.minimum(1.0, CEILING / np.maximum(peak, 1e-9))
    release = np.exp(-1 / (0.08 * SR))
    smoothed = signal.lfilter([1 - release], [1, -release], gain[::-1])[::-1]
    gain = np.minimum(gain, smoothed)
    return np.clip(x * gain, -CEILING, CEILING)


def track_beats(x):
    mono = x.mean(axis=0)
    hop = 512
    _, _, spec = signal.stft(mono, SR, nperseg=2048, noverlap=2048 - hop, boundary=None)
    magnitude = np.log1p(100 * np.abs(spec))
    flux = np.maximum(0, np.diff(magnitude, axis=1)).sum(axis=0)
    flux = flux - ndimage.uniform_filter1d(flux, 32)
    onset = np.maximum(flux, 0)
    onset /= np.max(onset) + 1e-9
    fps = SR / hop
    # Tempo: autocorrelation weighted by a log-normal prior around 120 BPM.
    ac = np.correlate(onset, onset, mode='full')[len(onset) - 1:]
    lags = np.arange(len(ac))
    bpm = 60 * fps / np.maximum(lags, 1)
    prior = np.exp(-0.5 * (np.log2(bpm / 120) / 0.9) ** 2)
    valid = (bpm >= 60) & (bpm <= 190)
    lag = int(lags[valid][np.argmax((ac * prior)[valid])])
    tempo = 60 * fps / lag
    # Dynamic-programming beat tracking (Ellis 2007).
    score = onset.copy()
    back = np.full(len(onset), -1)
    window = np.arange(-2 * lag, -lag // 2)
    penalty = -100 * np.log(np.maximum(-window / lag, 1e-9)) ** 2
    for t in range(len(onset)):
        candidates = t + window
        ok = candidates >= 0
        if not ok.any():
            continue
        values = score[candidates[ok]] + penalty[ok]
        best = int(np.argmax(values))
        score[t] = onset[t] + values[best]
        back[t] = candidates[ok][best]
    t = int(np.argmax(score[-lag:]) + len(score) - lag)
    beats = []
    while t >= 0:
        beats.append(t / fps)
        t = back[t]
    return float(tempo), sorted(beats)


def analyse(x, beats, fps=30):
    mono = x.mean(axis=0)
    hop = SR // fps
    frames = len(mono) // hop
    freqs = np.fft.rfftfreq(2048, 1 / SR)
    window = np.hanning(2048)
    bands = [(30, 160), (160, 2200), (2200, 12000)]
    energy = np.zeros((frames, 3))
    padded = np.concatenate([mono, np.zeros(2048)])
    for frame in range(frames):
        spectrum = np.abs(np.fft.rfft(padded[frame * hop:frame * hop + 2048] * window))
        for band, (low, high) in enumerate(bands):
            energy[frame, band] = np.sqrt(np.mean(spectrum[(freqs >= low) & (freqs < high)] ** 2))
    energy = np.clip(energy / np.maximum(np.percentile(energy, 95, axis=0), 1e-9), 0, 1) ** 1.2
    pulse = np.zeros(frames)
    for beat in beats:
        for frame in range(int(beat * fps), min(frames, int((beat + 0.5) * fps) + 1)):
            pulse[frame] = max(pulse[frame], np.exp(-(frame / fps - beat) * 9))
    # The pulse is louder on strong (bass-heavy) beats.
    pulse *= 0.55 + 0.45 * energy[:, 0]
    return [[round(float(v), 4) for v in (*energy[frame], pulse[frame])] for frame in range(frames)]


def write(path, audio):
    wavfile.write(path, SR, (np.clip(audio.T if audio.ndim == 2 else audio, -1, 1) * 32767).astype(np.int16))


if __name__ == '__main__':
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    report = {}
    for item in sys.argv[2:]:
        slug, source = item.split('=', 1)
        audio = decode(source)
        before = loudness(audio)
        audio, curve = smart_eq(audio)
        audio *= 10 ** ((TARGET_LUFS - loudness(audio)) / 20)
        audio = limit(audio)
        after = loudness(audio)
        tempo, beats = track_beats(audio)
        write(out / f'{slug}.wav', audio)
        write(out / f'{slug}_L.wav', audio[0])
        write(out / f'{slug}_R.wav', audio[1])
        frames = analyse(audio, beats)
        # Mix points for smart transitions: the first strong beat (intro skipped when the track
        # starts quietly) and the last beat at least 8 beats before the end.
        bass = np.array([f[0] for f in frames])
        strong = [b for b in beats if bass[min(len(bass) - 1, int(b * 30))] > 0.35]
        intro = strong[0] if strong and strong[0] < 20 else (beats[0] if beats else 0.0)
        period = 60 / tempo
        outro = max([b for b in beats if b < audio.shape[1] / SR - 8 * period] or [audio.shape[1] / SR - 8])
        (out / f'{slug}.analysis.json').write_text(json.dumps({'bpm': round(tempo, 1), 'fps': 30, 'frames': frames,
            'beats': [round(b, 3) for b in beats], 'intro': round(intro, 3), 'outro': round(outro, 3)}), encoding='utf-8')
        report[slug] = {'source': Path(source).name, 'seconds': round(audio.shape[1] / SR, 1), 'lufs_before': round(before, 1),
                        'lufs_after': round(after, 1), 'bpm': round(tempo, 1), 'beats': len(beats), 'eq_db': curve,
                        'mean_bass': round(float(np.mean([f[0] for f in frames])), 3), 'mean_high': round(float(np.mean([f[2] for f in frames])), 3)}
        print(slug, {k: v for k, v in report[slug].items() if k != 'eq_db'}, flush=True)
    (out / 'user_music_report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
