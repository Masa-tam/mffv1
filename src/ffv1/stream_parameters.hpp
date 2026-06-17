#pragma once

#include <cstdint>
#include <vector>

#include "ffv1/options.hpp"

namespace ffv1::syntax {

struct QuantTableSet {
    std::vector<std::int32_t> table;
};

struct InitialState {
    std::uint8_t state = 0;
};

struct StreamParameters {
    int version = 3;
    int micro_version = 0;
    EntropyMode entropy_mode = EntropyMode::Range;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t bits_per_raw_sample = 8;
    int colorspace_type = 0;
    bool chroma_planes = true;
    bool extra_plane = false;
    std::uint8_t log2_h_chroma_subsample = 0;
    std::uint8_t log2_v_chroma_subsample = 0;
    std::uint32_t num_h_slices = 1;
    std::uint32_t num_v_slices = 1;
    std::vector<QuantTableSet> quant_table_sets;
    std::vector<InitialState> initial_states;
    bool error_status_enabled = false;
};

[[nodiscard]] inline std::uint8_t coded_plane_count(const StreamParameters& stream) noexcept
{
    std::uint8_t count = 1;
    if (stream.chroma_planes) {
        count = static_cast<std::uint8_t>(count + 2);
    }
    if (stream.extra_plane) {
        count = static_cast<std::uint8_t>(count + 1);
    }
    return count;
}

} // namespace ffv1::syntax

