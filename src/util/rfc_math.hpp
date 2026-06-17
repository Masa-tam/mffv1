#pragma once

#include <cstdint>

namespace ffv1::util {

std::int64_t arithmetic_right_shift(std::int64_t value, std::uint8_t shift) noexcept;
std::int32_t median3(std::int32_t a, std::int32_t b, std::int32_t c) noexcept;
std::int32_t wrap_sample_difference(std::int32_t value, std::uint8_t bits_per_raw_sample) noexcept;

} // namespace ffv1::util

