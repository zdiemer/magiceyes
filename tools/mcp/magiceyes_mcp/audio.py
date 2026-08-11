"""Audio analysis: turn PCM into numbers and a picture, because I cannot listen to it.

Source is the engine-side ME_AUDIO_DUMP tap (devices.c), not the shm ring. That matters: dsp_write
never blocks, so when a consumer falls behind it drops the oldest samples -- polling the ring makes
"is the BGM actually broken, or is my capture lossy?" unanswerable. The tap captures what the game
produced, before the drop.

The metrics name specific failure modes rather than just describing the waveform:
  silence / near-silence        -> game never opened the device, or mixes to nothing
  clipping                      -> gain staging or a format mismatch (U8 read as S16, etc.)
  roughness + zero-crossing rate-> the reliable "radio static" detectors. Roughness is
                                   mean|adjacent-sample-change| / rms, so it is scale-free.
                                   Calibrated at 22 kHz: white noise ~1.15 roughness / ~0.50 zcr,
                                   a 440 Hz tone ~0.11 / ~0.04, real BGM below that.
  spectral flatness ABOVE 100Hz -> corroborates noise. Deliberately excludes the sub-bass bins:
                                   computed across the whole spectrum, one large low-frequency
                                   bin inflates the arithmetic mean and drives flatness to ~0,
                                   reporting "strongly tonal" for a signal that is broadband hiss
                                   sitting on a rumble. That mistake made this module call a
                                   visibly wrong Her Knights capture "normal audio".
  dominant peak below ~80Hz     -> not musically possible for these titles: the stream is being
                                   consumed too slowly, or the negotiated rate/format does not
                                   match what the game is writing.
  high discontinuity rate       -> gross byte garbage; a blunt check that misses subtler noise,
                                   so it is never the only signal relied on.
  channel imbalance             -> a downmix/stride bug
"""
from __future__ import annotations

import math
import struct
import wave
from pathlib import Path

import numpy as np

from .screen import _png


def _read_meta(pcm_path: Path) -> dict:
    meta = {"freq": 44100, "channels": 2, "bits": 16}
    mp = Path(str(pcm_path) + ".meta")
    try:
        for line in mp.read_text().splitlines():
            k, _, v = line.partition(" ")
            if k in ("freq", "channels", "bits"):
                meta[k] = int(v)
    except (OSError, ValueError):
        pass
    return meta


def load(pcm_path: Path, max_secs: float = 30.0, tail: bool = True):
    """Load PCM as float32 in [-1,1], shaped (frames, channels)."""
    meta = _read_meta(pcm_path)
    raw = pcm_path.read_bytes()
    if not raw:
        return None, meta
    ch = max(1, meta["channels"])
    if meta["bits"] == 8:
        a = (np.frombuffer(raw, dtype=np.uint8).astype(np.float32) - 128.0) / 128.0
    else:
        n = len(raw) // 2
        a = np.frombuffer(raw[:n * 2], dtype="<i2").astype(np.float32) / 32768.0
    a = a[:(len(a) // ch) * ch].reshape(-1, ch)
    cap = int(max_secs * meta["freq"])
    if len(a) > cap:
        a = a[-cap:] if tail else a[:cap]
    return a, meta


def analyse(pcm_path: Path, max_secs: float = 30.0) -> dict:
    a, meta = load(pcm_path, max_secs)
    if a is None or len(a) == 0:
        return {"ok": False, "error": "audio dump is empty -- the title never wrote to /dev/dsp "
                                      "(check status.audio_active; many titles open audio late)",
                "format": meta}

    freq, ch = meta["freq"], a.shape[1]
    mono = a.mean(axis=1)
    n = len(mono)

    peak = float(np.abs(a).max())
    rms = float(np.sqrt((mono ** 2).mean()))
    dc = float(mono.mean())
    clip = float((np.abs(a) >= 0.999).mean())

    # Silence: fraction of 20ms blocks whose peak is below -60dBFS, plus the longest such run.
    blk = max(1, int(0.02 * freq))
    nb = n // blk
    silent_blocks = np.zeros(0, dtype=bool)
    longest_silence = 0.0
    if nb:
        bp = np.abs(mono[:nb * blk].reshape(nb, blk)).max(axis=1)
        silent_blocks = bp < 10 ** (-60 / 20)
        run = best = 0
        for s in silent_blocks:
            run = run + 1 if s else 0
            best = max(best, run)
        longest_silence = best * blk / freq

    # Discontinuity: adjacent samples jumping more than half of full scale. Catches gross
    # byte-garbage, but it is a blunt instrument -- at moderate levels real noise rarely jumps that
    # far in one sample, so it must not be the only static detector (it read 0.0 on a signal whose
    # spectrum was 91% sub-100Hz).
    d = np.abs(np.diff(mono))
    disc = float((d > 0.5).mean())

    # Roughness and zero-crossing rate are the reliable noise discriminators, and they need no
    # spectrum. Measured against references at 22 kHz: white noise ~1.15 roughness / ~0.50 zcr,
    # a 440 Hz tone ~0.11 / ~0.04, real BGM well below that. Roughness is scale-free (mean
    # adjacent-sample change over rms), so it works regardless of how loud the title mixes.
    rms_safe = rms if rms > 1e-9 else 1e-9
    roughness = float(d.mean() / rms_safe)
    zcr = float(np.mean(np.diff(np.signbit(mono)) != 0))

    # Spectrum over the loudest window, so a mostly-silent dump still gets characterised.
    N = 4096
    flatness = centroid = dominant = float("nan")
    band = None
    if n >= N:
        nw = n // N
        energies = (mono[:nw * N].reshape(nw, N) ** 2).sum(axis=1)
        w = mono[int(energies.argmax()) * N:][:N] * np.hanning(N)
        spec = np.abs(np.fft.rfft(w)) ** 2
        spec = np.maximum(spec, 1e-20)
        pos = spec[1:]
        fr = np.fft.rfftfreq(N, 1 / freq)[1:]
        centroid = float((fr * pos).sum() / pos.sum())
        dominant = float(fr[int(pos.argmax())])
        tot = float(pos.sum())
        band = {"sub100_frac": round(float(pos[fr < 100].sum()) / tot, 4),
                "mid_100_2k_frac": round(float(pos[(fr >= 100) & (fr < 2000)].sum()) / tot, 4),
                "high_2k_frac": round(float(pos[fr >= 2000].sum()) / tot, 4)}
        # Flatness over 100Hz..Nyquist only. Computed across the whole spectrum it is dominated by
        # a large sub-bass bin: one huge value inflates the arithmetic mean, driving the ratio to
        # ~0 and reporting "strongly tonal" even when everything above it is broadband hiss.
        mb = pos[fr >= 100]
        if mb.size:
            flatness = float(np.exp(np.log(mb).mean()) / mb.mean())

    bal = None
    if ch == 2:
        lr = np.sqrt((a ** 2).mean(axis=0))
        bal = {"left_rms": round(float(lr[0]), 5), "right_rms": round(float(lr[1]), 5),
               "identical": bool(np.array_equal(a[:, 0], a[:, 1]))}

    verdict = []
    if rms < 1e-4:
        verdict.append("effectively silent")
    if disc > 0.05:
        verdict.append(f"HIGH discontinuity ({disc:.1%} of samples jump >50% FS) -- the bytes are "
                       f"not a waveform at all")
    if roughness > 0.6 or zcr > 0.3:
        verdict.append(f"NOISE-LIKE waveform (roughness {roughness:.2f}, zero-crossing rate "
                       f"{zcr:.2f}; white noise is ~1.15/0.50, a pure tone ~0.11/0.04) -- this is "
                       f"the radio-static signature")
    if flatness == flatness and flatness > 0.4:
        verdict.append(f"noise-like spectrum above 100Hz (flatness {flatness:.2f}; 1.0 = white "
                       f"noise)")
    # Energy concentrated in sub-bass is not musically possible for these titles' BGM: the stream
    # is being consumed too slowly (everything shifted down), or the negotiated rate/format does
    # not match what the game writes.
    # This must test the ENERGY DISTRIBUTION, not just the dominant bin: a bass-heavy track
    # legitimately has its single largest bin below 80Hz while its centroid sits in the midrange
    # (measured on real Blazar captures: dominant 11Hz and 32Hz, but centroid 1407Hz / 2191Hz and
    # only 20-35% of energy below 100Hz). Keying on the peak alone false-positived on both.
    if (band and rms > 1e-3 and band["sub100_frac"] > 0.8
            and centroid == centroid and centroid < 150):
        verdict.append(f"energy is almost entirely sub-bass ({band['sub100_frac']:.0%} below "
                       f"100Hz, dominant {dominant:.0f}Hz, centroid {centroid:.0f}Hz) -- "
                       f"implausible for BGM; suspect a playback-rate or sample-format mismatch "
                       f"rather than corrupt data")
    if clip > 0.01:
        verdict.append(f"clipping on {clip:.1%} of samples")
    if bal and bal["identical"] and ch == 2:
        verdict.append("both channels bit-identical -- expected for the many mono-sourced titles "
                       "here, but would also be how a downmix/stride bug looks")
    if not verdict:
        verdict.append("looks like normal audio")

    return {
        "ok": True,
        "format": {"freq": freq, "channels": ch, "bits": meta["bits"]},
        "duration_secs": round(n / freq, 2),
        "samples": int(n),
        "rms": round(rms, 5),
        "peak": round(peak, 5),
        "dc_offset": round(dc, 6),
        "clipping_frac": round(clip, 5),
        "silence_frac": round(float(silent_blocks.mean()), 4) if len(silent_blocks) else None,
        "longest_silence_secs": round(longest_silence, 2),
        "discontinuity_frac": round(disc, 5),
        "roughness": round(roughness, 4),            # mean|Δsample| / rms; noise ~1.15, tone ~0.11
        "zero_crossing_rate": round(zcr, 4),         # noise ~0.50, tone ~0.04
        "band_energy": band,
        "spectral_flatness": None if flatness != flatness else round(flatness, 4),
        "spectral_centroid_hz": None if centroid != centroid else round(centroid, 1),
        "dominant_hz": None if dominant != dominant else round(dominant, 1),
        "channel_balance": bal,
        "verdict": verdict,
    }


def _colormap(v: np.ndarray) -> np.ndarray:
    """Perceptually-ordered dark->bright ramp (v in 0..1) -> uint8 RGB."""
    v = np.clip(v, 0, 1)
    r = np.clip(1.5 * v - 0.3, 0, 1)
    g = np.clip(1.4 * v ** 1.4 - 0.1, 0, 1)
    # Clip the sine to >=0 BEFORE the fractional power: rounding can make it slightly negative at
    # the endpoints, and a negative base with a fractional exponent is NaN, which then casts to
    # garbage uint8 (it showed up as a RuntimeWarning and speckled pixels).
    lobe = np.clip(np.sin(np.pi * v), 0, 1) ** 1.2
    b = np.clip(1.2 * lobe, 0, 1) * (1 - 0.55 * v) + 0.12 * (1 - v)
    return (np.clip(np.stack([r, g, b], axis=-1), 0, 1) * 255).astype(np.uint8)


def render(pcm_path: Path, out: Path, max_secs: float = 30.0,
           width: int = 640, spec_h: int = 220, wave_h: int = 90) -> dict:
    """Waveform over a log-magnitude spectrogram, as one PNG. Time -> x, frequency -> y (low at
    the bottom). A glance distinguishes music (horizontal harmonic bands) from static (uniform
    wash) from silence (flat) far faster than reading numbers."""
    a, meta = load(pcm_path, max_secs)
    if a is None or len(a) == 0:
        return {"ok": False, "error": "audio dump is empty"}
    mono = a.mean(axis=1)
    freq = meta["freq"]

    N, hop = 1024, max(1, len(mono) // width)
    cols = max(1, min(width, (len(mono) - N) // hop + 1)) if len(mono) > N else 1
    win = np.hanning(N)
    mag = np.zeros((N // 2, cols), dtype=np.float32)
    for i in range(cols):
        seg = mono[i * hop:i * hop + N]
        if len(seg) < N:
            seg = np.pad(seg, (0, N - len(seg)))
        mag[:, i] = np.abs(np.fft.rfft(seg * win))[:N // 2]

    # Normalise to dBFS before mapping to colour. Raw rfft magnitude scales with the window length
    # (a full-scale tone in a 1024-pt Hann window peaks near N/4), so without this every audible
    # bin saturates the top of the ramp and the spectrogram renders as a solid block.
    mag /= (N / 4.0)
    db = 20 * np.log10(np.maximum(mag, 1e-8))
    db = np.clip((db + 80) / 80, 0, 1)                       # -80dBFS..0dBFS -> 0..1
    yidx = (np.geomspace(1, N // 2, spec_h) - 1).astype(int)  # log frequency axis
    img = _colormap(db[yidx][::-1])                           # low freq at the bottom

    xs = np.linspace(0, cols - 1, cols).astype(int)
    spec_rgb = np.zeros((spec_h, cols, 3), dtype=np.uint8)
    spec_rgb[:, :, :] = img[:, xs, :]

    # Waveform strip above the spectrogram.
    wav_rgb = np.full((wave_h, cols, 3), 16, dtype=np.uint8)
    step = max(1, len(mono) // cols)
    mid = wave_h // 2
    for x in range(cols):
        seg = mono[x * step:(x + 1) * step]
        if not len(seg):
            continue
        lo = int(mid - np.clip(seg.max(), -1, 1) * (mid - 2))
        hi = int(mid - np.clip(seg.min(), -1, 1) * (mid - 2))
        wav_rgb[min(lo, hi):max(lo, hi) + 1, x] = (90, 220, 160)
    wav_rgb[mid, :] = (70, 70, 70)

    canvas = np.vstack([wav_rgb, np.full((2, cols, 3), 40, dtype=np.uint8), spec_rgb])
    h, w = canvas.shape[:2]
    out.write_bytes(_png(bytearray(canvas.tobytes()), w, h))
    return {"ok": True, "path": str(out), "png": out.read_bytes(),
            "width": w, "height": h,
            "axes": {"x": f"time 0..{len(mono)/freq:.1f}s",
                     "y_top": "waveform (+1..-1)",
                     "y_bottom": f"log frequency 20Hz..{freq//2}Hz, -90..0 dB"}}


def to_wav(pcm_path: Path, out: Path, max_secs: float = 60.0) -> dict:
    """Write a real .wav so a human can actually listen to it."""
    a, meta = load(pcm_path, max_secs)
    if a is None or len(a) == 0:
        return {"ok": False, "error": "audio dump is empty"}
    pcm16 = np.clip(a * 32767, -32768, 32767).astype("<i2")
    with wave.open(str(out), "wb") as w:
        w.setnchannels(a.shape[1])
        w.setsampwidth(2)
        w.setframerate(meta["freq"])
        w.writeframes(pcm16.tobytes())
    return {"ok": True, "path": str(out), "duration_secs": round(len(a) / meta["freq"], 2)}
