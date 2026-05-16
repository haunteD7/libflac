#include "bit_writer.h"

#include <cassert>

void BitWriter::write(uint64_t value, uint8_t num_bits)
{
    bit_buf |= (value << bits_in_buf);
    bits_in_buf += num_bits;

    while(bits_in_buf >= 8)
    {
        push_byte(static_cast<uint8_t>(bit_buf));
        bits_in_buf -= 8;
        bit_buf >>= 8;
    }
}
void BitWriter::flush()
{
    if(bits_in_buf > 0)
    {

    }
}
void BitWriter::push_byte(uint8_t byte)
{
    assert(byte_pos < buffer.size());
    buffer[byte_pos] = byte;
    byte_pos++;
}