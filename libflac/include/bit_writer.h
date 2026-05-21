#pragma once
#include <span>
#include <cstdint>
#include <stdexcept>

class BitWriter
{
public:
    BitWriter() noexcept : buffer(std::span<uint8_t>()) {}
    explicit BitWriter(std::span<uint8_t> buf) noexcept
        : buffer(buf) {}

    /// Write the lowest num_bits of value, MSB first. num_bits must be [1..64].
    void write(uint64_t value, size_t num_bits);

    /// Write a single bit.
    void write_bit(bool bit);

    // Byte-aligned multi-byte writes 
    // All of these flush any sub-byte remainder first (align_to_byte).

    void write8  (uint8_t  value);
    void write16_be(uint16_t value);
    void write16_le(uint16_t value);
    void write24_be(uint32_t value);
    void write24_le(uint32_t value);
    void write32_be(uint32_t value);
    void write32_le(uint32_t value);
    void write64_be(uint64_t value);
    void write64_le(uint64_t value);

    // Alignment / state 

    /// Pad with zero bits until the writer is on a byte boundary.
    void align_to_byte() noexcept
    {
        size_t remainder = bits_in_buf % 8;
        if (remainder)
            bits_in_buf += 8 - remainder; // zero-pad: high bits of bit_buf are 0
    }

    /// Flush any buffered bits (zero-padded to a full byte) to the output.
    void flush();

    /// Reset to a new buffer, discarding all internal state.
    void set_buffer(std::span<uint8_t> buf) noexcept
    {
        buffer     = buf;
        byte_pos   = 0;
        bit_buf    = 0;
        bits_in_buf = 0;
    }

    // Introspection 

    /// Total capacity of the output buffer in bytes.
    [[nodiscard]] size_t get_buffer_size() const noexcept { return buffer.size(); }

    /// Number of bytes fully or partially written so far.
    [[nodiscard]] size_t get_bytes_written() const noexcept
    {
        return byte_pos + (bits_in_buf + 7) / 8;
    }

    /// Number of bits written so far (including bits still in the buffer).
    [[nodiscard]] size_t get_bits_written() const noexcept
    {
        return byte_pos * 8 + bits_in_buf;
    }

    /// Remaining space in bytes.
    [[nodiscard]] size_t get_bytes_remaining() const noexcept
    {
        return buffer.size() - get_bytes_written();
    }

    [[nodiscard]] bool full() const noexcept { return get_bytes_remaining() == 0; }

private:
    std::span<uint8_t> buffer;
    size_t   byte_pos    = 0;
    uint64_t bit_buf     = 0;
    size_t   bits_in_buf = 0;

    // Drain all complete bytes from bit_buf to the output.
    void drain();

    // Write exactly n aligned bytes from value (big-endian byte order).
    void write_bytes_be(uint64_t value, size_t n);
};