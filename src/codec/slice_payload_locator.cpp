#include "codec/slice_payload_locator.hpp"

#include "bitstream/bit_reader.hpp"
#include "codec/slice_footer_parser.hpp"
#include "util/status.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace ffv1::codec {

Status SlicePayloadLocator::locate_trailing_slice(ByteSpan frame_payload,
                                                  const syntax::StreamParameters& stream,
                                                  syntax::SliceDescriptor& descriptor) const
{
    const SliceFooterParser footer_parser;
    const auto footer_size = footer_parser.footer_size(stream);
    if (frame_payload.size() < footer_size) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "frame payload is too small to contain a slice footer",
                               0);
    }

    const auto footer_offset = frame_payload.size() - footer_size;
    descriptor.footer_byte_offset = footer_offset;

    bitstream::BitReader footer_reader(frame_payload.subspan(footer_offset, footer_size));
    Status status = footer_parser.read(footer_reader, stream, descriptor);
    if (!status.ok()) {
        if (status.location.has_byte_offset) {
            status.location.byte_offset += footer_offset;
        } else {
            set_byte_location_if_missing(status, footer_offset);
        }
        return status;
    }

    if (descriptor.slice_size < footer_size) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice footer size is smaller than the footer",
                               footer_offset);
    }
    if (descriptor.slice_size > frame_payload.size()) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice footer size is larger than the frame payload",
                               footer_offset);
    }

    descriptor.payload_byte_offset = frame_payload.size() - descriptor.slice_size;
    descriptor.payload = frame_payload.subspan(static_cast<std::size_t>(descriptor.payload_byte_offset),
                                               descriptor.slice_size);
    descriptor.footer_byte_offset = descriptor.payload_byte_offset + descriptor.slice_size - footer_size;
    return ok_status();
}

Status SlicePayloadLocator::locate_slices(ByteSpan frame_payload,
                                          const syntax::StreamParameters& stream,
                                          std::size_t expected_slice_count,
                                          std::vector<syntax::SliceDescriptor>& descriptors) const
{
    if (expected_slice_count == 0) {
        return make_error(ErrorCode::InvalidArgument, "expected slice count must be non-zero");
    }

    std::vector<syntax::SliceDescriptor> located;
    located.reserve(expected_slice_count);
    auto remaining_size = frame_payload.size();
    for (std::size_t index = 0; index < expected_slice_count; ++index) {
        syntax::SliceDescriptor descriptor;
        Status status = locate_trailing_slice(frame_payload.subspan(0, remaining_size), stream, descriptor);
        if (!status.ok()) {
            return status;
        }

        remaining_size = static_cast<std::size_t>(descriptor.payload_byte_offset);
        located.push_back(descriptor);
    }

    if (remaining_size != 0) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "frame payload contains bytes before the located slices",
                               0);
    }

    std::reverse(located.begin(), located.end());
    for (std::size_t index = 0; index < located.size(); ++index) {
        located[index].index = static_cast<std::uint32_t>(index);
    }
    descriptors = std::move(located);
    return ok_status();
}

} // namespace ffv1::codec
