#include "bit_writer.h"

#include <cassert>

void BitWriter::write(uint64_t value, uint8_t num_bits)
{
    bit_buf |= (value << (64 - bits_in_buf - num_bits));  
    bits_in_buf += num_bits;

    while (bits_in_buf >= 8)
    {
        uint8_t byte = static_cast<uint8_t>(bit_buf >> 56); 
        push_byte(byte);
        bit_buf <<= 8;      
        bits_in_buf -= 8;
    }
}

void BitWriter::write_bit(bool bit)
{
    bit_buf |= (static_cast<uint64_t>(bit) << (64 - bits_in_buf - 1));
    bits_in_buf += 1;

    if (bits_in_buf >= 8)
    {
        uint8_t byte = static_cast<uint8_t>(bit_buf >> 56);
        push_byte(byte);
        bit_buf <<= 8;
        bits_in_buf -= 8;
    }
}
void BitWriter::push_byte(uint8_t byte)
{
    assert(byte_pos < buffer.size());
    buffer[byte_pos] = byte;
    byte_pos++;
}