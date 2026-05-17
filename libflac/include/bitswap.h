#ifndef BITSWAP_H
#define BITSWAP_H

#include <stdint.h>

#if defined(__STDC_ENDIAN_LITTLE__)           // C23
    #define MY_LITTLE_ENDIAN 1
#elif defined(__STDC_ENDIAN_BIG__)
    #define MY_LITTLE_ENDIAN 0
#elif defined(__BYTE_ORDER__)                 // GCC/Clang
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        #define MY_LITTLE_ENDIAN 1
    #else
        #define MY_LITTLE_ENDIAN 0
    #endif
#elif defined(_WIN32) || defined(__LITTLE_ENDIAN__) // Windows is always LE
    #define MY_LITTLE_ENDIAN 1
#elif defined(__BIG_ENDIAN__)
    #define MY_LITTLE_ENDIAN 0
#else
    #error "Cannot determine endianness at compile time"
#endif


#ifdef __GNUC__

inline static uint64_t bitswap64(uint64_t val)
{
  return __builtin_bswap64(val);
}
inline static uint32_t bitswap32(uint32_t val)
{
  return __builtin_bswap32(val);
}
inline static uint16_t bitswap16(uint16_t val)
{
  return __builtin_bswap16(val);
}

#endif

#ifdef _MSC_VER

inline static uint64_t bitswap64(uint64_t val)
{
  return _byteswap_uint64(val);
}
inline static uint32_t bitswap32(uint32_t val)
{
  return _byteswap_ulong(val);
}
inline static uint16_t bitswap16(uint16_t val)
{
  return _byteswap_ushort(val);
}

#endif

#ifdef MY_LITTLE_ENDIAN /* Convert big endian to host */

inline static uint64_t betoh64(uint64_t val) 
{
  return bitswap64(val);
}
inline static uint32_t betoh32(uint32_t val)
{
  return bitswap32(val);
}
inline static uint16_t betoh16(uint16_t val)
{
  return bitswap16(val);
}

#else

inline static uint64_t betoh64(uint64_t val) 
{
  return val;
}
inline static uint32_t betoh32(uint32_t val)
{
  return val;
}
inline static uint16_t betoh16(uint16_t val)
{
  return val;
}

#endif

#endif