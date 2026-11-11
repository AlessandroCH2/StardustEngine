#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Stardust::Base64 {

inline std::string Encode(const void* data, size_t size) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* bytes = static_cast<const uint8_t*>(data);
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t n = (uint32_t)bytes[i] << 16 | (i + 1 < size ? (uint32_t)bytes[i + 1] << 8 : 0) | (i + 2 < size ? bytes[i + 2] : 0);
        out += table[n >> 18 & 63];
        out += table[n >> 12 & 63];
        out += i + 1 < size ? table[n >> 6 & 63] : '=';
        out += i + 2 < size ? table[n & 63] : '=';
    }
    return out;
}

// Ignores characters outside the alphabet (line breaks, spaces)
inline std::vector<uint8_t> Decode(std::string_view text) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    out.reserve(text.size() * 3 / 4);
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        const int v = value(c);
        if (v < 0) continue;
        buffer = buffer << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)(buffer >> bits & 0xFF)); }
    }
    return out;
}

} // namespace Stardust::Base64
