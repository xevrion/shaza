#include "shazam.hpp"

#include <cstdio>

#include "json.hpp"

namespace shaza {
namespace shazam {

std::string path(const std::string& uuid1, const std::string& uuid2) {
    return "/discovery/v5/en-US/IN/iphone/-/tag/" + uuid1 + "/" + uuid2 +
           "?sync=true&webv3=true&sampling=true&connected=&shazamapiversion=v3&sharehub=true&hubv5minorversion=v5.1&hidelb=true&video=v3";
}

std::string uuid(const uint8_t r[16]) {
    uint8_t b[16];
    for (int i = 0; i < 16; ++i) b[i] = r[i];
    b[6] = uint8_t((b[6] & 0x0F) | 0x40), b[8] = uint8_t((b[8] & 0x3F) | 0x80);
    char out[37];
    std::snprintf(out, sizeof out, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11],
                  b[12], b[13], b[14], b[15]);
    return out;
}

std::string body(const Fingerprint& fp, uint64_t timestamp_ms) {
    char tail[96];
    std::snprintf(tail, sizeof tail, "\",\"samplems\":%u},\"timestamp\":%llu,\"context\":{},\"geolocation\":{}}", unsigned(fp.sample_ms()), static_cast<unsigned long long>(timestamp_ms));
    return "{\"timezone\":\"Asia/Kolkata\",\"signature\":{\"uri\":\"" + fp.uri() + tail;
}

Match parse(const char* json, size_t n) {
    Match m;
    const auto root = Json::parse(json, n);
    if (!root || root->kind != Json::Kind::Object || (*root)["matches"].kind != Json::Kind::Array) return m;
    const Json& track = (*root)["track"];
    if ((*root)["matches"].items.empty() || track["title"].str().empty()) {
        m.result = Match::Result::None;
        return m;
    }
    m.result = Match::Result::Found;
    m.title = track["title"].str();
    m.artist = track["subtitle"].str();
    m.cover_url = track["images"]["coverart"].str();
    return m;
}

std::string cover_at(const std::string& url, int px) {
    // Apple's image URLs end in /<w>x<h><crop>.jpg, and the server renders whatever size is asked.
    const size_t slash = url.rfind('/');
    if (slash == std::string::npos || url.size() < 4 || url.compare(url.size() - 4, 4, ".jpg") != 0) return url;
    const std::string last = url.substr(slash + 1);
    const size_t x = last.find('x');
    if (x == std::string::npos || x == 0) return url;
    for (size_t i = 0; i < x; ++i)
        if (last[i] < '0' || last[i] > '9') return url;
    char size[32];
    std::snprintf(size, sizeof size, "%dx%dcc.jpg", px, px);
    return url.substr(0, slash + 1) + size;
}

}  // namespace shazam
}  // namespace shaza
