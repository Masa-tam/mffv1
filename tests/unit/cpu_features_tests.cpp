#include "simd/codec_kernels.hpp"
#include "simd/cpu_features.hpp"

#include <cstdint>

#include <gtest/gtest.h>

namespace {

constexpr std::uint64_t feature_bit(mffv1::CpuFeature feature) noexcept
{
    return static_cast<std::uint64_t>(feature);
}

TEST(CpuFeaturesTest, ForcedScalarDisablesAllFeatures)
{
    mffv1::CpuFeatures requested;
    requested.allowed = 0;
    requested.auto_detect = false;

    EXPECT_EQ(mffv1::simd::resolve_cpu_features(requested), 0u);
    const auto kernels = mffv1::simd::make_codec_kernels(requested);
    EXPECT_EQ(kernels.available_features, 0u);
    EXPECT_EQ(kernels.active_features, 0u);
}

TEST(CpuFeaturesTest, AutomaticFeaturesAreCompiledAndDetected)
{
    const auto compiled = mffv1::simd::compiled_cpu_features();
    const auto detected = mffv1::simd::detected_cpu_features();
    const mffv1::CpuFeatures requested;
    const auto resolved = mffv1::simd::resolve_cpu_features(requested);

    EXPECT_EQ(detected & ~compiled, 0u);
    EXPECT_EQ(resolved, detected);
}

TEST(CpuFeaturesTest, AutomaticMaskCannotEnableUndetectedFeatures)
{
    mffv1::CpuFeatures requested;
    requested.allowed =
        feature_bit(mffv1::CpuFeature::Sse2)
        | feature_bit(mffv1::CpuFeature::Ssse3)
        | feature_bit(mffv1::CpuFeature::Avx2)
        | feature_bit(mffv1::CpuFeature::Neon);

    EXPECT_EQ(
        mffv1::simd::resolve_cpu_features(requested),
        requested.allowed & mffv1::simd::detected_cpu_features());
}

TEST(CpuFeaturesTest, ExplicitMaskIsLimitedToCompiledBackends)
{
    mffv1::CpuFeatures requested;
    requested.auto_detect = false;
    requested.allowed = ~std::uint64_t{0};

    EXPECT_EQ(
        mffv1::simd::resolve_cpu_features(requested),
        mffv1::simd::compiled_cpu_features());
}

TEST(CpuFeaturesTest, ScalarKernelTableActivatesNoSimdFeatures)
{
    const mffv1::CpuFeatures requested;
    const auto kernels = mffv1::simd::make_codec_kernels(requested);

    EXPECT_EQ(
        kernels.available_features,
        mffv1::simd::detected_cpu_features());
    EXPECT_EQ(kernels.active_features, 0u);
    EXPECT_NE(kernels.forward_color_transform, nullptr);
}

} // namespace
