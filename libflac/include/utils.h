#pragma once

#include <cstdint>

static constexpr uint8_t MAX_CHANNELS = 7;
static constexpr uint8_t MAX_PARTITION_ORDER = 4;
static constexpr uint8_t MAX_PARTITIONS = 1 << MAX_PARTITION_ORDER;
static constexpr size_t  MAX_SAMPLES_IN_BLOCK = 16384;
static constexpr uint8_t DEFAULT_QLP_PRECISION = 12;
static constexpr uint8_t DEFAULT_QLP_SHIFT = 8;

static constexpr uint8_t MAX_LPC_ORDER = 32;

/// @brief Example for 24 bit: 0xFFFFFF -> -1
static inline int32_t sign_extend(uint64_t value, uint8_t bits)
{
    uint32_t sign_bit = 1u << (bits - 1);
    return int32_t((value ^ sign_bit) - sign_bit);
}
/// @brief Example for 24 bit: -1 -> 0xFFFFFF
static inline uint32_t unsign_extend(int32_t value, uint8_t bits)
{
    return uint32_t(value) & ((1u << bits) - 1);
}
/// @brief Example: 2 -> 1; 4 -> 2; 1 -> -1; 2 -> -2
static inline int32_t zigzag_decode(uint32_t u)
{
    return static_cast<int32_t>((u >> 1) ^ -(u & 1));
}
/// @brief Example: 1 -> 2; 2 -> 4; -1 -> 1; -2 -> 2
static inline uint32_t zigzag_encode(int32_t s)
{
    return (uint32_t(s) << 1) ^ uint32_t(s >> 31);
}