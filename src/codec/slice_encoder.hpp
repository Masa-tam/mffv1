#pragma once

#include <vector>

#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

class SliceEncoder {
public:
    explicit SliceEncoder(const syntax::StreamParameters& stream) noexcept;

    Status encode_content(FrameView input,
                          std::vector<std::byte>& out_payload) const;

private:
    Status validate_stream() const;

    const syntax::StreamParameters& stream_;
};

} // namespace mffv1::codec
