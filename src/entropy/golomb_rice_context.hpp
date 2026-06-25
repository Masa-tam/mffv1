#pragma once

#include "entropy/golomb_rice_reader.hpp"
#include "entropy/golomb_rice_writer.hpp"

#include <cstdint>

namespace mffv1::entropy {

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
Status read_golomb_rice_run_interruption(GolombRiceReader& reader,
                                         GolombRiceContextState& state,
                                         std::uint8_t bits_per_raw_sample,
                                         std::int32_t& out_value) noexcept;
Status write_golomb_rice_symbol(GolombRiceWriter& writer,
                                GolombRiceContextState& state,
                                std::uint8_t bits_per_raw_sample,
                                std::int32_t value);
Status write_golomb_rice_run_interruption(
    GolombRiceWriter& writer,
    GolombRiceContextState& state,
    std::uint8_t bits_per_raw_sample,
    std::int32_t value);

} // namespace mffv1::entropy
