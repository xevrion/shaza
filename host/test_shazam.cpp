#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.hpp"
#include "json.hpp"
#include "shazam.hpp"

using namespace shaza;

static std::string read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static shazam::Match parse(const std::string& s) { return shazam::parse(s.data(), s.size()); }

static void real_replies() {
    const auto found = read_file("host/fixtures/shazam_wildflower.json");
    CHECK(!found.empty());
    const auto m = parse(found);
    CHECK(m.result == shazam::Match::Result::Found);
    CHECK(m.title == "WILDFLOWER" && m.artist == "Billie Eilish");
    CHECK(m.cover_url.rfind("https://is1-ssl.mzstatic.com/", 0) == 0);
    CHECK(shazam::cover_at(m.cover_url, 200).size() > 20);
    CHECK(shazam::cover_at(m.cover_url, 200).compare(shazam::cover_at(m.cover_url, 200).size() - 15, 15, "/200x200cc.jpg") == 0 ||
          shazam::cover_at(m.cover_url, 200).find("/200x200cc.jpg") != std::string::npos);

    // Every truncation of a real reply is rejected, never half read.
    bool any = false;
    for (size_t cut = 0; cut + 1 < found.size(); cut += 37) any |= parse(found.substr(0, cut)).result != shazam::Match::Result::Bad;
    CHECK(!any);

    const auto none = parse(read_file("host/fixtures/shazam_none.json"));
    CHECK(none.result == shazam::Match::Result::None && none.title.empty());
}

static void odd_replies() {
    CHECK(parse("").result == shazam::Match::Result::Bad);
    CHECK(parse("<html>Too many requests</html>").result == shazam::Match::Result::Bad);
    CHECK(parse("{\"error\":\"x\"}").result == shazam::Match::Result::Bad);
    CHECK(parse("{\"matches\":[]}").result == shazam::Match::Result::None);
    CHECK(parse("{\"matches\":[{}],\"track\":{}}").result == shazam::Match::Result::None);
    const auto m = parse("{\"matches\":[{}],\"track\":{\"title\":\"\\u7d05\\u84ee\\u83ef\",\"subtitle\":\"Ren\\u00e9e \\\"x\\\" \\ud83c\\udfb5\"}}");
    CHECK(m.result == shazam::Match::Result::Found);
    CHECK(m.title == "\xe7\xb4\x85\xe8\x93\xae\xe8\x8f\xaf");             // 紅蓮華
    CHECK(m.artist == "Ren\xc3\xa9" "e \"x\" \xf0\x9f\x8e\xb5");           // Renée "x" 🎵
    CHECK(m.cover_url.empty());
}

static void json_edges() {
    auto ok = [](const char* s) { return Json::parse(s, std::string(s).size()) != nullptr; };
    CHECK(ok("{}") && ok("[]") && ok(" {\"a\" : [1, -2.5e3, true, false, null, \"s\"]} "));
    CHECK(!ok("{") && !ok("{\"a\":}") && !ok("[1,]") && !ok("{\"a\":1}x") && !ok("\"\\ud800\"") && !ok("\"\\q\"") && !ok("\"a\nb\""));
    std::string deep(200, '[');
    deep += std::string(200, ']');
    CHECK(!ok(deep.c_str()));  // too deep for the board's stack, refused rather than recursed into
    const std::string small = "{\"n\":42,\"s\":\"x\"}";
    const auto j = Json::parse(small.data(), small.size());
    CHECK(j && (*j)["n"].number == 42 && (*j)["s"].str() == "x" && (*j)["missing"]["deeper"].is_null());
}

static void request() {
    const uint8_t zeros[16] = {};
    const auto id = shazam::uuid(zeros);
    CHECK(id == "00000000-0000-4000-8000-000000000000");
    const auto p = shazam::path(id, id);
    CHECK(p.rfind("/discovery/v5/en-US/IN/iphone/-/tag/00000000-0000-4000-8000-000000000000/", 0) == 0);
    CHECK(p.find("?sync=true&webv3=true") != std::string::npos);

    Fingerprint fp;
    std::vector<int16_t> quiet(16000 * 4, 0);
    fp.feed(quiet.data(), quiet.size());
    const auto b = shazam::body(fp, 1791701248605ull);
    const auto j = Json::parse(b.data(), b.size());
    CHECK(j != nullptr);
    CHECK((*j)["signature"]["uri"].str() == fp.uri());
    CHECK((*j)["signature"]["samplems"].number == 4000);
    CHECK((*j)["timestamp"].number == 1791701248605.0);
    CHECK((*j)["context"].kind == Json::Kind::Object && (*j)["geolocation"].kind == Json::Kind::Object);

    CHECK(shazam::cover_at("https://x/y/400x400cc.jpg", 200) == "https://x/y/200x200cc.jpg");
    CHECK(shazam::cover_at("https://x/y/100x100bb.jpg", 300) == "https://x/y/300x300cc.jpg");
    CHECK(shazam::cover_at("https://x/y/cover.png", 200) == "https://x/y/cover.png");
}

int main() {
    real_replies();
    odd_replies();
    json_edges();
    request();
    return check_report("shazam");
}
