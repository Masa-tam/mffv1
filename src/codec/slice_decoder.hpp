#pragma once

#include "codec/slice_output_window.hpp"
#include "codec/slice_state.hpp"
#include "mffv1/result.hpp"
#include "mffv1/slice_descriptor.hpp"
#include "mffv1/stream_parameters.hpp"
#include "simd/codec_kernels.hpp"

namespace mffv1::codec {

class SliceDecoder {
public:
    explicit SliceDecoder(const syntax::StreamParameters& stream) noexcept;
    SliceDecoder(const syntax::StreamParameters& stream,
                 const simd::CodecKernels& kernels) noexcept;

    Status validate(const syntax::SliceDescriptor& slice,
                    const SliceOutputWindow& output) const;
    Status decode(const syntax::SliceDescriptor& slice,
                  SliceOutputWindow& output,
                  SliceState& state) const;

private:
    Status resolve_content_payload(const syntax::SliceDescriptor& slice,
                                   ByteSpan& out_payload) const;

    const syntax::StreamParameters& stream_;
    const simd::CodecKernels& kernels_;
};

} // namespace mffv1::codec
