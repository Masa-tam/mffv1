#pragma once

#include "mffv1/frame.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

[[nodiscard]] FrameInfo make_frame_info(
    const syntax::StreamParameters& stream) noexcept;

} // namespace mffv1::codec
