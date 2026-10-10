# Which recognition service, and why

Tried from the laptop on 2026-10-11, with ten second clips of five songs: Blinding Lights, Kesariya, Get Lucky, Gurenge and a YouTube upload of the Aashiqui 2 love theme. Each clip was tried clean and again made to sound like a room recording (band limited to 200 to 5000 Hz, reverb, pink noise, 16 kHz mono). Nothing here was played through a speaker yet; that test happens with the board's own microphones.

| Service | Room clips found | Time | Cost | Notes |
|---|---|---|---|---|
| Shazam through [shazamio](https://github.com/shazamio/shazamio) | 5 of 5 | about 0.5 s | free | Unofficial, can break whenever Shazam changes something, and it is against Shazam's terms. A burst of six requests got throttled and the sixth took 107 s. |
| AudD without a token | 4 of 5 | 0.5 to 1.2 s | free | Stopped answering after about ten requests in a day: "no api_token passed and the limit was reached". Its Apple Music block once named a completely different song; the Spotify block was right every time. |
| AudD with a token | not tried | | 300 requests free on signup, then $5 per 1000 | |
| ACRCloud | not tried | | 14 day trial, then metered, price behind a login | Not free. |
| AcoustID | 0 of 10, clean clips included | | free for non-commercial use | Built to identify whole files, not ten seconds from the middle of a song. |

What is used: Shazam first, AudD second, from a small relay on the laptop (`relay/shaza_relay.py`). The relay keeps any key off the board, makes swapping services a one line change, and does the work the board would find hard: fetching and shrinking the cover, picking the accent colour, and rendering the title in any script.

## Through a real microphone

The same evening the simulator was pointed at the laptop's built-in microphone while songs played from the laptop's speakers, five second clips at quiet levels (around -40 dBFS RMS). Of 22 tries, 14 were recognised, 6 came back as no match and 2 failed: Shazam took longer than the relay's 8 second limit and AudD had already used up its tokenless allowance for the day. Songs found included Vichudo (Kinjal Dave), Parvati (Sadhu Tiwari), KALYANI, golden hour, bad guy, Loser, Mockingbird and a Laufey track. Shazam answered in 0.5 to 0.7 s each time.

Unofficial endpoints do break: shazamio's song search already returns a 404 page, while its recognition still works. The relay only uses recognition.

## Privacy

Every tap sends a few seconds of whatever the room sounds like to Shazam, and sometimes to AudD. The relay keeps nothing unless it is started with `--save`.

If Shazam breaks for good, the fallback is AudD with a token, put in `~/.config/shaza/audd_token`.
