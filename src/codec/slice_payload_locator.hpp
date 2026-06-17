#pragma once

#include "ffv1/frame.hpp"
#include "ffv1/result.hpp"
#include "ffv1/slice_descriptor.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

class SlicePayloadLocator {
public:
    Status locate_trailing_slice(ByteSpan frame_payload,
                                 const syntax::StreamParameters& stream,
                                 syntax::SliceDescriptor& descriptor) const;
};

} // namespace ffv1::codec
