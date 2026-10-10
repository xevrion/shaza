#!/usr/bin/env python3
"""Catches the microphone test's recordings off the serial console and saves them as WAV files.

    python3 tools/grab_wav.py                     # resets the board, keeps the boot recording
    python3 tools/grab_wav.py --count 3 --no-reset  # waits for three taps

Each recording is saved twice: as recorded (both channels) and as left and right on their own.
Level lines from the board are echoed as they arrive. Standard library only.
"""

import argparse
import base64
import fcntl
import os
import struct
import sys
import termios
import time
import wave

TIOCMBIS, TIOCMBIC = 0x5416, 0x5417
TIOCM_DTR, TIOCM_RTS = 0x002, 0x004


def set_line(fd, bit, on):
    fcntl.ioctl(fd, TIOCMBIS if on else TIOCMBIC, struct.pack("I", bit))


def reset(fd):
    # What esptool does for a hard reset over the S3's USB serial: EN low, then released.
    set_line(fd, TIOCM_DTR, False)
    set_line(fd, TIOCM_RTS, True)
    time.sleep(0.1)
    set_line(fd, TIOCM_RTS, False)


def open_port(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    attrs[0] = attrs[1] = attrs[3] = 0
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def lines(fd):
    buf = b""
    while True:
        chunk = os.read(fd, 4096)
        if not chunk:
            time.sleep(0.01)
            continue
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            yield line.rstrip(b"\r").decode("utf-8", "replace")


def save(path, data):
    with open(path, "wb") as f:
        f.write(data)
    with wave.open(path) as w:
        rate, ch, frames = w.getframerate(), w.getnchannels(), w.readframes(w.getnframes())
    base = path[:-4]
    for i, name in enumerate("LR"[:ch] if ch > 1 else ""):
        mono = b"".join(frames[j:j + 2] for j in range(i * 2, len(frames), ch * 2))
        with wave.open("%s_%s.wav" % (base, name), "wb") as w:
            w.setnchannels(1), w.setsampwidth(2), w.setframerate(rate)
            w.writeframes(mono)
    print("saved %s (%d Hz, %d channels, %.1f s)" % (path, rate, ch, len(frames) / (2 * ch * rate)))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="/dev/ttyACM0")
    p.add_argument("--out", default="recordings")
    p.add_argument("--count", type=int, default=1)
    p.add_argument("--no-reset", action="store_true")
    args = p.parse_args()

    os.makedirs(args.out, exist_ok=True)
    fd = open_port(args.port)
    if not args.no_reset:
        reset(fd)
    got, collecting, chunks = 0, False, []
    for line in lines(fd):
        if line.startswith("SHAZA-WAV"):
            collecting, chunks = True, []
        elif line.startswith("SHAZA-END") and collecting:
            collecting = False
            path = os.path.join(args.out, time.strftime("mic-%Y%m%d-%H%M%S.wav"))
            save(path, base64.b64decode("".join(chunks)))
            got += 1
            if got >= args.count:
                return
        elif collecting:
            chunks.append(line.strip())
        elif "mictest" in line or "mic:" in line or " E " in line:
            print(line, flush=True)


if __name__ == "__main__":
    sys.exit(main())
