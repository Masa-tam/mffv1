#pragma once

#include "ffv1/frame.hpp"
#include "ffv1/result.hpp"
#include "ffv1/slice_descriptor.hpp"
#include "ffv1/stream_parameters.hpp"

#include <cstddef>
#include <vector>

namespace ffv1::codec {

class SlicePayloadLocator {
public:
    Status locate_trailing_slice(ByteSpan frame_payload,
                                 const syntax::StreamParameters& stream,
                                 syntax::SliceDescriptor& descriptor,
                                 bool verify_crc = false) const;

    Status locate_slices(ByteSpan frame_payload,
                         const syntax::StreamParameters& stream,
                         std::size_t expected_slice_count,
                         std::vector<syntax::SliceDescriptor>& descriptors,
                         bool verify_crc = false) const;
};

} // namespace ffv1::codec
