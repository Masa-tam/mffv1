#pragma once

#include "mffv1/config.hpp"
#include "mffv1/options.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

[[nodiscard]] Status normalize_encoder_profile(
    const EncoderOptions& options,
    const StreamInfo& info,
    syntax::StreamParameters& out_stream);

} // namespace mffv1::codec
