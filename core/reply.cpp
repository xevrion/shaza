#include "reply.hpp"

#include <cstdio>
#include <cstring>

namespace shaza {

namespace {

constexpr int kMaxSide = 466;

struct Blob {
    std::string name;
    int w, h;
    size_t bytes;
};

bool hex_colour(const std::string& s, Rgb& out) {
    if (s.size() != 6) return false;
    unsigned v = 0;
    for (char c : s) {
        v <<= 4;
        if (c >= '0' && c <= '9') v |= unsigned(c - '0');
        else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
        else return false;
    }
    out = {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    return true;
}

bool blob_line(const std::string& name, const std::string& rest, int per_px, Blob& b) {
    unsigned long bytes = 0;
    int w = 0, h = 0;
    char tail = 0;
    if (std::sscanf(rest.c_str(), "%d %d %lu%c", &w, &h, &bytes, &tail) != 3) return false;
    if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide || bytes != size_t(w) * size_t(h) * size_t(per_px)) return false;
    b = {name, w, h, size_t(bytes)};
    return true;
}

}  // namespace

bool parse_reply(const uint8_t* data, size_t n, Reply& out) {
    const char* text = reinterpret_cast<const char*>(data);
    size_t end = 0;
    while (end + 1 < n && !(text[end] == '\n' && text[end + 1] == '\n')) ++end;
    if (end + 1 >= n) return false;
    const std::string head(text, end + 1);
    size_t pos = end + 2;

    Reply r;
    std::vector<Blob> blobs;
    bool magic = false, status = false;
    size_t at = 0;
    while (at < head.size()) {
        const size_t nl = head.find('\n', at);
        const std::string line = head.substr(at, nl - at);
        at = nl + 1;
        if (!magic) {
            if (line != "shaza1") return false;
            magic = true;
            continue;
        }
        const size_t sp = line.find(' ');
        const std::string key = line.substr(0, sp), value = sp == std::string::npos ? "" : line.substr(sp + 1);
        if (key == "status") {
            if (value == "found") r.status = Reply::Status::Found;
            else if (value == "none") r.status = Reply::Status::None;
            else if (value == "error") r.status = Reply::Status::Error;
            else return false;
            status = true;
        } else if (key == "title") {
            r.title = value;
        } else if (key == "artist") {
            r.artist = value;
        } else if (key == "via") {
            r.via = value;
        } else if (key == "error") {
            r.error = value;
        } else if (key == "accent") {
            if (!hex_colour(value, r.accent)) return false;
        } else if (key == "cover" || key == "title_mask" || key == "artist_mask") {
            Blob b;
            if (!blob_line(key, value, key == "cover" ? 2 : 1, b)) return false;
            blobs.push_back(b);
        }
        // Unknown keys are skipped so the relay can add fields without breaking older boards.
    }
    if (!magic || !status) return false;

    for (const Blob& b : blobs) {
        if (pos + b.bytes > n) return false;
        if (b.name == "cover") {
            r.cover.w = b.w, r.cover.h = b.h;
            r.cover.px.resize(b.bytes / 2);
            for (size_t i = 0; i < r.cover.px.size(); ++i) r.cover.px[i] = uint16_t(data[pos + i * 2] | data[pos + i * 2 + 1] << 8);
        } else {
            Mask& m = b.name == "title_mask" ? r.title_mask : r.artist_mask;
            m.w = b.w, m.h = b.h;
            m.cov.assign(data + pos, data + pos + b.bytes);
        }
        pos += b.bytes;
    }
    if (pos != n) return false;
    if (r.status == Reply::Status::Found && r.title.empty()) return false;
    out = std::move(r);
    return true;
}

}  // namespace shaza
