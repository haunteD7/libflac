#include <decoder.h>

#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstring>

size_t FlacDecoder::decode(std::filesystem::path path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
    {
        std::cerr << "Can't open file" << path.filename() << "for reading\n";
        return 0;
    }

    size_t file_size = std::filesystem::file_size(path);
    input.resize(file_size);
    f.read(reinterpret_cast<char *>(input.data()), file_size);

    while (pos < file_size)
    {
        switch (state)
        {
        case State::Frame:
        {
            parse_frame();
            break;
        }
        case State::Metadata:
        {
            parse_metadata();
            break;
        }
        case State::Header:
        {
            if (std::strncmp(reinterpret_cast<const char *>(input.data()), "fLaC", 4))
            {
                throw std::runtime_error("File " + path.filename().string() + " is damaged\n");
                return 0;
            }
            pos += 4;
            state = State::Metadata;
            break;
        }
        }
    }

    return output_pos;
}
static int32_t sign_extend(uint64_t value, uint8_t bits)
{
    uint64_t sign_bit = uint64_t(1) << (bits - 1);
    return int32_t((value ^ sign_bit) - sign_bit);
}
static inline int32_t zigzag_decode(uint32_t u)
{
    return static_cast<int32_t>((u >> 1) ^ -(u & 1));
}

static int32_t decode_rice_sample(BitReader &br, uint8_t k)
{
    uint32_t q = 0;
    while (br.read_bit() == 0)
        ++q;

    uint32_t r = (k > 0) ? br.read(k) : 0;
    return zigzag_decode((q << k) | r);
}
void FlacDecoder::decode_residual(BitReader &br, uint8_t warmups, uint32_t block_size, std::span<int32_t> residuals)
{
    uint8_t method = br.read(2);
    uint8_t partition_order = br.read(4);
    uint32_t partitions = 1 << partition_order;

    if (block_size % partitions != 0)
        throw std::runtime_error("Block size is not divisible by partitions amount");

    int num_param_bits;
    uint32_t escape;

    if (method == 0)
    {
        num_param_bits = 4;
        escape = 0xF;
    }
    else if (method == 1)
    {
        num_param_bits = 5;
        escape = 0x1F;
    }
    else
        throw std::runtime_error("Reserved rice coding method");

    uint32_t partition_size = block_size >> partition_order;
    uint32_t offset = 0;
    for (uint32_t part = 0; part < partitions; part++)
    {
        uint8_t rice_param = br.read(num_param_bits);

        uint32_t num_samples;
        if (part == 0)
            num_samples = partition_size - warmups;
        else
            num_samples = partition_size;

        if (rice_param == escape) // Read binary data as it is
        {
            uint8_t raw_residual_bps = br.read(5);
            for (uint32_t i = 0; i < num_samples; i++)
            {
                residuals[i + offset] = sign_extend(br.read(raw_residual_bps), raw_residual_bps);
            }
        }
        else // Decode from Rice
        {
            for (uint32_t i = 0; i < num_samples; i++)
            {
                residuals[i + offset] = decode_rice_sample(br, rice_param);
            }
        }
        offset += num_samples;
    }
}
void FlacDecoder::parse_subframe_constant(BitReader &br, uint8_t channel, uint32_t block_size, uint8_t bps)
{
    int32_t sample = sign_extend(br.read(bps), bps);

    for (uint32_t i = 0; i < block_size; i++)
        frame_pcm[channel][i] = sample;
}
void FlacDecoder::parse_subframe_verbatim(BitReader &br, uint8_t channel, uint32_t block_size, uint8_t bps)
{
    for (uint32_t i = 0; i < block_size; i++)
    {
        frame_pcm[channel][i] = sign_extend(br.read(bps), bps);
    }
}
void FlacDecoder::parse_subframe_fp(BitReader &br, uint8_t channel, uint8_t bps, uint32_t block_size, uint8_t type)
{
    for (uint8_t i = 0; i < type; i++) // Warmup
        frame_pcm[channel][i] = sign_extend(br.read(bps), bps);

    decode_residual(br, type, block_size, std::span<int32_t>(frame_pcm[channel].data() + type, block_size - type));

    for (uint32_t i = type; i < block_size; i++)
    {
        int32_t predicted;

        switch (type)
        {
        case 0:
            predicted = 0;
            break;

        case 1:
            predicted =
                frame_pcm[channel][i - 1];
            break;

        case 2:
            predicted =
                2 * frame_pcm[channel][i - 1] - frame_pcm[channel][i - 2];
            break;

        case 3:
            predicted =
                3 * frame_pcm[channel][i - 1] - 3 * frame_pcm[channel][i - 2] + frame_pcm[channel][i - 3];
            break;

        case 4:
            predicted =
                4 * frame_pcm[channel][i - 1] - 6 * frame_pcm[channel][i - 2] + 4 * frame_pcm[channel][i - 3] - frame_pcm[channel][i - 4];
            break;
        }

        frame_pcm[channel][i] += predicted;
    }
}
void FlacDecoder::parse_subframe_lp(BitReader &br, uint8_t channel, uint8_t bps, uint32_t block_size, uint8_t type)
{
    for (uint8_t i = 0; i < type; i++) // Warmup
        frame_pcm[channel][i] = sign_extend(br.read(bps), bps);

    uint8_t qlp_precision = br.read(4);
    if (qlp_precision == 0xF)
        throw std::runtime_error("Reserved QLP precision");
    qlp_precision += 1;

    int8_t qlp_shift = sign_extend(br.read(5), 5);

    std::array<int32_t, 32> coeffs;
    for (uint8_t i = 0; i < type; i++)
    {
        coeffs[i] = sign_extend(br.read(qlp_precision), qlp_precision);
    }
    decode_residual(br, type, block_size, std::span<int32_t>(frame_pcm[channel].data() + type, block_size - type)); // Write residuals to frame pcm

    for (uint32_t i = type; i < block_size; i++) // Actual LPC recostruction
    {
        int64_t predicted = 0;
        for (uint8_t j = 0; j < type; j++)
        {
            predicted += static_cast<int64_t>(coeffs[j]) * frame_pcm[channel][i - 1 - j];
        }
        predicted >>= qlp_shift;
        frame_pcm[channel][i] += static_cast<int32_t>(predicted);
    }
}
void FlacDecoder::parse_subframe(BitReader &br, uint8_t channel, uint32_t block_size, uint8_t bps)
{
    if (br.read_bit() != 0)
        throw std::runtime_error("Invalid subframe padding bit");

    uint8_t type = br.read(6);
    bool has_wasted_bits = br.read_bit();
    uint8_t wasted_bits = 0;
    if (has_wasted_bits)
    {
        wasted_bits = 1;
        while (br.read_bit() == 0)
            wasted_bits++;
        bps -= wasted_bits;
    }
    if (type == 0)
        parse_subframe_constant(br, channel, block_size, bps);
    else if (type == 1)
        parse_subframe_verbatim(br, channel, block_size, bps);
    else if (type >= 8 && type <= 12)
        parse_subframe_fp(br, channel, bps, block_size, type - 8);
    else if (type >= 32)
        parse_subframe_lp(br, channel, bps, block_size, type - 31);
    else
        throw std::runtime_error("Invalid subframe type");

    if (wasted_bits)
    {
        for (auto &s : frame_pcm[channel])
            s <<= wasted_bits;
    }
}
uint32_t FlacDecoder::decode_block_size(uint8_t code)
{
    switch (code)
    {
    case 0:
        throw std::runtime_error("Reserved block size code");
    case 1:
        return 192;
    case 6:
        return read8() + 1; // 8-bit from frame header
    case 7:
        return read16() + 1; // 16-bit from frame header
    default:
        if (code >= 2 && code <= 5)
            return 576 * (1 << (code - 2)); // 576, 1152, 2304, 4608
        if (code >= 8 && code <= 15)
            return 256 * (1 << (code - 8)); // 256, 512, ..., 32768
        throw std::runtime_error("Invalid block size code");
    }
}
uint32_t FlacDecoder::decode_sample_rate(uint8_t code)
{
    switch (code)
    {
    case 0:
        return stream_info.sample_rate; // from STREAMINFO
    case 1:
        return 88200;
    case 2:
        return 176400;
    case 3:
        return 192000;
    case 4:
        return 8000;
    case 5:
        return 16000;
    case 6:
        return 22050;
    case 7:
        return 24000;
    case 8:
        return 32000;
    case 9:
        return 44100;
    case 10:
        return 48000;
    case 11:
        return 96000;
    case 12:
        return read8() * 1000; // kHz, 8-bit
    case 13:
        return read16(); // Hz, 16-bit
    case 14:
        return read16() * 10; // Hz / 10, 16-bit
    case 15:
        throw std::runtime_error("Invalid sample rate code (sync-fooling)");
    default:
        throw std::runtime_error("Invalid sample rate code");
    }
}
uint64_t FlacDecoder::read_utf8_coded_int()
{
    uint8_t first = read8();

    if ((first & 0x80) == 0)
        return first;

    int extra_bytes;
    uint64_t value;

    if ((first & 0xFE) == 0xFE)
    {
        throw std::runtime_error("Invalid UTF-8 leading byte");
    }
    else if ((first & 0xFC) == 0xFC)
    {
        extra_bytes = 5;
        value = first & 0x01;
    }
    else if ((first & 0xF8) == 0xF8)
    {
        extra_bytes = 4;
        value = first & 0x03;
    }
    else if ((first & 0xF0) == 0xF0)
    {
        extra_bytes = 3;
        value = first & 0x07;
    }
    else if ((first & 0xE0) == 0xE0)
    {
        extra_bytes = 2;
        value = first & 0x0F;
    }
    else if ((first & 0xC0) == 0xC0)
    {
        extra_bytes = 1;
        value = first & 0x1F;
    }
    else
    {
        throw std::runtime_error("Invalid UTF-8 leading byte");
    }

    for (int i = 0; i < extra_bytes; i++)
    {
        uint8_t byte = read8();
        if ((byte & 0xC0) != 0x80)
            throw std::runtime_error("Invalid UTF-8 continuation byte");
        value = (value << 6) | (byte & 0x3F);
    }

    return value;
}
uint8_t FlacDecoder::decode_channels(uint8_t code)
{
    if (code <= 7)
        return code + 1; // 1–8 independent channels

    switch (code)
    {
    case 8:
        return 2; // left/side stereo
    case 9:
        return 2; // right/side stereo
    case 10:
        return 2; // mid/side stereo
    default:
        throw std::runtime_error("Invalid channel assignment code");
    }
}
uint8_t FlacDecoder::decode_bps(uint8_t code)
{
    switch (code)
    {
    case 0:
        return stream_info.bits_per_sample; // from STREAMINFO
    case 1:
        return 8;
    case 2:
        return 12;
    case 3:
        throw std::runtime_error("Reserved bits per sample code");
    case 4:
        return 16;
    case 5:
        return 20;
    case 6:
        return 24;
    case 7:
        return 32;
    default:
        throw std::runtime_error("Invalid bits per sample code");
    }
}
void FlacDecoder::parse_frame()
{
    state = State::Frame;

    uint16_t sync = read16();
    if ((sync & 0xFFFE) != 0xFFF8)
        throw std::runtime_error("Invalid frame sync code");
    bool variable_block_size = sync & 0x1;

    uint8_t bs_sr = read8();  // Block size and sample rate bits
    uint8_t ch_bps = read8(); // Channel assignment and bits per sample

    uint64_t frame_number = read_utf8_coded_int();

    uint32_t block_size = decode_block_size(bs_sr >> 4);
    uint32_t sample_rate = decode_sample_rate(bs_sr & 0xF);

    uint8_t crc = read8();

    uint8_t ch_assignment = ch_bps >> 4;
    uint8_t num_channels = decode_channels(ch_assignment);
    uint8_t bps = decode_bps((ch_bps >> 1) & 0x7);

    frame_pcm.resize(num_channels);
    for (auto &ch : frame_pcm)
    {
        ch.resize(block_size);
    }

    BitReader br(std::span<uint8_t>(input.data() + pos, input.size() - pos));
    for (int ch = 0; ch < num_channels; ch++)
    {
        uint8_t ch_bps_actual = bps;
        if ((ch_assignment == 8 && ch == 1) || // side
            (ch_assignment == 9 && ch == 0) || // side
            (ch_assignment == 10 && ch == 1))  // side
            ch_bps_actual += 1;

        parse_subframe(br, ch, block_size, ch_bps_actual);
    }

    // Channel decorrelation
    if (ch_assignment == 8) // left/side: left=ch0, side=ch1
    {
        for (uint32_t i = 0; i < block_size; i++)
            frame_pcm[1][i] = frame_pcm[0][i] - frame_pcm[1][i];
    }
    else if (ch_assignment == 9) // right/side: side=ch0, right=ch1
    {
        for (uint32_t i = 0; i < block_size; i++)
            frame_pcm[0][i] = frame_pcm[0][i] + frame_pcm[1][i];
    }
    else if (ch_assignment == 10) // mid/side
    {
        for (uint32_t i = 0; i < block_size; i++)
        {
            int32_t mid = frame_pcm[0][i];
            int32_t side = frame_pcm[1][i];
            mid = (mid << 1) | (side & 1); // restore lost bit
            frame_pcm[0][i] = (mid + side) >> 1;
            frame_pcm[1][i] = (mid - side) >> 1;
        }
    }

    br.align_to_byte();
    pos += br.get_bytes_consumed();
    uint16_t crc16 = read16();

    flush_frame(block_size, num_channels, bps);
}
void FlacDecoder::parse_metadata()
{
    uint8_t block_type = read8();
    bool is_last = block_type >> 7;
    block_type &= 0x7F;
    uint32_t block_size = read24();
    if (pos + block_size > input.size())
        throw std::runtime_error("Invalid metadata block size");
    switch (block_type)
    {
    case 0: // Streaminfo
    {
        if (block_size != 34)
        {
            throw std::runtime_error("Invalid streaminfo metadata block size");
            break;
        }
        stream_info.min_block_size = read16(); // 16 bits
        stream_info.max_block_size = read16(); // 16 bits
        stream_info.min_frame_size = read24(); // 24 bits
        stream_info.max_frame_size = read24(); // 24 bits
        uint64_t packed = read64();
        stream_info.sample_rate = (packed >> 44) & 0xFFFFF;        // 20 bits
        stream_info.num_of_channels = ((packed >> 41) & 0x7) + 1;  // 3 bits
        stream_info.bits_per_sample = ((packed >> 36) & 0x1F) + 1; // 5 bits
        stream_info.total_samples = ((packed) & 0xFFFFFFFFF);      // 36 bits

        uint64_t md5[2];
        md5[0] = read64();
        md5[1] = read64();

        write_wav_header();
        break;
    }
    case 127:
    {
        throw std::runtime_error("Forbidden metadata block format");
        break;
    }
    default:
    {
        pos += block_size;
        break;
    }
    }
    if (is_last)
        state = State::Frame;
}

uint8_t FlacDecoder::read8()
{
    uint8_t ret = input[pos];
    pos++;
    return ret;
}
uint16_t FlacDecoder::read16()
{
    uint16_t ret =
        (uint16_t(input[pos]) << 8) |
        uint16_t(input[pos + 1]);

    pos += 2;
    return ret;
}
uint32_t FlacDecoder::read24()
{
    uint32_t ret =
        (uint32_t(input[pos]) << 16) |
        (uint32_t(input[pos + 1]) << 8) |
        uint32_t(input[pos + 2]);

    pos += 3;
    return ret;
}
uint32_t FlacDecoder::read32()
{
    uint32_t ret =
        (uint32_t(input[pos]) << 24) |
        (uint32_t(input[pos + 1]) << 16) |
        (uint32_t(input[pos + 2]) << 8) |
        uint32_t(input[pos + 3]);

    pos += 4;
    return ret;
}
uint64_t FlacDecoder::read64()
{
    uint64_t ret = 0;

    for (int i = 0; i < 8; i++)
    {
        ret = (ret << 8) | input[pos + i];
    }

    pos += 8;
    return ret;
}
void FlacDecoder::flush_frame(uint32_t block_size, uint8_t num_channels, uint8_t bps)
{
    const uint8_t bytes_per_sample = (bps + 7) / 8;
    const size_t needed = block_size * num_channels * bytes_per_sample;

    if (output_pos + needed > output.size())
        throw std::runtime_error("Output buffer is too small");

    for (uint32_t i = 0; i < block_size; i++)
    {
        for (uint8_t ch = 0; ch < num_channels; ch++)
        {
            int32_t sample = frame_pcm[ch][i];

            // Little-endian (WAV-compatible order)
            for (uint8_t b = 0; b < bytes_per_sample; b++)
            {
                output[output_pos++] = (sample >> (b * 8)) & 0xFF;
            }
        }
    }
}
void FlacDecoder::write_wav_header()
{
    const uint32_t data_size    = stream_info.total_samples
                                * stream_info.num_of_channels
                                * (stream_info.bits_per_sample / 8);
    const uint32_t byte_rate    = stream_info.sample_rate
                                * stream_info.num_of_channels
                                * (stream_info.bits_per_sample / 8);
    const uint16_t block_align  = stream_info.num_of_channels
                                * (stream_info.bits_per_sample / 8);

    auto write_u16 = [&](uint16_t v) {
        output[output_pos++] = v & 0xFF;
        output[output_pos++] = (v >> 8) & 0xFF;
    };
    auto write_u32 = [&](uint32_t v) {
        output[output_pos++] = v & 0xFF;
        output[output_pos++] = (v >> 8) & 0xFF;
        output[output_pos++] = (v >> 16) & 0xFF;
        output[output_pos++] = (v >> 24) & 0xFF;
    };

    // RIFF chunk
    output[output_pos++] = 'R'; output[output_pos++] = 'I';
    output[output_pos++] = 'F'; output[output_pos++] = 'F';
    write_u32(36 + data_size);
    output[output_pos++] = 'W'; output[output_pos++] = 'A';
    output[output_pos++] = 'V'; output[output_pos++] = 'E';

    // fmt chunk
    output[output_pos++] = 'f'; output[output_pos++] = 'm';
    output[output_pos++] = 't'; output[output_pos++] = ' ';
    write_u32(16);                                  // chunk size
    write_u16(1);                                   // PCM
    write_u16(stream_info.num_of_channels);
    write_u32(stream_info.sample_rate);
    write_u32(byte_rate);
    write_u16(block_align);
    write_u16(stream_info.bits_per_sample);

    // data chunk header
    output[output_pos++] = 'd'; output[output_pos++] = 'a';
    output[output_pos++] = 't'; output[output_pos++] = 'a';
    write_u32(data_size);
}