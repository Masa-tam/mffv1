#include "codec/frame_parser.hpp"

#include "codec/slice_header_parser.hpp"
#include "entropy/range_coder.hpp"
#include "util/status.hpp"

#include <cstdint>

namespace ffv1::codec {

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

    return ok_status();
}

Status FrameParser::parse_with_range_header(ByteSpan payload, FrameDecodeContext& out_frame) const
{
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
