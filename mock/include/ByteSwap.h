#pragma once

// Cross-platform byte swap utilities
// MSVC uses _byteswap_*, GCC/Clang use __builtin_bswap*

#ifdef _MSC_VER
#include <cstdlib>  // for _byteswap_*

inline uint16_t bswap16(uint16_t value) { return _byteswap_ushort(value); }
inline int16_t bswap16(int16_t value) { return static_cast<int16_t>(_byteswap_ushort(static_cast<uint16_t>(value))); }
inline uint32_t bswap32(uint32_t value) { return _byteswap_ulong(value); }
inline int32_t bswap32(int32_t value) { return static_cast<int32_t>(_byteswap_ulong(static_cast<uint32_t>(value))); }
inline uint64_t bswap64(uint64_t value) { return _byteswap_uint64(value); }
inline int64_t bswap64(int64_t value) { return static_cast<int64_t>(_byteswap_uint64(static_cast<uint64_t>(value))); }

#else
// GCC/Clang

inline uint16_t bswap16(uint16_t value) { return __builtin_bswap16(value); }
inline int16_t bswap16(int16_t value) { return static_cast<int16_t>(__builtin_bswap16(static_cast<uint16_t>(value))); }
inline uint32_t bswap32(uint32_t value) { return __builtin_bswap32(value); }
inline int32_t bswap32(int32_t value) { return static_cast<int32_t>(__builtin_bswap32(static_cast<uint32_t>(value))); }
inline uint64_t bswap64(uint64_t value) { return __builtin_bswap64(value); }
inline int64_t bswap64(int64_t value) { return static_cast<int64_t>(__builtin_bswap64(static_cast<uint64_t>(value))); }

#endif
