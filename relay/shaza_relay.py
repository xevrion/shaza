#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10,<3.13"
# dependencies = ["shazamio", "pillow", "fonttools"]
# ///
"""The laptop side of shaza.

The board records a few seconds of the room and posts the WAV here. The relay asks Shazam (through
the unofficial shazamio client) what it is, falls back to AudD's tokenless API when Shazam fails,
then does everything the board would find hard: it fetches the cover, shrinks it to RGB565 at the
size the board asked for, picks an accent colour, and renders the title and artist into anti-aliased
coverage masks with fonts for any script.

    uv run relay/shaza_relay.py          # listens on 0.0.0.0:8788

Python 3.13 and later are excluded on purpose: pydub, which shazamio uses, needs the audioop module
that 3.13 removed, and with audioop-lts patched in, shazamio's compiled core segfaults on 3.14.

Endpoints (the shared token goes in the X-Shaza-Token header):
    POST /listen?art=200&tw=360&ts=34&aw=320&as=24   body: a 16 bit mono WAV
    GET  /health                                      "ok", no token needed
"""

import argparse
import asyncio
import colorsys
import hmac
import io
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import threading
import time
import uuid
import wave
from functools import lru_cache
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse
from urllib.request import Request, urlopen

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, "..", "fonts")
TITLE_FONT = os.path.join(FONTS, "InterDisplay-SemiBold.ttf")
ARTIST_FONT = os.path.join(FONTS, "Inter-Medium.ttf")
# Tried in order for characters Inter does not have, before asking fontconfig.
FALLBACKS = [
    "/usr/share/fonts/google-noto-vf/NotoSansDevanagari[wght].ttf",
    "/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-VF.ttc",
    "/usr/share/fonts/google-noto-vf/NotoSans[wght].ttf",
]
DEFAULT_TOKEN_FILE = os.path.expanduser("~/.config/shaza/token")
AUDD_TOKEN_FILE = os.path.expanduser("~/.config/shaza/audd_token")
MAX_BODY = 2 * 1024 * 1024
SHAZAM_TIMEOUT_S = 8.0
AUDD_TIMEOUT_S = 10.0


# Recognition -------------------------------------------------------------------------------------

class Song:
    def __init__(self, title, artist, cover_url="", via=""):
        self.title, self.artist, self.cover_url, self.via = title, artist, cover_url, via


def from_shazam(reply):
    track = (reply or {}).get("track")
    if not track or not track.get("title"):
        return None
    url = (track.get("images") or {}).get("coverart", "")
    return Song(track["title"], track.get("subtitle", ""), url, "shazam")


def from_audd(reply):
    result = (reply or {}).get("result")
    if not result or not result.get("title"):
        return None
    # AudD's Apple Music block sometimes names a different song; its Spotify block has been right.
    spotify = result.get("spotify") or {}
    images = (spotify.get("album") or {}).get("images") or []
    url = max(images, key=lambda i: i.get("width", 0))["url"] if images else ""
    title = spotify.get("name") or result["title"]
    return Song(title, result.get("artist", ""), url, "audd")


def ask_shazam(wav):
    from shazamio import Shazam

    async def go():
        return await asyncio.wait_for(Shazam().recognize(wav), SHAZAM_TIMEOUT_S)

    return from_shazam(asyncio.run(go()))


def audd_token():
    """Optional. Without one AudD answers about ten requests a day."""
    try:
        with open(AUDD_TOKEN_FILE) as f:
            return f.read().strip()
    except FileNotFoundError:
        return ""


def ask_audd(wav):
    boundary = uuid.uuid4().hex
    fields = [("return", "spotify")]
    token = audd_token()
    if token:
        fields.append(("api_token", token))
    body = b""
    for name, value in fields:
        body += f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"\r\n\r\n{value}\r\n'.encode()
    body += f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="a.wav"\r\nContent-Type: audio/wav\r\n\r\n'.encode()
    body += wav + f"\r\n--{boundary}--\r\n".encode()
    req = Request("https://api.audd.io/", data=body, headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
    with urlopen(req, timeout=AUDD_TIMEOUT_S) as r:
        reply = json.load(r)
    if reply.get("status") == "error":
        raise RuntimeError("%s" % reply.get("error", {}).get("error_message"))
    return from_audd(reply)


def recognise(wav, services, log=print):
    """Asks each service in turn. A service that answers "no match" is believed; one that fails
    (timeout, network, throttled) hands over to the next. Returns (song or None, error or None)."""
    error = None
    for name, ask in services:
        t0 = time.monotonic()
        try:
            song = ask(wav)
        except Exception as e:
            error = "%s: %s" % (name, e or type(e).__name__)
            log("  %s failed after %.1fs, %s" % (name, time.monotonic() - t0, error))
            continue
        log("  %s %.1fs, %s" % (name, time.monotonic() - t0, "%s / %s" % (song.title, song.artist) if song else "no match"))
        return song, None
    return None, error


# Cover and colour --------------------------------------------------------------------------------

def fetch_cover(url, size):
    if not url:
        return None
    # Apple's image CDN serves any size named in the path.
    url = re.sub(r"/\d+x\d+(cc|bb)\.jpg$", "/%dx%dcc.jpg" % (size, size), url)
    with urlopen(Request(url, headers={"User-Agent": "shaza"}), timeout=8) as r:
        img = Image.open(io.BytesIO(r.read())).convert("RGB")
    return img.resize((size, size), Image.LANCZOS)


def rgb565(img):
    px = img.convert("RGB").tobytes()
    out = bytearray(len(px) // 3 * 2)
    for i in range(0, len(px) // 3):
        r, g, b = px[i * 3], px[i * 3 + 1], px[i * 3 + 2]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[i * 2], out[i * 2 + 1] = v & 0xFF, v >> 8
    return bytes(out)


def accent_from(img):
    """A colour from the cover that reads on black: the most common colourful swatch, lifted to a
    minimum brightness. Near-grey covers get a soft white."""
    small = img.resize((48, 48)).quantize(colors=8, method=Image.Quantize.MEDIANCUT)
    palette = small.getpalette()
    best, best_score = None, 0.0
    for count, index in small.getcolors():
        r, g, b = (c / 255 for c in palette[index * 3:index * 3 + 3])
        h, l, s = colorsys.rgb_to_hls(r, g, b)
        score = count * (s ** 2) * (0.2 + min(l, 1 - l))
        if s > 0.25 and score > best_score:
            best, best_score = (h, l, s), score
    if best is None:
        return (235, 235, 235)
    h, l, s = best
    r, g, b = colorsys.hls_to_rgb(h, max(0.62, min(l, 0.78)), min(1.0, s * 1.1))
    return (round(r * 255), round(g * 255), round(b * 255))


# Text --------------------------------------------------------------------------------------------

@lru_cache(maxsize=None)
def charset(path):
    from fontTools.ttLib import TTFont

    with TTFont(path, fontNumber=0) as font:
        return frozenset(font.getBestCmap() or {})


@lru_cache(maxsize=None)
def fontconfig_file(cp):
    if not shutil.which("fc-match"):
        return None
    out = subprocess.run(["fc-match", "-f", "%{file}", ":charset=%x" % cp], capture_output=True, text=True)
    return out.stdout.strip() or None


def font_file_for(ch, primary):
    cp = ord(ch)
    if ch.isspace() or cp in charset(primary):
        return primary
    for path in FALLBACKS:
        if os.path.exists(path) and cp in charset(path):
            return path
    return fontconfig_file(cp) or primary


@lru_cache(maxsize=None)
def load_font(path, px, weight):
    font = ImageFont.truetype(path, px)
    try:
        font.set_variation_by_axes([weight])
    except Exception:
        pass
    return font


def runs(text, primary, px, weight):
    """Splits text into stretches that one font can draw. Marks and joiners stay with the run
    before them so shaped scripts are not cut apart."""
    out = []
    for ch in text:
        path = font_file_for(ch, primary) if not (out and is_combining(ch)) else out[-1][0]
        if out and out[-1][0] == path:
            out[-1][1] += ch
        else:
            out.append([path, ch])
    return [(load_font(p, px, weight), s) for p, s in out]


def is_combining(ch):
    import unicodedata

    return unicodedata.category(ch) in ("Mn", "Mc", "Me") or ch in "‌‍"


def width_of(text, primary, px, weight):
    return sum(f.getlength(s) for f, s in runs(text, primary, px, weight))


def wrap(text, max_w, max_lines, measure):
    """Greedy wrap on spaces, breaking inside a word only when the word alone is too wide (which
    is every line of a script written without spaces). The last line ends in an ellipsis when
    the text does not fit."""
    words = re.findall(r"\S+\s*", " ".join(text.split()))
    lines, cur = [], ""
    pending = list(words)
    while pending and len(lines) < max_lines:
        word = pending.pop(0)
        if measure((cur + word).rstrip()) <= max_w:
            cur += word
            continue
        if cur:
            lines.append(cur.rstrip())
            cur = ""
            pending.insert(0, word)
            continue
        # One word wider than the line: take as many characters as fit.
        cut = 1
        while cut < len(word) and measure(word[:cut + 1]) <= max_w:
            cut += 1
        lines.append(word[:cut])
        rest = word[cut:].lstrip()
        if rest:
            pending.insert(0, rest)
    if cur and len(lines) < max_lines:
        lines.append(cur.rstrip())
        cur = ""
    if pending or cur:
        last = lines[-1] if lines else ""
        while last and measure(last + "…") > max_w:
            last = last[:-1]
        lines[-1:] = [last.rstrip(" ,-(") + "…"]
    return lines


def smart_quotes(s):
    s = re.sub(r'(^|[\s(\[])"', "\\1\u201c", s).replace('"', "\u201d")
    return re.sub(r"(^|[\s(\[])'", "\\1\u2018", s).replace("'", "\u2019")


def render_text(text, primary, px, max_w, max_lines=2, weight=600):
    """8 bit coverage, one byte per pixel, each line centred. Returns (w, h, bytes)."""
    text = smart_quotes(text.strip())
    if not text:
        return 0, 0, b""
    lines = wrap(text, max_w, max_lines, lambda s: width_of(s, primary, px, weight))
    step = round(px * 1.22)
    widths = [min(max_w, round(width_of(l, primary, px, weight))) for l in lines]
    w = max(widths) + 2
    # Room under the last baseline for descenders, above the first for accents.
    pad = round(px * 0.12)
    h = step * len(lines) + pad * 2
    img = Image.new("L", (w, h), 0)
    draw = ImageDraw.Draw(img)
    ascent = load_font(primary, px, weight).getmetrics()[0]
    for i, line in enumerate(lines):
        x = (w - widths[i]) / 2
        base = pad + i * step + ascent + (step - px * 1.2) / 2
        for font, s in runs(line, primary, px, weight):
            draw.text((x, base), s, font=font, fill=255, anchor="ls")
            x += font.getlength(s)
    return w, h, img.tobytes()


# The reply ---------------------------------------------------------------------------------------

def clean_line(s):
    return re.sub(r"[\x00-\x1f\x7f]", " ", s).strip()[:200]


def build_reply(status, song=None, art=None, accent=None, masks=(), error=""):
    """What the board parses. Header lines, a blank line, then the blobs the header announced, in
    the order it announced them:

        shaza1
        status found|none|error
        title <utf-8>
        artist <utf-8>
        via shazam|audd
        accent rrggbb
        cover <w> <h> <bytes>     RGB565 little endian
        title_mask <w> <h> <bytes>
        artist_mask <w> <h> <bytes>
        error <text>
    """
    head = ["shaza1", "status " + status]
    blobs = []
    if song:
        head += ["title " + clean_line(song.title), "artist " + clean_line(song.artist), "via " + song.via]
    if accent:
        head.append("accent %02x%02x%02x" % accent)
    if art is not None:
        data = rgb565(art)
        head.append("cover %d %d %d" % (art.width, art.height, len(data)))
        blobs.append(data)
    for name, (w, h, data) in masks:
        if w and h:
            head.append("%s %d %d %d" % (name, w, h, len(data)))
            blobs.append(data)
    if error:
        head.append("error " + clean_line(error))
    return ("\n".join(head) + "\n\n").encode() + b"".join(blobs)


def level_of(wav):
    """Peak and RMS of the clip, as fractions of full scale, for the log."""
    with wave.open(io.BytesIO(wav)) as w:
        if w.getsampwidth() != 2:
            return 0.0, 0.0
        frames = w.readframes(w.getnframes())
    import array

    a = array.array("h", frames)
    if sys.byteorder == "big":
        a.byteswap()
    if not a:
        return 0.0, 0.0
    peak = max(abs(min(a)), abs(max(a))) / 32768
    rms = (sum(x * x for x in a) / len(a)) ** 0.5 / 32768
    return peak, rms


def handle_listen(wav, query, services, log=print, fetch=fetch_cover, save_dir=None):
    def num(key, default, lo, hi):
        try:
            return max(lo, min(hi, int(query.get(key, [default])[0])))
        except ValueError:
            return default

    art_px = num("art", 200, 16, 466)
    try:
        peak, rms = level_of(wav)
    except (wave.Error, EOFError):
        return build_reply("error", error="not a wav")
    log("clip %d bytes, peak %.3f rms %.4f" % (len(wav), peak, rms))
    if save_dir:
        os.makedirs(save_dir, exist_ok=True)
        path = os.path.join(save_dir, time.strftime("%Y%m%d-%H%M%S") + ".wav")
        with open(path, "wb") as f:
            f.write(wav)
        log("  saved " + path)

    song, error = recognise(wav, services, log)
    if not song:
        return build_reply("error" if error else "none", error=error or "")

    art, accent = None, None
    try:
        art = fetch(song.cover_url, art_px)
    except Exception as e:
        log("  cover failed: %s" % e)
    if art is not None:
        accent = accent_from(art)
    masks = [
        ("title_mask", render_text(song.title, TITLE_FONT, num("ts", 34, 10, 80), num("tw", 360, 40, 466), 2, 600)),
        ("artist_mask", render_text(song.artist, ARTIST_FONT, num("as", 24, 10, 60), num("aw", 320, 40, 466), 1, 500)),
    ]
    return build_reply("found", song, art, accent or (235, 235, 235), masks)


# Server ------------------------------------------------------------------------------------------

def load_token(path):
    """The shared secret, created on first run and readable only by you."""
    try:
        with open(path) as f:
            token = f.read().strip()
            if token:
                return token
    except FileNotFoundError:
        pass
    os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
    token = secrets.token_urlsafe(18)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(token + "\n")
    return token


SERVICES = [("shazam", ask_shazam), ("audd", ask_audd)]


def make_handler(token, save_dir, log):
    # One recognition at a time: Shazam throttles bursts, and the board only ever sends one.
    busy = threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        server_version = "shaza"

        def log_message(self, *args):
            pass

        def reply(self, code, body, ctype="application/octet-stream"):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if urlparse(self.path).path == "/health":
                return self.reply(200, b"ok\n", "text/plain")
            self.reply(404, b"not found\n", "text/plain")

        def do_POST(self):
            url = urlparse(self.path)
            if url.path != "/listen":
                return self.reply(404, b"not found\n", "text/plain")
            if not hmac.compare_digest(self.headers.get("X-Shaza-Token", ""), token):
                return self.reply(403, b"bad token\n", "text/plain")
            n = int(self.headers.get("Content-Length") or 0)
            if n <= 0 or n > MAX_BODY:
                return self.reply(413, b"too big\n", "text/plain")
            wav = self.rfile.read(n)
            t0 = time.monotonic()
            with busy:
                body = handle_listen(wav, parse_qs(url.query), SERVICES, log, save_dir=save_dir)
            log("  answered in %.1fs" % (time.monotonic() - t0))
            self.reply(200, body)

    return Handler


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--host", default="0.0.0.0")
    p.add_argument("--port", type=int, default=8788)
    p.add_argument("--token-file", default=DEFAULT_TOKEN_FILE)
    p.add_argument("--save", metavar="DIR", help="keep every clip the board sends, to listen to later")
    p.add_argument("--try", dest="try_files", nargs="+", metavar="WAV", help="recognise these files and exit")
    p.add_argument("--reply-dir", help="with --try, also save each raw reply here")
    args = p.parse_args()

    if args.try_files:
        for path in args.try_files:
            with open(path, "rb") as f:
                body = handle_listen(f.read(), {}, SERVICES)
            print(body.split(b"\n\n", 1)[0].decode())
            if args.reply_dir:
                os.makedirs(args.reply_dir, exist_ok=True)
                name = os.path.splitext(os.path.basename(path))[0] + ".reply"
                with open(os.path.join(args.reply_dir, name), "wb") as f:
                    f.write(body)
            time.sleep(2)
        return

    token = load_token(args.token_file)
    log = lambda s: print(time.strftime("%H:%M:%S ") + s, flush=True)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(token, args.save, log))
    log("shaza relay on %s:%d, token in %s" % (args.host, args.port, args.token_file))
    server.serve_forever()


if __name__ == "__main__":
    main()
