"""Convert user-supplied recordings without modifying the AAC originals.

Run with the engine's Python, using the project's existing audio dependencies.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.work/python_combat311'))
import numpy as np
import soundfile as sf
import imageio_ffmpeg

SOURCE = ROOT / 'audio/shotgun/done'
OUT = ROOT / 'SourceArt/Audio'
OUT.mkdir(parents=True, exist_ok=True)
RATE = 48000
records = {}


def decode(path):
    process = subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), '-v', 'error', '-i', str(path),
                              '-f', 'f32le', '-ar', str(RATE), '-ac', '1', '-'],
                             check=True, capture_output=True)
    return np.frombuffer(process.stdout, dtype='<f4').copy()


def write(name, source, start=0, end=None, peak=.7):
    data = decode(source)
    data = data[int(start * RATE):int(end * RATE) if end else len(data)]
    # Remove encoder lead-in, retain 4 ms ahead of the attack and the full tail.
    audible = np.flatnonzero(np.abs(data) > .001)
    if len(audible):
        data = data[max(0, audible[0] - 192):]
    data *= peak / max(float(np.abs(data).max()), 1e-9)
    fade = min(96, len(data) // 8)
    data[:fade] *= np.linspace(0, 1, fade)
    data[-fade:] *= np.linspace(1, 0, fade)
    sf.write(OUT / (name + '.wav'), data, RATE, subtype='PCM_24')
    records[name] = {'source': source.relative_to(ROOT).as_posix(),
                     'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                     'start': start, 'end': end, 'seconds': len(data) / RATE,
                     'peak': peak, 'processing': 'mono 48 kHz PCM24, lead-in trim, gain, 2 ms fades'}


for i in range(1, 5):
    write('ShotgunFire' + str(i), SOURCE / f'shot_{i}.aac')
write('ShellLoad', SOURCE / 'loading.aac', peak=.45)
write('Pump', SOURCE / 'reloading.aac', peak=.5)
write('HitBody', next(SOURCE.glob('*Body Hit*.aac')), peak=.5)
write('HitWall', next(SOURCE.glob('*Ricochet*.aac')), peak=.45)
combined = next(SOURCE.glob('*Whiz*.aac'))
signal = decode(combined)
# The recording contains a passing sweep followed by a short impact. Find the
# strongest 8 ms transient after the lead-in, keeping the impact out of Whiz.
window = int(RATE * .008)
energy = np.convolve(signal * signal, np.ones(window) / window, mode='same')
begin = int(RATE * .12)
split = max(.12, (int(np.argmax(energy[begin:])) + begin) / RATE - .012)
write('BulletWhiz', combined, end=split, peak=.28)
write('HitSoft', combined, start=split, peak=.4)
(OUT / 'combat_provenance.json').write_text(json.dumps({
    'source_note': 'Recordings supplied by the project owner; no inferred licence.',
    'assets': records}, indent=2, ensure_ascii=False), encoding='utf-8')
print(json.dumps(records, indent=2))
