#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mffv1 {

using ByteSpan = std::span<const std::byte>;
using MutableByteSpan = std::span<std::byte>;

enum class PlaneRole : std::uint8_t {
    Y,
    Cb,
    Cr,
    Alpha,
    R,
    G,
    B,
};

enum class SampleFormat : std::uint8_t {
    UInt8,
    UInt16,
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
    std::uint8_t bits_per_raw_sample = 0;
    std::uint8_t plane_count = 0;
    bool keyframe = false;
    std::uint32_t slice_count = 0;
};

} // namespace mffv1
