#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "mffv1/options.hpp"

namespace mffv1 {

using ByteSpan = std::span<const std::byte>;
using MutableByteSpan = std::span<std::byte>;

inline constexpr std::size_t kMaxFramePlanes = 4;

enum class PlaneRole : std::uint8_t {
    Y = 0,
    Cb = 1,
    Cr = 2,
    Alpha = 3,
    R = 4,
    G = 5,
    B = 6,
};

enum class SampleFormat : std::uint8_t {
    UInt8 = 0,
    UInt16 = 1,
};

enum class ColorSpace : std::uint8_t {
    YCbCr = 0,
    Rgb = 1,
};

struct PlaneInfo {
    PlaneRole role = PlaneRole::Y;
    SampleFormat sample_format = SampleFormat::UInt8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::ptrdiff_t stride_bytes = 0;
};

struct PlaneView {
    const void* data = nullptr;
    PlaneInfo info;
};

struct MutablePlaneView {
    void* data = nullptr;
    PlaneInfo info;
};

struct FrameView {
    const PlaneView* planes = nullptr;
    std::size_t plane_count = 0;
};

struct MutableFrameView {
    MutablePlaneView* planes = nullptr;
    std::size_t plane_count = 0;
};

struct FrameInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t version = 0;
    std::uint16_t micro_version = 0;
    EntropyMode entropy_mode = EntropyMode::Range;
    std::uint8_t bits_per_raw_sample = 0;
    std::uint8_t plane_count = 0;
    std::array<PlaneInfo, kMaxFramePlanes> planes{};
    ColorSpace color_space = ColorSpace::YCbCr;
    bool has_chroma_planes = false;
    bool has_extra_plane = false;
    std::uint8_t log2_h_chroma_subsample = 0;
    std::uint8_t log2_v_chroma_subsample = 0;
    bool error_status_enabled = false;
    bool intra_only = false;
    bool keyframe = false;
    std::uint32_t slice_count = 0;
};

} // namespace mffv1
