#pragma once

#include "mffv1/frame.hpp"

#include <cstdint>

namespace mffv1::samples {

[[nodiscard]] inline SampleFormat sample_format_for_bit_depth(
    std::uint8_t bits_per_raw_sample) noexcept
{
    return bits_per_raw_sample <= 8 ? SampleFormat::UInt8 : SampleFormat::UInt16;
}

[[nodiscard]] inline std::uint32_t bytes_per_sample(
    SampleFormat format) noexcept
{
    return format == SampleFormat::UInt16 ? 2u : 1u;
}

} // namespace mffv1::samples
