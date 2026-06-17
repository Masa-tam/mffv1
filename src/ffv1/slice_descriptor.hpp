#pragma once

#include <cstdint>
#include <vector>

#include "ffv1/frame.hpp"

namespace ffv1::syntax {

struct SliceDescriptor {
    std::uint32_t index = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t raster_x = 0;
    std::uint32_t raster_y = 0;
    std::uint32_t raster_width = 0;
    std::uint32_t raster_height = 0;
    ByteSpan payload;
    std::uint64_t header_byte_offset = 0;
    std::uint64_t content_byte_offset = 0;
    std::uint64_t payload_byte_offset = 0;
    std::uint64_t footer_byte_offset = 0;
    std::uint32_t slice_size = 0;
    std::vector<std::uint32_t> quant_table_set_indexes;
    std::uint8_t error_status = 0;
    std::uint32_t expected_crc = 0;
    bool has_crc = false;
};

} // namespace ffv1::syntax
