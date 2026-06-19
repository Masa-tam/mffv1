#include "mffv1/color_transform.hpp"

#include <gtest/gtest.h>

namespace {

TEST(ColorTransformTest, InvertsEightBitJpeg2000Rct)
{
    const auto rgb = mffv1::syntax::inverse_jpeg2000_rct(112, 206, 356, 8, false);

    EXPECT_EQ(rgb.r, 200u);
    EXPECT_EQ(rgb.g, 100u);
    EXPECT_EQ(rgb.b, 50u);
}

TEST(ColorTransformTest, UsesArithmeticShiftForNegativeCorrection)
{
    const auto rgb = mffv1::syntax::inverse_jpeg2000_rct(127, 1, 1, 8, false);

    EXPECT_EQ(rgb.r, 0u);
    EXPECT_EQ(rgb.g, 255u);
    EXPECT_EQ(rgb.b, 0u);
}

TEST(ColorTransformTest, AppliesNineToFifteenBitCompatibilityTransform)
{
    const auto rgb = mffv1::syntax::inverse_jpeg2000_rct(450, 1624, 1824, 10, false);

    EXPECT_EQ(rgb.r, 900u);
    EXPECT_EQ(rgb.g, 700u);
    EXPECT_EQ(rgb.b, 100u);
}

TEST(ColorTransformTest, ExtraPlaneDisablesCompatibilityTransform)
{
    const auto rgb = mffv1::syntax::inverse_jpeg2000_rct(600, 424, 1224, 10, true);

    EXPECT_EQ(rgb.r, 900u);
    EXPECT_EQ(rgb.g, 700u);
    EXPECT_EQ(rgb.b, 100u);
}

TEST(ColorTransformTest, WrapsComponentsToRawSampleWidth)
{
    const auto rgb = mffv1::syntax::inverse_jpeg2000_rct(0, 0, 0, 8, false);

    EXPECT_EQ(rgb.r, 128u);
    EXPECT_EQ(rgb.g, 128u);
    EXPECT_EQ(rgb.b, 128u);
}

TEST(ColorTransformTest, RejectsInvalidBitWidthWithoutShifting)
{
    const auto rgb = mffv1::syntax::inverse_jpeg2000_rct(1, 2, 3, 0, false);

    EXPECT_EQ(rgb.r, 0u);
    EXPECT_EQ(rgb.g, 0u);
    EXPECT_EQ(rgb.b, 0u);
}

} // namespace
