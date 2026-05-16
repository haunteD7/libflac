#pragma once

#include <span>
#include <cstdint>

class BitWriter
{
public:
    explicit BitWriter(std::span<uint8_t> buffer) : buffer(buffer) {}
    ~BitWriter();

    void write(uint64_t value, uint8_t num_bits);
    void flush();
private:
    void push_byte(uint8_t byte);

    std::span<uint8_t> buffer;
    uint64_t bit_buf = 0;
    size_t byte_pos = 0;
    uint8_t bits_in_buf = 0;
};