#pragma once

#include <cstdint>
#include <vector>

#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

class SliceFooterWriter {
public:
    Status append(const syntax::StreamParameters& stream,
                  std::uint8_t error_status,
                  std::vector<std::byte>& slice_payload) const;
};

} // namespace mffv1::codec
