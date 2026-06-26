#pragma once

#include <cstdint>

namespace mffv1::codec {

inline constexpr std::uint64_t kVersion3CifPixelCount = 352u * 288u;

[[nodiscard]] inline bool requires_version3_parallel_slice_limit(
    int version,
    std::uint32_t width,
    std::uint32_t height) noexcept
{
    const auto frame_pixel_count =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    return version >= 3 && frame_pixel_count > kVersion3CifPixelCount;
}

} // namespace mffv1::codec
