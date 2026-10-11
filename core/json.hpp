#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace shaza {

// Just enough JSON for Shazam's replies: a tree of values, strings decoded to UTF-8. Anything
// malformed makes parse() return null rather than a half-built tree.
struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Json> items;
    std::map<std::string, Json> fields;

    static std::unique_ptr<Json> parse(const char* text, size_t n);

    // The field called key, or a shared null value when this is not an object or has no such field.
    const Json& operator[](const char* key) const;
    const std::string& str() const { return string; }
    bool is_null() const { return kind == Kind::Null; }
};

}  // namespace shaza
