#pragma once

#include <cstdint>

namespace mffv1::syntax {

struct RgbSample {
    std::uint16_t r = 0;
    std::uint16_t g = 0;
    std::uint16_t b = 0;
};

struct RgbCode {
    std::int32_t y = 0;
    std::int32_t cb = 0;
    std::int32_t cr = 0;
};

[[nodiscard]] RgbCode forward_jpeg2000_rct(
    std::uint16_t r,
    std::uint16_t g,
    std::uint16_t b,
    std::uint8_t bits_per_raw_sample,
    bool has_extra_plane) noexcept;

[[nodiscard]] RgbSample inverse_jpeg2000_rct(std::int32_t y,
                                             std::int32_t cb,
                                             std::int32_t cr,
                                             std::uint8_t bits_per_raw_sample,
                                             bool has_extra_plane) noexcept;

} // namespace mffv1::syntax
