#include "mffv1/predictor.hpp"

#include <array>
#include <cstdint>

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

TEST(PredictorTest, DifferenceUsesCanonicalSignedModuloRange)
{
    EXPECT_EQ(mffv1::syntax::Predictor::difference(0, 0, 8), 0);
    EXPECT_EQ(mffv1::syntax::Predictor::difference(255, 0, 8), -1);
    EXPECT_EQ(mffv1::syntax::Predictor::difference(0, 255, 8), 1);
    EXPECT_EQ(mffv1::syntax::Predictor::difference(128, 0, 8), -128);
    EXPECT_EQ(mffv1::syntax::Predictor::difference(0, 128, 8), -128);
}

TEST(PredictorTest, DifferenceInvertsReconstructionForSupportedBitDepths)
{
    for (std::uint8_t bits = 1; bits <= 16; ++bits) {
        const auto range = std::uint32_t{1} << bits;
        const std::array<std::uint32_t, 5> values{
            0,
            1,
            (range >> 1) - 1,
            range >> 1,
            range - 1,
        };
        for (const auto sample : values) {
            for (const auto prediction : values) {
                const auto difference = mffv1::syntax::Predictor::difference(
                    static_cast<std::int32_t>(sample),
                    static_cast<std::int32_t>(prediction),
                    bits);
                EXPECT_EQ(
                    mffv1::syntax::Predictor::reconstruct(
                        static_cast<std::int32_t>(prediction),
                        difference,
                        bits),
                    static_cast<std::int32_t>(sample))
                    << "bits=" << static_cast<int>(bits)
                    << " sample=" << sample
                    << " prediction=" << prediction;
            }
        }
    }
}

TEST(PredictorTest, DifferenceInvertsEveryEightBitSampleAndPrediction)
{
    for (std::int32_t sample = 0; sample <= 255; ++sample) {
        for (std::int32_t prediction = 0; prediction <= 255; ++prediction) {
            const auto difference =
                mffv1::syntax::Predictor::difference(sample, prediction, 8);
            EXPECT_GE(difference, -128);
            EXPECT_LE(difference, 127);
            EXPECT_EQ(
                mffv1::syntax::Predictor::reconstruct(
                    prediction, difference, 8),
                sample);
        }
    }
}

} // namespace
