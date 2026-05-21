#pragma once

#include <cstdint>

struct StreamInfo
{
    uint64_t total_samples;
    uint32_t min_frame_size;
    uint32_t max_frame_size;
    uint32_t sample_rate;
    uint16_t min_block_size;
    uint16_t max_block_size;
    uint8_t num_channels;
    uint8_t bits_per_sample;
};