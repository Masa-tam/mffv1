#pragma once

#include "codec/slice_output_window.hpp"
#include "codec/slice_state.hpp"
#include "ffv1/result.hpp"
#include "ffv1/slice_descriptor.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

class SliceDecoder {
public:
    explicit SliceDecoder(const syntax::StreamParameters& stream) noexcept;

    Status decode(const syntax::SliceDescriptor& slice,
                  SliceOutputWindow& output,
                  SliceState& state) const;

private:
    const syntax::StreamParameters& stream_;
};

} // namespace ffv1::codec

