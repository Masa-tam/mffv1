#include "codec/slice_footer_parser.hpp"

#include "util/crc32.hpp"
#include "util/status.hpp"

#include <cstddef>
#include <cstdint>

namespace mffv1::codec {

std::size_t SliceFooterParser::footer_size(const syntax::StreamParameters& stream) const noexcept
{
    return stream.error_status_enabled ? 8u : 3u;
}

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
    const auto slice_size = static_cast<std::uint32_t>(value);

    if (!stream.error_status_enabled) {
        descriptor.slice_size = slice_size;
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
    const auto error_status = static_cast<std::uint8_t>(value);

    const auto crc_offset = reader.byte_position();
    status = reader.read_bits(32, value);
    if (!status.ok()) {
        set_byte_location_if_missing(status, crc_offset);
        return status;
    }
    descriptor.slice_size = slice_size;
    descriptor.error_status = error_status;
    descriptor.expected_crc = static_cast<std::uint32_t>(value);
    descriptor.has_crc = true;
    return ok_status();
}

Status SliceFooterParser::read_from_end(ByteSpan slice_payload,
                                        const syntax::StreamParameters& stream,
                                        syntax::SliceDescriptor& descriptor,
                                        bool verify_crc) const
{
    const auto required_footer_size = footer_size(stream);
    if (slice_payload.size() < required_footer_size) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice payload is too small to contain a footer",
                               descriptor.payload_byte_offset);
    }

    const auto footer_offset = slice_payload.size() - required_footer_size;
    auto next = descriptor;
    next.footer_byte_offset = next.payload_byte_offset + footer_offset;

    bitstream::BitReader reader(slice_payload.subspan(footer_offset, required_footer_size));
    Status status = read(reader, stream, next);
    if (!status.ok()) {
        if (status.location.has_byte_offset) {
            status.location.byte_offset += next.footer_byte_offset;
        } else {
            set_byte_location_if_missing(status, next.footer_byte_offset);
        }
        return status;
    }

    if (next.slice_size != slice_payload.size()) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice footer size does not match slice payload size",
                               next.footer_byte_offset);
    }

    if (verify_crc && next.has_crc && util::crc32_ieee_msb(slice_payload) != 0) {
        return make_byte_error(ErrorCode::CrcMismatch,
                               "slice CRC remainder is non-zero",
                               next.footer_byte_offset + 4);
    }

    descriptor = next;
    return ok_status();
}

} // namespace mffv1::codec
