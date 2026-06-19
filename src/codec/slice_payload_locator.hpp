#pragma once

#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/slice_descriptor.hpp"
#include "mffv1/stream_parameters.hpp"

#include <cstddef>
#include <vector>

namespace mffv1::codec {

class SlicePayloadLocator {
public:
    Status locate_trailing_slice(ByteSpan frame_payload,
                                 const syntax::StreamParameters& stream,
                                 syntax::SliceDescriptor& descriptor,
                                 bool verify_crc = false) const;

    Status locate_slices(ByteSpan frame_payload,
                         const syntax::StreamParameters& stream,
                         std::size_t maximum_slice_count,
                         std::vector<syntax::SliceDescriptor>& descriptors,
                         bool verify_crc = false) const;
};

} // namespace mffv1::codec
