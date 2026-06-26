#include "simd/color_transform_sse2.hpp"

#include "simd/codec_kernels.hpp"

#if defined(_M_IX86) || defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace mffv1::simd {

void forward_color_transform_row_sse2(
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
    if (count == 0) {
        return;
    }
#if defined(_M_IX86) || defined(_M_X64) || defined(__SSE2__)
    if (bits_per_raw_sample == 0 || bits_per_raw_sample > 16) {
        forward_color_transform_row_scalar(
            r, g, b, y, cb, cr, count, bits_per_raw_sample, has_extra_plane);
        return;
    }

    const auto offset_value = std::int32_t{1} << bits_per_raw_sample;
    const auto coded_mask_value = (offset_value << 1) - 1;
    const auto offset = _mm_set1_epi32(offset_value);
    const auto coded_mask = _mm_set1_epi32(coded_mask_value);
    const auto zero = _mm_setzero_si128();
    const bool compatibility_layout =
        bits_per_raw_sample >= 9
        && bits_per_raw_sample <= 15
        && !has_extra_plane;

    std::size_t index = 0;
    for (; index + 4 <= count; index += 4) {
        const auto rv = _mm_unpacklo_epi16(
            _mm_loadl_epi64(
                reinterpret_cast<const __m128i*>(r + index)),
            zero);
        const auto gv = _mm_unpacklo_epi16(
            _mm_loadl_epi64(
                reinterpret_cast<const __m128i*>(g + index)),
            zero);
        const auto bv = _mm_unpacklo_epi16(
            _mm_loadl_epi64(
                reinterpret_cast<const __m128i*>(b + index)),
            zero);

        const auto base = compatibility_layout ? bv : gv;
        const auto signed_cb = compatibility_layout
            ? _mm_sub_epi32(gv, bv)
            : _mm_sub_epi32(bv, gv);
        const auto signed_cr = compatibility_layout
            ? _mm_sub_epi32(rv, bv)
            : _mm_sub_epi32(rv, gv);
        const auto correction = _mm_srai_epi32(
            _mm_add_epi32(signed_cb, signed_cr), 2);
        const auto yv =
            _mm_and_si128(_mm_add_epi32(base, correction), coded_mask);
        const auto cbv = _mm_add_epi32(signed_cb, offset);
        const auto crv = _mm_add_epi32(signed_cr, offset);

        _mm_storeu_si128(reinterpret_cast<__m128i*>(y + index), yv);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(cb + index), cbv);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(cr + index), crv);
    }
    forward_color_transform_row_scalar(
        r + index,
        g + index,
        b + index,
        y + index,
        cb + index,
        cr + index,
        count - index,
        bits_per_raw_sample,
        has_extra_plane);
#else
    forward_color_transform_row_scalar(
        r, g, b, y, cb, cr, count, bits_per_raw_sample, has_extra_plane);
#endif
}

void inverse_color_transform_row_sse2(
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
    if (count == 0) {
        return;
    }
#if defined(_M_IX86) || defined(_M_X64) || defined(__SSE2__)
    if (bits_per_raw_sample == 0 || bits_per_raw_sample > 16) {
        inverse_color_transform_row_scalar(
            y, cb, cr, r, g, b, count, bits_per_raw_sample, has_extra_plane);
        return;
    }

    const auto offset_value = std::int32_t{1} << bits_per_raw_sample;
    const auto offset = _mm_set1_epi32(offset_value);
    const auto mask = _mm_set1_epi32(offset_value - 1);
    const auto packing_bias = _mm_set1_epi32(0x8000);
    const auto output_bias = _mm_set1_epi16(
        static_cast<short>(0x8000));
    const auto zero = _mm_setzero_si128();
    const bool compatibility_layout =
        bits_per_raw_sample >= 9
        && bits_per_raw_sample <= 15
        && !has_extra_plane;

    const auto store_samples = [&](std::uint16_t* destination,
                                   __m128i values) noexcept {
        const auto adjusted = _mm_sub_epi32(
            _mm_and_si128(values, mask), packing_bias);
        const auto packed = _mm_xor_si128(
            _mm_packs_epi32(adjusted, zero), output_bias);
        _mm_storel_epi64(
            reinterpret_cast<__m128i*>(destination), packed);
    };

    std::size_t index = 0;
    for (; index + 4 <= count; index += 4) {
        const auto yv = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(y + index));
        const auto signed_cb = _mm_sub_epi32(
            _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(cb + index)),
            offset);
        const auto signed_cr = _mm_sub_epi32(
            _mm_loadu_si128(
                reinterpret_cast<const __m128i*>(cr + index)),
            offset);
        const auto correction = _mm_srai_epi32(
            _mm_add_epi32(signed_cb, signed_cr), 2);
        const auto base = _mm_sub_epi32(yv, correction);
        const auto rv = _mm_add_epi32(signed_cr, base);
        const auto gv = compatibility_layout
            ? _mm_add_epi32(signed_cb, base)
            : base;
        const auto bv = compatibility_layout
            ? base
            : _mm_add_epi32(signed_cb, base);

        store_samples(r + index, rv);
        store_samples(g + index, gv);
        store_samples(b + index, bv);
    }
    inverse_color_transform_row_scalar(
        y + index,
        cb + index,
        cr + index,
        r + index,
        g + index,
        b + index,
        count - index,
        bits_per_raw_sample,
        has_extra_plane);
#else
    inverse_color_transform_row_scalar(
        y, cb, cr, r, g, b, count, bits_per_raw_sample, has_extra_plane);
#endif
}

} // namespace mffv1::simd
