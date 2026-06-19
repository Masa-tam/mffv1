#pragma once

#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

class FrameValidator {
public:
    Status validate_output(const syntax::StreamParameters& stream,
                           MutableFrameView output) const;
    Status validate_input(const syntax::StreamParameters& stream,
                          FrameView input) const;
};

} // namespace mffv1::codec

