#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.hpp"
#include "signature.hpp"

using namespace shaza;

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static std::vector<int16_t> read_wav(const std::string& path) {
    const auto b = read_file(path);
    std::vector<int16_t> s;
    for (size_t i = 44; i + 1 < b.size(); i += 2) s.push_back(int16_t(b[i] | b[i + 1] << 8));
    return s;
}

static std::vector<uint8_t> unbase64(const std::string& s) {
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (v < 0) continue;
        acc = acc << 6 | uint32_t(v), bits += 6;
        if (bits >= 8) bits -= 8, out.push_back(uint8_t(acc >> bits));
    }
    return out;
}

static uint32_t u32(const std::vector<uint8_t>& v, size_t at) { return v[at] | v[at + 1] << 8 | v[at + 2] << 16 | uint32_t(v[at + 3]) << 24; }

struct Decoded {
    uint32_t samples_field = 0;
    std::vector<Fingerprint::Peak> bands[4];
};

// Reads a signature back into peaks, the way the format is laid out in signature_format.rs.
static Decoded decode(const std::vector<uint8_t>& v) {
    Decoded d;
    d.samples_field = u32(v, 40);
    size_t at = 56;
    while (at + 8 <= v.size()) {
        const int band = int(u32(v, at) - 0x60030040u);
        const size_t len = u32(v, at + 4);
        size_t p = at + 8;
        uint32_t pass = 0;
        while (p < at + 8 + len) {
            if (v[p] == 0xff) pass = u32(v, p + 1), p += 5;
            pass += v[p];
            d.bands[band].push_back({pass, uint16_t(v[p + 1] | v[p + 2] << 8), uint16_t(v[p + 3] | v[p + 4] << 8)});
            p += 5;
        }
        at += 8 + len + (4 - len % 4) % 4;
    }
    return d;
}

// How many of a's peaks have a counterpart in b: same band and pass, frequency within two
// sixty-fourths of a bin, magnitude within 4 (it is a log scale, 1477 per factor of e).
static int matched(const Decoded& a, const Decoded& b) {
    int n = 0;
    for (int band = 0; band < 4; ++band)
        for (const auto& p : a.bands[band])
            for (const auto& q : b.bands[band])
                if (p.pass == q.pass && std::abs(int(p.bin) - int(q.bin)) <= 2 && std::abs(int(p.magnitude) - int(q.magnitude)) <= 4) {
                    ++n;
                    break;
                }
    return n;
}

static int total(const Decoded& d) { return int(d.bands[0].size() + d.bands[1].size() + d.bands[2].size() + d.bands[3].size()); }

static void matches_shazamio_core_on_board_recordings() {
    for (const char* name : {"board_wildflower_l", "board_wildflower_r"}) {
        const auto audio = read_wav(std::string("host/fixtures/") + name + ".wav");
        auto uri = read_file(std::string("host/fixtures/") + name + ".uri");
        const std::string ref_uri(uri.begin(), uri.end());
        const std::string prefix = "data:audio/vnd.shazam.sig;base64,";
        CHECK(ref_uri.rfind(prefix, 0) == 0);
        const Decoded ref = decode(unbase64(ref_uri.substr(prefix.size())));

        Fingerprint fp;
        fp.feed(audio.data(), audio.size());
        const auto bin = fp.encode();
        const Decoded ours = decode(bin);

        const int nref = total(ref), nours = total(ours);
        const int found = matched(ref, ours), back = matched(ours, ref);
        int exact = 0;
        for (int band = 0; band < 4; ++band)
            for (size_t i = 0; i < std::min(ref.bands[band].size(), ours.bands[band].size()); ++i) {
                const auto &p = ref.bands[band][i], &q = ours.bands[band][i];
                exact += p.pass == q.pass && p.bin == q.bin && p.magnitude == q.magnitude;
            }
        std::printf("  %s: reference %d peaks, ours %d; %d of theirs found in ours, %d of ours in theirs, %d identical in place%s\n", name, nref, nours, found, back, exact,
                    fp.uri() == ref_uri.substr(0, ref_uri.find('\n')) ? ", whole signature byte-identical" : "");
        CHECK(nref > 100);
        CHECK(found >= nref * 97 / 100);
        CHECK(back >= nours * 97 / 100);
        CHECK(ours.samples_field == ref.samples_field);
        CHECK(fp.sample_ms() == 4000);
    }
}

static void feeding_in_pieces_changes_nothing() {
    const auto audio = read_wav("host/fixtures/board_wildflower_l.wav");
    Fingerprint whole, pieces;
    whole.feed(audio.data(), audio.size());
    std::srand(3);
    for (size_t at = 0; at < audio.size();) {
        const size_t n = std::min(audio.size() - at, size_t(1 + std::rand() % 700));
        pieces.feed(audio.data() + at, n);
        at += n;
    }
    CHECK(whole.uri() == pieces.uri());
    pieces.reset();
    pieces.feed(audio.data(), audio.size());
    CHECK(whole.uri() == pieces.uri());
}

static void format_pieces() {
    const uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(crc32(digits, 9) == 0xCBF43926u);
    const uint8_t foob[] = {'f', 'o', 'o', 'b', 'a', 'r'};
    CHECK(base64(foob, 6) == "Zm9vYmFy");
    CHECK(base64(foob, 4) == "Zm9vYg==");
    CHECK(base64(foob, 5) == "Zm9vYmE=");

    // Silence has no peaks: just the 56 byte header, with the sample count padded by 240 ms.
    Fingerprint fp;
    std::vector<int16_t> quiet(16000, 0);
    fp.feed(quiet.data(), quiet.size());
    const auto v = fp.encode();
    CHECK(v.size() == 56);
    CHECK(u32(v, 0) == 0xcafe2580u && u32(v, 12) == 0x94119c00u);
    CHECK(u32(v, 28) == 3u << 27);
    CHECK(u32(v, 40) == 16000 + 3840);
    CHECK(u32(v, 8) == 8 && u32(v, 52) == 8);
    CHECK(u32(v, 4) == crc32(v.data() + 8, v.size() - 8));
    CHECK(fp.uri().rfind("data:audio/vnd.shazam.sig;base64,gCX+y", 0) == 0);
}

static void a_tone_lands_in_its_band() {
    // 1 kHz is in the 520 to 1450 Hz band; its peaks should sit on bin 1000 / 7.8125 * 64. Rounding
    // the sine to 16 bits adds faint odd harmonics, so 3 and 5 kHz may show up too, barely above
    // the 6144 floor, and nothing else may.
    std::vector<int16_t> tone(16000 * 3);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = int16_t(8000 * std::sin(2 * 3.14159265358979 * 1000.0 * double(i) / 16000.0) * (0.6 + 0.4 * std::sin(double(i) / 900.0)));
    Fingerprint fp;
    fp.feed(tone.data(), tone.size());
    CHECK(fp.band(0).empty());
    CHECK(!fp.band(1).empty());
    for (const auto& p : fp.band(1)) CHECK(std::abs(int(p.bin) - 8192) <= 64 && p.magnitude > 30000);
    for (int b : {2, 3})
        for (const auto& p : fp.band(b)) CHECK((std::abs(int(p.bin) - 3 * 8192) <= 64 || std::abs(int(p.bin) - 5 * 8192) <= 64) && p.magnitude < 6300);
}

int main() {
    matches_shazamio_core_on_board_recordings();
    feeding_in_pieces_changes_nothing();
    format_pieces();
    a_tone_lands_in_its_band();
    return check_report("sig");
}
