#pragma once

#include "codec/frame_decode_context.hpp"
#include "entropy/symbol_reader.hpp"
#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

class FrameParser {
public:
    explicit FrameParser(const syntax::StreamParameters& stream) noexcept;
    FrameParser(const syntax::StreamParameters& stream, bool verify_crc) noexcept;

    Status parse(ByteSpan payload, FrameDecodeContext& out_frame) const;
    Status parse_with_range_header(ByteSpan payload, FrameDecodeContext& out_frame) const;
    Status parse_with_header_reader(ByteSpan payload,
                                    entropy::SymbolReader& header_reader,
                                    FrameDecodeContext& out_frame) const;

private:
    Status initialize_frame(ByteSpan payload, FrameDecodeContext& out_frame) const;
    Status parse_legacy_range_slices_after_keyframe(ByteSpan payload,
                                                    entropy::SymbolReader& header_reader,
                                                    bool keyframe,
                                                    bool uses_legacy_v0_arithmetic,
                                                    FrameDecodeContext& out_frame) const;
    Status parse_located_range_slices(ByteSpan payload, FrameDecodeContext& out_frame) const;

    const syntax::StreamParameters& stream_;
    bool verify_crc_ = false;
};

} // namespace mffv1::codec
