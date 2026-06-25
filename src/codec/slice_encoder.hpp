#pragma once

#include <vector>

#include "bitstream/bit_writer.hpp"
#include "codec/slice_header_parser.hpp"
#include "codec/slice_input_window.hpp"
#include "entropy/range_encoder.hpp"
#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"
#include "simd/codec_kernels.hpp"

namespace mffv1::codec {

class SliceEncoder {
public:
    explicit SliceEncoder(const syntax::StreamParameters& stream) noexcept;
    SliceEncoder(const syntax::StreamParameters& stream,
                 const simd::CodecKernels& kernels) noexcept;

    Status encode_content(FrameView input,
                          std::vector<std::byte>& out_payload) const;
    Status encode_slice(FrameView input,
                        bool keyframe,
                        std::vector<std::byte>& out_payload) const;
    Status encode_slice(FrameView input,
                        const SliceHeaderValues& header,
                        bool write_keyframe,
                        bool keyframe,
                        std::vector<std::byte>& out_payload) const;

private:
    Status validate_stream() const;
    Status encode_samples(const SliceInputWindow& input,
                          entropy::RangeEncoder& writer) const;
    Status encode_golomb_rice_samples(
        const SliceInputWindow& input,
        bitstream::BitWriter& writer) const;

    const syntax::StreamParameters& stream_;
    const simd::CodecKernels& kernels_;
};

} // namespace mffv1::codec
