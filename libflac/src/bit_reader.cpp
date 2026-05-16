#include "bit_reader.h"

#include <algorithm>

uint64_t BitReader::read(size_t num_bits)
{
    refill(num_bits);
    int bits_to_read = std::min(num_bits, bits_left);
    
    uint64_t result = bit_buf >> (64 - bits_to_read);
    
    bit_buf <<= bits_to_read;
    bits_left -= bits_to_read;
    
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