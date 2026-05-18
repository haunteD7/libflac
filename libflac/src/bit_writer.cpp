#include "bit_writer.h"

// ── Helpers ──────────────────────────────────────────────────────────────────

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

// drain 

void BitWriter::drain()
{
    while (bits_in_buf >= 8)
    {
        if (byte_pos >= buffer.size())
            throw std::runtime_error("BitWriter: buffer overflow");

        buffer[byte_pos++] = static_cast<uint8_t>(bit_buf >> 56);
        bit_buf     <<= 8;
        bits_in_buf  -= 8;
    }
}

// write 

void BitWriter::write(uint64_t value, size_t num_bits)
{
    if (num_bits == 0)  return;
    if (num_bits > 64)  throw std::runtime_error("BitWriter: requested more than 64 bits");

    // Mask off any stray high bits in value
    if (num_bits < 64)
        value &= (uint64_t{1} << num_bits) - 1;

    // Case: fits entirely into the remaining space in bit_buf
    if (bits_in_buf + num_bits <= 64)
    {
        bit_buf     |= value << (64 - bits_in_buf - num_bits);
        bits_in_buf += num_bits;
        drain();
        return;
    }

    // Case: straddles the 64-bit boundary — split into two writes
    size_t hi_bits = 64 - bits_in_buf;          // bits that fit now
    size_t lo_bits = num_bits - hi_bits;         // bits for the next round

    bit_buf     |= value >> lo_bits;             // top hi_bits of value
    bits_in_buf  = 64;
    drain();                                     // flushes all 8 bytes

    bit_buf      = value << (64 - lo_bits);      // remaining lo_bits, left-aligned
    bits_in_buf  = lo_bits;
    drain();
}

// write_bit

void BitWriter::write_bit(bool bit)
{
    bit_buf     |= static_cast<uint64_t>(bit) << (63 - bits_in_buf);
    bits_in_buf += 1;
    if (bits_in_buf == 8) [[unlikely]]
        drain();
}

// flush

void BitWriter::flush()
{
    if (bits_in_buf == 0) return;

    // Zero-pad to a full byte and emit
    align_to_byte();
    drain();
}

// Aligned multi-byte helpers 

void BitWriter::write_bytes_be(uint64_t value, size_t n)
{
    align_to_byte();

    // Shift value so the first byte to emit is in the top 8 bits
    for (size_t i = n; i > 0; --i)
        write(( value >> ((i - 1) * 8)) & 0xFF, 8);
}

// 8-bit 

void BitWriter::write8(uint8_t value)
{
    write_bytes_be(value, 1);
}

// 16-bit 

void BitWriter::write16_be(uint16_t value)
{
    write_bytes_be(value, 2);
}

void BitWriter::write16_le(uint16_t value)
{
    write_bytes_be(bswap16(value), 2);
}

// 24-bit 

void BitWriter::write24_be(uint32_t value)
{
    write_bytes_be(value & 0x00FFFFFFu, 3);
}

void BitWriter::write24_le(uint32_t value)
{
    // Swap 3 bytes: [b0, b1, b2] → [b2, b1, b0]
    uint32_t swapped = ((value & 0x0000FFu) << 16)
                     |  (value & 0x00FF00u)
                     | ((value & 0xFF0000u) >> 16);
    write_bytes_be(swapped, 3);
}

// 32-bit

void BitWriter::write32_be(uint32_t value)
{
    write_bytes_be(value, 4);
}

void BitWriter::write32_le(uint32_t value)
{
    write_bytes_be(bswap32(value), 4);
}

// 64-bit 

void BitWriter::write64_be(uint64_t value)
{
    write_bytes_be(value, 8);
}

void BitWriter::write64_le(uint64_t value)
{
    write_bytes_be(bswap64(value), 8);
}