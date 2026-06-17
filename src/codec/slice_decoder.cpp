#include "codec/slice_decoder.hpp"

#include "entropy/range_coder.hpp"
#include "ffv1/context_model.hpp"
#include "ffv1/predictor.hpp"

#include <cstdint>
#include <limits>

namespace ffv1::syntax {

Status LineState::reset(std::uint32_t width)
{
    if (width == 0) {
        return make_error(ErrorCode::InvalidArgument, "line state width must be non-zero");
    }
    previous_.assign(width, 0);
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
    previous_.swap(current_);
    std::fill(current_.begin(), current_.end(), 0);
}

} // namespace ffv1::syntax

namespace ffv1::codec {

Status SliceState::reset(const syntax::StreamParameters& stream)
{
    const auto planes = syntax::coded_plane_count(stream);
    line_states_.resize(planes);
    for (std::size_t i = 0; i < line_states_.size(); ++i) {
        std::uint32_t width = stream.width;
        if (i == 1 || i == 2) {
            const std::uint32_t add = (std::uint32_t{1} << stream.log2_h_chroma_subsample) - 1;
            width = (stream.width + add) >> stream.log2_h_chroma_subsample;
        }
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
    if (slice.payload.empty()) {
        return make_error(ErrorCode::SyntaxError, "slice payload is empty");
    }
    if (output.plane_count() != state.plane_count()) {
        return make_error(ErrorCode::InvalidArgument, "slice output and state plane counts differ");
    }
    if (output.plane_count() != syntax::coded_plane_count(stream_)) {
        return make_error(ErrorCode::InvalidArgument, "slice output plane count does not match stream");
    }
    if (stream_.chroma_planes || stream_.extra_plane || stream_.bits_per_raw_sample > 8
        || output.plane_count() != 1) {
        return make_error(ErrorCode::NotImplemented, "only 8-bit Y-only slice decoding is implemented");
    }

    entropy::RangeCoder reader;
    if (stream_.quant_table_sets.empty()) {
        return make_error(ErrorCode::InvalidState, "stream has no quantization table sets");
    }
    const syntax::ContextModel context_model(stream_.quant_table_sets.front());

    Status status = reader.reset(slice.payload, context_model.context_count());
    if (!status.ok()) {
        return status;
    }

    auto& line = state.line_state(0);
    for (std::uint32_t y = 0; y < output.plane_height(0); ++y) {
        auto* row = output.row_u8(0, y);
        if (row == nullptr) {
            return make_error(ErrorCode::InvalidArgument, "slice output row is not writable as uint8");
        }

        std::int32_t left = 0;
        for (std::uint32_t x = 0; x < output.plane_width(0); ++x) {
            const std::int32_t top = line.previous()[x];
            const std::int32_t top_left = x == 0 ? 0 : line.previous()[x - 1];
            const std::int32_t top_right =
                (x + 1) < output.plane_width(0) ? line.previous()[x + 1] : top;
            const std::int32_t prediction = syntax::Predictor::median_predict(left, top, top_left);

            std::uint32_t context_id = 0;
            status = context_model.derive_context({left, top, top_left, top_right}, context_id);
            if (!status.ok()) {
                return status;
            }

            std::int64_t difference64 = 0;
            status = reader.read_signed(context_id, difference64);
            if (!status.ok()) {
                return status;
            }
            if (difference64 < std::numeric_limits<std::int32_t>::min()
                || difference64 > std::numeric_limits<std::int32_t>::max()) {
                return make_error(ErrorCode::SyntaxError, "sample difference is outside int32 range");
            }

            const std::int32_t sample =
                syntax::Predictor::reconstruct(prediction,
                                               static_cast<std::int32_t>(difference64),
                                               stream_.bits_per_raw_sample);
            row[x] = static_cast<std::uint8_t>(sample);
            line.mutable_current()[x] = sample;
            left = sample;
        }
        line.swap_lines();
    }

    return ok_status();
}

} // namespace ffv1::codec
