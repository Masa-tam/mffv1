#include "codec/version3_constraints.hpp"

#include <gtest/gtest.h>

namespace {

TEST(Version3ConstraintsTest, DoesNotRequireParallelLimitAtCifThreshold)
{
    EXPECT_FALSE(mffv1::codec::requires_version3_parallel_slice_limit(
        3, 352, 288));
}

TEST(Version3ConstraintsTest, RequiresParallelLimitAboveCifForVersionThree)
{
    EXPECT_TRUE(mffv1::codec::requires_version3_parallel_slice_limit(
        3, 353, 288));
}

TEST(Version3ConstraintsTest, DoesNotRequireParallelLimitBeforeVersionThree)
{
    EXPECT_FALSE(mffv1::codec::requires_version3_parallel_slice_limit(
        2, 353, 288));
}

TEST(Version3ConstraintsTest, UsesWideArithmeticForMaximumDimensions)
{
    EXPECT_TRUE(mffv1::codec::requires_version3_parallel_slice_limit(
        3, 0xffffffffu, 0xffffffffu));
}

} // namespace
