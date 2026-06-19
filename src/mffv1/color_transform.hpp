#pragma once

#include <cstdint>

namespace mffv1::syntax {

struct RgbSample {
    std::uint16_t r = 0;
    std::uint16_t g = 0;
    std::uint16_t b = 0;
};

[[nodiscard]] RgbSample inverse_jpeg2000_rct(std::int32_t y,
                                             std::int32_t cb,
                                             std::int32_t cr,
                                             std::uint8_t bits_per_raw_sample,
                                             bool has_extra_plane) noexcept;

} // namespace mffv1::syntax
