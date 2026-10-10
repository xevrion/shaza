#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow"]
# ///
"""Rebuilds the pictures in docs/ from the simulator: the demo GIFs, a strip of every state for each
look, and the result screen for every saved song. Needs ffmpeg and a built host/build/sim.

    make media
"""

import os
import shutil
import subprocess

from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "host", "out")
DOCS = os.path.join(ROOT, "docs")
SIM = os.path.join(ROOT, "host", "build", "sim")
LOOKS = ["orb", "halo", "dial"]  # in make_look's order
STATES = ["1_ready", "2_listening", "3_thinking", "4_found", "5_not_found", "6_ready_after"]
SONGS = ["vichudo", "kesariya", "gurenge_jp", "golden_hour", "bad_guy", "get_lucky", "blinding_lights", "loser", "laufey", "gurenge"]
GIF = "scale=360:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle"


def sheet(paths, cols, out):
    tiles = [Image.open(p) for p in paths]
    rows = (len(tiles) + cols - 1) // cols
    img = Image.new("RGB", (cols * 476 + 10, rows * 476 + 10), (14, 14, 14))
    for i, t in enumerate(tiles):
        img.paste(t, (10 + (i % cols) * 476, 10 + (i // cols) * 476))
    img.resize((img.width // 2, img.height // 2), Image.LANCZOS).save(out, optimize=True)


def main():
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(os.path.join(OUT, "gallery"))
    subprocess.run([SIM, "--gallery", os.path.join(OUT, "gallery")], check=True, cwd=ROOT)
    subprocess.run([SIM, "--shots", OUT], check=True, cwd=ROOT)
    for i, look in enumerate(LOOKS):
        frames = os.path.join(OUT, "frames_" + look)
        os.makedirs(frames)
        subprocess.run([SIM, "--frames", frames, str(i)], check=True, cwd=ROOT, stdout=subprocess.DEVNULL)
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-framerate", "30", "-i", os.path.join(frames, "f%04d.ppm"), "-vf", GIF, os.path.join(DOCS, "demo_%s.gif" % look)], check=True)
        sheet([os.path.join(OUT, "%s_%s.ppm" % (look, s)) for s in STATES], 6, os.path.join(DOCS, "states_%s.png" % look))
    for look in ["halo", "orb"]:
        sheet([os.path.join(OUT, "gallery", "%s_%s.ppm" % (look, s)) for s in SONGS], 5, os.path.join(DOCS, "songs_%s.png" % look))
    print("docs/ updated")


if __name__ == "__main__":
    main()
