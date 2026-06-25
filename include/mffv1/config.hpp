#pragma once

#include <cstdint>
#include <vector>

#include "mffv1/frame.hpp"

namespace mffv1 {

enum class ColorSpace : std::uint8_t {
    YCbCr = 0,
    Rgb = 1,
};

struct StreamInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t version = 3;
    std::uint8_t bits_per_raw_sample = 8;
    std::uint8_t log2_h_chroma_subsample = 0;
    std::uint8_t log2_v_chroma_subsample = 0;
    bool has_chroma_planes = true;
    bool has_extra_plane = false;
    ColorSpace color_space = ColorSpace::YCbCr;
};

struct ConfigurationRecord {
    std::vector<std::byte> bytes;
};

struct EncodedFrame {
    std::vector<std::byte> bytes;
};

} // namespace mffv1
