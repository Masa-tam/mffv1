#include "mffv1/profile_constraints.hpp"

#include <gtest/gtest.h>

namespace {

TEST(ProfileConstraintsTest, AcceptsOnlyEncoderBitDepths)
{
    EXPECT_FALSE(mffv1::constraints::is_supported_encoder_bit_depth(7));
    EXPECT_TRUE(mffv1::constraints::is_supported_encoder_bit_depth(8));
    EXPECT_TRUE(mffv1::constraints::is_supported_encoder_bit_depth(16));
    EXPECT_FALSE(mffv1::constraints::is_supported_encoder_bit_depth(17));
}

TEST(ProfileConstraintsTest, AcceptsOnlyDecoderBitDepths)
{
    EXPECT_FALSE(mffv1::constraints::is_supported_decoder_bit_depth(0));
    EXPECT_TRUE(mffv1::constraints::is_supported_decoder_bit_depth(1));
    EXPECT_TRUE(mffv1::constraints::is_supported_decoder_bit_depth(16));
    EXPECT_FALSE(mffv1::constraints::is_supported_decoder_bit_depth(17));
}

TEST(ProfileConstraintsTest, AcceptsOnlyKnownColorSpaces)
{
    EXPECT_TRUE(mffv1::constraints::is_supported_syntax_colorspace(0));
    EXPECT_TRUE(mffv1::constraints::is_supported_syntax_colorspace(1));
    EXPECT_FALSE(mffv1::constraints::is_supported_syntax_colorspace(-1));
    EXPECT_FALSE(mffv1::constraints::is_supported_syntax_colorspace(2));

    EXPECT_TRUE(mffv1::constraints::is_supported_public_color_space(
        mffv1::ColorSpace::YCbCr));
    EXPECT_TRUE(mffv1::constraints::is_supported_public_color_space(
        mffv1::ColorSpace::Rgb));
}

TEST(ProfileConstraintsTest, RejectsInvalidRgbGeometry)
{
    EXPECT_FALSE(mffv1::constraints::has_invalid_rgb_geometry(
        false, false, 1, 1));
    EXPECT_FALSE(mffv1::constraints::has_invalid_rgb_geometry(
        true, true, 0, 0));
    EXPECT_TRUE(mffv1::constraints::has_invalid_rgb_geometry(
        true, false, 0, 0));
    EXPECT_TRUE(mffv1::constraints::has_invalid_rgb_geometry(
        true, true, 1, 0));
}

TEST(ProfileConstraintsTest, RejectsSubsamplingWithoutChroma)
{
    EXPECT_FALSE(mffv1::constraints::has_subsampling_without_chroma(
        false, 0, 0));
    EXPECT_FALSE(mffv1::constraints::has_subsampling_without_chroma(
        true, 1, 1));
    EXPECT_TRUE(mffv1::constraints::has_subsampling_without_chroma(
        false, 1, 0));
    EXPECT_TRUE(mffv1::constraints::has_subsampling_without_chroma(
        false, 0, 1));
}

TEST(ProfileConstraintsTest, AcceptsSupportedChromaSubsampling)
{
    EXPECT_TRUE(mffv1::constraints::is_supported_chroma_subsampling(0, 0));
    EXPECT_TRUE(mffv1::constraints::is_supported_chroma_subsampling(1, 0));
    EXPECT_TRUE(mffv1::constraints::is_supported_chroma_subsampling(1, 1));
    EXPECT_FALSE(mffv1::constraints::is_supported_chroma_subsampling(0, 1));
    EXPECT_FALSE(mffv1::constraints::is_supported_chroma_subsampling(2, 0));
    EXPECT_FALSE(mffv1::constraints::is_supported_chroma_subsampling(1, 2));
}

} // namespace
