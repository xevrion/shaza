#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "paint.hpp"

namespace shaza {

struct Picture {
    int w = 0, h = 0;
    std::vector<uint16_t> px;  // RGB565
};

struct Mask {
    int w = 0, h = 0;
    std::vector<uint8_t> cov;
};

struct Reply {
    enum class Status { Found, None, Error };
    Status status = Status::Error;
    std::string title, artist, via, error;
    Rgb accent = {235, 235, 235};
    Picture cover;
    Mask title_mask, artist_mask;
};

// Reads what relay/shaza_relay.py sends (see build_reply there). Anything malformed, truncated
// or of the wrong size makes it return false and leaves out untouched.
bool parse_reply(const uint8_t* data, size_t n, Reply& out);

}  // namespace shaza
