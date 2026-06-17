#pragma once

#include <cstdint>
#include <array>
#include <cstddef>
#include <vector>

#include "ffv1/frame.hpp"
#include "ffv1/options.hpp"

namespace ffv1::syntax {

struct QuantTableSet {
    static constexpr std::size_t kContextInputs = 5;
    static constexpr std::size_t kTableEntries = 256;

    std::array<std::array<std::int32_t, kTableEntries>, kContextInputs> tables{};
    std::uint32_t context_count = 0;
};

QuantTableSet make_zero_quant_table_set();

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

[[nodiscard]] inline std::uint32_t subsampled_extent(std::uint32_t value,
                                                     std::uint8_t log2_subsample) noexcept
{
    if (log2_subsample == 0) {
        return value;
    }
    const std::uint32_t add = (std::uint32_t{1} << log2_subsample) - 1;
    return (value + add) >> log2_subsample;
}

[[nodiscard]] inline bool is_chroma_plane(const StreamParameters& stream, std::size_t plane_index) noexcept
{
    return stream.chroma_planes && (plane_index == 1 || plane_index == 2);
}

[[nodiscard]] inline std::uint32_t plane_width(const StreamParameters& stream,
                                               std::size_t plane_index) noexcept
{
    if (is_chroma_plane(stream, plane_index)) {
        return subsampled_extent(stream.width, stream.log2_h_chroma_subsample);
    }
    return stream.width;
}

[[nodiscard]] inline std::uint32_t plane_height(const StreamParameters& stream,
                                                std::size_t plane_index) noexcept
{
    if (is_chroma_plane(stream, plane_index)) {
        return subsampled_extent(stream.height, stream.log2_v_chroma_subsample);
    }
    return stream.height;
}

[[nodiscard]] inline PlaneRole expected_plane_role(const StreamParameters& stream,
                                                   std::size_t plane_index) noexcept
{
    if (plane_index == 0) {
        return PlaneRole::Y;
    }
    if (stream.chroma_planes) {
        if (plane_index == 1) {
            return PlaneRole::Cb;
        }
        if (plane_index == 2) {
            return PlaneRole::Cr;
        }
    }
    return PlaneRole::Alpha;
}

} // namespace ffv1::syntax
