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
    void decorrelate(std::span<const int32_t> in_left, std::span<const int32_t> in_right, std::span<int32_t> out_left, std::span<int32_t> out_right, ChannelDecorrelationType type);
    void deinterleave();

    inline void write_metadata_header(uint32_t body_size, uint8_t type, bool is_last);
    inline void write_flac_header_and_metadata();
    inline void write_frame_header(size_t num_samples, ChannelDecorrelationType dec_type);
    /// @param pred_order 0...4 for fixed predictor, 1...32 for linear
    inline void write_subframe_header(PredictorType pred_type, uint8_t pred_order);
    inline void write_rice_sample(int32_t sample, uint8_t k);
    inline void write_utf8_coded_int(uint64_t num);

    void write_encoded(std::span<const int32_t> in, uint8_t bps, PredictorType pred_type, uint8_t pred_order = 0, uint8_t partition_order = 0,
                       std::span<const uint8_t> rice_params = {}, std::span<const int32_t> qlp_coeffs = {});
    void write_verbatim(std::span<const int32_t> in, uint8_t bps);

    uint8_t encode_block_size_and_sample_rate(uint32_t block_size, uint32_t sample_rate);
    uint8_t encode_channels_and_bit_depth(uint8_t channel_assignment, uint8_t bit_depth);

    void encode_fp(std::span<const int32_t> in, std::span<int32_t> out, uint8_t order);
    void encode_lp(std::span<const int32_t> in, std::span<int32_t> out, std::span<const int32_t> coeffs, uint8_t order, uint8_t qlp_shift);
    void compute_autocorrelation(std::span<const int32_t> samples, std::span<double> r, uint8_t order);
    void levinson_durbin(std::span<const double> r, std::span<double> lpc, uint8_t order);
    void quantize_lpc(std::span<const double> lpc, std::span<int32_t> qlp, uint8_t shift);

    size_t sum_abs(std::span<const int32_t> in);
    uint8_t estimate_rice_param(std::span<const int32_t> in);
    size_t estimate_residual(std::span<const int32_t> in, uint8_t rice_param);
    size_t estimate_encoded(std::span<const int32_t> in, uint8_t bps, PredictorType pred_type, uint8_t pred_order,
                            uint8_t partition_order, std::span<const uint8_t> rice_params = {});

    struct BestEncoding
    {
        PredictorType type = PredictorType::Verbatim;
        uint8_t order = 0;
        uint8_t partition_order = 0;
        std::array<uint8_t, MAX_PARTITIONS> rice_params{};
        size_t bits = SIZE_MAX;
        std::array<int32_t, MAX_SAMPLES_IN_BLOCK> residual{};
        std::array<int32_t, MAX_LPC_ORDER> qlp{};
    };
    BestEncoding find_best(std::span<const int32_t> samples, uint8_t bps);

private:
    StreamInfo stream_info = {0};

    BitReader br;
    BitWriter bw;

    std::array<std::vector<int32_t>, 8> channels;
    // Buffers for decorrelated channels (each variant)
    std::array<std::array<int32_t, MAX_SAMPLES_IN_BLOCK>, static_cast<size_t>(ChannelDecorrelationType::Count)> dec_left;
    std::array<std::array<int32_t, MAX_SAMPLES_IN_BLOCK>, static_cast<size_t>(ChannelDecorrelationType::Count)> dec_right;

    uint64_t frame_number = 0;
};