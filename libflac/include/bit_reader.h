#pragma once

#include <span>
#include <cstdint>

class BitReader
{
public:
    explicit BitReader(std::span<const uint8_t> buffer) : buffer(buffer) {}

    uint64_t read(size_t num_bits);
    bool read_bit() { return read(1); } // TODO: Optimize

    size_t get_available_bits() const { return bits_left + (buffer.size() - byte_pos) * 8; }
    size_t get_bytes_consumed() const
    {
        size_t consumed_bits = byte_pos * 8 - bits_left;
        return (consumed_bits + 7) / 8; // Round up
    }
    void set_buffer(std::span<const uint8_t> buf)
    {
        buffer = buf;
        byte_pos = 0;
        bit_buf = 0;
        bits_left = 0;
    }
    void align_to_byte()
    {
        bits_left -= bits_left % 8;
    }
private:
    std::span<const uint8_t> buffer;
    size_t byte_pos = 0;
    uint64_t bit_buf = 0;
    size_t bits_left = 0;

    void refill(size_t needed);
};