#pragma once

#include <span>
#include <cstdint>

class BitReader {
public:
    explicit BitReader(std::span<const uint8_t> buffer) : buffer(buffer) {}
    
    uint64_t read(int num_bits);
    
    int available_bits() const 
    {
        return bits_left + (buffer.size() - byte_pos) * 8;
    }
private:
    std::span<const uint8_t> buffer; 
    size_t byte_pos = 0;
    uint64_t bit_buf = 0;
    int bits_left = 0;

    void refill(int needed);
};