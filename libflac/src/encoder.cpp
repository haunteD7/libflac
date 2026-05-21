#include <encoder.h>

#include <fstream>
#include <cstring>

size_t FlacEncoder::encode(std::filesystem::path path, uint32_t block_size)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
    {
        throw std::runtime_error("Can't open file " + path.filename().string() + " for reading");
        return 0;
    }

    size_t file_size = std::filesystem::file_size(path);
    std::vector<uint8_t> br_buffer(file_size);
    f.read(reinterpret_cast<char *>(br_buffer.data()), file_size);
    br = BitReader(br_buffer);

    read_wav_header();
    stream_info.max_block_size = block_size;
    stream_info.min_block_size = stream_info.total_samples % block_size;
    write_flac_header_and_metadata();
    deinterleave();

    auto bps = stream_info.bits_per_sample;
    auto num_channels = stream_info.num_channels;

    uint8_t channels_bps[4][MAX_CHANNELS];
    std::memset(channels_bps, bps, 4 * MAX_CHANNELS);
    channels_bps[static_cast<size_t>(ChannelDecorrelationType::LeftSide)][1] = bps + 1;
    channels_bps[static_cast<size_t>(ChannelDecorrelationType::RightSide)][0] = bps + 1;
    channels_bps[static_cast<size_t>(ChannelDecorrelationType::MidSide)][1] = bps + 1;

    size_t pos = 0;
    std::array<std::span<int32_t>, MAX_CHANNELS> channels_to_write;
    while (pos < stream_info.total_samples)
    {
        std::array<std::array<int32_t, MAX_SAMPLES_IN_BLOCK>, static_cast<size_t>(ChannelDecorrelationType::Count)> decorrelated_left;
        std::array<std::array<int32_t, MAX_SAMPLES_IN_BLOCK>, static_cast<size_t>(ChannelDecorrelationType::Count)> decorrelated_right;

        size_t num_samples_to_encode = std::min(static_cast<uint64_t>(block_size), stream_info.total_samples - pos);

        ChannelDecorrelationType dec_type = ChannelDecorrelationType::Independent;
        if (num_channels == 2)
        {
            size_t min_energy;
            uint8_t test_type = 0;
            do
            {
                ChannelDecorrelationType type = static_cast<ChannelDecorrelationType>(test_type);
                auto left_channel_part = std::span<int32_t>(channels[0].data() + pos, num_samples_to_encode);
                auto right_channel_part = std::span<int32_t>(channels[1].data() + pos, num_samples_to_encode);
                decorrelate(left_channel_part, right_channel_part, decorrelated_left[test_type], decorrelated_right[test_type], type);
                size_t current_energy = energy(left_channel_part) + energy(right_channel_part);
                if (min_energy < current_energy)
                {
                    min_energy = current_energy;
                    dec_type = type;
                }
                test_type++;
            } while (test_type < static_cast<uint8_t>(ChannelDecorrelationType::Count));

            channels_to_write[0] = std::span<int32_t>(decorrelated_left[static_cast<size_t>(dec_type)].data(), num_samples_to_encode);
            channels_to_write[1] = std::span<int32_t>(decorrelated_right[static_cast<size_t>(dec_type)].data(), num_samples_to_encode);
        }
        else
        {
            for (size_t i = 0; i < num_channels; i++)
                channels_to_write[i] = std::span<int32_t>(channels[i].data() + pos, num_samples_to_encode);
        }
        write_frame_header(num_samples_to_encode, dec_type);
        for (size_t i = 0; i < num_channels; i++)
        {
            write_subframe_header(PredictorType::Verbatim, 0);
            write_verbatim(channels_to_write[i], channels_bps[static_cast<size_t>(dec_type)][i]);
        }

        bw.align_to_byte();
        bw.write16_be(0); // TODO: Implement CRC16
        pos += num_samples_to_encode;
    }

    return bw.get_bytes_written();
}
void FlacEncoder::read_wav_header()
{
    // Read WAV header

    // --- RIFF chunk ---
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
    else if (num <= 0xFFFFFFFFF) // 7 bytes
    {
        // 11111110 ...
        uint8_t b1 = 0xFE;
        uint8_t b2 = 0x80 | ((num >> 30) & 0x3F);
        uint8_t b3 = 0x80 | ((num >> 24) & 0x3F);
        uint8_t b4 = 0x80 | ((num >> 18) & 0x3F);
        uint8_t b5 = 0x80 | ((num >> 12) & 0x3F);
        uint8_t b6 = 0x80 | ((num >> 6) & 0x3F);
        uint8_t b7 = 0x80 | (num & 0x3F);

        bw.write8(b1);
        bw.write8(b2);
        bw.write8(b3);
        bw.write8(b4);
        bw.write8(b5);
        bw.write8(b6);
        bw.write8(b7);
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
    bw.write16_be(0xFFF8); // Sync code
    bw.write8(encode_block_size_and_sample_rate(num_samples, stream_info.sample_rate));

    uint8_t channel_assignment;
    switch (dec_type)
    {
    case ChannelDecorrelationType::Independent:
        channel_assignment = stream_info.num_channels - 1;
        break;
    case ChannelDecorrelationType::LeftSide:
        channel_assignment = 8; // Left/side stereo
        break;
    case ChannelDecorrelationType::RightSide:
        channel_assignment = 9; // Right/side stereo
        break;
    case ChannelDecorrelationType::MidSide:
        channel_assignment = 10; // Mid/side stereo
        break;
    default:
        throw std::runtime_error("Invalid channel decorrelation type");
    }
    bw.write8(encode_channels_and_bit_depth(channel_assignment, stream_info.bits_per_sample));
    if (num_samples != stream_info.max_block_size) // Uncommon block size
    {
        write_utf8_coded_int(stream_info.total_samples - num_samples); // Write sample shift
        bw.write16_be(num_samples - 1);                                // Write uncommon size
    }
    else
    {
        write_utf8_coded_int(frame_number);
    }
    bw.write8(0); // TODO: This should be CRC8
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
void FlacEncoder::write_verbatim(std::span<int32_t> in, uint8_t bps)
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
void FlacEncoder::decorrelate(std::span<int32_t> in_left, std::span<int32_t> in_right, std::span<int32_t> out_left, std::span<int32_t> out_right, ChannelDecorrelationType type)
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
        // out_left = left - right
        // out_right = (left + right) / 2

        for (size_t i = 0; i < in_left.size(); i++)
        {
            int32_t left = in_left[i];
            int32_t right = in_right[i];

            out_left[i] = left - right;
            out_right[i] = (left + right) >> 1;
        }
        break;
    }
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
size_t FlacEncoder::energy(std::span<const int32_t> in)
{
    size_t result = 0;
    for (auto sample : in)
        result += std::abs(sample);

    return result;
}