#include "mffv1/sample_format.hpp"

#include <gtest/gtest.h>

namespace {

TEST(SampleFormatTest, SelectsStorageFormatFromBitDepth)
{
    EXPECT_EQ(mffv1::samples::sample_format_for_bit_depth(1),
              mffv1::SampleFormat::UInt8);
    EXPECT_EQ(mffv1::samples::sample_format_for_bit_depth(8),
              mffv1::SampleFormat::UInt8);
    EXPECT_EQ(mffv1::samples::sample_format_for_bit_depth(9),
              mffv1::SampleFormat::UInt16);
    EXPECT_EQ(mffv1::samples::sample_format_for_bit_depth(16),
              mffv1::SampleFormat::UInt16);
}

TEST(SampleFormatTest, ComputesBytesPerSample)
{
    EXPECT_EQ(mffv1::samples::bytes_per_sample(mffv1::SampleFormat::UInt8), 1u);
    EXPECT_EQ(mffv1::samples::bytes_per_sample(mffv1::SampleFormat::UInt16), 2u);
}

} // namespace
