#pragma once

#include <cstdint>

#include "ffv1/frame.hpp"

namespace ffv1::syntax {

struct SliceDescriptor {
    std::uint32_t index = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ByteSpan payload;
    std::uint64_t payload_byte_offset = 0;
    std::uint32_t expected_crc = 0;
    bool has_crc = false;
};

} // namespace ffv1::syntax

