#include "codec/frame_parser.hpp"

#include "codec/slice_header_parser.hpp"
#include "codec/slice_payload_locator.hpp"
#include "codec/slice_raster_validator.hpp"
#include "entropy/range_coder.hpp"
#include "util/status.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ffv1::codec {

namespace {

std::size_t expected_slice_count(const syntax::StreamParameters& stream) noexcept
{
    return static_cast<std::size_t>(stream.num_h_slices) * static_cast<std::size_t>(stream.num_v_slices);
}

void add_byte_offset(Status& status, std::uint64_t base_offset) noexcept
{
    if (status.location.has_byte_offset) {
        status.location.byte_offset += base_offset;
    } else {
        set_byte_location_if_missing(status, base_offset);
    }
}

} // namespace

FrameParser::FrameParser(const syntax::StreamParameters& stream) noexcept
    : stream_(stream)
{
}

Status FrameParser::parse(ByteSpan payload, FrameDecodeContext& out_frame) const
{
    Status status = initialize_frame(payload, out_frame);
    if (!status.ok()) {
        return status;
    }

    if (stream_.num_h_slices != 1 || stream_.num_v_slices != 1) {
        return make_error(ErrorCode::NotImplemented, "multi-slice frame parsing is not implemented yet");
    }

    syntax::SliceDescriptor slice;
    slice.index = 0;
    SliceHeaderValues header;
    header.x = 0;
    header.y = 0;
    header.width = 1;
    header.height = 1;
    header.quant_table_set_indexes.push_back(0);
    const SliceHeaderParser header_parser;
    status = header_parser.apply_raster(stream_, header, slice);
    if (!status.ok()) {
        set_slice_location_if_missing(status, slice.index);
        return status;
    }
    slice.payload = payload;
    slice.header_byte_offset = 0;
    slice.content_byte_offset = 0;
    slice.payload_byte_offset = 0;
    out_frame.slices.push_back(slice);

    status = validate_slice_raster_coverage(stream_, out_frame.slices);
    if (!status.ok()) {
        set_slice_location_if_missing(status, slice.index);
        return status;
    }
    return ok_status();
}

Status FrameParser::parse_with_range_header(ByteSpan payload, FrameDecodeContext& out_frame) const
{
    if (stream_.num_h_slices != 1 || stream_.num_v_slices != 1) {
        return parse_located_range_slices(payload, out_frame);
    }

    entropy::RangeCoder header_reader;
    Status status = header_reader.reset(payload);
    if (!status.ok()) {
        set_slice_location_if_missing(status, 0);
        return status;
    }
    return parse_with_header_reader(payload, header_reader, out_frame);
}

Status FrameParser::parse_with_header_reader(ByteSpan payload,
                                             entropy::SymbolReader& header_reader,
                                             FrameDecodeContext& out_frame) const
{
    Status status = initialize_frame(payload, out_frame);
    if (!status.ok()) {
        return status;
    }

    if (stream_.num_h_slices != 1 || stream_.num_v_slices != 1) {
        return make_error(ErrorCode::NotImplemented, "multi-slice frame parsing is not implemented yet");
    }

    syntax::SliceDescriptor slice;
    slice.index = 0;
    const SliceHeaderParser header_parser;
    status = header_parser.read_descriptor(header_reader, stream_, slice);
    if (!status.ok()) {
        set_slice_location_if_missing(status, slice.index);
        return status;
    }
    if (slice.content_byte_offset > payload.size()) {
        status = make_byte_error(ErrorCode::SyntaxError,
                                 "slice header consumes more bytes than the frame payload contains",
                                 slice.content_byte_offset);
        set_slice_location_if_missing(status, slice.index);
        return status;
    }
    slice.payload = payload;
    out_frame.slices.push_back(slice);
    status = validate_slice_raster_coverage(stream_, out_frame.slices);
    if (!status.ok()) {
        set_slice_location_if_missing(status, slice.index);
        return status;
    }
    return ok_status();
}

Status FrameParser::parse_located_range_slices(ByteSpan payload, FrameDecodeContext& out_frame) const
{
    Status status = initialize_frame(payload, out_frame);
    if (!status.ok()) {
        return status;
    }

    std::vector<syntax::SliceDescriptor> located_slices;
    const SlicePayloadLocator payload_locator;
    status = payload_locator.locate_slices(payload, stream_, expected_slice_count(stream_), located_slices);
    if (!status.ok()) {
        return status;
    }

    const SliceHeaderParser header_parser;
    for (const auto& located_slice : located_slices) {
        entropy::RangeCoder header_reader;
        status = header_reader.reset(located_slice.payload);
        if (!status.ok()) {
            add_byte_offset(status, located_slice.payload_byte_offset);
            set_slice_location_if_missing(status, located_slice.index);
            return status;
        }

        syntax::SliceDescriptor parsed_slice;
        parsed_slice.index = located_slice.index;
        status = header_parser.read_descriptor(header_reader, stream_, parsed_slice);
        if (!status.ok()) {
            add_byte_offset(status, located_slice.payload_byte_offset);
            set_slice_location_if_missing(status, located_slice.index);
            return status;
        }
        if (parsed_slice.content_byte_offset > located_slice.payload.size()) {
            status = make_byte_error(ErrorCode::SyntaxError,
                                     "slice header consumes more bytes than the slice payload contains",
                                     located_slice.payload_byte_offset + parsed_slice.content_byte_offset);
            set_slice_location_if_missing(status, located_slice.index);
            return status;
        }

        parsed_slice.index = located_slice.index;
        parsed_slice.payload = located_slice.payload;
        parsed_slice.payload_byte_offset = located_slice.payload_byte_offset;
        parsed_slice.header_byte_offset += located_slice.payload_byte_offset;
        parsed_slice.content_byte_offset += located_slice.payload_byte_offset;
        parsed_slice.footer_byte_offset = located_slice.footer_byte_offset;
        parsed_slice.slice_size = located_slice.slice_size;
        parsed_slice.error_status = located_slice.error_status;
        parsed_slice.expected_crc = located_slice.expected_crc;
        parsed_slice.has_crc = located_slice.has_crc;
        out_frame.slices.push_back(parsed_slice);
    }

    status = validate_slice_raster_coverage(stream_, out_frame.slices);
    if (!status.ok()) {
        return status;
    }
    return ok_status();
}

Status FrameParser::initialize_frame(ByteSpan payload, FrameDecodeContext& out_frame) const
{
    if (payload.empty()) {
        return make_error(ErrorCode::InvalidArgument, "frame payload is empty");
    }
    if (stream_.width == 0 || stream_.height == 0) {
        return make_error(ErrorCode::InvalidState, "stream dimensions must be known before parsing frames");
    }
    if (stream_.num_h_slices == 0 || stream_.num_v_slices == 0) {
        return make_error(ErrorCode::InvalidState, "slice grid dimensions must be non-zero");
    }

    out_frame.stream = &stream_;
    out_frame.slices.clear();
    out_frame.frame_info.width = stream_.width;
    out_frame.frame_info.height = stream_.height;
    out_frame.frame_info.version = static_cast<std::uint8_t>(stream_.version);
    out_frame.frame_info.bits_per_raw_sample = stream_.bits_per_raw_sample;
    out_frame.frame_info.plane_count = syntax::coded_plane_count(stream_);

    return ok_status();
}

} // namespace ffv1::codec
