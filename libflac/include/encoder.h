#pragma once

#include <filesystem>
#include <vector>

#include <utils.h>
#include "bit_reader.h"
#include "bit_writer.h"
#include "stream_info.h"

class FlacEncoder
{
public:
    FlacEncoder(std::span<uint8_t> output) : br(std::span<uint8_t>()), bw(output) {}
    size_t encode(std::filesystem::path path, uint32_t block_size);

private:
    enum class ChannelDecorrelationType : uint8_t
    {
        Independent = 0,
        LeftSide,
        RightSide,
        MidSide,

        Count
    };
    enum class PredictorType : uint8_t
    {
        Constant = 0,
        Verbatim,
        Fixed,
        Linear
    };
    void read_wav_header();
    // TODO: move those out of here
    void write_metadata_header(uint32_t body_size, uint8_t type, bool is_last);
    void write_flac_header_and_metadata();
    //
    void write_frame_header(size_t num_samples, ChannelDecorrelationType dec_type);
    /// @param pred_order 0...4 for fixed predictor, 1...32 for linear
    void write_subframe_header(PredictorType pred_type, uint8_t pred_order);
    void write_rice_sample(int32_t sample, uint8_t k);

    uint8_t encode_block_size_and_sample_rate(uint32_t block_size, uint32_t sample_rate);
    uint8_t encode_channels_and_bit_depth(uint8_t channel_assignment, uint8_t bit_depth);
    
    void write_utf8_coded_int(uint64_t num);
    void write_verbatim(std::span<int32_t> in, uint8_t bps);

    void decorrelate(std::span<int32_t> in_left, std::span<int32_t> in_right, std::span<int32_t> out_left, std::span<int32_t> out_right, ChannelDecorrelationType type);
    void deinterleave();
    size_t energy(std::span<const int32_t> in);
private:
    StreamInfo stream_info = {0};

    BitReader br;
    BitWriter bw;

    std::array<std::vector<int32_t>, 8> channels;
    std::array<uint8_t, MAX_SAMPLES_IN_BLOCK * 4> subframe_buffer;

    uint64_t frame_number = 0;
};