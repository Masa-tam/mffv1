#pragma once

#include <cstddef>
#include <cstdint>

#include "mffv1/options.hpp"

namespace mffv1::simd {

using ForwardColorTransformRow = void (*)(
    const std::uint16_t*,
    const std::uint16_t*,
    const std::uint16_t*,
    std::int32_t*,
    std::int32_t*,
    std::int32_t*,
    std::size_t,
    std::uint8_t,
    bool) noexcept;

void forward_color_transform_row_scalar(
    const std::uint16_t* r,
    const std::uint16_t* g,
    const std::uint16_t* b,
    std::int32_t* y,
    std::int32_t* cb,
    std::int32_t* cr,
    std::size_t count,
    std::uint8_t bits_per_raw_sample,
    bool has_extra_plane) noexcept;

struct CodecKernels {
    ForwardColorTransformRow forward_color_transform_row =
        forward_color_transform_row_scalar;
    std::uint64_t available_features = 0;
    std::uint64_t active_features = 0;
};

[[nodiscard]] CodecKernels make_codec_kernels(
    const CpuFeatures& requested) noexcept;

} // namespace mffv1::simd
