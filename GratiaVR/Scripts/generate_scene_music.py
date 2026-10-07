"""Generate the scene music (original, deterministic) and its light analysis.

System Python with numpy/scipy (development tooling only; the game plays the imported WAVs).
Each track is a seamless loop: the reverb/delay tail of the last bars wraps onto the start.
The analysis (30 frames/s: bass, mids, highs, beat) is measured from the rendered audio; the
beat pulse comes from the score's own kick times, so lights hit exactly on the drum.

    python generate_scene_music.py <out_dir>
"""
import json
import sys
from pathlib import Path

import numpy as np
from scipy import signal

SR = 44100
RNG = np.random.default_rng(466)


def midi(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def adsr(n, a, d, s, r, sustain_len=None):
    a, d, r = int(a * SR), int(d * SR), int(r * SR)
    hold = max(0, (n if sustain_len is None else int(sustain_len * SR)) - a - d)
    env = np.concatenate([np.linspace(0, 1, max(1, a), endpoint=False), np.linspace(1, s, max(1, d), endpoint=False),
                          np.full(hold, s), np.linspace(s, 0, max(1, r))])
    out = np.zeros(n)
    out[:min(n, len(env))] = env[:n]
    return out


def saw(freq, n, phase=0.0):
    # Band-limited sawtooth (additive up to Nyquist).
    t = np.arange(n) / SR
    out = np.zeros(n)
    k = 1
    while k * freq < SR / 2 and k < 60:
        out += np.sin(2 * np.pi * k * freq * t + phase * k) / k
        k += 1
    return out * (2 / np.pi)


def square(freq, n):
    t = np.arange(n) / SR
    out = np.zeros(n)
    k = 1
    while k * freq < SR / 2 and k < 40:
        out += np.sin(2 * np.pi * k * freq * t) / k
        k += 2
    return out * (4 / np.pi)


def lowpass(x, cutoff, q=0.707):
    b, a = signal.iirfilter(2, min(0.99, cutoff / (SR / 2)), btype='low', ftype='butter')
    return signal.lfilter(b, a, x)


def highpass(x, cutoff):
    b, a = signal.iirfilter(2, min(0.99, cutoff / (SR / 2)), btype='high', ftype='butter')
    return signal.lfilter(b, a, x)


def bandpass(x, low, high):
    b, a = signal.iirfilter(2, [low / (SR / 2), min(0.99, high / (SR / 2))], btype='band', ftype='butter')
    return signal.lfilter(b, a, x)


def fm_piano(freq, n, dur):
    # Electric piano: carrier with a decaying 1:1 modulator plus a soft bell partial.
    t = np.arange(n) / SR
    index = 2.2 * np.exp(-t * 3.0)
    tone = np.sin(2 * np.pi * freq * t + index * np.sin(2 * np.pi * freq * t))
    tone += 0.25 * np.sin(2 * np.pi * freq * 14 * t) * np.exp(-t * 18)
    return tone * adsr(n, 0.004, 0.6, 0.35, 0.5, dur) * np.exp(-t * 0.8)


def fm_bell(freq, n):
    t = np.arange(n) / SR
    index = 3.0 * np.exp(-t * 2.5)
    return np.sin(2 * np.pi * freq * t + index * np.sin(2 * np.pi * freq * 3.5 * t)) * np.exp(-t * 2.2)


def kick(n, punch=1.0):
    t = np.arange(n) / SR
    f = 46 + 110 * np.exp(-t * 32)
    phase = 2 * np.pi * np.cumsum(f) / SR
    return np.sin(phase) * np.exp(-t * 7.5) * punch + 0.15 * RNG.standard_normal(n) * np.exp(-t * 120)


def snare(n):
    t = np.arange(n) / SR
    noise = bandpass(RNG.standard_normal(n), 900, 9000) * np.exp(-t * 14)
    body = np.sin(2 * np.pi * 190 * t) * np.exp(-t * 22)
    return 0.8 * noise + 0.5 * body


def hat(n, open_=False):
    t = np.arange(n) / SR
    return highpass(RNG.standard_normal(n), 7000) * np.exp(-t * (9 if open_ else 55))


def rim(n):
    t = np.arange(n) / SR
    return bandpass(RNG.standard_normal(n), 1500, 5000) * np.exp(-t * 60) + 0.4 * np.sin(2 * np.pi * 820 * t) * np.exp(-t * 50)


def impulse_response(seconds, damping):
    n = int(seconds * SR)
    t = np.arange(n) / SR
    ir = np.stack([RNG.standard_normal(n), RNG.standard_normal(n)]) * np.exp(-t * (6.9 / seconds))
    ir = np.stack([lowpass(channel, damping) for channel in ir])
    ir[:, :int(0.012 * SR)] *= np.linspace(0, 1, int(0.012 * SR))
    return ir / np.sqrt(np.sum(ir ** 2, axis=1, keepdims=True))


class Track:
    def __init__(self, bpm, bars):
        self.bpm = bpm
        self.beat = 60.0 / bpm
        self.length = int(round(bars * 4 * self.beat * SR))
        self.tail = int(6 * SR)
        self.buses = {}
        self.kicks = []

    def bus(self, name):
        if name not in self.buses:
            self.buses[name] = np.zeros((2, self.length + self.tail))
        return self.buses[name]

    def add(self, name, start_beat, sound, pan=0.0, gain=1.0):
        start = int(round(start_beat * self.beat * SR))
        bus = self.bus(name)
        end = min(bus.shape[1], start + len(sound))
        left, right = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
        bus[0, start:end] += sound[:end - start] * gain * left * 1.414
        bus[1, start:end] += sound[:end - start] * gain * right * 1.414

    def sidechain(self, depth, release=0.22):
        # Ducking envelope from the kicks (pads and bass breathe with the drum).
        env = np.ones(self.length + self.tail)
        for beat in self.kicks:
            start = int(beat * self.beat * SR)
            n = int(release * SR)
            curve = 1 - depth * np.exp(-np.arange(n) / SR * (5 / release))
            end = min(len(env), start + n)
            env[start:end] = np.minimum(env[start:end], curve[:end - start])
        return env

    def render(self, mix, reverb_seconds=2.2, reverb_damp=6000):
        ir = impulse_response(reverb_seconds, reverb_damp)
        dry = np.zeros((2, self.length + self.tail))
        wet_send = np.zeros_like(dry)
        for name, (gain, send, duck) in mix.items():
            if name not in self.buses:
                continue
            bus = self.buses[name] * gain
            if duck:
                bus = bus * self.sidechain(duck)
            dry += bus
            wet_send += bus * send
        wet = np.stack([signal.fftconvolve(wet_send[c], ir[c])[:dry.shape[1]] for c in range(2)])
        out = dry + wet
        # Seamless loop: the tail past the end wraps onto the start.
        loop = out[:, :self.length].copy()
        loop[:, :self.tail] += out[:, self.length:self.length + self.tail]
        loop = np.stack([highpass(channel, 28) for channel in loop])
        # Gentle bus compression (soft knee) and loudness to ~-14 dB RMS, peaks below -1 dBFS.
        rms = np.sqrt(np.mean(loop ** 2))
        loop *= 0.2 / max(rms, 1e-9)
        loop = np.tanh(loop * 1.2) / np.tanh(1.2)
        loop *= 0.89 / max(np.max(np.abs(loop)), 1e-9) if np.max(np.abs(loop)) > 0.89 else 1.0
        return loop


def chord_tones(root, kind):
    shapes = {'min': (0, 3, 7), 'maj': (0, 4, 7), 'min9': (0, 3, 7, 10, 14), 'maj9': (0, 4, 7, 11, 14),
              'dom13': (0, 4, 10, 14, 21), 'sus2': (0, 2, 7), 'add9': (0, 4, 7, 14)}
    return [root + step for step in shapes[kind]]


def neon_drive(soft=False):
    """Synthwave, 100 BPM, Am - F - C - G (2 bars each)."""
    track = Track(90 if soft else 100, 32)
    progression = [(57, 'min'), (53, 'maj'), (48, 'maj'), (55, 'maj')]
    for bar in range(32):
        root, kind = progression[(bar // 2) % 4]
        section = bar // 8  # intro, verse, chorus, outro
        # Pads: three detuned saws per chord tone, one chord per 2 bars.
        if bar % 2 == 0:
            n = int(8 * track.beat * SR)
            pad = np.zeros(n)
            for note in chord_tones(root, kind):
                for detune in (-0.08, 0.0, 0.08):
                    pad += saw(midi(note + 12 + detune), n, RNG.uniform(0, 6))
            pad = lowpass(pad, 1800 if soft else 2600) * adsr(n, 0.9, 0.5, 0.8, 1.2, 7.5 * track.beat) * 0.07
            track.add('pad', bar * 4, pad, pan=-0.2)
            track.add('pad', bar * 4 + 0.02, pad, pan=0.2, gain=0.9)
        # Bass: pulsing eighths on the root.
        if section > 0 or soft:
            for eighth in range(8):
                n = int(0.45 * track.beat * SR)
                note = root - 12 + (12 if eighth % 4 == 3 else 0)
                tone = lowpass(saw(midi(note), n), 520 + 380 * (eighth % 2)) * adsr(n, 0.004, 0.12, 0.6, 0.05)
                track.add('bass', bar * 4 + eighth * 0.5, tone, gain=0.35 if soft else 0.45)
        # Arpeggio: sixteenths over the chord, plucky square, ping-pong.
        if section in (1, 2) or (soft and section != 3):
            tones = chord_tones(root, kind) + [chord_tones(root, kind)[0] + 12]
            for step in range(16):
                n = int(0.3 * track.beat * SR)
                note = tones[(step * 3 + bar) % len(tones)] + 12
                tone = lowpass(square(midi(note), n), 3200) * adsr(n, 0.002, 0.09, 0.15, 0.06)
                track.add('arp', bar * 4 + step * 0.25, tone, pan=-0.5 if step % 2 else 0.5, gain=0.09)
                track.add('arp', bar * 4 + step * 0.25 + 0.75, tone, pan=0.5 if step % 2 else -0.5, gain=0.035)
        # Drums.
        if section > 0 and not (soft and section == 3):
            for beat in range(4):
                position = bar * 4 + beat
                track.add('kick', position, kick(int(0.5 * SR), 0.8 if soft else 1.0), gain=0.85)
                track.kicks.append(position)
                if not soft:
                    if beat % 2 == 1:
                        track.add('snare', position, snare(int(0.4 * SR)), gain=0.5)
                    for half in (0, 0.5):
                        track.add('hat', position + half, hat(int(0.08 * SR)), pan=0.3, gain=0.12 if half else 0.07)
                    if section == 2:
                        track.add('hat', position + 0.5, hat(int(0.35 * SR), True), pan=-0.3, gain=0.07)
                elif beat % 2 == 1:
                    track.add('snare', position, rim(int(0.15 * SR)), gain=0.25)
    mix = {'pad': (1.0, 0.35, 0.55), 'bass': (1.0, 0.05, 0.6), 'arp': (1.0, 0.45, 0.3),
           'kick': (1.0, 0.03, 0), 'snare': (1.0, 0.4, 0), 'hat': (1.0, 0.1, 0)}
    return track, track.render(mix, 2.6, 5200)


def velvet_night():
    """Slow R&B, 76 BPM: Dm9 - G13 - Cmaj9 - Am9, electric piano, sub bass, soft kit, swing."""
    track = Track(76, 24)
    progression = [(50, 'min9'), (55, 'dom13'), (48, 'maj9'), (57, 'min9')]
    swing = 0.06
    for bar in range(24):
        root, kind = progression[bar % 4]
        n = int(4 * track.beat * SR)
        for index, note in enumerate(chord_tones(root, kind)):
            strum = index * 0.025
            piano = fm_piano(midi(note + 12), n, 3.6 * track.beat)
            track.add('keys', bar * 4 + strum, piano, pan=-0.35 + 0.18 * index, gain=0.09)
            if bar % 2:
                track.add('keys', bar * 4 + 2.5 + strum, fm_piano(midi(note + 12), n // 2, 1.2 * track.beat), pan=0.3 - 0.15 * index, gain=0.05)
        sub_n = int(1.9 * track.beat * SR)
        for beat, note in ((0, root - 24), (2.5, root - 24 + (7 if bar % 2 else 5))):
            t = np.arange(sub_n) / SR
            tone = np.sin(2 * np.pi * midi(note) * t) + 0.25 * np.sin(4 * np.pi * midi(note) * t)
            track.add('bass', bar * 4 + beat, tone * adsr(sub_n, 0.01, 0.3, 0.7, 0.2), gain=0.5)
        if bar >= 2:
            for beat in (0, 2.75):
                track.add('kick', bar * 4 + beat, kick(int(0.45 * SR), 0.75), gain=0.8)
                track.kicks.append(bar * 4 + beat)
            for beat in (1, 3):
                track.add('snare', bar * 4 + beat, rim(int(0.3 * SR)) * 0.8 + snare(int(0.3 * SR)) * 0.25, gain=0.45)
            for eighth in range(8):
                offset = eighth * 0.5 + (swing if eighth % 2 else 0)
                track.add('hat', bar * 4 + offset, hat(int(0.06 * SR)), pan=0.25, gain=0.07 if eighth % 2 else 0.1)
    crackle = np.zeros(track.length)
    pops = RNG.integers(0, track.length, size=int(track.length / SR * 9))
    crackle[pops] = RNG.uniform(-0.6, 0.6, size=len(pops))
    track.add('vinyl', 0, highpass(crackle, 2500) + 0.004 * highpass(RNG.standard_normal(track.length), 3000), gain=0.6)
    mix = {'keys': (1.0, 0.5, 0.25), 'bass': (1.0, 0.0, 0.35), 'kick': (1.0, 0.05, 0), 'snare': (1.0, 0.35, 0),
           'hat': (1.0, 0.15, 0), 'vinyl': (1.0, 0.0, 0)}
    return track, track.render(mix, 2.0, 4200)


def moonlight():
    """Ambient, 64 BPM: slow pads, FM bells on a pentatonic line, a soft pulse on the beat."""
    track = Track(64, 16)
    progression = [(52, 'sus2'), (48, 'add9'), (55, 'sus2'), (50, 'add9')]
    scale = [64, 67, 69, 71, 74, 76, 79, 81]
    for bar in range(16):
        root, kind = progression[(bar // 2) % 4]
        if bar % 2 == 0:
            n = int(8 * track.beat * SR)
            pad = np.zeros(n)
            for note in chord_tones(root, kind):
                for detune in (-0.06, 0.06):
                    pad += saw(midi(note + 12 + detune), n, RNG.uniform(0, 6))
            pad = lowpass(pad, 1200) * adsr(n, 2.0, 1.0, 0.85, 2.5, 7.2 * track.beat) * 0.06
            track.add('pad', bar * 4, pad, pan=-0.3)
            track.add('pad', bar * 4 + 0.05, pad, pan=0.3)
        for beat in range(4):
            if RNG.random() < 0.55:
                note = scale[RNG.integers(0, len(scale))]
                track.add('bell', bar * 4 + beat + RNG.choice([0, 0.5]), fm_bell(midi(note), int(3 * SR)), pan=RNG.uniform(-0.6, 0.6), gain=0.08)
            pulse_n = int(0.6 * SR)
            t = np.arange(pulse_n) / SR
            pulse = np.sin(2 * np.pi * 55 * t) * np.exp(-t * 6)
            track.add('pulse', bar * 4 + beat, pulse, gain=0.22 if bar >= 2 else 0.1)
            track.kicks.append(bar * 4 + beat)
    mix = {'pad': (1.0, 0.6, 0.15), 'bell': (1.0, 0.7, 0), 'pulse': (1.0, 0.1, 0)}
    return track, track.render(mix, 4.5, 3800)


def analyse(track, audio, fps=30):
    mono = audio.mean(axis=0)
    hop = SR // fps
    frames = len(mono) // hop
    window = np.hanning(2048)
    freqs = np.fft.rfftfreq(2048, 1 / SR)
    bands = [(30, 160), (160, 2200), (2200, 12000)]
    energy = np.zeros((frames, 3))
    padded = np.concatenate([mono, mono[:2048]])  # the loop continues past the end
    for frame in range(frames):
        chunk = padded[frame * hop:frame * hop + 2048] * window
        spectrum = np.abs(np.fft.rfft(chunk))
        for band, (low, high) in enumerate(bands):
            energy[frame, band] = np.sqrt(np.mean(spectrum[(freqs >= low) & (freqs < high)] ** 2))
    energy = energy / np.maximum(np.percentile(energy, 95, axis=0), 1e-9)
    energy = np.clip(energy, 0, 1) ** 1.2
    beat = np.zeros(frames)
    for kick_beat in track.kicks:
        start = kick_beat * track.beat
        for frame in range(int(start * fps), min(frames, int((start + 0.5) * fps) + 1)):
            beat[frame] = max(beat[frame], np.exp(-(frame / fps - start) * 9))
    return [[round(float(x), 4) for x in (*energy[frame], beat[frame])] for frame in range(frames)]


def write_wav(path, audio):
    from scipy.io import wavfile
    wavfile.write(path, SR, (np.clip(audio.T, -1, 1) * 32767).astype(np.int16))


if __name__ == '__main__':
    out = Path(sys.argv[1] if len(sys.argv) > 1 else '.')
    out.mkdir(parents=True, exist_ok=True)
    report = {}
    for name, make in (('GratiaLobby', lambda: neon_drive(soft=True)), ('GratiaNeon', neon_drive),
                       ('GratiaVelvet', velvet_night), ('GratiaMoon', moonlight)):
        track, audio = make()
        write_wav(out / f'{name}.wav', audio)
        frames = analyse(track, audio)
        (out / f'{name}.analysis.json').write_text(json.dumps({'bpm': track.bpm, 'fps': 30, 'frames': frames}), encoding='utf-8')
        report[name] = {'seconds': round(audio.shape[1] / SR, 2), 'bpm': track.bpm, 'peak': round(float(np.max(np.abs(audio))), 3),
                        'rms_db': round(float(20 * np.log10(np.sqrt(np.mean(audio ** 2)))), 1)}
        print(name, report[name], flush=True)
    (out / 'music_report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
