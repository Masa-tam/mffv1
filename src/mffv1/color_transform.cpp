#include "mffv1/color_transform.hpp"

#include "util/rfc_math.hpp"

#include <cstdint>

namespace mffv1::syntax {

RgbCode forward_jpeg2000_rct(std::uint16_t r,
                             std::uint16_t g,
                             std::uint16_t b,
                             std::uint8_t bits_per_raw_sample,
                             bool has_extra_plane) noexcept
{
    if (bits_per_raw_sample == 0 || bits_per_raw_sample > 16) {
        return {};
    }

    const auto offset = std::int64_t{1} << bits_per_raw_sample;
    const auto coded_mask = static_cast<std::uint64_t>((offset << 1) - 1);
    std::int64_t signed_cb = 0;
    std::int64_t signed_cr = 0;
    std::int64_t y = 0;
    if (bits_per_raw_sample >= 9
        && bits_per_raw_sample <= 15
        && !has_extra_plane) {
        signed_cb = static_cast<std::int64_t>(g) - b;
        signed_cr = static_cast<std::int64_t>(r) - b;
        const auto correction =
            util::arithmetic_right_shift(signed_cb + signed_cr, 2);
        y = static_cast<std::int64_t>(b) + correction;
    } else {
        signed_cb = static_cast<std::int64_t>(b) - g;
        signed_cr = static_cast<std::int64_t>(r) - g;
        const auto correction =
            util::arithmetic_right_shift(signed_cb + signed_cr, 2);
        y = static_cast<std::int64_t>(g) + correction;
    }

    return {
        static_cast<std::int32_t>(
            static_cast<std::uint64_t>(y) & coded_mask),
        static_cast<std::int32_t>(signed_cb + offset),
        static_cast<std::int32_t>(signed_cr + offset),
    };
}

RgbSample inverse_jpeg2000_rct(std::int32_t y,
                               std::int32_t cb,
                               std::int32_t cr,
                               std::uint8_t bits_per_raw_sample,
                               bool has_extra_plane) noexcept
{
    if (bits_per_raw_sample == 0 || bits_per_raw_sample > 16) {
        return {};
    }

    const auto offset = std::int64_t{1} << bits_per_raw_sample;
    const auto mask = static_cast<std::uint64_t>(offset - 1);
    const auto signed_cb = static_cast<std::int64_t>(cb) - offset;
    const auto signed_cr = static_cast<std::int64_t>(cr) - offset;
    const auto correction = util::arithmetic_right_shift(signed_cb + signed_cr, 2);

    std::int64_t r = 0;
    std::int64_t g = 0;
    std::int64_t b = 0;
    if (bits_per_raw_sample >= 9 && bits_per_raw_sample <= 15 && !has_extra_plane) {
        b = static_cast<std::int64_t>(y) - correction;
        r = signed_cr + b;
        g = signed_cb + b;
    } else {
        g = static_cast<std::int64_t>(y) - correction;
        r = signed_cr + g;
        b = signed_cb + g;
    }

    return {
        static_cast<std::uint16_t>(static_cast<std::uint64_t>(r) & mask),
        static_cast<std::uint16_t>(static_cast<std::uint64_t>(g) & mask),
        static_cast<std::uint16_t>(static_cast<std::uint64_t>(b) & mask),
    };
}

} // namespace mffv1::syntax
