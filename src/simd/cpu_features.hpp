#pragma once

#include <cstdint>

#include "mffv1/options.hpp"

namespace mffv1::simd {

[[nodiscard]] std::uint64_t compiled_cpu_features() noexcept;
[[nodiscard]] std::uint64_t detected_cpu_features() noexcept;
[[nodiscard]] std::uint64_t resolve_cpu_features(
    const CpuFeatures& requested) noexcept;

} // namespace mffv1::simd
