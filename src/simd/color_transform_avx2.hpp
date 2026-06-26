#pragma once

#include <cstddef>
#include <cstdint>

namespace mffv1::simd {

void forward_color_transform_row_avx2(
    const std::uint16_t* r,
    const std::uint16_t* g,
    const std::uint16_t* b,
    std::int32_t* y,
    std::int32_t* cb,
    std::int32_t* cr,
    std::size_t count,
    std::uint8_t bits_per_raw_sample,
    bool has_extra_plane) noexcept;

void inverse_color_transform_row_avx2(
    const std::int32_t* y,
    const std::int32_t* cb,
    const std::int32_t* cr,
    std::uint16_t* r,
    std::uint16_t* g,
    std::uint16_t* b,
    std::size_t count,
    std::uint8_t bits_per_raw_sample,
    bool has_extra_plane) noexcept;

} // namespace mffv1::simd
