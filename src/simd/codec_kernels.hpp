#pragma once

#include <cstdint>

#include "mffv1/color_transform.hpp"
#include "mffv1/options.hpp"

namespace mffv1::simd {

using ForwardColorTransform = syntax::RgbCode (*)(
    std::uint16_t,
    std::uint16_t,
    std::uint16_t,
    std::uint8_t,
    bool) noexcept;

struct CodecKernels {
    ForwardColorTransform forward_color_transform =
        syntax::forward_jpeg2000_rct;
    std::uint64_t available_features = 0;
    std::uint64_t active_features = 0;
};

[[nodiscard]] CodecKernels make_codec_kernels(
    const CpuFeatures& requested) noexcept;

} // namespace mffv1::simd
