#include "signature.hpp"

#include <algorithm>
#include <cmath>

namespace shaza {

namespace {

constexpr int kN = 2048, kHalf = kN / 2, kBins = kHalf + 1, kRows = 256;

void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i)));
}
void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x)), v.push_back(uint8_t(x >> 8));
}
void set32(std::vector<uint8_t>& v, size_t at, uint32_t x) {
    for (int i = 0; i < 4; ++i) v[at + i] = uint8_t(x >> (8 * i));
}

}  // namespace

Fingerprint::Fingerprint()
    : ring_(kN), window_(kN), frame_(kN), re_(kHalf), im_(kHalf), cos_(kHalf), sin_(kHalf), bitrev_(kHalf),
      fft_out_(size_t(kRows) * kBins), spread_out_(size_t(kRows) * kBins) {
    const double pi = 3.14159265358979323846;
    // The Hann window shazamio-core tabulates, as the formula it was tabulated from.
    for (int i = 0; i < kN; ++i) window_[i] = float(0.5 - 0.5 * std::cos(2.0 * pi * (i + 1) / (kN + 1)));
    // Twiddles for the 2048-point transform; the 1024-point one uses every second of them.
    for (int k = 0; k < kHalf; ++k) cos_[k] = float(std::cos(2.0 * pi * k / kN)), sin_[k] = float(std::sin(2.0 * pi * k / kN));
    for (int i = 0, bits = 10; i < kHalf; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
        bitrev_[i] = uint16_t(r);
    }
    reset();
}

void Fingerprint::reset() {
    std::fill(ring_.begin(), ring_.end(), 0);
    std::fill(fft_out_.begin(), fft_out_.end(), 0.0f);
    std::fill(spread_out_.begin(), spread_out_.end(), 0.0f);
    pending_.clear();
    for (auto& b : bands_) b.clear();
    ring_index_ = fft_index_ = spread_index_ = samples_ = 0;
    spread_done_ = 0;
}

void Fingerprint::feed(const int16_t* s, size_t n) {
    samples_ += n;
    // Whole blocks of 128; a remainder waits for the next call, and one left at the end is
    // dropped, as shazamio-core's chunks_exact drops it.
    while (n > 0) {
        if (pending_.empty() && n >= 128) {
            block(s);
            s += 128, n -= 128;
            continue;
        }
        const size_t take = std::min(n, 128 - pending_.size());
        pending_.insert(pending_.end(), s, s + take);
        s += take, n -= take;
        if (pending_.size() == 128) {
            block(pending_.data());
            pending_.clear();
        }
    }
}

void Fingerprint::block(const int16_t* s) {
    std::copy(s, s + 128, ring_.begin() + long(ring_index_));
    ring_index_ = (ring_index_ + 128) & (kN - 1);
    fft();
    spread();
    if (++spread_done_ >= 46) recognise();
}

// Power spectrum of the windowed last 2048 samples. The real input is packed into a 1024-point
// complex transform (even samples real, odd imaginary) and split apart afterwards.
void Fingerprint::fft() {
    for (int i = 0; i < kN; ++i) frame_[i] = float(ring_[(i + ring_index_) & (kN - 1)]) * window_[i];
    for (int i = 0; i < kHalf; ++i) re_[bitrev_[i]] = frame_[2 * i], im_[bitrev_[i]] = frame_[2 * i + 1];
    for (int len = 2; len <= kHalf; len <<= 1) {
        const int step = kN / len;  // into the 2048-point twiddles
        for (int i = 0; i < kHalf; i += len) {
            for (int j = 0; j < len / 2; ++j) {
                const float wr = cos_[j * step], wi = -sin_[j * step];
                const int a = i + j, b = a + len / 2;
                const float tr = re_[b] * wr - im_[b] * wi, ti = re_[b] * wi + im_[b] * wr;
                re_[b] = re_[a] - tr, im_[b] = im_[a] - ti;
                re_[a] += tr, im_[a] += ti;
            }
        }
    }
    float* out = &fft_out_[fft_index_ * kBins];
    for (int k = 0; k <= kHalf; ++k) {
        const int a = k % kHalf, b = (kHalf - k) % kHalf;
        // Even and odd halves of the real input's spectrum, then recombined with the twiddle.
        const float er = 0.5f * (re_[a] + re_[b]), ei = 0.5f * (im_[a] - im_[b]);
        const float orr = 0.5f * (im_[a] + im_[b]), oi = -0.5f * (re_[a] - re_[b]);
        float wr = 1, wi = 0;
        if (k < kHalf) wr = cos_[k], wi = -sin_[k];
        else wr = -1, wi = 0;
        const float xr = er + orr * wr - oi * wi, xi = ei + orr * wi + oi * wr;
        out[k] = std::max((xr * xr + xi * xi) / float(1 << 17), 0.0000000001f);
    }
    fft_index_ = (fft_index_ + 1) & (kRows - 1);
}

void Fingerprint::spread() {
    const float* src = &fft_out_[((fft_index_ - 1) & (kRows - 1)) * kBins];
    float* dst = &spread_out_[spread_index_ * kBins];
    std::copy(src, src + kBins, dst);
    for (int p = 0; p <= kBins - 3; ++p) dst[p] = std::max(std::max(dst[p], dst[p + 1]), dst[p + 2]);
    for (int former : {1, 3, 6}) {
        float* f = &spread_out_[((spread_index_ - former) & (kRows - 1)) * kBins];
        for (int p = 0; p < kBins; ++p) f[p] = std::max(f[p], dst[p]);
    }
    spread_index_ = (spread_index_ + 1) & (kRows - 1);
}

void Fingerprint::recognise() {
    const float* f46 = &fft_out_[((fft_index_ - 46) & (kRows - 1)) * kBins];
    const float* s49 = &spread_out_[((spread_index_ - 49) & (kRows - 1)) * kBins];
    static const int kNear[] = {-10, -7, -4, -3, 1, 2, 5, 8};
    static const int kOther[] = {-53, -45, 165, 172, 179, 186, 193, 200, 214, 221, 228, 235, 242, 249};
    auto magnitude = [](float v) { return std::max(std::log(v), 1.0f / 64.0f) * 1477.3f + 6144.0f; };

    for (int bin = 10; bin <= 1014; ++bin) {
        const float v = f46[bin];
        if (v < 1.0f / 64.0f || v < s49[bin - 1]) continue;
        float near = 0;
        for (int o : kNear) near = std::max(near, s49[bin + o]);
        if (!(v > near)) continue;
        float other = near;
        for (int o : kOther) other = std::max(other, spread_out_[((spread_index_ + size_t(o)) & (kRows - 1)) * kBins + size_t(bin - 1)]);
        if (!(v > other)) continue;

        const float m = magnitude(v), before = magnitude(f46[bin - 1]), after = magnitude(f46[bin + 1]);
        const float v1 = m * 2.0f - before - after;
        const float v2 = (after - before) * 32.0f / v1;
        const uint16_t corrected = uint16_t(bin * 64 + int(v2));
        const float hz = float(corrected) * (16000.0f / 2.0f / 1024.0f / 64.0f);
        const int band = int(hz) >= 250 && int(hz) <= 519 ? 0 : int(hz) >= 520 && int(hz) <= 1449 ? 1 : int(hz) >= 1450 && int(hz) <= 3499 ? 2 : int(hz) >= 3500 && int(hz) <= 5500 ? 3 : -1;
        if (band < 0) continue;
        bands_[band].push_back({spread_done_ - 46, uint16_t(m), corrected});
    }
}

std::vector<uint8_t> Fingerprint::encode() const {
    std::vector<uint8_t> v;
    put32(v, 0xcafe2580);
    put32(v, 0);  // crc32, filled in last
    put32(v, 0);  // size after the header
    put32(v, 0x94119c00);
    put32(v, 0), put32(v, 0), put32(v, 0);
    put32(v, 3u << 27);  // 16 kHz
    put32(v, 0), put32(v, 0);
    put32(v, uint32_t(samples_) + uint32_t(float(kRate) * 0.24f));
    put32(v, (15u << 19) + 0x40000);
    put32(v, 0x40000000);
    put32(v, 0);  // size after the header, again
    for (int b = 0; b < 4; ++b) {
        if (bands_[b].empty()) continue;
        std::vector<uint8_t> peaks;
        uint32_t pass = 0;
        for (const Peak& p : bands_[b]) {
            if (p.pass - pass >= 255) {
                peaks.push_back(0xff);
                put32(peaks, p.pass);
                pass = p.pass;
            }
            peaks.push_back(uint8_t(p.pass - pass));
            put16(peaks, p.magnitude);
            put16(peaks, p.bin);
            pass = p.pass;
        }
        put32(v, 0x60030040u + uint32_t(b));
        put32(v, uint32_t(peaks.size()));
        v.insert(v.end(), peaks.begin(), peaks.end());
        while (v.size() % 4) v.push_back(0);
    }
    set32(v, 8, uint32_t(v.size() - 48));
    set32(v, 52, uint32_t(v.size() - 48));
    set32(v, 4, crc32(v.data() + 8, v.size() - 8));
    return v;
}

std::string Fingerprint::uri() const {
    const auto bin = encode();
    return "data:audio/vnd.shazam.sig;base64," + base64(bin.data(), bin.size());
}

std::string base64(const uint8_t* p, size_t n) {
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = uint32_t(p[i]) << 16 | (i + 1 < n ? uint32_t(p[i + 1]) << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        out += abc[(v >> 18) & 63];
        out += abc[(v >> 12) & 63];
        out += i + 1 < n ? abc[(v >> 6) & 63] : '=';
        out += i + 2 < n ? abc[v & 63] : '=';
    }
    return out;
}

uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

}  // namespace shaza
