#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "signature.hpp"

namespace shaza {

// Shazam's recognition endpoint, called the way shazamio calls it (MIT,
// https://github.com/shazamio/shazamio, misc.py and api.py). Unofficial: it can change or stop
// answering without notice, and using it is against Shazam's terms.
namespace shazam {

constexpr const char* kHost = "amp.shazam.com";
constexpr const char* kUserAgent = "Dalvik/2.1.0 (Linux; U; Android 5.0.2; VS980 4G Build/LRX22G)";

// The path and query for one request; the two ids are fresh UUIDs, upper case.
std::string path(const std::string& uuid1, const std::string& uuid2);
// A random version 4 UUID from 16 random bytes.
std::string uuid(const uint8_t random[16]);
// The JSON body. timestamp_ms is the wall clock in milliseconds; Shazam does not seem to mind
// when it is wrong, but the board sets it from NTP anyway.
std::string body(const Fingerprint& fp, uint64_t timestamp_ms);

struct Match {
    enum class Result { Found, None, Bad };
    Result result = Result::Bad;
    std::string title, artist, cover_url;
};

// Reads a reply. A reply with no matches is None; anything that is not the JSON Shazam sends is Bad.
Match parse(const char* json, size_t n);

// The cover URL rewritten to ask Apple's image server for a square of side px.
std::string cover_at(const std::string& url, int px);

}  // namespace shazam

}  // namespace shaza
