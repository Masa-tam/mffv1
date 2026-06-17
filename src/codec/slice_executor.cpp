#include "codec/slice_executor.hpp"

#include "codec/slice_decoder.hpp"
#include "codec/slice_output_window.hpp"
#include "util/status.hpp"

namespace ffv1::codec {

SliceExecutor::SliceExecutor(const syntax::StreamParameters& stream) noexcept
    : stream_(stream)
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
