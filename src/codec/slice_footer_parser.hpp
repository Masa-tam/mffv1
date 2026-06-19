#pragma once

#include "bitstream/bit_reader.hpp"
#include "mffv1/result.hpp"
#include "mffv1/slice_descriptor.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

class SliceFooterParser {
public:
    [[nodiscard]] std::size_t footer_size(const syntax::StreamParameters& stream) const noexcept;

    Status read(bitstream::BitReader& reader,
                const syntax::StreamParameters& stream,
                syntax::SliceDescriptor& descriptor) const;

    Status read_from_end(ByteSpan slice_payload,
                         const syntax::StreamParameters& stream,
                         syntax::SliceDescriptor& descriptor,
                         bool verify_crc = false) const;
};

} // namespace mffv1::codec
