#pragma once

#include <cstddef>
#include <cstdint>

namespace mffv1::simd {

void forward_color_transform_row_sse2(
    const std::uint16_t* r,
    const std::uint16_t* g,
    const std::uint16_t* b,
    std::int32_t* y,
    std::int32_t* cb,
    std::int32_t* cr,
    std::size_t count,
    std::uint8_t bits_per_raw_sample,
    bool has_extra_plane) noexcept;

} // namespace mffv1::simd
