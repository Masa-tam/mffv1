#include "codec/slice_encoder.hpp"
#include "simd/codec_kernels.hpp"
#include "simd/color_transform_avx2.hpp"
#include "simd/color_transform_sse2.hpp"
#include "simd/cpu_features.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

constexpr std::uint64_t feature_bit(mffv1::CpuFeature feature) noexcept
{
    return static_cast<std::uint64_t>(feature);
}

TEST(SimdColorTransformTest, Sse2MatchesScalarAcrossProfilesAndTails)
{
    constexpr std::size_t kMaximumCount = 17;
    for (const std::uint8_t bits : {
             std::uint8_t{8},
             std::uint8_t{9},
             std::uint8_t{10},
             std::uint8_t{15},
             std::uint8_t{16},
         }) {
        const auto maximum = static_cast<std::uint32_t>(
            bits == 16
                ? 0xffffu
                : (std::uint32_t{1} << bits) - 1u);
        std::array<std::uint16_t, kMaximumCount> r{};
        std::array<std::uint16_t, kMaximumCount> g{};
        std::array<std::uint16_t, kMaximumCount> b{};
        for (std::size_t index = 0; index < kMaximumCount; ++index) {
            r[index] = static_cast<std::uint16_t>(
                (index * 7919u + maximum) % (maximum + 1u));
            g[index] = static_cast<std::uint16_t>(
                (index * 3571u + maximum / 3u) % (maximum + 1u));
            b[index] = static_cast<std::uint16_t>(
                (index * 1237u + maximum / 2u) % (maximum + 1u));
        }
        r[0] = 0;
        g[0] = static_cast<std::uint16_t>(maximum);
        b[0] = 0;
        r[1] = static_cast<std::uint16_t>(maximum);
        g[1] = 0;
        b[1] = static_cast<std::uint16_t>(maximum);

        for (const bool has_extra_plane : {false, true}) {
            for (std::size_t count = 1; count <= kMaximumCount; ++count) {
                std::vector<std::int32_t> scalar_y(count);
                std::vector<std::int32_t> scalar_cb(count);
                std::vector<std::int32_t> scalar_cr(count);
                std::vector<std::int32_t> sse2_y(count);
                std::vector<std::int32_t> sse2_cb(count);
                std::vector<std::int32_t> sse2_cr(count);

                mffv1::simd::forward_color_transform_row_scalar(
                    r.data(),
                    g.data(),
                    b.data(),
                    scalar_y.data(),
                    scalar_cb.data(),
                    scalar_cr.data(),
                    count,
                    bits,
                    has_extra_plane);
                mffv1::simd::forward_color_transform_row_sse2(
                    r.data(),
                    g.data(),
                    b.data(),
                    sse2_y.data(),
                    sse2_cb.data(),
                    sse2_cr.data(),
                    count,
                    bits,
                    has_extra_plane);

                EXPECT_EQ(sse2_y, scalar_y)
                    << "bits=" << static_cast<int>(bits)
                    << " extra=" << has_extra_plane
                    << " count=" << count;
                EXPECT_EQ(sse2_cb, scalar_cb)
                    << "bits=" << static_cast<int>(bits)
                    << " extra=" << has_extra_plane
                    << " count=" << count;
                EXPECT_EQ(sse2_cr, scalar_cr)
                    << "bits=" << static_cast<int>(bits)
                    << " extra=" << has_extra_plane
                    << " count=" << count;
            }
        }
    }
}

TEST(SimdColorTransformTest, InvalidBitDepthMatchesScalar)
{
    const std::array<std::uint16_t, 4> source{0, 1, 2, 3};
    std::array<std::int32_t, 4> scalar_y{};
    std::array<std::int32_t, 4> scalar_cb{};
    std::array<std::int32_t, 4> scalar_cr{};
    std::array<std::int32_t, 4> sse2_y{};
    std::array<std::int32_t, 4> sse2_cb{};
    std::array<std::int32_t, 4> sse2_cr{};

    mffv1::simd::forward_color_transform_row_scalar(
        source.data(),
        source.data(),
        source.data(),
        scalar_y.data(),
        scalar_cb.data(),
        scalar_cr.data(),
        source.size(),
        0,
        false);
    mffv1::simd::forward_color_transform_row_sse2(
        source.data(),
        source.data(),
        source.data(),
        sse2_y.data(),
        sse2_cb.data(),
        sse2_cr.data(),
        source.size(),
        0,
        false);

    EXPECT_EQ(sse2_y, scalar_y);
    EXPECT_EQ(sse2_cb, scalar_cb);
    EXPECT_EQ(sse2_cr, scalar_cr);
}

TEST(SimdColorTransformTest, EmptyRowIsANoOp)
{
    mffv1::simd::forward_color_transform_row_sse2(
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        0,
        8,
        false);
}

TEST(SimdColorTransformTest, Avx2MatchesScalarAcrossProfilesAndTails)
{
    if ((mffv1::simd::detected_cpu_features()
         & feature_bit(mffv1::CpuFeature::Avx2))
        == 0) {
        GTEST_SKIP() << "AVX2 is unavailable on this CPU or build";
    }

    constexpr std::size_t kMaximumCount = 25;
    for (const std::uint8_t bits : {
             std::uint8_t{8},
             std::uint8_t{9},
             std::uint8_t{10},
             std::uint8_t{15},
             std::uint8_t{16},
         }) {
        const auto maximum = static_cast<std::uint32_t>(
            bits == 16
                ? 0xffffu
                : (std::uint32_t{1} << bits) - 1u);
        std::array<std::uint16_t, kMaximumCount> r{};
        std::array<std::uint16_t, kMaximumCount> g{};
        std::array<std::uint16_t, kMaximumCount> b{};
        for (std::size_t index = 0; index < kMaximumCount; ++index) {
            r[index] = static_cast<std::uint16_t>(
                (index * 7919u + maximum) % (maximum + 1u));
            g[index] = static_cast<std::uint16_t>(
                (index * 3571u + maximum / 3u) % (maximum + 1u));
            b[index] = static_cast<std::uint16_t>(
                (index * 1237u + maximum / 2u) % (maximum + 1u));
        }
        r[0] = 0;
        g[0] = static_cast<std::uint16_t>(maximum);
        b[0] = 0;
        r[1] = static_cast<std::uint16_t>(maximum);
        g[1] = 0;
        b[1] = static_cast<std::uint16_t>(maximum);

        for (const bool has_extra_plane : {false, true}) {
            for (std::size_t count = 1; count <= kMaximumCount; ++count) {
                std::vector<std::int32_t> scalar_y(count);
                std::vector<std::int32_t> scalar_cb(count);
                std::vector<std::int32_t> scalar_cr(count);
                std::vector<std::int32_t> avx2_y(count);
                std::vector<std::int32_t> avx2_cb(count);
                std::vector<std::int32_t> avx2_cr(count);

                mffv1::simd::forward_color_transform_row_scalar(
                    r.data(),
                    g.data(),
                    b.data(),
                    scalar_y.data(),
                    scalar_cb.data(),
                    scalar_cr.data(),
                    count,
                    bits,
                    has_extra_plane);
                mffv1::simd::forward_color_transform_row_avx2(
                    r.data(),
                    g.data(),
                    b.data(),
                    avx2_y.data(),
                    avx2_cb.data(),
                    avx2_cr.data(),
                    count,
                    bits,
                    has_extra_plane);

                EXPECT_EQ(avx2_y, scalar_y)
                    << "bits=" << static_cast<int>(bits)
                    << " extra=" << has_extra_plane
                    << " count=" << count;
                EXPECT_EQ(avx2_cb, scalar_cb)
                    << "bits=" << static_cast<int>(bits)
                    << " extra=" << has_extra_plane
                    << " count=" << count;
                EXPECT_EQ(avx2_cr, scalar_cr)
                    << "bits=" << static_cast<int>(bits)
                    << " extra=" << has_extra_plane
                    << " count=" << count;
            }
        }
    }
}

TEST(SimdColorTransformTest, SimdProducesScalarIdenticalRgbSlices)
{
    constexpr std::uint32_t kWidth = 9;
    constexpr std::uint32_t kHeight = 2;
    std::array<std::uint16_t, kWidth * kHeight> r{};
    std::array<std::uint16_t, kWidth * kHeight> g{};
    std::array<std::uint16_t, kWidth * kHeight> b{};
    for (std::size_t index = 0; index < r.size(); ++index) {
        r[index] = static_cast<std::uint16_t>((index * 101u) & 0x3ffu);
        g[index] = static_cast<std::uint16_t>((900u - index * 37u) & 0x3ffu);
        b[index] = static_cast<std::uint16_t>((index * 211u) & 0x3ffu);
    }
    const std::array<mffv1::PlaneView, 3> planes{{
        {
            r.data(),
            {mffv1::PlaneRole::R,
             mffv1::SampleFormat::UInt16,
             kWidth,
             kHeight,
             static_cast<std::ptrdiff_t>(kWidth * sizeof(std::uint16_t))},
        },
        {
            g.data(),
            {mffv1::PlaneRole::G,
             mffv1::SampleFormat::UInt16,
             kWidth,
             kHeight,
             static_cast<std::ptrdiff_t>(kWidth * sizeof(std::uint16_t))},
        },
        {
            b.data(),
            {mffv1::PlaneRole::B,
             mffv1::SampleFormat::UInt16,
             kWidth,
             kHeight,
             static_cast<std::ptrdiff_t>(kWidth * sizeof(std::uint16_t))},
        },
    }};
    const mffv1::FrameView input{planes.data(), planes.size()};

    for (const auto entropy_mode : {
             mffv1::EntropyMode::Range,
             mffv1::EntropyMode::GolombRice,
         }) {
        mffv1::syntax::StreamParameters stream;
        stream.version = 3;
        stream.micro_version = 4;
        stream.entropy_mode = entropy_mode;
        stream.width = kWidth;
        stream.height = kHeight;
        stream.bits_per_raw_sample = 10;
        stream.colorspace_type = 1;
        stream.chroma_planes = true;
        stream.quant_table_sets.push_back(
            mffv1::syntax::make_zero_quant_table_set());
        stream.intra_only = true;

        mffv1::simd::CodecKernels scalar_kernels;
        mffv1::simd::CodecKernels sse2_kernels;
        sse2_kernels.forward_color_transform_row =
            mffv1::simd::forward_color_transform_row_sse2;
        mffv1::simd::CodecKernels avx2_kernels;
        const bool has_avx2 =
            (mffv1::simd::detected_cpu_features()
             & feature_bit(mffv1::CpuFeature::Avx2))
            != 0;
        if (has_avx2) {
            avx2_kernels.forward_color_transform_row =
                mffv1::simd::forward_color_transform_row_avx2;
        }
        std::vector<std::byte> scalar_payload;
        std::vector<std::byte> sse2_payload;
        std::vector<std::byte> avx2_payload;

        const mffv1::codec::SliceEncoder scalar_encoder(
            stream, scalar_kernels);
        const mffv1::codec::SliceEncoder sse2_encoder(
            stream, sse2_kernels);
        ASSERT_TRUE(
            scalar_encoder.encode_slice(input, true, scalar_payload).ok());
        ASSERT_TRUE(
            sse2_encoder.encode_slice(input, true, sse2_payload).ok());

        EXPECT_EQ(sse2_payload, scalar_payload);
        if (has_avx2) {
            const mffv1::codec::SliceEncoder avx2_encoder(
                stream, avx2_kernels);
            ASSERT_TRUE(
                avx2_encoder.encode_slice(input, true, avx2_payload).ok());
            EXPECT_EQ(avx2_payload, scalar_payload);
        }
    }
}

} // namespace
