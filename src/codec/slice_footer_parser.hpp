#pragma once

#include "bitstream/bit_reader.hpp"
#include "ffv1/result.hpp"
#include "ffv1/slice_descriptor.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

class SliceFooterParser {
public:
    Status read(bitstream::BitReader& reader,
                const syntax::StreamParameters& stream,
                syntax::SliceDescriptor& descriptor) const;
};

} // namespace ffv1::codec
