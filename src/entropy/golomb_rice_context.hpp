#pragma once

#include "entropy/golomb_rice_reader.hpp"

#include <cstdint>

namespace ffv1::entropy {

struct GolombRiceContextState {
    std::int64_t drift = 0;
    std::int64_t error_sum = 4;
    std::int64_t bias = 0;
    std::int64_t count = 1;

    void reset() noexcept;
};

Status read_golomb_rice_symbol(GolombRiceReader& reader,
                               GolombRiceContextState& state,
                               std::uint8_t bits_per_raw_sample,
                               std::int32_t& out_value) noexcept;

} // namespace ffv1::entropy
