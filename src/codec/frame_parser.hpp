#pragma once

#include "codec/frame_decode_context.hpp"
#include "ffv1/frame.hpp"
#include "ffv1/result.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

class FrameParser {
public:
    explicit FrameParser(const syntax::StreamParameters& stream) noexcept;

    Status parse(ByteSpan payload, FrameDecodeContext& out_frame) const;

private:
    const syntax::StreamParameters& stream_;
};

} // namespace ffv1::codec

