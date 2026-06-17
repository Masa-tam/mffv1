#pragma once

#include "ffv1/frame.hpp"
#include "ffv1/result.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

class FrameValidator {
public:
    Status validate_output(const syntax::StreamParameters& stream,
                           MutableFrameView output) const;
    Status validate_input(const syntax::StreamParameters& stream,
                          FrameView input) const;
};

} // namespace ffv1::codec

