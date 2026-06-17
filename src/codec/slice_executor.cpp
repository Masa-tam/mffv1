#include "codec/slice_executor.hpp"

#include "codec/slice_decoder.hpp"
#include "codec/slice_output_window.hpp"
#include "util/status.hpp"

#include <cstdint>

namespace ffv1::codec {

namespace {

std::uint32_t normalize_thread_count(int thread_count) noexcept
{
    if (thread_count <= 1) {
        return 1;
    }
    return static_cast<std::uint32_t>(thread_count);
}

} // namespace

SliceExecutor::SliceExecutor(const syntax::StreamParameters& stream) noexcept
    : SliceExecutor(stream, 1)
{
}

SliceExecutor::SliceExecutor(const syntax::StreamParameters& stream, int thread_count) noexcept
    : stream_(stream)
    , thread_count_(normalize_thread_count(thread_count))
{
}

Status SliceExecutor::decode(MutableFrameView output, std::span<const syntax::SliceDescriptor> slices) const
{
    for (const auto& slice : slices) {
        Status status = decode_slice(output, slice);
        if (!status.ok()) {
            set_slice_location_if_missing(status, slice.index);
            return status;
        }
    }
    return ok_status();
}

std::uint32_t SliceExecutor::thread_count() const noexcept
{
    return thread_count_;
}

Status SliceExecutor::decode_slice(MutableFrameView output, const syntax::SliceDescriptor& slice) const
{
    SliceOutputWindow window;
    Status status = window.validate(stream_, output, slice);
    if (!status.ok()) {
        return status;
    }

    SliceState state;
    status = state.reset(stream_);
    if (!status.ok()) {
        return status;
    }

    const SliceDecoder decoder(stream_);
    return decoder.decode(slice, window, state);
}

} // namespace ffv1::codec
