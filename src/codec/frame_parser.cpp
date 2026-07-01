#include "codec/frame_parser.hpp"

#include "codec/frame_info_builder.hpp"
#include "codec/slice_header_parser.hpp"
#include "codec/slice_payload_locator.hpp"
#include "codec/slice_raster_validator.hpp"
#include "bitstream/bit_reader.hpp"
#include "entropy/range_coder.hpp"
#include "util/status.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace mffv1::codec {

namespace {

std::size_t maximum_slice_count(const syntax::StreamParameters& stream) noexcept
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

Status validate_keyframe(const syntax::StreamParameters& stream,
                         bool keyframe,
                         std::uint64_t byte_offset)
{
    if (!keyframe && stream.intra_only) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "non-keyframe is invalid for an intra-only stream",
                               byte_offset);
    }
    return ok_status();
}

void finalize_frame_metadata(FrameDecodeContext& frame, bool keyframe) noexcept
{
    frame.keyframe = keyframe;
    frame.frame_info.keyframe = keyframe;
    frame.frame_info.slice_count =
        static_cast<std::uint32_t>(frame.slices.size());
}

} // namespace

FrameParser::FrameParser(const syntax::StreamParameters& stream) noexcept
    : FrameParser(stream, false)
{
}

FrameParser::FrameParser(const syntax::StreamParameters& stream, bool verify_crc) noexcept
    : stream_(stream)
    , verify_crc_(verify_crc)
{
}

Status FrameParser::parse(ByteSpan payload, FrameDecodeContext& out_frame) const
{
    FrameDecodeContext next_frame;
    Status status = initialize_frame(payload, next_frame);
    if (!status.ok()) {
        return status;
    }

    if (stream_.num_h_slices != 1 || stream_.num_v_slices != 1) {
        if (stream_.version >= 3) {
            return parse_with_range_header(payload, out_frame);
        }
        return make_error(ErrorCode::NotImplemented, "multi-slice frame parsing is not implemented yet");
    }
    entropy::RangeCoder frame_reader;
    const bool parse_legacy_range_header = stream_.version <= 1
        && stream_.entropy_mode == EntropyMode::Range;
    if (parse_legacy_range_header) {
        status = frame_reader.reset(payload, stream_.state_transition);
        if (!status.ok()) {
            return status;
        }
        bool keyframe = false;
        status = frame_reader.read_bool(keyframe);
        if (!status.ok()) {
            return status;
        }
        status = validate_keyframe(stream_, keyframe, 0);
        if (!status.ok()) {
            return status;
        }
        next_frame.keyframe = keyframe;
        next_frame.frame_info.keyframe = keyframe;
    } else if (stream_.version <= 1 && stream_.entropy_mode == EntropyMode::GolombRice) {
        bitstream::BitReader frame_bits(payload);
        std::uint8_t keyframe = 0;
        status = frame_bits.read_bit(keyframe);
        if (!status.ok()) {
            return status;
        }
        status = validate_keyframe(stream_, keyframe != 0, 0);
        if (!status.ok()) {
            return status;
        }
        next_frame.keyframe = keyframe != 0;
        next_frame.frame_info.keyframe = keyframe != 0;
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
    slice.content_byte_offset = parse_legacy_range_header ? frame_reader.byte_position() : 0;
    slice.content_bit_offset = stream_.version <= 1
            && stream_.entropy_mode == EntropyMode::GolombRice
        ? 1
        : 0;
    slice.payload_byte_offset = 0;
    slice.continues_frame_range_state = parse_legacy_range_header;
    next_frame.slices.push_back(slice);

    status = validate_slice_raster_coverage(stream_, next_frame.slices);
    if (!status.ok()) {
        set_slice_location_if_missing(status, slice.index);
        return status;
    }
    finalize_frame_metadata(next_frame, next_frame.keyframe);
    out_frame = std::move(next_frame);
    return ok_status();
}

Status FrameParser::parse_with_range_header(ByteSpan payload, FrameDecodeContext& out_frame) const
{
    FrameDecodeContext next_frame;
    Status status = parse_located_range_slices(payload, next_frame);
    if (!status.ok()) {
        return status;
    }
    out_frame = std::move(next_frame);
    return ok_status();
}

Status FrameParser::parse_with_header_reader(ByteSpan payload,
                                             entropy::SymbolReader& header_reader,
                                             FrameDecodeContext& out_frame) const
{
    FrameDecodeContext next_frame;
    Status status = initialize_frame(payload, next_frame);
    if (!status.ok()) {
        return status;
    }

    bool keyframe = false;
    status = header_reader.read_bool(keyframe);
    if (!status.ok()) {
        add_byte_offset(status, 0);
        return status;
    }
    status = validate_keyframe(stream_, keyframe, 0);
    if (!status.ok()) {
        return status;
    }

    const SliceHeaderParser header_parser;
    const auto max_slices = maximum_slice_count(stream_);
    for (std::size_t slice_index = 0; slice_index < max_slices; ++slice_index) {
        syntax::SliceDescriptor slice;
        slice.index = static_cast<std::uint32_t>(slice_index);
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
        next_frame.slices.push_back(slice);

        status = validate_slice_raster_coverage(stream_, next_frame.slices);
        if (status.ok()) {
            finalize_frame_metadata(next_frame, keyframe);
            out_frame = std::move(next_frame);
            return ok_status();
        }
        if (status.code != ErrorCode::SyntaxError
            || status.message != "slice raster coverage has missing cells") {
            set_slice_location_if_missing(status, slice.index);
            return status;
        }
    }

    status = validate_slice_raster_coverage(stream_, next_frame.slices);
    if (!status.ok()) {
        return status;
    }
    finalize_frame_metadata(next_frame, keyframe);
    out_frame = std::move(next_frame);
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
    status = payload_locator.locate_slices(payload,
                                           stream_,
                                           maximum_slice_count(stream_),
                                           located_slices,
                                           verify_crc_);
    if (!status.ok()) {
        return status;
    }

    const SliceHeaderParser header_parser;
    std::vector<syntax::SliceDescriptor> parsed_slices;
    parsed_slices.reserve(located_slices.size());
    bool keyframe = false;
    for (std::size_t slice_index = 0; slice_index < located_slices.size(); ++slice_index) {
        const auto& located_slice = located_slices[slice_index];
        entropy::RangeCoder header_reader;
        status = header_reader.reset(located_slice.payload, stream_.state_transition);
        if (!status.ok()) {
            add_byte_offset(status, located_slice.payload_byte_offset);
            set_slice_location_if_missing(status, located_slice.index);
            return status;
        }

        if (slice_index == 0) {
            status = header_reader.read_bool(keyframe);
            if (!status.ok()) {
                add_byte_offset(status, located_slice.payload_byte_offset);
                set_slice_location_if_missing(status, located_slice.index);
                return status;
            }
            status = validate_keyframe(stream_, keyframe, located_slice.payload_byte_offset);
            if (!status.ok()) {
                set_slice_location_if_missing(status, located_slice.index);
                return status;
            }
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
        parsed_slices.push_back(parsed_slice);
    }

    status = validate_slice_raster_coverage(stream_, parsed_slices);
    if (!status.ok()) {
        return status;
    }
    out_frame.slices = std::move(parsed_slices);
    finalize_frame_metadata(out_frame, keyframe);
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
    out_frame.keyframe = false;
    out_frame.frame_info = make_frame_info(stream_);
    out_frame.frame_info.keyframe = false;
    out_frame.frame_info.slice_count = 0;

    return ok_status();
}

} // namespace mffv1::codec
