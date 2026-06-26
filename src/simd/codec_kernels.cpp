#include "simd/codec_kernels.hpp"

#include "mffv1/color_transform.hpp"
#include "simd/color_transform_avx2.hpp"
#include "simd/color_transform_sse2.hpp"
#include "simd/cpu_features.hpp"

namespace mffv1::simd {

namespace {

constexpr std::uint64_t feature_bit(CpuFeature feature) noexcept
{
    return static_cast<std::uint64_t>(feature);
}

} // namespace

void forward_color_transform_row_scalar(
    const std::uint16_t* r,
    const std::uint16_t* g,
    const std::uint16_t* b,
    std::int32_t* y,
    std::int32_t* cb,
    std::int32_t* cr,
    std::size_t count,
    std::uint8_t bits_per_raw_sample,
    bool has_extra_plane) noexcept
{
    for (std::size_t index = 0; index < count; ++index) {
        const auto code = syntax::forward_jpeg2000_rct(
            r[index],
            g[index],
            b[index],
            bits_per_raw_sample,
            has_extra_plane);
        y[index] = code.y;
        cb[index] = code.cb;
        cr[index] = code.cr;
    }
}

void inverse_color_transform_row_scalar(
    const std::int32_t* y,
    const std::int32_t* cb,
    const std::int32_t* cr,
    std::uint16_t* r,
    std::uint16_t* g,
    std::uint16_t* b,
    std::size_t count,
    std::uint8_t bits_per_raw_sample,
    bool has_extra_plane) noexcept
{
    for (std::size_t index = 0; index < count; ++index) {
        const auto sample = syntax::inverse_jpeg2000_rct(
            y[index],
            cb[index],
            cr[index],
            bits_per_raw_sample,
            has_extra_plane);
        r[index] = sample.r;
        g[index] = sample.g;
        b[index] = sample.b;
    }
}

CodecKernels make_codec_kernels(const CpuFeatures& requested) noexcept
{
    CodecKernels kernels;
    kernels.available_features = resolve_cpu_features(requested);
    const auto sse2 = feature_bit(CpuFeature::Sse2);
    if ((kernels.available_features & sse2) != 0) {
        kernels.forward_color_transform_row =
            forward_color_transform_row_sse2;
        kernels.inverse_color_transform_row =
            inverse_color_transform_row_sse2;
        kernels.active_features |= sse2;
    }
#if defined(MFFV1_HAS_AVX2_KERNEL)
    const auto avx2 = feature_bit(CpuFeature::Avx2);
    if ((kernels.available_features & avx2) != 0) {
        kernels.forward_color_transform_row =
            forward_color_transform_row_avx2;
        kernels.inverse_color_transform_row =
            inverse_color_transform_row_avx2;
        kernels.active_features = avx2;
    }
#endif
    return kernels;
}

} // namespace mffv1::simd
