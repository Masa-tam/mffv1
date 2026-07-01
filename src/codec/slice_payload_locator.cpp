#include "codec/slice_payload_locator.hpp"

#include "bitstream/bit_reader.hpp"
#include "codec/slice_footer_parser.hpp"
#include "util/crc32.hpp"
#include "util/status.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace mffv1::codec {

namespace {

struct TrailingSliceResult {
    Status status;
    syntax::SliceDescriptor descriptor;
    bool has_descriptor = false;
};

TrailingSliceResult locate_trailing_slice_candidate(ByteSpan frame_payload,
                                                    const syntax::StreamParameters& stream,
                                                    bool verify_crc,
                                                    const syntax::SliceDescriptor* seed = nullptr)
{
    const SliceFooterParser footer_parser;
    const auto footer_size = footer_parser.footer_size(stream);
    if (frame_payload.size() < footer_size) {
        return {
            make_byte_error(ErrorCode::SyntaxError,
                            "frame payload is too small to contain a slice footer",
                            0),
            {},
            false,
        };
    }

    syntax::SliceDescriptor next;
    if (seed != nullptr) {
        next = *seed;
    }
    const auto footer_offset = frame_payload.size() - footer_size;
    next.footer_byte_offset = footer_offset;

    bitstream::BitReader footer_reader(frame_payload.subspan(footer_offset, footer_size));
    Status status = footer_parser.read(footer_reader, stream, next);
    if (!status.ok()) {
        if (status.location.has_byte_offset) {
            status.location.byte_offset += footer_offset;
        } else {
            set_byte_location_if_missing(status, footer_offset);
        }
        return {std::move(status), {}, false};
    }

    if (next.slice_size < footer_size) {
        return {
            make_byte_error(ErrorCode::SyntaxError,
                            "slice footer size is smaller than the footer",
                            footer_offset),
            {},
            false,
        };
    }
    if (next.slice_size > frame_payload.size()) {
        return {
            make_byte_error(ErrorCode::SyntaxError,
                            "slice footer size is larger than the frame payload",
                            footer_offset),
            {},
            false,
        };
    }

    next.payload_byte_offset = frame_payload.size() - next.slice_size;
    next.payload = frame_payload.subspan(static_cast<std::size_t>(next.payload_byte_offset),
                                         next.slice_size);
    next.footer_byte_offset = next.payload_byte_offset + next.slice_size - footer_size;
    if (verify_crc && next.has_crc && util::crc32_ieee_msb(next.payload) != 0) {
        return {
            make_byte_error(ErrorCode::CrcMismatch,
                            "slice CRC remainder is non-zero",
                            next.footer_byte_offset + 4),
            next,
            true,
        };
    }
    return {ok_status(), next, true};
}

bool count_preceding_slices(ByteSpan frame_payload,
                            const syntax::StreamParameters& stream,
                            std::size_t prefix_size,
                            std::size_t limit,
                            std::size_t& out_count)
{
    out_count = 0;
    auto remaining_size = prefix_size;
    while (remaining_size != 0) {
        if (out_count == limit) {
            return false;
        }
        const auto result = locate_trailing_slice_candidate(
            frame_payload.subspan(0, remaining_size), stream, false);
        if (!result.status.ok() || !result.has_descriptor) {
            return false;
        }
        remaining_size = static_cast<std::size_t>(result.descriptor.payload_byte_offset);
        ++out_count;
    }
    return true;
}

} // namespace

Status SlicePayloadLocator::locate_trailing_slice(ByteSpan frame_payload,
                                                  const syntax::StreamParameters& stream,
                                                  syntax::SliceDescriptor& descriptor,
                                                  bool verify_crc) const
{
    const auto result = locate_trailing_slice_candidate(
        frame_payload, stream, verify_crc, &descriptor);
    if (!result.status.ok()) {
        return result.status;
    }
    descriptor = result.descriptor;
    return ok_status();
}

Status SlicePayloadLocator::locate_slices(ByteSpan frame_payload,
                                          const syntax::StreamParameters& stream,
                                          std::size_t maximum_slice_count,
                                          std::vector<syntax::SliceDescriptor>& descriptors,
                                          bool verify_crc) const
{
    if (maximum_slice_count == 0) {
        return make_error(ErrorCode::InvalidArgument, "maximum slice count must be non-zero");
    }
    if (maximum_slice_count > std::numeric_limits<std::uint32_t>::max()) {
        return make_error(ErrorCode::ResourceExhausted,
                          "maximum slice count exceeds the supported index range");
    }

    const SliceFooterParser footer_parser;
    const auto footer_size = footer_parser.footer_size(stream);
    std::vector<syntax::SliceDescriptor> located;
    located.reserve(std::min(maximum_slice_count, frame_payload.size() / footer_size));
    auto remaining_size = frame_payload.size();
    while (remaining_size != 0) {
        if (located.size() == maximum_slice_count) {
            Status status = make_byte_error(ErrorCode::SyntaxError,
                                            "frame contains more slices than raster cells",
                                            0);
            set_slice_location_if_missing(status, 0);
            return status;
        }

        const auto result = locate_trailing_slice_candidate(
            frame_payload.subspan(0, remaining_size), stream, verify_crc);
        if (!result.status.ok()) {
            Status status = result.status;
            if (status.code == ErrorCode::CrcMismatch && result.has_descriptor) {
                std::size_t preceding_count = 0;
                const auto remaining_limit =
                    maximum_slice_count - located.size() - 1;
                if (count_preceding_slices(frame_payload,
                                           stream,
                                           static_cast<std::size_t>(
                                               result.descriptor.payload_byte_offset),
                                           remaining_limit,
                                           preceding_count)) {
                    status.location.slice_index =
                        static_cast<std::uint32_t>(preceding_count);
                    status.location.has_slice_index = true;
                    return status;
                }
            }
            set_slice_location_if_missing(status, 0);
            return status;
        }

        remaining_size = static_cast<std::size_t>(result.descriptor.payload_byte_offset);
        located.push_back(result.descriptor);
    }

    std::reverse(located.begin(), located.end());
    for (std::size_t index = 0; index < located.size(); ++index) {
        located[index].index = static_cast<std::uint32_t>(index);
    }
    descriptors = std::move(located);
    return ok_status();
}

} // namespace mffv1::codec
