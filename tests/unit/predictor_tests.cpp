#include "mffv1/predictor.hpp"

#include <gtest/gtest.h>

namespace {

TEST(PredictorTest, MedianPredictUsesLeftTopAndGradient)
{
    EXPECT_EQ(mffv1::syntax::Predictor::median_predict(10, 20, 15), 15);
    EXPECT_EQ(mffv1::syntax::Predictor::median_predict(50, 10, 20), 40);
    EXPECT_EQ(mffv1::syntax::Predictor::median_predict(3, 30, 10), 23);
}

TEST(PredictorTest, Signed16BitMedianPredictReinterpretsHighSamples)
{
    EXPECT_EQ(mffv1::syntax::Predictor::median_predict(65535, 32768, 0), 65535);
    EXPECT_EQ(mffv1::syntax::Predictor::median_predict_signed_16bit(65535, 32768, 0), -32768);
}

TEST(PredictorTest, Signed16BitMedianPredictUsesTwosComplementBoundary)
{
    EXPECT_EQ(mffv1::syntax::Predictor::median_predict_signed_16bit(32767, 32768, 65535), 0);
    EXPECT_EQ(mffv1::syntax::Predictor::median_predict_signed_16bit(65535, 65535, 65535), -1);
}

TEST(PredictorTest, ReconstructWrapsIntoSampleRange)
{
    EXPECT_EQ(mffv1::syntax::Predictor::reconstruct(250, 10, 8), 4);
    EXPECT_EQ(mffv1::syntax::Predictor::reconstruct(3, -5, 8), 254);
    EXPECT_EQ(mffv1::syntax::Predictor::reconstruct(65530, 10, 16), 4);
}

} // namespace
