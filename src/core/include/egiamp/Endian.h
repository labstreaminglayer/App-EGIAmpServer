#ifndef EGIAMP_ENDIAN_H
#define EGIAMP_ENDIAN_H

#include <bit>        // C++20: std::endian
#include <cstdint>
#include <cstring>

namespace egiamp {

// C++20 provides std::endian for compile-time endianness detection
constexpr bool is_big_endian = (std::endian::native == std::endian::big);

// Byte swap functions (std::byteswap is C++23, so we keep custom versions)
constexpr uint16_t byteswap16(uint16_t val) {
    return static_cast<uint16_t>((val >> 8) | (val << 8));
}

constexpr uint32_t byteswap32(uint32_t val) {
    return ((val >> 24) & 0x000000FF) |
           ((val >> 8)  & 0x0000FF00) |
           ((val << 8)  & 0x00FF0000) |
           ((val << 24) & 0xFF000000);
}

constexpr uint64_t byteswap64(uint64_t val) {
    return ((val >> 56) & 0x00000000000000FFULL) |
           ((val >> 40) & 0x000000000000FF00ULL) |
           ((val >> 24) & 0x0000000000FF0000ULL) |
           ((val >> 8)  & 0x00000000FF000000ULL) |
           ((val << 8)  & 0x000000FF00000000ULL) |
           ((val << 24) & 0x0000FF0000000000ULL) |
           ((val << 40) & 0x00FF000000000000ULL) |
           ((val << 56) & 0xFF00000000000000ULL);
}

// Big-endian to native conversions
constexpr int64_t big_to_native(int64_t val) {
    if constexpr (is_big_endian) return val;
    return static_cast<int64_t>(byteswap64(static_cast<uint64_t>(val)));
}

constexpr uint64_t big_to_native(uint64_t val) {
    if constexpr (is_big_endian) return val;
    return byteswap64(val);
}

constexpr int32_t big_to_native(int32_t val) {
    if constexpr (is_big_endian) return val;
    return static_cast<int32_t>(byteswap32(static_cast<uint32_t>(val)));
}

constexpr uint32_t big_to_native(uint32_t val) {
    if constexpr (is_big_endian) return val;
    return byteswap32(val);
}

// In-place conversion for float (reinterpret as uint32_t)
inline void big_to_native_inplace(float& val) {
    if constexpr (!is_big_endian) {
        uint32_t tmp;
        std::memcpy(&tmp, &val, sizeof(tmp));
        tmp = byteswap32(tmp);
        std::memcpy(&val, &tmp, sizeof(val));
    }
}

} // namespace egiamp

#endif // EGIAMP_ENDIAN_H
