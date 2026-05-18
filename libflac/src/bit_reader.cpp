#include "bit_reader.h"

// Helpers 

static inline uint16_t bswap16(uint16_t v) noexcept
{
    return static_cast<uint16_t>((v << 8) | (v >> 8));
}
static inline uint32_t bswap32(uint32_t v) noexcept
{
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8)
         | ((v & 0x00FF0000u) >>  8) | ((v & 0xFF000000u) >> 24);
}
static inline uint64_t bswap64(uint64_t v) noexcept
{
    v = ((v & 0x00FF00FF00FF00FFull) <<  8) | ((v & 0xFF00FF00FF00FF00ull) >>  8);
    v = ((v & 0x0000FFFF0000FFFFull) << 16) | ((v & 0xFFFF0000FFFF0000ull) >> 16);
    v = ((v & 0x00000000FFFFFFFFull) << 32) | (v >> 32);
    return v;
}

// refill

void BitReader::refill(size_t needed) noexcept
{
    // We can hold at most 64 bits; load bytes until we have `needed` or EOF.
    while (bits_left < needed && byte_pos < buffer.size())
    {
        // Guard: don't shift into undefined territory (bits_left must be < 64
        // before we OR in another byte, otherwise the shift is UB on 64-bit).
        if (bits_left > 56) break;   // already have 57-63 bits — enough for ≤63

        bit_buf   |= static_cast<uint64_t>(buffer[byte_pos++]) << (56 - bits_left);
        bits_left += 8;
    }
}

// read 

uint64_t BitReader::read(size_t num_bits)
{
    if (num_bits == 0)   return 0;
    if (num_bits > 64)   throw std::runtime_error("BitReader: requested more than 64 bits");

    refill(num_bits);

    if (bits_left < num_bits)
        throw std::runtime_error("BitReader: unexpected end of data");

    uint64_t result;
    if (num_bits == 64)
    {
        result  = bit_buf;          // take all 64 bits
        bit_buf = 0;
    }
    else
    {
        result   = bit_buf >> (64 - num_bits);
        bit_buf <<= num_bits;
    }
    bits_left -= num_bits;
    return result;
}

// peek 

uint64_t BitReader::peek(size_t num_bits)
{
    if (num_bits == 0)  return 0;
    if (num_bits > 64)  throw std::runtime_error("BitReader: requested more than 64 bits");

    refill(num_bits);

    if (bits_left < num_bits)
        throw std::runtime_error("BitReader: unexpected end of data");

    return (num_bits == 64) ? bit_buf : (bit_buf >> (64 - num_bits));
}

// skip 

void BitReader::skip(size_t num_bits)
{
    size_t from_buf = std::min(num_bits, bits_left);
    bit_buf   <<= from_buf;
    bits_left  -= from_buf;
    num_bits   -= from_buf;
 
    size_t whole_bytes = num_bits / 8;
    if (byte_pos + whole_bytes > buffer.size())
        throw std::runtime_error("BitReader::skip: unexpected end of data");
    byte_pos += whole_bytes;
    num_bits %= 8;
 
    if (num_bits > 0)
    {
        refill(num_bits);
        if (bits_left < num_bits)
            throw std::runtime_error("BitReader::skip: unexpected end of data");
        bit_buf   <<= num_bits;
        bits_left  -= num_bits;
    }
}
 
void BitReader::skip_bytes(size_t num_bytes)
{
    align_to_byte();
 
    size_t from_buf = std::min(num_bytes, bits_left / 8);
    bit_buf   <<= from_buf * 8;
    bits_left  -= from_buf * 8;
    num_bytes  -= from_buf;
 
    if (byte_pos + num_bytes > buffer.size())
        throw std::runtime_error("BitReader::skip_bytes: unexpected end of data");
    byte_pos += num_bytes;
}
 


// Aligned multi-byte helpers 

uint64_t BitReader::read_bytes_be(size_t n)
{
    align_to_byte();

    uint64_t result = 0;
    for (size_t i = 0; i < n; ++i)
        result = (result << 8) | read(8);

    return result;
}

// 8-bit 

uint8_t BitReader::read8()
{
    return static_cast<uint8_t>(read_bytes_be(1));
}

// 16-bit 

uint16_t BitReader::read16_be()
{
    return static_cast<uint16_t>(read_bytes_be(2));
}

uint16_t BitReader::read16_le()
{
    return bswap16(read16_be());
}

// 24-bit 

uint32_t BitReader::read24_be()
{
    return static_cast<uint32_t>(read_bytes_be(3));
}

uint32_t BitReader::read24_le()
{
    // BE reads bytes as [b2, b1, b0]; swap to LE: b0 | b1<<8 | b2<<16
    uint32_t v = read24_be();
    return ((v & 0x0000FFu) << 16)
         |  (v & 0x00FF00u)
         | ((v & 0xFF0000u) >> 16);
}

// 32-bit 

uint32_t BitReader::read32_be()
{
    return static_cast<uint32_t>(read_bytes_be(4));
}

uint32_t BitReader::read32_le()
{
    return bswap32(read32_be());
}

// 64-bit 

uint64_t BitReader::read64_be()
{
    return read_bytes_be(8);
}

uint64_t BitReader::read64_le()
{
    return bswap64(read64_be());
}