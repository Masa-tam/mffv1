#include "util/rfc_math.hpp"

#include <algorithm>
#include <cstdint>

namespace mffv1::util {

std::int64_t arithmetic_right_shift(std::int64_t value, std::uint8_t shift) noexcept
{
    if (shift == 0) {
        return value;
    }
    if (shift >= 63) {
        return value < 0 ? -1 : 0;
    }

    const auto divisor = std::int64_t{1} << shift;
    if (value >= 0) {
        return value / divisor;
    }

    return -(((-value) + divisor - 1) / divisor);
}

std::int32_t median3(std::int32_t a, std::int32_t b, std::int32_t c) noexcept
{
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}

std::int32_t wrap_sample_difference(std::int32_t value, std::uint8_t bits_per_raw_sample) noexcept
{
    if (bits_per_raw_sample == 0 || bits_per_raw_sample >= 31) {
        return value;
    }

    // RFC 9043 Section 3.8, Figure 10.
    const std::int32_t range = std::int32_t{1} << bits_per_raw_sample;
    const std::int32_t half_range = range >> 1;

    value %= range;
    if (value < -half_range) {
        value += range;
    } else if (value >= half_range) {
        value -= range;
    }
    return value;
}

} // namespace mffv1::util
