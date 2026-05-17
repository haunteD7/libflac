#pragma once

#include <filesystem>
#include <fstream>
#include <vector>
#include <bitswap.h>

#include "bit_reader.h"

class FlacDecoder
{
public:
    FlacDecoder(std::span<uint8_t> output) : output(output) {}

    size_t decode(std::filesystem::path path);

private:
    enum class State : uint8_t
    {
        Header,
        Metadata,
        Frame,
        Error,
    };
    struct StreamInfo
    {
        uint64_t total_samples;
        uint32_t min_frame_size;
        uint32_t max_frame_size;
        uint32_t sample_rate;
        uint16_t min_block_size;
        uint16_t max_block_size;
        uint8_t num_of_channels;
        uint8_t bits_per_sample;
    };
    void decode_residual(BitReader& br, uint8_t warmups, uint32_t block_size, std::span<int32_t> residuals);
    uint32_t decode_block_size(uint8_t code);
    uint32_t decode_sample_rate(uint8_t code);
    uint64_t read_utf8_coded_int();
    uint8_t decode_channels(uint8_t code);
    uint8_t decode_bps(uint8_t code);
    void parse_subframe_constant(BitReader &br, uint8_t channel, uint32_t block_size, uint8_t bps);
    void parse_subframe_verbatim(BitReader &br, uint8_t channel, uint32_t block_size, uint8_t bps);
    void parse_subframe_fp(BitReader &br, uint8_t channel, uint8_t bps, uint32_t block_size, uint8_t type);
    void parse_subframe_lp(BitReader &br, uint8_t channel, uint8_t bps, uint32_t block_size, uint8_t type);
    void parse_subframe(BitReader &br, uint8_t channel, uint32_t block_size, uint8_t bps);
    void parse_frame();
    void parse_metadata();
    void flush_frame(uint32_t block_size, uint8_t num_channels, uint8_t bps);

    void write_wav_header();

    inline uint8_t read8();
    inline uint16_t read16();
    inline uint32_t read24();
    inline uint32_t read32();
    inline uint64_t read64();

    std::vector<std::vector<int32_t>> frame_pcm; // For each frame

    std::vector<uint8_t> input;
    std::span<uint8_t> output;

    StreamInfo stream_info;
    size_t pos = 0;
    size_t output_pos = 0;
    State state = State::Header;
};