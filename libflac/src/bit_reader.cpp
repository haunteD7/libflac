#include "bit_reader.h"

#include <algorithm>
#include <stdexcept>

uint64_t BitReader::read(size_t num_bits)
{
    if (num_bits == 0) return 0;
    if (num_bits > 64) throw std::runtime_error("Too many bits requested");
    
    refill(num_bits);
    
    if (bits_left < num_bits)
        throw std::runtime_error("Unexpected end of file in bit reader");
    
    uint64_t result = bit_buf >> (64 - num_bits);
    bit_buf <<= num_bits;
    bits_left -= num_bits;
    
    return result;
}

void BitReader::refill(size_t needed)
{
    while (bits_left < needed && byte_pos < buffer.size())
    {
        bit_buf |= (static_cast<uint64_t>(buffer[byte_pos++]) << (56 - bits_left));
        bits_left += 8;
    }
}