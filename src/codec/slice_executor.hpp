#pragma once

#include "ffv1/frame.hpp"
#include "ffv1/result.hpp"
#include "ffv1/slice_descriptor.hpp"
#include "ffv1/stream_parameters.hpp"

#include <span>

namespace ffv1::codec {

class SliceExecutor {
public:
    explicit SliceExecutor(const syntax::StreamParameters& stream) noexcept;

    Status decode(MutableFrameView output, std::span<const syntax::SliceDescriptor> slices) const;

private:
    Status decode_slice(MutableFrameView output, const syntax::SliceDescriptor& slice) const;

    const syntax::StreamParameters& stream_;
};

} // namespace ffv1::codec
