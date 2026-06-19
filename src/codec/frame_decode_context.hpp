#pragma once

#include <vector>

#include "mffv1/frame.hpp"
#include "mffv1/slice_descriptor.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

struct FrameDecodeContext {
    const syntax::StreamParameters* stream = nullptr;
    MutableFrameView output;
    std::vector<syntax::SliceDescriptor> slices;
    FrameInfo frame_info;
    bool keyframe = false;
};

} // namespace mffv1::codec
