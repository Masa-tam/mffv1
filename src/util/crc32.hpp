#pragma once

#include "mffv1/frame.hpp"

#include <cstdint>

namespace mffv1::util {

[[nodiscard]] std::uint32_t crc32_ieee_msb(ByteSpan bytes) noexcept;

} // namespace mffv1::util
