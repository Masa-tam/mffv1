#pragma once

#include "mffv1/frame.hpp"

#include <cstdint>

namespace mffv1::constraints {

[[nodiscard]] inline bool is_supported_encoder_bit_depth(
    std::uint8_t bits_per_raw_sample) noexcept
{
    return bits_per_raw_sample >= 1 && bits_per_raw_sample <= 16;
}

[[nodiscard]] inline bool is_supported_decoder_bit_depth(
    std::uint8_t bits_per_raw_sample) noexcept
{
    return bits_per_raw_sample >= 1 && bits_per_raw_sample <= 16;
}

[[nodiscard]] inline bool is_supported_syntax_colorspace(
    int colorspace_type) noexcept
{
    return colorspace_type == 0 || colorspace_type == 1;
}

[[nodiscard]] inline bool is_supported_public_color_space(
    ColorSpace color_space) noexcept
{
    return color_space == ColorSpace::YCbCr || color_space == ColorSpace::Rgb;
}

[[nodiscard]] inline bool has_invalid_rgb_geometry(
    bool is_rgb,
    bool has_chroma_planes,
    std::uint8_t log2_h_chroma_subsample,
    std::uint8_t log2_v_chroma_subsample) noexcept
{
    return is_rgb
        && (!has_chroma_planes
            || log2_h_chroma_subsample != 0
            || log2_v_chroma_subsample != 0);
}

[[nodiscard]] inline bool has_subsampling_without_chroma(
    bool has_chroma_planes,
    std::uint8_t log2_h_chroma_subsample,
    std::uint8_t log2_v_chroma_subsample) noexcept
{
    return !has_chroma_planes
        && (log2_h_chroma_subsample != 0
            || log2_v_chroma_subsample != 0);
}

[[nodiscard]] inline bool is_supported_chroma_subsampling(
    std::uint8_t log2_h_chroma_subsample,
    std::uint8_t log2_v_chroma_subsample) noexcept
{
    return log2_h_chroma_subsample <= 1
        && log2_v_chroma_subsample <= 1
        && log2_v_chroma_subsample <= log2_h_chroma_subsample;
}

} // namespace mffv1::constraints
