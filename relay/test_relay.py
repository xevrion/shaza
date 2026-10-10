import io
import os
import sys
import unittest
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shaza_relay as r  # noqa: E402

from PIL import Image  # noqa: E402


def make_wav(samples):
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(b"".join(int(s).to_bytes(2, "little", signed=True) for s in samples))
    return buf.getvalue()


def parse(reply):
    head, rest = reply.split(b"\n\n", 1)
    lines = head.decode().split("\n")
    fields, blobs = {}, {}
    for line in lines[1:]:
        key, _, value = line.partition(" ")
        fields[key] = value
        if key in ("cover", "title_mask", "artist_mask"):
            w, h, n = map(int, value.split())
            blobs[key] = (w, h, rest[:n])
            rest = rest[n:]
    return lines[0], fields, blobs, rest


def by_chars(s):
    return 10 * len(s)


class Wrap(unittest.TestCase):
    def test_fits_on_one_line(self):
        self.assertEqual(r.wrap("Get Lucky", 200, 2, by_chars), ["Get Lucky"])

    def test_breaks_on_spaces(self):
        self.assertEqual(r.wrap("aaaa bbbb cccc", 90, 2, by_chars), ["aaaa bbbb", "cccc"])

    def test_ellipsis_when_too_long(self):
        lines = r.wrap("aaaa bbbb cccc dddd eeee", 90, 2, by_chars)
        self.assertEqual(len(lines), 2)
        self.assertTrue(lines[-1].endswith("…"))
        self.assertTrue(all(by_chars(l) <= 90 for l in lines))

    def test_word_wider_than_line_is_cut(self):
        lines = r.wrap("abcdefghijklmnop", 50, 3, by_chars)
        self.assertEqual(lines, ["abcde", "fghij", "klmn…"])

    def test_text_without_spaces_fills_lines(self):
        lines = r.wrap("紅蓮華紅蓮華", 30, 2, by_chars)
        self.assertEqual(lines, ["紅蓮華", "紅蓮華"])

    def test_single_line_limit(self):
        lines = r.wrap("Pritam, Arijit Singh & Amitabh", 120, 1, by_chars)
        self.assertEqual(len(lines), 1)
        self.assertTrue(lines[0].endswith("…"))

    def test_blank(self):
        self.assertEqual(r.render_text("   ", r.TITLE_FONT, 30, 200), (0, 0, b""))


class Render(unittest.TestCase):
    def test_mask_size_matches_bytes_and_width(self):
        for text in ["Blinding Lights", "紅蓮華", "केसरिया", "A much longer title that has to wrap over two lines"]:
            w, h, data = r.render_text(text, r.TITLE_FONT, 34, 300)
            self.assertEqual(len(data), w * h, text)
            self.assertLessEqual(w, 302, text)
            self.assertGreater(max(data), 200, text)

    def test_non_latin_uses_a_font_that_has_it(self):
        for ch in "紅क":
            self.assertNotEqual(r.font_file_for(ch, r.TITLE_FONT), r.TITLE_FONT)
        self.assertEqual(r.font_file_for("a", r.TITLE_FONT), r.TITLE_FONT)

    def test_smart_quotes(self):
        self.assertEqual(r.smart_quotes('Kesariya (From "Brahmastra")'), "Kesariya (From “Brahmastra”)")
        self.assertEqual(r.smart_quotes("Don't"), "Don’t")


class Colour(unittest.TestCase):
    def test_grey_cover_gets_soft_white(self):
        self.assertEqual(r.accent_from(Image.new("RGB", (60, 60), (90, 90, 90))), (235, 235, 235))

    def test_dark_red_cover_is_lifted_but_stays_red(self):
        red, g, b = r.accent_from(Image.new("RGB", (60, 60), (90, 10, 10)))
        self.assertGreater(red, 180)
        self.assertGreater(red, g + 60)

    def test_rgb565_little_endian(self):
        self.assertEqual(r.rgb565(Image.new("RGB", (1, 1), (255, 0, 0))), b"\x00\xf8")
        self.assertEqual(r.rgb565(Image.new("RGB", (1, 1), (0, 0, 255))), b"\x1f\x00")


class Services(unittest.TestCase):
    def test_shazam_reply(self):
        song = r.from_shazam({"track": {"title": "Gurenge", "subtitle": "LiSA", "images": {"coverart": "u"}}})
        self.assertEqual((song.title, song.artist, song.cover_url, song.via), ("Gurenge", "LiSA", "u", "shazam"))
        self.assertIsNone(r.from_shazam({"matches": []}))

    def test_audd_prefers_spotify(self):
        reply = {"result": {"title": "Gurenge", "artist": "LiSA", "spotify": {"name": "紅蓮華", "album": {"images": [{"width": 64, "url": "s"}, {"width": 640, "url": "big"}]}}}}
        song = r.from_audd(reply)
        self.assertEqual((song.title, song.cover_url), ("紅蓮華", "big"))
        self.assertIsNone(r.from_audd({"status": "success", "result": None}))

    def test_falls_back_when_a_service_fails(self):
        def broken(wav):
            raise TimeoutError()

        song, err = r.recognise(b"", [("a", broken), ("b", lambda w: r.Song("T", "A"))], log=lambda s: None)
        self.assertEqual(song.title, "T")
        self.assertIsNone(err)

    def test_no_match_is_believed(self):
        calls = []
        song, err = r.recognise(b"", [("a", lambda w: None), ("b", lambda w: calls.append(1))], log=lambda s: None)
        self.assertIsNone(song)
        self.assertIsNone(err)
        self.assertEqual(calls, [])

    def test_all_failing_reports_the_last_error(self):
        def broken(wav):
            raise OSError("down")

        song, err = r.recognise(b"", [("a", broken), ("b", broken)], log=lambda s: None)
        self.assertIsNone(song)
        self.assertIn("down", err)


class Listen(unittest.TestCase):
    wav = make_wav([0, 1000, -1000, 32767, -32768] * 100)

    def test_found(self):
        services = [("fake", lambda w: r.Song('Hey "You"', "Someone", "u", "fake"))]
        cover = lambda url, size: Image.new("RGB", (size, size), (20, 40, 200))
        reply = r.handle_listen(self.wav, {"art": ["32"]}, services, log=lambda s: None, fetch=cover)
        magic, f, blobs, rest = parse(reply)
        self.assertEqual(magic, "shaza1")
        self.assertEqual(f["status"], "found")
        self.assertEqual(f["title"], 'Hey "You"')
        self.assertEqual(blobs["cover"][:2], (32, 32))
        self.assertEqual(len(blobs["cover"][2]), 32 * 32 * 2)
        self.assertEqual(len(f["accent"]), 6)
        w, h, data = blobs["title_mask"]
        self.assertEqual(len(data), w * h)
        self.assertEqual(rest, b"")

    def test_cover_failure_still_answers(self):
        def no_cover(url, size):
            raise OSError("cdn down")

        services = [("fake", lambda w: r.Song("T", "A", "u", "fake"))]
        _, f, blobs, _ = parse(r.handle_listen(self.wav, {}, services, log=lambda s: None, fetch=no_cover))
        self.assertEqual(f["status"], "found")
        self.assertNotIn("cover", blobs)
        self.assertEqual(f["accent"], "ebebeb")

    def test_none(self):
        _, f, _, _ = parse(r.handle_listen(self.wav, {}, [("fake", lambda w: None)], log=lambda s: None))
        self.assertEqual(f["status"], "none")

    def test_not_a_wav(self):
        _, f, _, _ = parse(r.handle_listen(b"garbage", {}, [], log=lambda s: None))
        self.assertEqual(f["status"], "error")

    def test_newlines_in_titles_cannot_break_the_header(self):
        services = [("fake", lambda w: r.Song("one\ntwo", "a\r\nb", "", "fake"))]
        magic, f, _, _ = parse(r.handle_listen(self.wav, {}, services, log=lambda s: None, fetch=lambda u, s: None))
        self.assertEqual(f["title"], "one two")
        self.assertEqual(f["artist"], "a  b")

    def test_level(self):
        peak, rms = r.level_of(self.wav)
        self.assertAlmostEqual(peak, 1.0)
        self.assertGreater(rms, 0.5)
        self.assertEqual(r.level_of(make_wav([0] * 50)), (0.0, 0.0))


if __name__ == "__main__":
    unittest.main()
