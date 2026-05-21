#pragma once

#include <filesystem>
#include <fstream>
#include <vector>

#include "bit_reader.h"
#include "bit_writer.h"
#include "stream_info.h"

class FlacDecoder
{
public:
    FlacDecoder(std::span<uint8_t> output) : br(std::vector<uint8_t>()), bw(output) {}

    size_t decode(std::filesystem::path path);

private:
    enum class State : uint8_t
    {
        Header,
        Metadata,
        Frame,
    };
    int32_t decode_rice_sample(uint8_t k);
    void decode_residual(uint8_t warmups, uint32_t block_size, std::span<int32_t> residuals);
    uint32_t decode_block_size(uint8_t code);
    uint32_t decode_sample_rate(uint8_t code);
    uint64_t read_utf8_coded_int();
    uint8_t decode_channels(uint8_t code);
    uint8_t decode_bps(uint8_t code);
    void decode_subframe_constant(uint8_t channel, uint32_t block_size, uint8_t bps);
    void decode_subframe_verbatim(uint8_t channel, uint32_t block_size, uint8_t bps);
    void decode_subframe_fp(uint8_t channel, uint8_t bps, uint32_t block_size, uint8_t type);
    void decode_subframe_lp(uint8_t channel, uint8_t bps, uint32_t block_size, uint8_t type);
    void parse_subframe(uint8_t channel, uint32_t block_size, uint8_t bps);
    void parse_frame();
    void parse_metadata();
    void flush_frame(uint32_t block_size, uint8_t num_channels, uint8_t bps);

    void write_wav_header(); // TODO: Move it out of here

    std::vector<std::vector<int32_t>> frame_pcm; // For each frame

    BitReader br;
    BitWriter bw;

    StreamInfo stream_info;
    State state = State::Header;
};