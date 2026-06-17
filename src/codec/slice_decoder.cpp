#include "codec/slice_decoder.hpp"

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
    return make_error(ErrorCode::NotImplemented, "slice decoding is not implemented yet");
}

} // namespace ffv1::codec

