#include "json.hpp"

#include <cstdlib>

namespace shaza {

namespace {

constexpr int kMaxDepth = 64;

struct Reader {
    const char* p;
    const char* end;

    void space() {
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    }
    bool eat(char c) {
        space();
        if (p < end && *p == c) return ++p, true;
        return false;
    }
    bool word(const char* w) {
        const char* q = p;
        while (*w) {
            if (q >= end || *q != *w) return false;
            ++q, ++w;
        }
        p = q;
        return true;
    }

    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) out += char(0xC0 | cp >> 6), out += char(0x80 | (cp & 63));
        else if (cp < 0x10000) out += char(0xE0 | cp >> 12), out += char(0x80 | ((cp >> 6) & 63)), out += char(0x80 | (cp & 63));
        else out += char(0xF0 | cp >> 18), out += char(0x80 | ((cp >> 12) & 63)), out += char(0x80 | ((cp >> 6) & 63)), out += char(0x80 | (cp & 63));
    }

    bool hex4(unsigned& v) {
        if (end - p < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i, ++p) {
            const char c = *p;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool string(std::string& out) {
        if (!eat('"')) return false;
        while (p < end && *p != '"') {
            if (static_cast<unsigned char>(*p) < 0x20) return false;
            if (*p != '\\') {
                out += *p++;
                continue;
            }
            if (++p >= end) return false;
            const char e = *p++;
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00) {
                    unsigned lo;
                    if (end - p < 6 || p[0] != '\\' || p[1] != 'u') return false;
                    p += 2;
                    if (!hex4(lo) || lo < 0xDC00 || lo >= 0xE000) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp < 0xE000) {
                    return false;
                }
                utf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return p < end && *p++ == '"';
    }

    bool value(Json& v, int depth) {
        if (depth > kMaxDepth) return false;
        space();
        if (p >= end) return false;
        const char c = *p;
        if (c == '{') {
            ++p;
            v.kind = Json::Kind::Object;
            if (eat('}')) return true;
            do {
                std::string key;
                space();
                if (!string(key) || !eat(':')) return false;
                if (!value(v.fields[key], depth + 1)) return false;
            } while (eat(','));
            return eat('}');
        }
        if (c == '[') {
            ++p;
            v.kind = Json::Kind::Array;
            if (eat(']')) return true;
            do {
                v.items.emplace_back();
                if (!value(v.items.back(), depth + 1)) return false;
            } while (eat(','));
            return eat(']');
        }
        if (c == '"') {
            v.kind = Json::Kind::String;
            return string(v.string);
        }
        if (word("true")) return v.kind = Json::Kind::Bool, v.boolean = true, true;
        if (word("false")) return v.kind = Json::Kind::Bool, true;
        if (word("null")) return true;
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* start = p;
            while (p < end && (*p == '-' || *p == '+' || *p == '.' || *p == 'e' || *p == 'E' || (*p >= '0' && *p <= '9'))) ++p;
            v.kind = Json::Kind::Number;
            v.number = std::strtod(std::string(start, p).c_str(), nullptr);
            return true;
        }
        return false;
    }
};

}  // namespace

std::unique_ptr<Json> Json::parse(const char* text, size_t n) {
    auto root = std::make_unique<Json>();
    Reader r{text, text + n};
    if (!r.value(*root, 0)) return nullptr;
    r.space();
    if (r.p != r.end) return nullptr;
    return root;
}

const Json& Json::operator[](const char* key) const {
    static const Json null;
    if (kind != Kind::Object) return null;
    const auto it = fields.find(key);
    return it == fields.end() ? null : it->second;
}

}  // namespace shaza
