#include "simd/cpu_features.hpp"

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
#include <cpuid.h>
#endif

namespace mffv1::simd {

namespace {

constexpr std::uint64_t feature_bit(CpuFeature feature) noexcept
{
    return static_cast<std::uint64_t>(feature);
}

} // namespace

std::uint64_t compiled_cpu_features() noexcept
{
    std::uint64_t features = 0;
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    features |= feature_bit(CpuFeature::Sse2);
    features |= feature_bit(CpuFeature::Ssse3);
    features |= feature_bit(CpuFeature::Avx2);
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
    features |= feature_bit(CpuFeature::Neon);
#endif
    return features;
}

std::uint64_t detected_cpu_features() noexcept
{
    std::uint64_t features = 0;
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    int registers[4]{};
    __cpuid(registers, 0);
    const int maximum_leaf = registers[0];
    if (maximum_leaf >= 1) {
        __cpuidex(registers, 1, 0);
        if ((registers[3] & (1 << 26)) != 0) {
            features |= feature_bit(CpuFeature::Sse2);
        }
        if ((registers[2] & (1 << 9)) != 0) {
            features |= feature_bit(CpuFeature::Ssse3);
        }
        const bool has_osxsave = (registers[2] & (1 << 27)) != 0;
        const bool has_avx = (registers[2] & (1 << 28)) != 0;
        const bool os_has_avx_state =
            has_osxsave && has_avx && (_xgetbv(0) & 0x6) == 0x6;
        if (maximum_leaf >= 7 && os_has_avx_state) {
            __cpuidex(registers, 7, 0);
            if ((registers[1] & (1 << 5)) != 0) {
                features |= feature_bit(CpuFeature::Avx2);
            }
        }
    }
#elif defined(__GNUC__) && (defined(__i386__) || defined(__x86_64__))
    unsigned int eax = 0;
    unsigned int ebx = 0;
    unsigned int ecx = 0;
    unsigned int edx = 0;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) != 0) {
        if ((edx & bit_SSE2) != 0) {
            features |= feature_bit(CpuFeature::Sse2);
        }
        if ((ecx & bit_SSSE3) != 0) {
            features |= feature_bit(CpuFeature::Ssse3);
        }
    }
    if (__builtin_cpu_supports("avx2")) {
        features |= feature_bit(CpuFeature::Avx2);
    }
#elif defined(_M_ARM64) || defined(__aarch64__)
    features |= feature_bit(CpuFeature::Neon);
#endif
    return features & compiled_cpu_features();
}

std::uint64_t resolve_cpu_features(const CpuFeatures& requested) noexcept
{
    const auto compiled = compiled_cpu_features();
    if (!requested.auto_detect) {
        return requested.allowed & compiled;
    }

    const auto detected = detected_cpu_features();
    if (requested.allowed == 0) {
        return detected;
    }
    return requested.allowed & detected;
}

} // namespace mffv1::simd
