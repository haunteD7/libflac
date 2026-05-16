#include "bit_reader.h"

uint64_t BitReader::read(int num_bits)
{
    refill(num_bits);
    int bits_to_read = std::min(num_bits, bits_left);
    uint64_t result = bit_buf & ((1ULL << bits_to_read) - 1);
    bit_buf >>= bits_to_read;
    bits_left -= bits_to_read;
    return result;
}

void BitReader::refill(int needed)
{
    while (bits_left < needed && byte_pos < buffer.size())
    {
        bit_buf |= (static_cast<uint64_t>(buffer[byte_pos++]) << bits_left);
        bits_left += 8;
    }
}