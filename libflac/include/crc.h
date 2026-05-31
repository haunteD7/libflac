#pragma once
#include <cstdint>
#include <span>

inline uint8_t crc8(std::span<const uint8_t> data) 
{
    uint8_t crc = 0;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}

inline uint16_t crc16(std::span<const uint8_t> data) 
{
    uint16_t crc = 0;
    for (uint8_t byte : data) {
        crc ^= (uint16_t)byte << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x8005 : (crc << 1);
    }
    return crc;
}