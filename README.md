# shaza

Tap the round screen, it listens to the room for five seconds, and shows the song that is playing: the cover, the title and the artist, in a colour taken from the cover. That is the whole product. No menus, no settings.

Built for the Waveshare ESP32-S3-Touch-AMOLED-1.75C (466x466 round AMOLED), with a simulator that runs the same code on a laptop and uses the laptop's microphone.

<p align="center">
  <img src="docs/demo_orb.gif" width="300" alt="orb look: listening, thinking, then Vichudo by Kinjal Dave">
  <img src="docs/demo_halo.gif" width="300" alt="halo look">
  <img src="docs/demo_dial.gif" width="300" alt="dial look">
</p>

Real songs, recognised and rendered by the actual code:

![ten songs in the orb look](docs/songs_orb.png)

## Status

What has been checked, and how:

- **Recognition works through a real microphone.** In the simulator, with the laptop's built-in mic and songs playing from the laptop's speakers, 14 of 22 tries were recognised, in about a second each including the relay. Details in [docs/recognition.md](docs/recognition.md).
- **Tests pass:** `make test` runs 24 relay tests and four C++ suites (199 checks). Every drawing shortcut is compared pixel for pixel with a slow reference, and the frame engine that repaints only what changed is compared with a full repaint on every frame of a scripted run through every state.
- **The board:** only the microphone bring-up firmware exists so far (`main/`). It builds. It has **not** been run on the board yet, so the microphones are unverified, and the full app firmware (Wi-Fi, upload, the looks on the real panel, frame timing) is not written yet.
- **Frame budget:** measured in the simulator as pixels sent per frame, not yet as milliseconds on the chip. See [Smoothness](#smoothness).

## How it works

```
 board or simulator                     laptop                          internet
 ------------------                     ------                          --------
 tap, record 5 s of 16 kHz audio  --->  relay/shaza_relay.py  -------->  Shazam (unofficial, via shazamio)
                                        |                                AudD, if Shazam fails
 show cover, title, artist  <-------    fetch the cover, shrink it to RGB565,
                                        pick an accent colour, render the
                                        title and artist in any script
```

The board never talks to a recognition service and holds no API key. The relay does everything a microcontroller would find hard, and sends back one small reply: a few header lines and three raw blobs (the 200x200 cover as RGB565, and the title and artist as 8 bit anti-aliased masks). The board only blends pixels.

Titles can be long and can be in any script, so the relay renders them with real fonts: Inter for Latin, Noto Sans Devanagari, Noto Sans CJK, and whatever fontconfig finds for the rest. It wraps to two lines and ends with an ellipsis when a title does not fit. The board's own few words ("No match", "Tap to try again", "2m ago") come from Inter baked into a glyph atlas.

**Privacy:** every tap sends five seconds of whatever the room sounds like to Shazam, and sometimes AudD. The relay keeps nothing unless you start it with `--save`.

## Try it in the simulator

You need a C++17 compiler, SDL2, [uv](https://docs.astral.sh/uv/) and curl. On Fedora: `sudo dnf install gcc-c++ SDL2-devel`; on Ubuntu: `sudo apt install g++ libsdl2-dev`.

    git clone --recursive https://github.com/xevrion/shaza
    cd shaza
    make test

**Without a microphone or network**, the simulator plays made-up music levels and answers from the saved replies in `host/fixtures`:

    make fake

**For real**, start the relay in one terminal and the simulator in another, then play a song out loud (from a phone or the laptop speakers, not into earbuds) and click the circle:

    make relay      # first run creates a token in ~/.config/shaza/token
    make sim

Keys: click or space taps the screen, `D` switches between the three looks, `W` pretends the Wi-Fi dropped, Esc quits. The window title shows the look, the phase and how many pixels each frame would send to the panel.

The relay needs Python 3.12 or older. shazamio uses pydub, which needs the `audioop` module that Python 3.13 removed, and its compiled core crashes on 3.14. `uv run` picks a suitable Python on its own.

## The relay

    uv run relay/shaza_relay.py                   # listens on 0.0.0.0:8788
    uv run relay/shaza_relay.py --save recordings # also keeps every clip, to listen to
    uv run relay/shaza_relay.py --try clip.wav    # recognise a file and print the reply header

It asks Shazam first, with an 8 second limit, and AudD second. AudD answers about ten requests a day without a token. For more, sign up at AudD (300 free requests) and put the token in `~/.config/shaza/audd_token`. That file lives outside the repo; never paste it anywhere else.

Requests need the shared token in an `X-Shaza-Token` header. Endpoints: `POST /listen?art=200&tw=360&ts=34&aw=320&as=24` with a 16 bit WAV as the body, and `GET /health`.

## On the board

What is there now is the microphone test. It shows the level of both microphones as two bars, logs the level four times a second, and records four seconds at boot and on every tap, which `tools/grab_wav.py` saves as WAV files you can listen to.

The build runs ESP-IDF 5.5.1 in Docker through `roundkit/scripts/idf`, so nothing else has to be installed. The image is about 12 GB.

    make build
    make flash PORT=/dev/ttyACM0
    python3 tools/grab_wav.py          # resets the board and saves the boot recording in recordings/

Things to check first, in this order: the samples are not all zero (one report for this board family had silent microphones until MCLK was right; the board package sets it up, so this should be fine, but it is the first thing to look at), a clap is clearly louder than the room, and the recording sounds like the room.

Flashing replaces whatever firmware is on the board.

## Three looks

They share the result screen and differ in how they listen. `D` switches between them in the simulator.

**Halo**: one hairline ring that breathes. Listening sends thin ripples out of it, as strong as the room is loud, while a bright arc fills clockwise over the five seconds. Thinking is a short comet arc going round.

![halo](docs/states_halo.png)

**Orb**: a small glowing pearl that swells with the room inside a faint progress ring, shrinks to a slow heartbeat while thinking, and opens into a round cover with a glow in the cover's colour.

![orb](docs/states_orb.png)

**Dial**: 64 fine ticks round the edge. Listening sweeps once round them and each tick keeps the loudness it heard, so the clip is left drawn as a groove. Thinking sends one bright tick round the groove.

![dial](docs/states_dial.png)

Left to right: ready, listening, thinking, found, no match, ready again with the time since the last song.

## Smoothness

The panel takes about 10 megapixels a second, so a full screen (217k pixels) costs about 20 ms and cannot hold 60 fps. Each look describes its frame as a few items, each with a box and a key that changes whenever the item would look different. Only items whose key changed are repainted, clipped to their box, and a screen that is not moving sends nothing.

Pixels sent per frame in a scripted run (`make shots`), averaged over each phase:

| | ready | listening | thinking | found | no match |
|---|---|---|---|---|---|
| halo | 18k | 79k (worst 94k) | 42k | 9k | 1k |
| orb | 20k | 75k (worst 99k) | 30k | 15k | 1k |
| dial | 3k | 6k | 12k | 9k | 1k |

Halo and orb stay under about 100k pixels (10 ms) a frame while listening. The dial breaks that on purpose for a single frame whenever every tick changes at once (a new phase starting), which repaints the whole dial: about 149k pixels, one late frame. These are pixel counts from the simulator. Render and send times on the chip still need measuring.

## Tests

    make test

| Suite | What it checks |
|---|---|
| `relay/test_relay.py` | wrapping and ellipsis, fonts for each script, accent colours, RGB565, the reply format, falling back from one service to the next |
| `host/test_audio.cpp` | the WAV header, loudness in dBFS, the level meter |
| `host/test_paint.cpp` | rings, arcs and glows against a per-pixel reference for 300 random shapes, clipping, masks, images |
| `host/test_app.cpp` | parsing every saved reply and every truncation of it, malformed replies, the state machine (cancel, time out, late replies, no Wi-Fi) |
| `host/test_look.cpp` | each look drawn incrementally matches a full repaint on every frame; a still screen sends nothing |

`make media` rebuilds the GIFs and screenshots in `docs/` (needs ffmpeg).

## Layout

| Path | What |
|---|---|
| `core/` | The state machine, the reply parser, drawing, text and the three looks. Pure C++, no ESP-IDF. |
| `host/` | Tests, the simulator, and saved relay replies in `host/fixtures/`. |
| `relay/` | The laptop relay, one Python file. |
| `main/` | Firmware for the board: for now the microphone test. |
| `tools/` | `grab_wav.py` (recordings off the board), `make_font.py` (the glyph atlas), `make_media.py` (the pictures here). |
| `fonts/` | Inter, used by the relay and baked into the atlas. |
| `roundkit/` | Shared code for this board, a [submodule](https://github.com/xevrion/roundkit). |

## License

MIT for the code. Inter is under the SIL Open Font License 1.1, see `fonts/LICENSE.txt`. Album covers in the screenshots and fixtures belong to their owners and are shown only to demonstrate the display.
