#include "text.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace shaza {

namespace {

struct Glyph {
    int code, advance4, x0, y0, w, h, offset;  // advance in quarter pixels; y0 is above the baseline when negative
};

struct Kern {
    int left, right, adjust4;
};

struct Face {
    int px, ascent, descent, count;
    const Glyph* glyphs;
    const uint8_t* bits;
    int kern_count;
    const Kern* kerns;
};

#include "ui_font.inc"

const Face& face(Size s) { return s == Size::Small ? k_small : s == Size::Body ? k_body : k_title; }

const Glyph* find(const Face& f, int code) {
    const Glyph* g = std::lower_bound(f.glyphs, f.glyphs + f.count, code, [](const Glyph& a, int c) { return a.code < c; });
    return g != f.glyphs + f.count && g->code == code ? g : nullptr;
}

int kern(const Face& f, int a, int b) {
    if (a > 0x7E || b > 0x7E) return 0;
    const Kern* k = std::lower_bound(f.kerns, f.kerns + f.kern_count, std::make_pair(a, b),
                                     [](const Kern& x, std::pair<int, int> v) { return x.left < v.first || (x.left == v.first && x.right < v.second); });
    return k != f.kerns + f.kern_count && k->left == a && k->right == b ? k->adjust4 : 0;
}

std::vector<int> decode(const std::string& s) {
    std::vector<int> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + size_t(n) > s.size()) {
            ++i;
            continue;
        }
        int cp = n == 1 ? c : c & (0x7F >> n);
        for (int k = 1; k < n; ++k) cp = cp << 6 | (static_cast<unsigned char>(s[i + size_t(k)]) & 63);
        out.push_back(cp);
        i += size_t(n);
    }
    return out;
}

// Only the characters the face can draw; anything else would be a hole in the middle of a word.
std::vector<int> drawable_codes(const Face& f, const std::vector<int>& cps) {
    std::vector<int> out;
    for (int c : cps)
        if (find(f, c)) out.push_back(c);
    return out;
}

void smart_quotes(std::vector<int>& cps) {
    for (size_t i = 0; i < cps.size(); ++i) {
        const bool opening = i == 0 || cps[i - 1] == ' ' || cps[i - 1] == '(' || cps[i - 1] == '[';
        if (cps[i] == '"') cps[i] = opening ? 0x201C : 0x201D;
        else if (cps[i] == '\'') cps[i] = opening ? 0x2018 : 0x2019;
    }
}

int width4(const Face& f, const int* c, size_t n) {
    int w = 0;
    for (size_t i = 0; i < n; ++i) {
        if (const Glyph* g = find(f, c[i])) w += g->advance4;
        if (i + 1 < n) w += kern(f, c[i], c[i + 1]);
    }
    return w;
}

int width(const Face& f, const std::vector<int>& c) { return (width4(f, c.data(), c.size()) + 2) / 4; }

std::vector<int> trimmed(std::vector<int> v) {
    while (!v.empty() && v.back() == ' ') v.pop_back();
    return v;
}

// Greedy wrap on spaces, breaking inside a word only when the word alone is too wide. The same
// rules as the relay's wrap() in relay/shaza_relay.py.
std::vector<std::vector<int>> wrap(const Face& f, const std::vector<int>& text, int max_w, int max_lines) {
    std::vector<std::vector<int>> words;
    for (size_t i = 0; i < text.size();) {
        std::vector<int> w;
        while (i < text.size() && text[i] != ' ') w.push_back(text[i++]);
        while (i < text.size() && text[i] == ' ') ++i;
        if (!w.empty()) w.push_back(' '), words.push_back(w);
    }
    std::vector<std::vector<int>> lines;
    std::vector<int> cur;
    size_t next = 0;
    while (next < words.size() && int(lines.size()) < max_lines) {
        std::vector<int> word = words[next];
        std::vector<int> joined = cur;
        joined.insert(joined.end(), word.begin(), word.end());
        if (width(f, trimmed(joined)) <= max_w) {
            cur = joined, ++next;
            continue;
        }
        if (!cur.empty()) {
            lines.push_back(trimmed(cur)), cur.clear();
            continue;
        }
        size_t cut = 1;
        while (cut < word.size() && width(f, std::vector<int>(word.begin(), word.begin() + long(cut) + 1)) <= max_w) ++cut;
        lines.push_back(trimmed(std::vector<int>(word.begin(), word.begin() + long(cut))));
        std::vector<int> rest(word.begin() + long(cut), word.end());
        while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
        if (trimmed(rest).empty()) ++next;
        else words[next] = rest;
    }
    if (!cur.empty() && int(lines.size()) < max_lines) lines.push_back(trimmed(cur)), cur.clear();
    if (next < words.size() || !cur.empty()) {
        std::vector<int> last = lines.empty() ? std::vector<int>() : lines.back();
        auto with_dots = [&](std::vector<int> v) { return v.push_back(0x2026), v; };
        while (!last.empty() && width(f, with_dots(last)) > max_w) last.pop_back();
        while (!last.empty() && (last.back() == ' ' || last.back() == ',' || last.back() == '-' || last.back() == '(')) last.pop_back();
        if (lines.empty()) lines.push_back({});
        lines.back() = with_dots(last);
    }
    return lines;
}

void draw_glyphs(const Face& f, const std::vector<int>& line, int x4, int baseline, uint8_t* out, int w, int h) {
    for (size_t i = 0; i < line.size(); ++i) {
        const Glyph* g = find(f, line[i]);
        if (!g) continue;
        const int gx = x4 / 4 + g->x0, gy = baseline + g->y0;
        for (int y = 0; y < g->h; ++y) {
            const int oy = gy + y;
            if (oy < 0 || oy >= h) continue;
            for (int x = 0; x < g->w; ++x) {
                const int ox = gx + x;
                if (ox < 0 || ox >= w) continue;
                uint8_t& d = out[oy * w + ox];
                d = std::max(d, f.bits[g->offset + y * g->w + x]);
            }
        }
        x4 += g->advance4 + (i + 1 < line.size() ? kern(f, line[i], line[i + 1]) : 0);
    }
}

}  // namespace

int text_width(const char* s, Size size) {
    const Face& f = face(size);
    return width(f, drawable_codes(f, decode(s)));
}

void text(rk::Surface& sf, int cx, int y, const char* s, Size size, Rgb c, float alpha) {
    const Face& f = face(size);
    const std::vector<int> cps = drawable_codes(f, decode(s));
    int x4 = (cx - width(f, cps) / 2) * 4;
    for (size_t i = 0; i < cps.size(); ++i) {
        const Glyph* g = find(f, cps[i]);
        if (g->w && g->h) mask(sf, x4 / 4 + g->x0, y + g->y0, g->w, g->h, f.bits + g->offset, c, alpha);
        x4 += g->advance4 + (i + 1 < cps.size() ? kern(f, cps[i], cps[i + 1]) : 0);
    }
}

int drawable(const std::string& utf8, Size size) {
    const Face& f = face(size);
    int n = 0;
    for (int c : decode(utf8)) n += c != ' ' && find(f, c) != nullptr;
    return n;
}

Mask layout(const std::string& utf8, Size size, int max_w, int max_lines) {
    const Face& f = face(size);
    std::vector<int> cps = decode(utf8);
    smart_quotes(cps);
    cps = drawable_codes(f, cps);
    // Runs of spaces left behind by skipped characters collapse, and the ends are trimmed.
    std::vector<int> clean;
    for (int c : cps)
        if (!(c == ' ' && (clean.empty() || clean.back() == ' '))) clean.push_back(c);
    clean = trimmed(clean);
    Mask m;
    if (clean.empty() || max_w <= 0 || max_lines <= 0) return m;

    const auto lines = wrap(f, clean, max_w, max_lines);
    const int step = int(std::lround(f.px * 1.22)), pad = int(std::lround(f.px * 0.12));
    std::vector<int> widths;
    for (const auto& l : lines) widths.push_back(std::min(max_w, width(f, l)));
    m.w = *std::max_element(widths.begin(), widths.end()) + 2;
    m.h = step * int(lines.size()) + pad * 2;
    m.cov.assign(size_t(m.w) * size_t(m.h), 0);
    for (size_t i = 0; i < lines.size(); ++i) {
        const int baseline = pad + int(i) * step + f.ascent + int((step - f.px * 1.2f) / 2);
        draw_glyphs(f, lines[i], (m.w - widths[i]) * 2, baseline, m.cov.data(), m.w, m.h);
    }
    return m;
}

}  // namespace shaza
