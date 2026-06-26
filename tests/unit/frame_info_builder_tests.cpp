#include "codec/frame_info_builder.hpp"

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

TEST(FrameInfoBuilderTest, BuildsSubsampledHighBitYcbcrPlaneTable)
{
    mffv1::syntax::StreamParameters stream;
    stream.version = 3;
    stream.micro_version = 4;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.width = 5;
    stream.height = 3;
    stream.bits_per_raw_sample = 10;
    stream.chroma_planes = true;
    stream.extra_plane = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;
    stream.error_status_enabled = true;
    stream.intra_only = false;

    const auto info = mffv1::codec::make_frame_info(stream);

    EXPECT_EQ(info.width, 5u);
    EXPECT_EQ(info.height, 3u);
    EXPECT_EQ(info.version, 3u);
    EXPECT_EQ(info.micro_version, 4u);
    EXPECT_EQ(info.entropy_mode, mffv1::EntropyMode::GolombRice);
    EXPECT_EQ(info.bits_per_raw_sample, 10u);
    EXPECT_EQ(info.plane_count, 4u);
    EXPECT_EQ(info.color_space, mffv1::ColorSpace::YCbCr);
    EXPECT_TRUE(info.has_chroma_planes);
    EXPECT_TRUE(info.has_extra_plane);
    EXPECT_EQ(info.log2_h_chroma_subsample, 1u);
    EXPECT_EQ(info.log2_v_chroma_subsample, 1u);
    EXPECT_TRUE(info.error_status_enabled);
    EXPECT_FALSE(info.intra_only);
    EXPECT_FALSE(info.keyframe);
    EXPECT_EQ(info.slice_count, 0u);

    EXPECT_EQ(info.planes[0].role, mffv1::PlaneRole::Y);
    EXPECT_EQ(info.planes[1].role, mffv1::PlaneRole::Cb);
    EXPECT_EQ(info.planes[2].role, mffv1::PlaneRole::Cr);
    EXPECT_EQ(info.planes[3].role, mffv1::PlaneRole::Alpha);
    for (std::size_t plane = 0; plane < info.plane_count; ++plane) {
        EXPECT_EQ(info.planes[plane].sample_format,
                  mffv1::SampleFormat::UInt16);
    }
    EXPECT_EQ(info.planes[0].width, 5u);
    EXPECT_EQ(info.planes[0].height, 3u);
    EXPECT_EQ(info.planes[0].stride_bytes, 10);
    EXPECT_EQ(info.planes[1].width, 3u);
    EXPECT_EQ(info.planes[1].height, 2u);
    EXPECT_EQ(info.planes[1].stride_bytes, 6);
    EXPECT_EQ(info.planes[2].width, 3u);
    EXPECT_EQ(info.planes[2].height, 2u);
    EXPECT_EQ(info.planes[2].stride_bytes, 6);
    EXPECT_EQ(info.planes[3].width, 5u);
    EXPECT_EQ(info.planes[3].height, 3u);
    EXPECT_EQ(info.planes[3].stride_bytes, 10);
}

TEST(FrameInfoBuilderTest, BuildsRgbPlanesAtFullResolution)
{
    mffv1::syntax::StreamParameters stream;
    stream.colorspace_type = 1;
    stream.width = 7;
    stream.height = 4;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = true;
    stream.extra_plane = false;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;
    stream.intra_only = true;

    const auto info = mffv1::codec::make_frame_info(stream);

    EXPECT_EQ(info.plane_count, 3u);
    EXPECT_EQ(info.color_space, mffv1::ColorSpace::Rgb);
    EXPECT_TRUE(info.intra_only);
    EXPECT_EQ(info.planes[0].role, mffv1::PlaneRole::R);
    EXPECT_EQ(info.planes[1].role, mffv1::PlaneRole::G);
    EXPECT_EQ(info.planes[2].role, mffv1::PlaneRole::B);
    for (std::size_t plane = 0; plane < info.plane_count; ++plane) {
        EXPECT_EQ(info.planes[plane].sample_format,
                  mffv1::SampleFormat::UInt8);
        EXPECT_EQ(info.planes[plane].width, stream.width);
        EXPECT_EQ(info.planes[plane].height, stream.height);
        EXPECT_EQ(info.planes[plane].stride_bytes, 7);
    }
    EXPECT_EQ(info.planes[3].width, 0u);
    EXPECT_EQ(info.planes[3].height, 0u);
}

} // namespace
