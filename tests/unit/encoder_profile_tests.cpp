#include "codec/encoder_profile.hpp"

#include <cstddef>

#include <gtest/gtest.h>

namespace {

mffv1::StreamInfo make_stream_info()
{
    mffv1::StreamInfo info;
    info.width = 16;
    info.height = 8;
    info.version = 3;
    info.bits_per_raw_sample = 10;
    info.has_chroma_planes = true;
    info.has_extra_plane = true;
    info.log2_h_chroma_subsample = 1;
    info.log2_v_chroma_subsample = 1;
    info.num_h_slices = 2;
    info.num_v_slices = 1;
    info.error_status_enabled = true;
    return info;
}

TEST(EncoderProfileTest, NormalizesPublicStreamInfo)
{
    mffv1::EncoderOptions options;
    options.entropy_mode = mffv1::EntropyMode::GolombRice;
    options.keyframe_interval = 3;
    const auto info = make_stream_info();
    mffv1::syntax::StreamParameters stream;

    const auto status =
        mffv1::codec::normalize_encoder_profile(options, info, stream);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.version, 3);
    EXPECT_EQ(stream.micro_version, 4);
    EXPECT_EQ(stream.entropy_mode, mffv1::EntropyMode::GolombRice);
    EXPECT_EQ(stream.width, info.width);
    EXPECT_EQ(stream.height, info.height);
    EXPECT_EQ(stream.bits_per_raw_sample, info.bits_per_raw_sample);
    EXPECT_EQ(stream.colorspace_type, 0);
    EXPECT_TRUE(stream.chroma_planes);
    EXPECT_TRUE(stream.extra_plane);
    EXPECT_EQ(stream.log2_h_chroma_subsample, 1u);
    EXPECT_EQ(stream.log2_v_chroma_subsample, 1u);
    EXPECT_EQ(stream.num_h_slices, 2u);
    EXPECT_EQ(stream.num_v_slices, 1u);
    EXPECT_TRUE(stream.error_status_enabled);
    EXPECT_FALSE(stream.intra_only);
    ASSERT_EQ(stream.quant_table_sets.size(), 1u);
    EXPECT_EQ(stream.quant_table_sets[0].context_count, 1u);
}

TEST(EncoderProfileTest, MarksSingleFrameCadenceAsIntraOnly)
{
    mffv1::EncoderOptions options;
    options.keyframe_interval = 1;
    auto info = make_stream_info();
    info.has_extra_plane = false;
    mffv1::syntax::StreamParameters stream;

    const auto status =
        mffv1::codec::normalize_encoder_profile(options, info, stream);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(stream.intra_only);
}

TEST(EncoderProfileTest, RejectsLargeVersionThreeFrameWithTooFewSlices)
{
    auto info = make_stream_info();
    info.width = 353;
    info.height = 288;
    info.has_chroma_planes = false;
    info.has_extra_plane = false;
    info.log2_h_chroma_subsample = 0;
    info.log2_v_chroma_subsample = 0;
    info.num_h_slices = 1;
    info.num_v_slices = 1;
    mffv1::syntax::StreamParameters stream;

    const auto status =
        mffv1::codec::normalize_encoder_profile({}, info, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

TEST(EncoderProfileTest, RejectsSliceGridWithEmptySubsampledPlaneRegion)
{
    auto info = make_stream_info();
    info.width = 3;
    info.height = 2;
    info.has_extra_plane = false;
    info.num_h_slices = 3;
    info.num_v_slices = 1;
    mffv1::syntax::StreamParameters stream;

    const auto status =
        mffv1::codec::normalize_encoder_profile({}, info, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

TEST(EncoderProfileTest, RejectsSubsampledRgbProfile)
{
    auto info = make_stream_info();
    info.color_space = mffv1::ColorSpace::Rgb;
    info.log2_h_chroma_subsample = 1;
    info.log2_v_chroma_subsample = 0;
    mffv1::syntax::StreamParameters stream;

    const auto status =
        mffv1::codec::normalize_encoder_profile({}, info, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
}

TEST(EncoderProfileTest, FailedNormalizationDoesNotChangeOutputStream)
{
    mffv1::syntax::StreamParameters stream;
    stream.version = 2;
    stream.micro_version = 99;
    stream.width = 123;
    stream.height = 45;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.bits_per_raw_sample = 16;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.extra_plane = true;
    stream.log2_h_chroma_subsample = 0;
    stream.log2_v_chroma_subsample = 0;
    stream.num_h_slices = 7;
    stream.num_v_slices = 3;
    stream.error_status_enabled = true;
    stream.quant_table_sets.push_back(
        mffv1::syntax::make_zero_quant_table_set());
    stream.intra_only = true;
    const auto original = stream;

    auto info = make_stream_info();
    info.width = 0;
    const auto status =
        mffv1::codec::normalize_encoder_profile({}, info, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(stream.version, original.version);
    EXPECT_EQ(stream.micro_version, original.micro_version);
    EXPECT_EQ(stream.width, original.width);
    EXPECT_EQ(stream.height, original.height);
    EXPECT_EQ(stream.entropy_mode, original.entropy_mode);
    EXPECT_EQ(stream.bits_per_raw_sample, original.bits_per_raw_sample);
    EXPECT_EQ(stream.colorspace_type, original.colorspace_type);
    EXPECT_EQ(stream.chroma_planes, original.chroma_planes);
    EXPECT_EQ(stream.extra_plane, original.extra_plane);
    EXPECT_EQ(stream.log2_h_chroma_subsample,
              original.log2_h_chroma_subsample);
    EXPECT_EQ(stream.log2_v_chroma_subsample,
              original.log2_v_chroma_subsample);
    EXPECT_EQ(stream.num_h_slices, original.num_h_slices);
    EXPECT_EQ(stream.num_v_slices, original.num_v_slices);
    EXPECT_EQ(stream.error_status_enabled, original.error_status_enabled);
    EXPECT_EQ(stream.quant_table_sets.size(), original.quant_table_sets.size());
    EXPECT_EQ(stream.intra_only, original.intra_only);
}

} // namespace
