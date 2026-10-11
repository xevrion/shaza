#!/usr/bin/env python3
"""Prints the board's log for a while, after resetting it. Standard library only.

    python3 tools/monitor.py                  # 60 seconds
    python3 tools/monitor.py --seconds 0      # until Ctrl+C
    python3 tools/monitor.py --no-reset
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from grab_wav import open_port, reset  # noqa: E402


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="/dev/ttyACM0")
    p.add_argument("--seconds", type=float, default=60)
    p.add_argument("--no-reset", action="store_true")
    args = p.parse_args()

    fd = open_port(args.port)
    if not args.no_reset:
        reset(fd)
    end = time.monotonic() + args.seconds if args.seconds > 0 else float("inf")
    buf = b""
    try:
        while time.monotonic() < end:
            chunk = os.read(fd, 4096)
            if not chunk:
                time.sleep(0.01)
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                print(line.rstrip(b"\r").decode("utf-8", "replace"), flush=True)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
