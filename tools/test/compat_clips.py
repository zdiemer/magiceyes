#!/usr/bin/env python3
"""Build a short animated GIF per title from the frames a run captured.

A single screenshot shows one moment. The run keeps a frame every couple of seconds, so stitching
them gives a time-lapse of the whole 25 seconds: boot, splash, menu, gameplay. That is usually
enough to see a title actually doing something, and to catch corruption that only appears once the
game gets going.

Needs Pillow. Without it this degrades to doing nothing rather than failing the pipeline, since the
screenshots and the report do not depend on it.

Usage:
  compat_clips.py --manifest PATH --out-dir DIR [--ms 600] [--max-frames 16] [--width 320]
"""
import argparse, json, os, re, sys

try:
    from PIL import Image
except ImportError:                                     # optional dependency, by design
    Image = None


def slug(s):
    return re.sub(r"[^A-Za-z0-9._-]", "-", s).strip("-")[:60]


def build_gif(frames, dest, ms=600, max_frames=16, width=0):
    """Write an animated GIF. Returns True if one was written."""
    if Image is None or len(frames) < 2:
        return False
    imgs = []
    for p in frames[:max_frames]:
        try:
            im = Image.open(p).convert("RGB")
        except Exception:
            continue
        if width and im.width != width:
            im = im.resize((width, max(1, im.height * width // im.width)), Image.NEAREST)
        imgs.append(im)
    if len(imgs) < 2:
        return False
    # one adaptive palette for the whole clip, so colours do not shimmer between frames
    imgs = [im.quantize(colors=128, method=Image.MEDIANCUT, dither=Image.Dither.NONE)
            for im in imgs]
    # If nothing ever changed, this is a still, not a clip. Writing it anyway would put a frozen
    # image under a caption promising a time-lapse; the screenshot already covers that case.
    if len({im.tobytes() for im in imgs}) < 2:
        return False
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    imgs[0].save(dest, save_all=True, append_images=imgs[1:], duration=ms, loop=0,
                 optimize=True, disposal=2)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--out-dir", required=True, help="written as <out-dir>/<platform>/<slug>.gif")
    ap.add_argument("--ms", type=int, default=600, help="ms per frame")
    ap.add_argument("--max-frames", type=int, default=16)
    ap.add_argument("--width", type=int, default=0, help="0 keeps the native 320px")
    a = ap.parse_args()

    if Image is None:
        print("Pillow not installed; skipping clips", file=sys.stderr)
        return 0

    with open(a.manifest) as f:
        records = json.load(f)["titles"]

    made = skipped = 0
    total = 0
    for r in records:
        # Only titles that actually drew something. A black run makes a black clip, which says
        # nothing the "black" verdict has not already said, and costs repo space per title.
        if not r.get("screenshot"):
            skipped += 1
            continue
        frames = r.get("frame_pngs") or []
        frames = [p for p in frames if os.path.exists(p)]
        if len(frames) < 2:
            skipped += 1
            continue
        dest = os.path.join(a.out_dir, r["platform"], slug(r["title"]) + ".gif")
        if build_gif(frames, dest, a.ms, a.max_frames, a.width):
            made += 1
            total += os.path.getsize(dest)
        else:
            skipped += 1
    print("clips: %d written (%.1f MB), %d titles had too few frames"
          % (made, total / 1e6, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
