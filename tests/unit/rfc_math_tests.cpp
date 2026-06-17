#include "util/rfc_math.hpp"

#include <gtest/gtest.h>

namespace {

TEST(RfcMathTest, ArithmeticRightShiftRoundsTowardNegativeInfinity)
{
    EXPECT_EQ(ffv1::util::arithmetic_right_shift(7, 1), 3);
    EXPECT_EQ(ffv1::util::arithmetic_right_shift(-7, 1), -4);
    EXPECT_EQ(ffv1::util::arithmetic_right_shift(-1, 1), -1);
}

TEST(RfcMathTest, Median3ReturnsMiddleValue)
{
    EXPECT_EQ(ffv1::util::median3(1, 2, 3), 2);
    EXPECT_EQ(ffv1::util::median3(3, 1, 2), 2);
    EXPECT_EQ(ffv1::util::median3(2, 2, 9), 2);
}

TEST(RfcMathTest, WrapSampleDifferenceUsesSignedHalfRange)
{
    EXPECT_EQ(ffv1::util::wrap_sample_difference(127, 8), 127);
    EXPECT_EQ(ffv1::util::wrap_sample_difference(128, 8), -128);
    EXPECT_EQ(ffv1::util::wrap_sample_difference(-129, 8), 127);
}

} // namespace

