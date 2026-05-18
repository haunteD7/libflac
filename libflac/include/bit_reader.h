#pragma once
#include <span>
#include <cstdint>
#include <stdexcept>

class BitReader
{
public:
    explicit BitReader(std::span<const uint8_t> buf) noexcept
        : buffer(buf) {}

    /// Read [1..64] bits, MSB first.
    [[nodiscard]] uint64_t read(size_t num_bits);

    /// Peek at the next [1..64] bits without consuming them.
    [[nodiscard]] uint64_t peek(size_t num_bits);

    void skip(size_t num_bits);
    void skip_bytes(size_t num_bytes);

    /// Read a single bit (returns 0 or 1).
    [[nodiscard]] bool read_bit() { return read(1) != 0; }

    // Byte-aligned multi-byte reads
    // All of these flush any sub-byte remainder first (align_to_byte).

    [[nodiscard]] uint8_t read8();
    [[nodiscard]] uint16_t read16_le();
    [[nodiscard]] uint16_t read16_be();
    [[nodiscard]] uint32_t read24_le();
    [[nodiscard]] uint32_t read24_be();
    [[nodiscard]] uint32_t read32_le();
    [[nodiscard]] uint32_t read32_be();
    [[nodiscard]] uint64_t read64_le();
    [[nodiscard]] uint64_t read64_be();

    // Alignment / state

    /// Discard bits until the reader is on a byte boundary.
    void align_to_byte() noexcept
    {
        size_t remainder = bits_left % 8;
        bit_buf <<= remainder;
        bits_left -= remainder;
    }

    /// Reset to a new buffer, discarding all internal state.
    void set_buffer(std::span<const uint8_t> buf) noexcept
    {
        buffer = buf;
        byte_pos = 0;
        bit_buf = 0;
        bits_left = 0;
    }

    // Introspection

    /// Total bits still available (buffered + unread bytes).
    [[nodiscard]] size_t get_available_bits() const noexcept
    {
        return bits_left + (buffer.size() - byte_pos) * 8;
    }

    /// Number of source bytes fully or partially consumed so far.
    [[nodiscard]] size_t get_bytes_consumed() const noexcept
    {
        // byte_pos counts bytes already pulled into the bit buffer;
        // bits_left of those bytes are still unread.
        size_t buffered_bytes = bits_left / 8; // whole bytes still in bit_buf
        return byte_pos > buffered_bytes
                   ? byte_pos - buffered_bytes
                   : 0;
    }

    [[nodiscard]] bool empty() const noexcept { return get_available_bits() == 0; }

private:
    std::span<const uint8_t> buffer;
    size_t byte_pos = 0;
    uint64_t bit_buf = 0;
    size_t bits_left = 0;

    // Pull bytes from the source until we have at least `needed` bits (or EOF).
    void refill(size_t needed) noexcept;

    // Read exactly `n` aligned bytes into a uint64_t (big-endian byte order).
    uint64_t read_bytes_be(size_t n);
};