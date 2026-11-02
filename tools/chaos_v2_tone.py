#!/usr/bin/env python3
"""Generate a new low-level, bounded A2DP test tone (no device contact)."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import wave

RATE = 48000
SECONDS = 10
PEAK_DBFS = -26
PEAK = round(32767 * 10 ** (PEAK_DBFS / 20))
FADE_FRAMES = round(RATE * 0.02)


def amplitude(second):
    if 0 <= second < 4:
        return 0, 440.0, second, 4 - second
    if 5 <= second < 9:
        return 1, 880.0, second - 5, 9 - second
    return None, 0.0, 0.0, 0.0


def generate(path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        block = bytearray()
        for frame in range(RATE * SECONDS):
            second = frame / RATE
            channel, freq, elapsed, remaining = amplitude(second)
            left = right = 0
            if channel is not None:
                gain = min(1.0, elapsed * RATE / FADE_FRAMES,
                           remaining * RATE / FADE_FRAMES)
                sample = round(PEAK * gain * math.sin(2 * math.pi * freq * elapsed))
                if channel == 0:
                    left = sample
                else:
                    right = sample
            block.extend(struct.pack("<hh", left, right))
            if len(block) >= 32768:
                wav.writeframesraw(block)
                block.clear()
        if block:
            wav.writeframesraw(block)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    with wave.open(str(path), "rb") as wav:
        assert wav.getnchannels() == 2 and wav.getsampwidth() == 2
        assert wav.getframerate() == RATE and wav.getnframes() == RATE * SECONDS
        samples = struct.iter_unpack("<hh", wav.readframes(RATE * SECONDS))
        observed = 0
        crosstalk = 0
        for index, (left, right) in enumerate(samples):
            observed = max(observed, abs(left), abs(right))
            if index < 4 * RATE and right or 5 * RATE <= index < 9 * RATE and left:
                crosstalk += 1
        assert crosstalk == 0 and observed <= PEAK
    manifest = {"format": "RIFF/WAVE PCM16 stereo 48000 Hz", "duration_seconds": SECONDS,
                "peak_dbfs_limit": PEAK_DBFS, "observed_peak_integer": observed,
                "channels": "0-4 s LEFT 440 Hz; 4-5 s silence; 5-9 s RIGHT 880 Hz; 9-10 s silence",
                "fade_ms": 20, "channel_leak_samples": crosstalk,
                "size_bytes": path.stat().st_size, "sha256": digest}
    path.with_suffix(".manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path,
                   default=Path.home() / "voxone-hil" / "chaos-v2" / "bt-tone-10s.wav")
    args = p.parse_args()
    print(json.dumps(generate(args.output), indent=2))


if __name__ == "__main__":
    main()
