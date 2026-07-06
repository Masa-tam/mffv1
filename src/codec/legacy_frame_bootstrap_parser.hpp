#pragma once

#include <cstdint>

#include "entropy/range_coder.hpp"
#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

struct LegacyFrameBootstrap {
    bool keyframe = false;
    bool has_embedded_parameters = false;
    std::uint64_t content_byte_offset = 0;
    entropy::RangeCoder::ArithmeticState range_state_after_keyframe;
    entropy::RangeCoder::ArithmeticState range_state_after_parameters;
    syntax::StreamParameters stream;
};

class LegacyFrameBootstrapParser {
public:
    Status parse(ByteSpan frame_payload,
                 std::uint32_t frame_width,
                 std::uint32_t frame_height,
                 LegacyFrameBootstrap& out_bootstrap) const;
};

} // namespace mffv1::codec
