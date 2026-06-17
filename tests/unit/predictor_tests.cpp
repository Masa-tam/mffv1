#include "ffv1/predictor.hpp"

#include <gtest/gtest.h>

namespace {

TEST(PredictorTest, MedianPredictUsesLeftTopAndGradient)
{
    EXPECT_EQ(ffv1::syntax::Predictor::median_predict(10, 20, 15), 15);
    EXPECT_EQ(ffv1::syntax::Predictor::median_predict(50, 10, 20), 40);
    EXPECT_EQ(ffv1::syntax::Predictor::median_predict(3, 30, 10), 23);
}

TEST(PredictorTest, ReconstructWrapsIntoSampleRange)
{
    EXPECT_EQ(ffv1::syntax::Predictor::reconstruct(250, 10, 8), 4);
    EXPECT_EQ(ffv1::syntax::Predictor::reconstruct(3, -5, 8), 254);
    EXPECT_EQ(ffv1::syntax::Predictor::reconstruct(65530, 10, 16), 4);
}

} // namespace

