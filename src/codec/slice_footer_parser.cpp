#include "codec/slice_footer_parser.hpp"

#include "util/status.hpp"

#include <cstdint>

namespace ffv1::codec {

Status SliceFooterParser::read(bitstream::BitReader& reader,
                               const syntax::StreamParameters& stream,
                               syntax::SliceDescriptor& descriptor) const
{
    Status status = reader.require_byte_aligned();
    if (!status.ok()) {
        set_byte_location_if_missing(status, reader.byte_position());
        return status;
    }

    std::uint64_t value = 0;
    const auto footer_offset = reader.byte_position();
    status = reader.read_bits(24, value);
    if (!status.ok()) {
        set_byte_location_if_missing(status, footer_offset);
        return status;
    }
    descriptor.slice_size = static_cast<std::uint32_t>(value);

    if (!stream.error_status_enabled) {
        descriptor.error_status = 0;
        descriptor.expected_crc = 0;
        descriptor.has_crc = false;
        return ok_status();
    }

    const auto error_status_offset = reader.byte_position();
    status = reader.read_bits(8, value);
    if (!status.ok()) {
        set_byte_location_if_missing(status, error_status_offset);
        return status;
    }
    if (value > 2) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice footer error_status is reserved",
                               error_status_offset);
    }
    descriptor.error_status = static_cast<std::uint8_t>(value);

    const auto crc_offset = reader.byte_position();
    status = reader.read_bits(32, value);
    if (!status.ok()) {
        set_byte_location_if_missing(status, crc_offset);
        return status;
    }
    descriptor.expected_crc = static_cast<std::uint32_t>(value);
    descriptor.has_crc = true;
    return ok_status();
}

} // namespace ffv1::codec
