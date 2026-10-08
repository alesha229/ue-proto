"""Reaction voice from a recorded voice pack instead of the synthesized one (generate_reaction_voice.py).

System Python (numpy, scipy, imageio-ffmpeg; matplotlib for --plot). Cuts single vocalizations out of the pack's
MP3 takes, sorts them into the reaction kinds of DA_Gratia.ReactionLines and writes
Exports/Gratia/Audio/Reactions/<Kind>_<n>.wav (mono 48 kHz, equal loudness, -1 dBFS peak) plus reactions.json;
setup_character_presentation.py imports them. Used pack: VoxAfterHours Voice Pack (WHSFX MP3 demo, credit
https://x.com/VoxAfterHours); it is not in git and its clips are not committed (no licence text in the archive).

    python GratiaVR/Scripts/prepare_reaction_voice_pack.py "<pack folder>/Processed" [--per-kind 6] [--plot sheet.png]

Only the woman's single, wordless vocalizations: no takes with dialogue or partner, no BJ/licking takes.
"""
import argparse
import json
import subprocess
from pathlib import Path

import imageio_ffmpeg
import numpy as np
from scipy.io import wavfile
from scipy.signal import find_peaks

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Exports/Gratia/Audio/Reactions'
RATE = 48000
HOP = 0.01

# Kind -> pack takes (relative to Processed) and the allowed length of one vocalization.
KINDS = {
    'startle': (['Pack 1/Moans High/Medium High Pitched Soft Gasp Sounds 4.mp3'], 0.22, 0.9),
    'soft': (['Pack 1/Moans High/Medium-High Pitched Soft_Timid Moans 4.mp3', 'Pack 2/Moans (Mid)/Soft Moans.mp3'], 0.35, 1.4),
    'hum': (['Pack 1/Moans Mid/Medium-Low Sultry Moans 10.mp3', 'Pack 1/Moans Mid/Soft Moans and Breaths 1.mp3',
             'Pack 1/Moans Low/Deep-Voiced Sultry Teasing Moans 4.mp3'], 0.45, 1.6),
    'sigh': (['Pack 1/Moans Mid/Additional Moans 12 Soft Breathing.mp3', 'Pack 1/Moans Mid/Soft Moans and Breaths 1.mp3'], 0.5, 2.2),
    'giggle': (['Pack 1/Moans High/High-Pitched Teasing Moans and Giggles 1.mp3',
                'Pack 1/Moans Mid/Additional Moans 24 playful soft with giggles.mp3',
                'Pack 1/Moans Mid/Medium-Low Sultry Giggles and Soft Moans 7.mp3',
                'Pack 1/Moans Low/Additional Moans 26 playful soft with giggles.mp3'], 0.45, 2.2),
    'ask': (['Pack 2/Unique Sounds - Breathing - Efforts/_Ara Ara_ Sounds (Muffled).mp3'], 0.4, 2.0),
    'moan': (['Pack 2/Moans (High)/Flirtatious-Energetic Soft to Intense Moans 3.mp3', 'Pack 2/Moans (High)/Medium to Intense Moans 3.mp3',
              'Pack 2/Moans (Mid)/Medium Intense Moans 2.mp3', 'Pack 1/Moans High/Additional Moans 10.mp3'], 0.6, 2.8),
}


def decode(path):
    command = [imageio_ffmpeg.get_ffmpeg_exe(), '-v', 'error', '-i', str(path), '-ac', '1', '-ar', str(RATE), '-f', 'f32le', '-']
    return np.frombuffer(subprocess.run(command, check=True, capture_output=True).stdout, dtype=np.float32).copy()


def envelope(audio):
    size, hop = int(0.02 * RATE), int(HOP * RATE)
    frames = np.lib.stride_tricks.sliding_window_view(audio, size)[::hop]
    return 20 * np.log10(np.sqrt((frames.astype(np.float64) ** 2).mean(axis=1)) + 1e-9)


def segments(audio, low, high):
    """Vocalizations: runs above the take's noise floor, short gaps bridged, padded by 40/90 ms."""
    level = envelope(audio)
    threshold = max(np.percentile(level, 10) + 14.0, level.max() - 38.0)
    active = level > threshold
    runs, start = [], None
    for index, on in enumerate(np.append(active, False)):
        if on and start is None:
            start = index
        elif not on and start is not None:
            runs.append([start, index]); start = None
    merged = []
    for run in runs:
        if merged and (run[0] - merged[-1][1]) * HOP < 0.16:
            merged[-1][1] = run[1]
        else:
            merged.append(run)
    for a, b in merged:
        begin, end = max(0.0, a * HOP - 0.04), min(len(audio) / RATE, b * HOP + 0.09)
        if low <= end - begin <= high:
            yield begin, end


def features(clip):
    level = envelope(clip) if len(clip) > int(0.02 * RATE) else np.array([-120.0])
    peaks, _ = find_peaks(level, prominence=6.0, distance=8)
    # Pitch: autocorrelation of the loudest 40 ms, 120-700 Hz.
    centre = int(np.argmax(level) * HOP * RATE)
    window = clip[max(0, centre - 960):centre + 960]
    window = window - window.mean()
    corr = np.correlate(window, window, 'full')[len(window) - 1:] if len(window) > 200 else np.zeros(1)
    lags = slice(RATE // 700, RATE // 120)
    pitch = RATE / (lags.start + int(np.argmax(corr[lags]))) if len(corr) > lags.stop else 0.0
    voiced = float(corr[lags].max() / (corr[0] + 1e-9)) if len(corr) > lags.stop else 0.0
    spectrum = np.abs(np.fft.rfft(clip * np.hanning(len(clip)))) + 1e-9
    freqs = np.fft.rfftfreq(len(clip), 1 / RATE)
    centroid = float((spectrum * freqs).sum() / spectrum.sum())
    flatness = float(np.exp(np.log(spectrum).mean()) / spectrum.mean())
    attack = float(np.argmax(level >= level.max() - 6.0) * HOP)
    return dict(seconds=len(clip) / RATE, bursts=int(len(peaks)), pitch=round(pitch, 1), voiced=round(voiced, 3),
                centroid=round(centroid), flatness=round(flatness, 4), attack=round(attack, 3), peak=float(np.abs(clip).max()))


def score(kind, f):
    """Higher is a better example of the kind (wordless, the right shape)."""
    seconds, bursts = f['seconds'], f['bursts']
    if f['peak'] > 0.995:
        return -1e9  # clipped
    if kind == 'startle':
        return -abs(seconds - 0.45) * 4 - f['attack'] * 6 + (f['flatness'] > 0.02) * 0.5
    if kind == 'giggle':
        return (3 <= bursts <= 9) * 3 + min(bursts, 6) * 0.3 - abs(seconds - 1.1)
    if kind == 'soft':
        return -abs(seconds - 0.75) * 2 - max(0, bursts - 2) + f['voiced'] + (f['pitch'] > 230) * 0.6
    if kind == 'hum':
        return -abs(seconds - 0.9) * 1.5 - f['centroid'] / 1500 + f['voiced'] - max(0, bursts - 2) * 0.5
    if kind == 'sigh':
        return f['flatness'] * 40 - abs(seconds - 1.2) - max(0, bursts - 2) * 0.5
    if kind == 'ask':
        return -abs(seconds - 1.0) + f['voiced']
    return f['voiced'] * 2 - abs(seconds - 1.4) * 0.8 + (f['pitch'] > 220) * 0.5 - max(0, bursts - 4) * 0.3


def focus(clip, longest):
    """The loudest event alone: from just before it until it falls 24 dB (a gasp without the breath after it)."""
    level = envelope(clip)
    top = int(np.argmax(level))
    quiet = level < level[top] - 24.0
    left = top
    while left > 0 and not quiet[left] and (top - left) * HOP < 0.12:
        left -= 1
    right = top
    while right < len(level) - 1 and not quiet[right] and (right - top) * HOP < longest:
        right += 1
    begin, end = max(0, int((left * HOP - 0.03) * RATE)), int((right * HOP + 0.06) * RATE)
    return begin / RATE, min(len(clip), end) / RATE


def finish(clip):
    """Equal loudness (active RMS -18 dBFS), -1 dBFS peak ceiling, short fades."""
    level = envelope(clip)
    active = level[level > level.max() - 20.0]
    rms = 10 ** (np.mean(active) / 20) if len(active) else 1e-3
    clip = clip * (10 ** (-18 / 20) / rms)
    clip = clip * min(1.0, 10 ** (-1 / 20) / (np.abs(clip).max() + 1e-9))
    fade_in, fade_out = int(0.008 * RATE), int(0.04 * RATE)
    clip[:fade_in] *= np.linspace(0, 1, fade_in)
    clip[-fade_out:] *= np.linspace(1, 0, fade_out)
    return clip


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('pack', type=Path, help='The pack\'s Processed folder')
    parser.add_argument('--per-kind', type=int, default=6)
    parser.add_argument('--plot', type=Path)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob('*.wav'):
        old.unlink()
    manifest, chosen = [], []
    cache = {}
    for kind, (sources, low, high) in KINDS.items():
        candidates = []
        for source in sources:
            path = args.pack / source
            assert path.is_file(), f'Missing pack take: {path}'
            audio = cache.setdefault(source, decode(path))
            for begin, end in segments(audio, low, high):
                if kind == 'startle':
                    inner_begin, inner_end = focus(audio[int(begin * RATE):int(end * RATE)], 0.45)
                    begin, end = begin + inner_begin, begin + inner_end
                clip = audio[int(begin * RATE):int(end * RATE)]
                f = features(clip)
                candidates.append((score(kind, f), source, begin, end, f))
        candidates.sort(key=lambda c: -c[0])
        picked = []
        for candidate in candidates:
            # Different moments of the takes (no overlapping cuts across kinds either).
            if any(c[1] == candidate[1] and c[2] < candidate[3] and candidate[2] < c[3] for c in picked + chosen):
                continue
            picked.append(candidate)
            if len(picked) == args.per_kind:
                break
        assert len(picked) >= 3, f'Only {len(picked)} {kind} cuts in the pack'
        chosen += picked
        for variant, (value, source, begin, end, f) in enumerate(picked, 1):
            name = f'{kind.capitalize()}_{variant}'
            clip = finish(cache[source][int(begin * RATE):int(end * RATE)].copy())
            wavfile.write(OUT / f'{name}.wav', RATE, (clip * 32767).astype(np.int16))
            manifest.append(dict(name=name, kind=kind, variant=variant, seconds=round(end - begin, 3), source=source,
                                 start=round(begin, 3), pitch=f['pitch'], bursts=f['bursts'], score=round(float(value), 3)))
    (OUT / 'reactions.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'{len(manifest)} reaction takes -> {OUT}')
    for kind in KINDS:
        print(kind, [m['seconds'] for m in manifest if m['kind'] == kind])
    if args.plot:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        figure, axes = plt.subplots(len(KINDS), args.per_kind, figsize=(3 * args.per_kind, 1.6 * len(KINDS)), squeeze=False)
        for row, kind in enumerate(KINDS):
            takes = [m for m in manifest if m['kind'] == kind]
            for column in range(args.per_kind):
                axis = axes[row][column]
                axis.set_xticks([]); axis.set_yticks([])
                if column < len(takes):
                    rate, data = wavfile.read(OUT / f"{takes[column]['name']}.wav")
                    axis.plot(np.arange(len(data)) / rate, data / 32767, linewidth=0.4)
                    axis.set_ylim(-1, 1)
                    axis.set_title(f"{takes[column]['name']} {takes[column]['seconds']}s f0 {takes[column]['pitch']:.0f}", fontsize=7)
        figure.tight_layout()
        figure.savefig(args.plot, dpi=90)


if __name__ == '__main__':
    main()
