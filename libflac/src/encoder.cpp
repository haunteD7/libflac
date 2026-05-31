#include <encoder.h>
#include <crc.h>

#include <fstream>
#include <cstring>
#include <cmath>
#include <numbers>

FlacEncoder::BestEncoding FlacEncoder::find_best(std::span<const int32_t> samples, uint8_t bps)
{
    BestEncoding best;

    const size_t n = samples.size();
    const uint8_t MAX_ORDER = static_cast<uint8_t>(std::min<size_t>(MAX_LPC_ORDER, n - 1));

    // Helper: counts rice-params by partitions and fills best if better one has been found
    auto try_predictor = [&](PredictorType type,
                             uint8_t order,
                             const std::array<int32_t, MAX_SAMPLES_IN_BLOCK> &residual_buf,
                             const std::array<int32_t, MAX_LPC_ORDER> &qlp_buf)
    {
        uint8_t p_order = MAX_PARTITION_ORDER;
        /* Lower partition if warmup samples are not fitting in first partition,
           or if block size is not divisible by partition size */
        while (p_order > 0 && ((n >> p_order) <= order || (n % (1u << p_order)) != 0))
            --p_order;

        const size_t num_parts = 1u << p_order;
        const size_t part_size = n / num_parts;

        std::array<uint8_t, MAX_PARTITIONS> rp{}; // Estimate rice params
        for (size_t p = 0; p < num_parts; p++)
        {
            const size_t warmup_in_part = (p == 0) ? order : 0;
            const size_t start = p * part_size + warmup_in_part;
            const size_t count = part_size - warmup_in_part;
            if (count == 0)
            {
                rp[p] = 0;
                continue;
            }
            rp[p] = estimate_rice_param(std::span<const int32_t>(residual_buf.data() + start, count));
        }

        const size_t bits = estimate_encoded(
            std::span<const int32_t>(residual_buf.data(), n),
            bps, type, order, p_order,
            std::span<const uint8_t>(rp.data(), num_parts));

        if (bits < best.bits)
        {
            best.type = type;
            best.order = order;
            best.partition_order = p_order;
            best.bits = bits;
            best.rice_params = rp;
            std::copy_n(residual_buf.begin(), n, best.residual.begin());
            best.qlp = qlp_buf;
        }
    };

    // 1. Constant
    {
        bool is_constant = true;
        for (size_t i = 1; i < n; i++)
        {
            if (samples[i] != samples[0])
            {
                is_constant = false;
                break;
            }
        }

        if (is_constant)
        {
            size_t bits = bps;
            if (bits < best.bits)
            {
                best.type = PredictorType::Constant;
                best.order = 0;
                best.partition_order = 0;
                best.bits = bits;
                best.residual[0] = samples[0];
            }
        }
    }

    // 2. Verbatim
    {
        const size_t bits = estimate_encoded(
            samples, bps, PredictorType::Verbatim, 0, 0, {});
        if (bits < best.bits)
        {
            best.type = PredictorType::Verbatim;
            best.order = 0;
            best.partition_order = 0;
            best.bits = bits;
            std::copy_n(samples.begin(), n, best.residual.begin());
        }
    }

    // 3. Fixed predictor, orders 0..4
    {
        std::array<int32_t, MAX_SAMPLES_IN_BLOCK> residual_buf{};
        std::array<int32_t, MAX_LPC_ORDER> dummy_qlp{};

        for (uint8_t order = 0; order <= 4 && order < n; order++)
        {
            encode_fp(samples, residual_buf, order);
            try_predictor(PredictorType::Fixed, order, residual_buf, dummy_qlp);
        }
        if (best.type == PredictorType::Fixed &&
            best.bits < (size_t)(0.5 * bps * n))
        {
            return best; // skip whole LPC
        }
    }

    // 4. LPC, orders 1..min(MAX_LPC_ORDER, n-1)
    if (MAX_ORDER >= 1)
    {
        // Single autocorrelation for all orders
        std::array<double, MAX_LPC_ORDER + 1> r{};
        std::array<double, MAX_SAMPLES_IN_BLOCK> windowed;
        for (size_t i = 0; i < n; i++)
            windowed[i] = samples[i] * (0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / (n - 1)));
        compute_autocorrelation(samples, r, MAX_ORDER);

        if (r[0] > 0.0)
        {
            std::array<double, MAX_LPC_ORDER> lpc_f{};
            std::array<int32_t, MAX_LPC_ORDER> qlp_buf{};
            std::array<int32_t, MAX_SAMPLES_IN_BLOCK> residual_buf{};

            for (uint8_t order = 1; order <= MAX_ORDER; order++)
            {
                levinson_durbin(
                    std::span<const double>(r.data(), order + 1),
                    std::span<double>(lpc_f.data(), order),
                    order);

                quantize_lpc(
                    std::span<const double>(lpc_f.data(), order),
                    std::span<int32_t>(qlp_buf.data(), order),
                    DEFAULT_QLP_SHIFT);

                encode_lp(samples, residual_buf,
                          std::span<const int32_t>(qlp_buf.data(), order),
                          order, DEFAULT_QLP_SHIFT);

                try_predictor(PredictorType::Linear, order, residual_buf, qlp_buf);
            }
        }
    }

    return best;
}

size_t FlacEncoder::encode(std::filesystem::path path, uint32_t block_size)
{
    // Read file
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("Can't open file " + path.filename().string() + " for reading");

    const size_t file_size = std::filesystem::file_size(path);
    std::vector<uint8_t> br_buffer(file_size);
    f.read(reinterpret_cast<char *>(br_buffer.data()), file_size);
    br = BitReader(br_buffer);

    // WAV Header + FLAC
    read_wav_header();
    stream_info.max_block_size = block_size;
    stream_info.min_block_size = block_size;
    write_flac_header_and_metadata();
    deinterleave();

    const auto bps = stream_info.bits_per_sample;
    if (stream_info.num_channels > MAX_CHANNELS)
        throw std::runtime_error("Exceeded max amount of channels");
    const auto num_channels = stream_info.num_channels;

    // BPS for channels for each decorrelation type
    // Index [decorrelation_type][channel]
    uint8_t channels_bps[static_cast<size_t>(ChannelDecorrelationType::Count)][MAX_CHANNELS];
    for (auto &row : channels_bps)
        std::fill(std::begin(row), std::end(row), bps);
    channels_bps[static_cast<size_t>(ChannelDecorrelationType::LeftSide)][1] = bps + 1;
    channels_bps[static_cast<size_t>(ChannelDecorrelationType::RightSide)][0] = bps + 1;
    channels_bps[static_cast<size_t>(ChannelDecorrelationType::MidSide)][1] = bps + 1;

    size_t offset = 0;
    while (offset < stream_info.total_samples)
    {
        const size_t num_samples = std::min(
            static_cast<uint64_t>(block_size),
            stream_info.total_samples - offset);

        // Source data spans for current block
        std::span<const int32_t> src_left(channels[0].data() + offset, num_samples);
        std::span<const int32_t> src_right(num_channels >= 2
                                               ? channels[1].data() + offset
                                               : channels[0].data() + offset,
                                           num_samples);

        // Choosing decorrelation type (only for stereo)
        ChannelDecorrelationType dec_type = ChannelDecorrelationType::Independent;

        // find_best results for both channels of the best variants
        BestEncoding best_ch[MAX_CHANNELS];

        if (num_channels == 2)
        {
            size_t best_total_bits = SIZE_MAX;

            for (uint8_t d = 0;
                 d < static_cast<uint8_t>(ChannelDecorrelationType::Count); d++)
            {
                auto type = static_cast<ChannelDecorrelationType>(d);

                decorrelate(src_left, src_right,
                            dec_left[d], dec_right[d], type);

                const uint8_t bps_l = channels_bps[d][0];
                const uint8_t bps_r = channels_bps[d][1];

                BestEncoding enc_l = find_best(std::span<const int32_t>(dec_left[d].data(), num_samples), bps_l);
                BestEncoding enc_r = find_best(std::span<const int32_t>(dec_right[d].data(), num_samples), bps_r);

                const size_t total = enc_l.bits + enc_r.bits;
                if (total < best_total_bits)
                {
                    best_total_bits = total;
                    dec_type = type;
                    best_ch[0] = enc_l;
                    best_ch[1] = enc_r;
                }
            }
        }
        else // Independent channels for non-stereo
        {
            dec_type = ChannelDecorrelationType::Independent;
            for (size_t i = 0; i < num_channels; i++)
            {
                best_ch[i] = find_best(
                    std::span<const int32_t>(channels[i].data() + offset, num_samples),
                    channels_bps[static_cast<size_t>(dec_type)][i]);
            }
        }

        // Write frame
        write_frame_header(num_samples, dec_type);

        for (size_t i = 0; i < num_channels; i++)
        {
            const BestEncoding &enc = best_ch[i];
            const uint8_t ch_bps =
                channels_bps[static_cast<size_t>(dec_type)][i];
            const size_t np = 1u << enc.partition_order;

            write_subframe_header(enc.type, enc.order);
            write_encoded(
                std::span<const int32_t>(enc.residual.data(), num_samples),
                ch_bps,
                enc.type,
                enc.order,
                enc.partition_order,
                std::span<const uint8_t>(enc.rice_params.data(), np),
                std::span<const int32_t>(enc.qlp.data(), enc.order));
        }

        bw.align_to_byte();
        
        size_t frame_len = bw.get_bytes_written() - frame_header_start;
        uint16_t c16 = crc16({bw.data_at(frame_header_start), frame_len});
        bw.write16_be(c16);
        
        offset += num_samples;
    }

    return bw.get_bytes_written();
}
void FlacEncoder::read_wav_header()
{
    // Read WAV header

    // RIFF chunk
    uint32_t riff_id = br.read32_be();
    if (riff_id != 0x52494646u) // "RIFF"
        throw std::runtime_error("Not a RIFF file");

    br.read32_le();

    uint32_t wave_id = br.read32_be();
    if (wave_id != 0x57415645u) // "WAVE"
        throw std::runtime_error("Not a WAVE file");

    bool got_data = false;
    while (!br.empty() && !got_data)
    {
        uint32_t chunk_id = br.read32_be();
        uint32_t chunk_size = br.read32_le();
        switch (chunk_id)
        {
        case 0x666D7420u: // "fmt "
        {
            if (chunk_size < 16)
                throw std::runtime_error("fmt chunk is too small");

            if (br.read16_le() != 1)
            {
                throw std::runtime_error("Unsupported WAV format");
            }
            stream_info.num_channels = br.read16_le();
            stream_info.sample_rate = br.read32_le();
            br.read32_le();
            br.read16_le();
            stream_info.bits_per_sample = br.read16_le();

            if (chunk_size > 16)
                br.skip_bytes(chunk_size - 16);

            break;
        }
        case 0x64617461u: // "data"
        {
            if (stream_info.num_channels == 0 || stream_info.bits_per_sample == 0)
                throw std::runtime_error("data chunk before fmt");
            stream_info.total_samples = chunk_size / (stream_info.num_channels * (stream_info.bits_per_sample / 8));
            got_data = true;
            break;
        }
        default:
            br.skip_bytes(chunk_size);
            break;
        }
    }
}

uint8_t FlacEncoder::encode_block_size_and_sample_rate(uint32_t block_size, uint32_t sample_rate)
{
    uint8_t block_size_part;
    switch (block_size)
    {
    case 192:
        block_size_part = 1;
        break;
    case 576:
        block_size_part = 2;
        break;
    case 1152:
        block_size_part = 3;
        break;
    case 2304:
        block_size_part = 4;
        break;
    case 4608:
        block_size_part = 5;
        break;

    case 256:
        block_size_part = 8;
        break;
    case 512:
        block_size_part = 9;
        break;
    case 1024:
        block_size_part = 10;
        break;
    case 2048:
        block_size_part = 11;
        break;
    case 4096:
        block_size_part = 12;
        break;
    case 8192:
        block_size_part = 13;
        break;
    case 16384:
        block_size_part = 14;
        break;
    case 32768:
        block_size_part = 15;
        break;

    default:
        block_size_part = 7;
        break;
    }
    uint8_t sample_rate_part;
    switch (sample_rate)
    {
    case 88200:
        sample_rate_part = 1;
        break;
    case 176400:
        sample_rate_part = 2;
        break;
    case 192000:
        sample_rate_part = 3;
        break;

    case 8000:
        sample_rate_part = 4;
        break;
    case 16000:
        sample_rate_part = 5;
        break;
    case 22050:
        sample_rate_part = 6;
        break;
    case 24000:
        sample_rate_part = 7;
        break;
    case 32000:
        sample_rate_part = 8;
        break;
    case 44100:
        sample_rate_part = 9;
        break;
    case 48000:
        sample_rate_part = 10;
        break;
    case 96000:
        sample_rate_part = 11;
        break;

    default:
        throw std::runtime_error("Reserved sample rate");
    }

    return (block_size_part << 4) | (sample_rate_part);
}
uint8_t FlacEncoder::encode_channels_and_bit_depth(uint8_t channel_assignment, uint8_t bit_depth)
{
    uint8_t bit_depth_part;
    switch (bit_depth)
    {
    case 8:
        bit_depth_part = 1;
        break;
    case 12:
        bit_depth_part = 2;
        break;
    case 16:
        bit_depth_part = 4;
        break;
    case 20:
        bit_depth_part = 5;
        break;
    case 24:
        bit_depth_part = 6;
        break;

    default:
        throw std::runtime_error("Unsupported bit depth");
    }

    return (channel_assignment << 4) | (bit_depth_part << 1);
}
void FlacEncoder::write_utf8_coded_int(uint64_t num)
{
    if (num <= 0x7F) // 1 byte
    {
        // 0xxxxxxx
        bw.write8((uint8_t)num);
    }
    else if (num <= 0x7FF) // 2 bytes
    {
        // 110xxxxx 10xxxxxx
        uint8_t b1 = 0xC0 | ((num >> 6) & 0x1F);
        uint8_t b2 = 0x80 | (num & 0x3F);

        bw.write8(b1);
        bw.write8(b2);
    }
    else if (num <= 0xFFFF) // 3 bytes
    {
        // 1110xxxx 10xxxxxx 10xxxxxx
        uint8_t b1 = 0xE0 | ((num >> 12) & 0x0F);
        uint8_t b2 = 0x80 | ((num >> 6) & 0x3F);
        uint8_t b3 = 0x80 | (num & 0x3F);

        bw.write8(b1);
        bw.write8(b2);
        bw.write8(b3);
    }
    else if (num <= 0x1FFFFF) // 4 bytes
    {
        // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        uint8_t b1 = 0xF0 | ((num >> 18) & 0x07);
        uint8_t b2 = 0x80 | ((num >> 12) & 0x3F);
        uint8_t b3 = 0x80 | ((num >> 6) & 0x3F);
        uint8_t b4 = 0x80 | (num & 0x3F);

        bw.write8(b1);
        bw.write8(b2);
        bw.write8(b3);
        bw.write8(b4);
    }
    else if (num <= 0x3FFFFFF) // 5 bytes
    {
        // 111110xx ...
        uint8_t b1 = 0xF8 | ((num >> 24) & 0x03);
        uint8_t b2 = 0x80 | ((num >> 18) & 0x3F);
        uint8_t b3 = 0x80 | ((num >> 12) & 0x3F);
        uint8_t b4 = 0x80 | ((num >> 6) & 0x3F);
        uint8_t b5 = 0x80 | (num & 0x3F);

        bw.write8(b1);
        bw.write8(b2);
        bw.write8(b3);
        bw.write8(b4);
        bw.write8(b5);
    }
    else if (num <= 0x7FFFFFFF) // 6 bytes
    {
        // 1111110x ...
        uint8_t b1 = 0xFC | ((num >> 30) & 0x01);
        uint8_t b2 = 0x80 | ((num >> 24) & 0x3F);
        uint8_t b3 = 0x80 | ((num >> 18) & 0x3F);
        uint8_t b4 = 0x80 | ((num >> 12) & 0x3F);
        uint8_t b5 = 0x80 | ((num >> 6) & 0x3F);
        uint8_t b6 = 0x80 | (num & 0x3F);

        bw.write8(b1);
        bw.write8(b2);
        bw.write8(b3);
        bw.write8(b4);
        bw.write8(b5);
        bw.write8(b6);
    }
    else
    {
        throw std::runtime_error("UTF-8 frame number is too large");
    }
}
void FlacEncoder::write_subframe_header(PredictorType pred_type, uint8_t pred_order)
{
    uint8_t subframe_type;
    switch (pred_type)
    {
    case PredictorType::Constant:
        subframe_type = 0;
        break;
    case PredictorType::Verbatim:
        subframe_type = 1;
        break;
    case PredictorType::Fixed:
        subframe_type = 8 + pred_order;
        break;
    case PredictorType::Linear:
        subframe_type = 31 + pred_order;
        break;
    }
    subframe_type <<= 1;
    bw.write(subframe_type, 8);
}
void FlacEncoder::write_frame_header(size_t num_samples, ChannelDecorrelationType dec_type)
{
    frame_header_start = bw.get_bytes_written();
    bw.write16_be(0xFFF8); // fixed blocksize stream

    bw.write8(encode_block_size_and_sample_rate(stream_info.max_block_size, stream_info.sample_rate));

    uint8_t channel_assignment;
    switch (dec_type)
    {
    case ChannelDecorrelationType::Independent:
        channel_assignment = stream_info.num_channels - 1; break;
    case ChannelDecorrelationType::LeftSide:
        channel_assignment = 8; break;
    case ChannelDecorrelationType::RightSide:
        channel_assignment = 9; break;
    case ChannelDecorrelationType::MidSide:
        channel_assignment = 10; break;
    default:
        throw std::runtime_error("Invalid channel decorrelation type");
    }
    bw.write8(encode_channels_and_bit_depth(channel_assignment, stream_info.bits_per_sample));

    write_utf8_coded_int(frame_number); 

    size_t header_len = bw.get_bytes_written() - frame_header_start;
    uint8_t c8 = crc8({bw.data_at(frame_header_start), header_len});
    bw.write8(c8);
    frame_number++;
}

void FlacEncoder::write_metadata_header(uint32_t body_size, uint8_t type, bool is_last)
{
    // [1] is_last | [7] type | [24] length
    uint32_t header = ((is_last ? 1u : 0u) << 31) | ((uint32_t)type << 24) | (body_size & 0x00FFFFFFu);
    bw.write32_be(header);
}
void FlacEncoder::write_flac_header_and_metadata()
{
    // fLaC
    bw.write8('f');
    bw.write8('L');
    bw.write8('a');
    bw.write8('C');
    write_metadata_header(34, 0, true);

    bw.write16_be(stream_info.min_block_size);
    bw.write16_be(stream_info.max_block_size);
    bw.write24_be(stream_info.min_frame_size);
    bw.write24_be(stream_info.max_frame_size);
    bw.write(stream_info.sample_rate, 20);
    bw.write(stream_info.num_channels - 1, 3);
    bw.write(stream_info.bits_per_sample - 1, 5);
    bw.write(stream_info.total_samples, 36);

    uint64_t md5[2] = {0}; // TODO: Implement actual md5
    bw.align_to_byte();
    bw.write64_be(md5[0]);
    bw.write64_be(md5[1]);
}
void FlacEncoder::write_encoded(std::span<const int32_t> in, uint8_t bps, PredictorType pred_type, uint8_t pred_order, uint8_t partition_order,
                                std::span<const uint8_t> rice_params, std::span<const int32_t> qlp_coeffs)
{

    if (pred_type == PredictorType::Verbatim)
    {
        write_verbatim(in, bps);
        return;
    }
    else if (pred_type == PredictorType::Constant)
    {
        bw.write(unsign_extend(in[0], bps), bps);
        return;
    }

    size_t num_partitions = 1 << partition_order;
    uint8_t warmups = pred_order;
    for (size_t i = 0; i < warmups; i++) // Write warmups
        bw.write(unsign_extend(in[i], bps), bps);

    size_t num_samples = in.size();
    size_t num_samples_in_partition = num_samples / num_partitions;

    if (pred_type == PredictorType::Linear) // Header of linear predictor
    {
        bw.write(DEFAULT_QLP_PRECISION - 1, 4);
        bw.write(unsign_extend(DEFAULT_QLP_SHIFT, 5), 5);
        for (size_t i = 0; i < pred_order; i++) // Write lpc coeffs
        {
            bw.write(unsign_extend(qlp_coeffs[i], DEFAULT_QLP_PRECISION), DEFAULT_QLP_PRECISION);
        }
    }
    // Residuals header
    bw.write(0, 2); // Partitioned Rice code with 4-bit parameters
    bw.write(partition_order, 4);

    for (size_t part = 0; part < num_partitions; part++)
    {
        auto rice_param = rice_params[part];
        bw.write(rice_param, 4); // Header of partition
        for (size_t sample_idx = part * num_samples_in_partition + warmups; sample_idx < (part + 1) * num_samples_in_partition; sample_idx++)
        {
            write_rice_sample(in[sample_idx], rice_param);
        }

        warmups = 0;
    }
}
void FlacEncoder::write_verbatim(std::span<const int32_t> in, uint8_t bps)
{
    for (auto sample : in)
    {
        bw.write(unsign_extend(sample, bps), bps);
    }
}
void FlacEncoder::deinterleave()
{
    for (size_t i = 0; i < stream_info.num_channels; i++)
        channels[i].resize(stream_info.total_samples);

    switch (stream_info.bits_per_sample)
    {
    case 8:
    {
        for (size_t i = 0; i < stream_info.total_samples; i++)
        {
            for (size_t ch = 0; ch < stream_info.num_channels; ch++)
            {
                int32_t s = (int32_t)br.read8() - 128;
                channels[ch][i] = s;
            }
        }

        break;
    }

    case 16:
    {
        for (size_t i = 0; i < stream_info.total_samples; i++)
        {
            for (size_t ch = 0; ch < stream_info.num_channels; ch++)
            {
                channels[ch][i] = (int16_t)br.read16_le();
            }
        }

        break;
    }

    case 24:
    {
        for (size_t i = 0; i < stream_info.total_samples; i++)
        {
            for (size_t ch = 0; ch < stream_info.num_channels; ch++)
            {
                int32_t s = br.read24_le();

                if (s & 0x800000)
                    s |= ~0xFFFFFF;

                channels[ch][i] = s;
            }
        }

        break;
    }

    case 32:
    {
        for (size_t i = 0; i < stream_info.total_samples; i++)
        {
            for (size_t ch = 0; ch < stream_info.num_channels; ch++)
            {
                channels[ch][i] = (int32_t)br.read32_le();
            }
        }

        break;
    }

    default:
        throw std::runtime_error("Unsupported bit depth");
    }
}
void FlacEncoder::decorrelate(std::span<const int32_t> in_left, std::span<const int32_t> in_right, std::span<int32_t> out_left, std::span<int32_t> out_right, ChannelDecorrelationType type)
{
    switch (type)
    {
    case ChannelDecorrelationType::Independent:
    {

        for (size_t i = 0; i < in_left.size(); i++)
        {
            out_left[i] = in_left[i];
            out_right[i] = in_right[i];
        }
        break;
    }
    case ChannelDecorrelationType::LeftSide:
    {
        // out_left = left
        // out_right = left - right

        for (size_t i = 0; i < in_left.size(); i++)
        {
            int32_t left = in_left[i];
            out_left[i] = left;
            out_right[i] = left - in_right[i];
        }
        break;
    }
    case ChannelDecorrelationType::RightSide:
    {
        // out_left = left - right
        // out_right = right

        for (size_t i = 0; i < in_left.size(); i++)
        {
            int32_t right = in_right[i];
            out_left[i] = in_left[i] - right;
            out_right[i] = right;
        }
        break;
    }
    case ChannelDecorrelationType::MidSide:
    {
        // out_left = (left + right) / 2
        // out_right = left - right

        for (size_t i = 0; i < in_left.size(); i++)
        {
            int32_t left = in_left[i];
            int32_t right = in_right[i];

            out_left[i] = (left + right) >> 1;
            out_right[i] = left - right;
        }
        break;
    }
    }
}
void FlacEncoder::encode_fp(std::span<const int32_t> in, std::span<int32_t> out, uint8_t order)
{
    size_t num_samples = in.size();

    size_t i = 0;
    for (; i < order; i++) // Copy warmup samples
        out[i] = in[i];

    switch (order)
    {
    case 0:
    {
        for (; i < num_samples; i++)
        {
            out[i] = in[i];
        }
        break;
    }
    case 1:
    {
        for (; i < num_samples; i++)
        {
            out[i] = in[i] - in[i - 1];
        }
        break;
    }
    case 2:
    {
        for (; i < num_samples; i++)
        {
            out[i] = in[i] - 2 * in[i - 1] + in[i - 2];
        }
        break;
    }
    case 3:
    {
        for (; i < num_samples; i++)
        {
            out[i] = in[i] - 3 * in[i - 1] + 3 * in[i - 2] - in[i - 3];
        }
        break;
    }
    case 4:
    {
        for (; i < num_samples; i++)
        {
            out[i] = in[i] - 4 * in[i - 1] + 6 * in[i - 2] - 4 * in[i - 3] + in[i - 4];
        }
        break;
    }
    default:
        throw std::runtime_error("Invalid fixed predictor order");
    }
}
void FlacEncoder::encode_lp(std::span<const int32_t> in, std::span<int32_t> out, std::span<const int32_t> coeffs, uint8_t order, uint8_t qlp_shift)
{
    size_t num_samples = in.size();

    for (size_t i = 0; i < order; i++) // Warmup samples
        out[i] = in[i];

    for (size_t i = order; i < num_samples; i++)
    {
        int64_t predicted = 0;
        for (size_t k = 0; k < order; k++)
        {
            predicted +=
                static_cast<int64_t>(coeffs[order - 1 - k]) *
                static_cast<int64_t>(in[i - k - 1]);
        }
        predicted >>= qlp_shift;
        out[i] = in[i] - static_cast<int32_t>(predicted);
    }
}
void FlacEncoder::compute_autocorrelation(std::span<const int32_t> samples, std::span<double> r, uint8_t order)
{
    // One-time conversion - double*double then
    static thread_local std::array<double, MAX_SAMPLES_IN_BLOCK> s;
    const size_t n = samples.size();
    for (size_t i = 0; i < n; i++)
        s[i] = samples[i];

    for (size_t lag = 0; lag <= order; lag++)
    {
        double sum = 0.0;
        // Makes compiler use AVX with -O2 and -O3
        const double *a = s.data() + lag;
        const double *b = s.data();
        //
        const size_t len = n - lag;
        for (size_t i = 0; i < len; i++)
            sum += a[i] * b[i];
        r[lag] = sum;
    }
}
void FlacEncoder::levinson_durbin(std::span<const double> r,
                                  std::span<double> a, uint8_t order)
{
    static thread_local std::array<double, MAX_LPC_ORDER> tmp;
    double error = r[0];

    for (size_t i = 0; i < order; i++)
    {
        double lambda = r[i + 1];
        for (size_t j = 0; j < i; j++)
            lambda -= a[j] * r[i - j];
        lambda /= error;

        for (size_t j = 0; j < i; j++)
            tmp[j] = a[j] - lambda * a[i - j - 1];
        for (size_t j = 0; j < i; j++)
            a[j] = tmp[j];

        a[i] = lambda;
        error *= (1.0 - lambda * lambda);
    }
}
void FlacEncoder::quantize_lpc(std::span<const double> lpc, std::span<int32_t> qlp, uint8_t shift)
{
    for (size_t i = 0; i < lpc.size(); i++)
    {
        qlp[i] = static_cast<int32_t>(std::round(lpc[i] * (1 << shift)));
    }
}
void FlacEncoder::write_rice_sample(int32_t sample, uint8_t k)
{
    uint32_t u = zigzag_encode(sample);

    uint32_t q = u >> k;
    uint32_t r = u & ((1u << k) - 1u);

    // unary quotient
    while (q)
    {
        uint32_t chunk = std::min(q, 64u);
        bw.write(0, chunk);
        q -= chunk;
    }

    bw.write_bit(1);

    // binary remainder
    if (k != 0)
        bw.write(r, k);
}
uint8_t FlacEncoder::estimate_rice_param(std::span<const int32_t> in)
{
    size_t mean = sum_abs(in) / in.size();

    if (mean == 0)
        return 0;
    else
        return static_cast<uint8_t>(std::clamp((int)std::log2(mean), 0, 30));
}
size_t FlacEncoder::sum_abs(std::span<const int32_t> in)
{
    size_t result = 0;
    for (auto sample : in)
        result += std::abs(sample);

    return result;
}
size_t FlacEncoder::estimate_residual(std::span<const int32_t> in, uint8_t k)
{
    size_t bits = in.size() * (k + 1);
    for (int32_t v : in)
        bits += zigzag_encode(v) >> k;
    return bits;
}
size_t FlacEncoder::estimate_encoded(std::span<const int32_t> in, uint8_t bps, PredictorType pred_type, uint8_t pred_order,
                                     uint8_t partition_order, std::span<const uint8_t> rice_params)
{
    if (pred_type == PredictorType::Verbatim)
    {
        return bps * in.size();
    }
    else if (pred_type == PredictorType::Constant)
    {
        return bps;
    }

    size_t num_partitions = 1 << partition_order;
    uint8_t warmups = pred_order;
    uint8_t warmup_bits = bps * warmups;

    size_t num_samples = in.size();
    size_t num_samples_in_partition = num_samples / num_partitions;

    size_t result = 0;
    for (size_t i = 0; i < num_partitions; i++)
    {
        result += estimate_residual(std::span<const int32_t>(in.data() + i * num_samples_in_partition + warmups, num_samples_in_partition - warmups), rice_params[i]);

        warmups = 0;
    }
    result += warmup_bits;
    result += 6;                  // Residuals header
    result += num_partitions * 4; // Partitions' header

    if (pred_type == PredictorType::Linear)
    {
        result += DEFAULT_QLP_PRECISION * pred_order + 9; // linear predictor's header
    }

    return result;
}