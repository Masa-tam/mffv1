#pragma once

#include "ffv1/frame.hpp"

#include <cstdint>

namespace ffv1::util {

[[nodiscard]] std::uint32_t crc32_ieee_msb(ByteSpan bytes) noexcept;

} // namespace ffv1::util
