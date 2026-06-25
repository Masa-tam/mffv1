#pragma once

#include <vector>

#include "bitstream/bit_writer.hpp"
#include "entropy/range_encoder.hpp"
#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

class SliceEncoder {
public:
    explicit SliceEncoder(const syntax::StreamParameters& stream) noexcept;

    Status encode_content(FrameView input,
                          std::vector<std::byte>& out_payload) const;
    Status encode_slice(FrameView input,
                        bool keyframe,
                        std::vector<std::byte>& out_payload) const;

private:
    Status validate_stream() const;
    Status encode_samples(FrameView input,
                          entropy::RangeEncoder& writer) const;
    Status encode_golomb_rice_samples(
        FrameView input,
        bitstream::BitWriter& writer) const;

    const syntax::StreamParameters& stream_;
};

} // namespace mffv1::codec
