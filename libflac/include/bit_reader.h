#pragma once

#include <span>
#include <cstdint>

class BitReader {
public:
    explicit BitReader(std::span<const uint8_t> buffer) : buffer(buffer) {}
    
    uint64_t read(size_t num_bits);
    
    size_t get_available_bits() const { return bits_left + (buffer.size() - byte_pos) * 8; }
private:
    std::span<const uint8_t> buffer; 
    size_t byte_pos = 0;
    uint64_t bit_buf = 0;
    size_t bits_left = 0;

    void refill(size_t needed);
};