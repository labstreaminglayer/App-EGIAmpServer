#pragma once

#include <cstdint>

// Cross-platform byte swap utilities
// MSVC uses _byteswap_*, GCC/Clang use __builtin_bswap*
// Only unsigned overloads to avoid ambiguity with size_t on different platforms

#ifdef _MSC_VER
#include <cstdlib>  // for _byteswap_*

inline uint16_t bswap16(uint16_t value) { return _byteswap_ushort(value); }
inline uint32_t bswap32(uint32_t value) { return _byteswap_ulong(value); }
inline uint64_t bswap64(uint64_t value) { return _byteswap_uint64(value); }

#else
// GCC/Clang

inline uint16_t bswap16(uint16_t value) { return __builtin_bswap16(value); }
inline uint32_t bswap32(uint32_t value) { return __builtin_bswap32(value); }
inline uint64_t bswap64(uint64_t value) { return __builtin_bswap64(value); }

#endif
