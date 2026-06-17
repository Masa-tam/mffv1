#include "codec/slice_decoder.hpp"

#include "entropy/range_coder.hpp"
#include "ffv1/context_model.hpp"
#include "ffv1/predictor.hpp"
#include "util/status.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace ffv1::syntax {

Status LineState::reset(std::uint32_t width)
{
    if (width == 0) {
        return make_error(ErrorCode::InvalidArgument, "line state width must be non-zero");
    }
    previous_.assign(width, 0);
    second_previous_.assign(width, 0);
    current_.assign(width, 0);
    return ok_status();
}

std::uint32_t LineState::width() const noexcept
{
    return static_cast<std::uint32_t>(current_.size());
}

const std::vector<std::int32_t>& LineState::previous() const noexcept
{
    return previous_;
}

const std::vector<std::int32_t>& LineState::second_previous() const noexcept
{
    return second_previous_;
}

const std::vector<std::int32_t>& LineState::current() const noexcept
{
    return current_;
}

std::vector<std::int32_t>& LineState::mutable_previous() noexcept
{
    return previous_;
}

std::vector<std::int32_t>& LineState::mutable_current() noexcept
{
    return current_;
}

void LineState::swap_lines() noexcept
{
    second_previous_ = previous_;
    previous_.swap(current_);
    std::fill(current_.begin(), current_.end(), 0);
}

} // namespace ffv1::syntax

namespace ffv1::codec {

namespace {

void add_byte_offset(Status& status, std::uint64_t base_offset) noexcept
{
    if (status.location.has_byte_offset) {
        status.location.byte_offset += base_offset;
    } else {
        set_byte_location_if_missing(status, base_offset);
    }
}

void set_reader_byte_offset(Status& status,
                            std::uint64_t content_offset,
                            std::uint64_t reader_offset) noexcept
{
    if (status.location.has_byte_offset) {
        status.location.byte_offset += content_offset;
    } else {
        set_byte_location_if_missing(status, content_offset + reader_offset);
    }
}

} // namespace

Status SliceState::reset(const syntax::StreamParameters& stream)
{
    const auto planes = syntax::coded_plane_count(stream);
    line_states_.resize(planes);
    for (std::size_t i = 0; i < line_states_.size(); ++i) {
        const std::uint32_t width = syntax::plane_width(stream, i);
        Status status = line_states_[i].reset(width);
        if (!status.ok()) {
            return status;
        }
    }
    return ok_status();
}

std::size_t SliceState::plane_count() const noexcept
{
    return line_states_.size();
}

syntax::LineState& SliceState::line_state(std::size_t plane_index) noexcept
{
    return line_states_[plane_index];
}

const syntax::LineState& SliceState::line_state(std::size_t plane_index) const noexcept
{
    return line_states_[plane_index];
}

SliceDecoder::SliceDecoder(const syntax::StreamParameters& stream) noexcept
    : stream_(stream)
{
}

Status SliceDecoder::decode(const syntax::SliceDescriptor& slice,
                            SliceOutputWindow& output,
                            SliceState& state) const
{
    if (slice.content_byte_offset < slice.payload_byte_offset) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice content offset is before payload",
                               slice.content_byte_offset);
    }
    const auto local_content_offset = slice.content_byte_offset - slice.payload_byte_offset;
    if (local_content_offset > slice.payload.size()) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice content offset is outside payload",
                               slice.content_byte_offset);
    }
    auto local_content_end = static_cast<std::uint64_t>(slice.payload.size());
    if (slice.footer_byte_offset != 0 || slice.slice_size != 0) {
        if (slice.footer_byte_offset < slice.payload_byte_offset) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "slice footer offset is before payload",
                                   slice.footer_byte_offset);
        }
        const auto local_footer_offset = slice.footer_byte_offset - slice.payload_byte_offset;
        if (local_footer_offset > slice.payload.size()) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "slice footer offset is outside payload",
                                   slice.footer_byte_offset);
        }
        if (local_footer_offset < local_content_offset) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "slice footer offset is before content",
                                   slice.footer_byte_offset);
        }
        local_content_end = local_footer_offset;
    }
    const auto content_payload = slice.payload.subspan(static_cast<std::size_t>(local_content_offset),
                                                       static_cast<std::size_t>(local_content_end
                                                                                - local_content_offset));
    if (content_payload.empty()) {
        return make_byte_error(ErrorCode::SyntaxError, "slice payload is empty", slice.content_byte_offset);
    }
    if (output.plane_count() != state.plane_count()) {
        return make_error(ErrorCode::InvalidArgument, "slice output and state plane counts differ");
    }
    if (output.plane_count() != syntax::coded_plane_count(stream_)) {
        return make_error(ErrorCode::InvalidArgument, "slice output plane count does not match stream");
    }
    if (stream_.bits_per_raw_sample == 0 || stream_.bits_per_raw_sample > 16) {
        return make_error(ErrorCode::UnsupportedFeature, "only 1-16 bit samples are supported");
    }
    entropy::RangeCoder reader;
    if (stream_.quant_table_sets.empty()) {
        return make_error(ErrorCode::InvalidState, "stream has no quantization table sets");
    }
    if (slice.quant_table_set_indexes.empty()) {
        return make_error(ErrorCode::SyntaxError, "slice has no quantization table set indexes");
    }
    const auto quant_table_set_index = slice.quant_table_set_indexes[0];
    if (quant_table_set_index >= stream_.quant_table_sets.size()) {
        return make_error(ErrorCode::SyntaxError, "slice quantization table set index is out of range");
    }
    const syntax::ContextModel context_model(stream_.quant_table_sets[quant_table_set_index]);

    Status status = reader.reset(content_payload, context_model.context_count());
    if (!status.ok()) {
        add_byte_offset(status, slice.content_byte_offset);
        return status;
    }

    for (std::size_t plane_index = 0; plane_index < output.plane_count(); ++plane_index) {
        auto& line = state.line_state(plane_index);
        for (std::uint32_t y = 0; y < output.plane_height(plane_index); ++y) {
            auto* row_u8 = output.row_u8(plane_index, y);
            auto* row_u16 = output.row_u16(plane_index, y);
            if (stream_.bits_per_raw_sample <= 8 && row_u8 == nullptr) {
                return make_error(ErrorCode::InvalidArgument, "slice output row is not writable as uint8");
            }
            if (stream_.bits_per_raw_sample > 8 && row_u16 == nullptr) {
                return make_error(ErrorCode::InvalidArgument, "slice output row is not writable as uint16");
            }

            std::int32_t left = 0;
            for (std::uint32_t x = 0; x < output.plane_width(plane_index); ++x) {
                const std::int32_t far_left = x > 1 ? line.current()[x - 2] : 0;
                const std::int32_t top = line.previous()[x];
                const std::int32_t top_left = x == 0 ? 0 : line.previous()[x - 1];
                const std::int32_t top_right =
                    (x + 1) < output.plane_width(plane_index) ? line.previous()[x + 1] : top;
                const std::int32_t top_top = line.second_previous()[x];
                const std::int32_t prediction = syntax::Predictor::median_predict(left, top, top_left);

                syntax::ContextDecision context;
                status = context_model.derive_context({far_left, left, top, top_left, top_right, top_top}, context);
                if (!status.ok()) {
                    return status;
                }

                std::int64_t difference64 = 0;
                status = reader.read_signed(context.context, difference64);
                if (!status.ok()) {
                    set_reader_byte_offset(status, slice.content_byte_offset, reader.byte_position());
                    return status;
                }
                if (context.invert_difference) {
                    difference64 = -difference64;
                }
                if (difference64 < std::numeric_limits<std::int32_t>::min()
                    || difference64 > std::numeric_limits<std::int32_t>::max()) {
                    return make_error(ErrorCode::SyntaxError, "sample difference is outside int32 range");
                }

                const std::int32_t sample =
                    syntax::Predictor::reconstruct(prediction,
                                                   static_cast<std::int32_t>(difference64),
                                                   stream_.bits_per_raw_sample);
                if (stream_.bits_per_raw_sample <= 8) {
                    row_u8[x] = static_cast<std::uint8_t>(sample);
                } else {
                    row_u16[x] = static_cast<std::uint16_t>(sample);
                }
                line.mutable_current()[x] = sample;
                left = sample;
            }
            line.swap_lines();
        }
    }

    return ok_status();
}

} // namespace ffv1::codec
