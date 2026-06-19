#pragma once

#include <cstdint>
#include <array>
#include <cstddef>
#include <vector>

#include "mffv1/frame.hpp"
#include "mffv1/options.hpp"
#include "mffv1/state_transition.hpp"

namespace mffv1::syntax {

struct QuantTableSet {
    static constexpr std::size_t kContextInputs = 5;
    static constexpr std::size_t kTableEntries = 256;

    std::array<std::array<std::int32_t, kTableEntries>, kContextInputs> tables{};
    std::uint32_t context_count = 0;
};

QuantTableSet make_zero_quant_table_set();

using InitialState = std::array<std::uint8_t, 32>;

struct InitialStateSet {
    std::vector<InitialState> contexts;
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
    std::vector<InitialStateSet> initial_states;
    StateTransitionTable state_transition = kDefaultStateTransition;
    bool error_status_enabled = false;
    bool intra_only = false;
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

[[nodiscard]] inline bool uses_signed_16bit_predictor(const StreamParameters& stream) noexcept
{
    return stream.colorspace_type == 0
        && stream.bits_per_raw_sample == 16
        && stream.entropy_mode == EntropyMode::Range;
}

[[nodiscard]] inline std::uint32_t subsampled_extent(std::uint32_t value,
                                                     std::uint8_t log2_subsample) noexcept
{
    if (log2_subsample == 0) {
        return value;
    }
    if (log2_subsample >= 32) {
        return value == 0 ? 0 : 1;
    }
    const std::uint64_t divisor = std::uint64_t{1} << log2_subsample;
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(value) + divisor - 1) / divisor);
}

[[nodiscard]] inline bool is_chroma_plane(const StreamParameters& stream, std::size_t plane_index) noexcept
{
    return stream.chroma_planes && (plane_index == 1 || plane_index == 2);
}

[[nodiscard]] inline std::size_t quant_table_set_index_count(const StreamParameters& stream) noexcept
{
    return 1 + ((stream.chroma_planes || stream.version <= 3) ? 1 : 0)
        + (stream.extra_plane ? 1 : 0);
}

[[nodiscard]] inline std::size_t plane_quant_table_set_index_slot(const StreamParameters& stream,
                                                                  std::size_t plane_index) noexcept
{
    if (plane_index == 0) {
        return 0;
    }
    if (is_chroma_plane(stream, plane_index)) {
        return 1;
    }
    return (stream.version <= 3 || stream.chroma_planes) ? 2 : 1;
}

[[nodiscard]] inline std::uint32_t plane_width(const StreamParameters& stream,
                                               std::size_t plane_index) noexcept
{
    if (stream.colorspace_type == 0 && is_chroma_plane(stream, plane_index)) {
        return subsampled_extent(stream.width, stream.log2_h_chroma_subsample);
    }
    return stream.width;
}

[[nodiscard]] inline std::uint32_t plane_height(const StreamParameters& stream,
                                                std::size_t plane_index) noexcept
{
    if (stream.colorspace_type == 0 && is_chroma_plane(stream, plane_index)) {
        return subsampled_extent(stream.height, stream.log2_v_chroma_subsample);
    }
    return stream.height;
}

[[nodiscard]] inline std::uint32_t scaled_grid_position(std::uint32_t extent,
                                                        std::uint32_t grid_count,
                                                        std::uint64_t grid_position) noexcept
{
    if (grid_count == 0) {
        return 0;
    }
    const std::uint64_t scaled = grid_position * static_cast<std::uint64_t>(extent);
    return static_cast<std::uint32_t>(scaled / grid_count);
}

[[nodiscard]] inline std::uint32_t slice_pixel_x(const StreamParameters& stream,
                                                 std::uint32_t slice_x) noexcept
{
    return scaled_grid_position(stream.width, stream.num_h_slices, slice_x);
}

[[nodiscard]] inline std::uint32_t slice_pixel_y(const StreamParameters& stream,
                                                 std::uint32_t slice_y) noexcept
{
    return scaled_grid_position(stream.height, stream.num_v_slices, slice_y);
}

[[nodiscard]] inline std::uint32_t slice_pixel_width(const StreamParameters& stream,
                                                     std::uint32_t slice_x,
                                                     std::uint32_t slice_width) noexcept
{
    const std::uint64_t slice_end =
        static_cast<std::uint64_t>(slice_x) + static_cast<std::uint64_t>(slice_width);
    const auto pixel_x = scaled_grid_position(stream.width, stream.num_h_slices, slice_x);
    const auto pixel_end = scaled_grid_position(stream.width, stream.num_h_slices, slice_end);
    return pixel_end - pixel_x;
}

[[nodiscard]] inline std::uint32_t slice_pixel_height(const StreamParameters& stream,
                                                      std::uint32_t slice_y,
                                                      std::uint32_t slice_height) noexcept
{
    const std::uint64_t slice_end =
        static_cast<std::uint64_t>(slice_y) + static_cast<std::uint64_t>(slice_height);
    const auto pixel_y = scaled_grid_position(stream.height, stream.num_v_slices, slice_y);
    const auto pixel_end = scaled_grid_position(stream.height, stream.num_v_slices, slice_end);
    return pixel_end - pixel_y;
}

[[nodiscard]] inline PlaneRole expected_plane_role(const StreamParameters& stream,
                                                   std::size_t plane_index) noexcept
{
    if (stream.colorspace_type == 1) {
        if (plane_index == 0) {
            return PlaneRole::R;
        }
        if (plane_index == 1) {
            return PlaneRole::G;
        }
        if (plane_index == 2) {
            return PlaneRole::B;
        }
        return PlaneRole::Alpha;
    }
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

} // namespace mffv1::syntax
