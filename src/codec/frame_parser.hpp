#pragma once

#include "codec/frame_decode_context.hpp"
#include "entropy/symbol_reader.hpp"
#include "ffv1/frame.hpp"
#include "ffv1/result.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

class FrameParser {
public:
    explicit FrameParser(const syntax::StreamParameters& stream) noexcept;

    Status parse(ByteSpan payload, FrameDecodeContext& out_frame) const;
    Status parse_with_header_reader(ByteSpan payload,
                                    entropy::SymbolReader& header_reader,
                                    FrameDecodeContext& out_frame) const;

private:
    Status initialize_frame(ByteSpan payload, FrameDecodeContext& out_frame) const;

    const syntax::StreamParameters& stream_;
};

} // namespace ffv1::codec
