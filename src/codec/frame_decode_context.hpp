#pragma once

#include <vector>

#include "ffv1/frame.hpp"
#include "ffv1/slice_descriptor.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

struct FrameDecodeContext {
    const syntax::StreamParameters* stream = nullptr;
    MutableFrameView output;
    std::vector<syntax::SliceDescriptor> slices;
    FrameInfo frame_info;
    bool keyframe = false;
};

} // namespace ffv1::codec
