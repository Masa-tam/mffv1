#include "mffv1/color_transform.hpp"

#include <array>
#include <cstdint>

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

TEST(ColorTransformTest, ForwardTransformInvertsRepresentativeRgbSamples)
{
    for (const std::uint8_t bits : {std::uint8_t{8},
                                    std::uint8_t{10},
                                    std::uint8_t{16}}) {
        const auto maximum = static_cast<std::uint16_t>(
            bits == 16 ? 0xffffu : (std::uint32_t{1} << bits) - 1u);
        const std::array<std::uint16_t, 5> values{
            0,
            1,
            static_cast<std::uint16_t>(maximum / 2),
            static_cast<std::uint16_t>(maximum - 1),
            maximum,
        };
        for (const bool has_extra_plane : {false, true}) {
            for (const auto r : values) {
                for (const auto g : values) {
                    for (const auto b : values) {
                        const auto code =
                            mffv1::syntax::forward_jpeg2000_rct(
                                r, g, b, bits, has_extra_plane);
                        const auto decoded =
                            mffv1::syntax::inverse_jpeg2000_rct(
                                code.y,
                                code.cb,
                                code.cr,
                                bits,
                                has_extra_plane);
                        EXPECT_EQ(decoded.r, r);
                        EXPECT_EQ(decoded.g, g);
                        EXPECT_EQ(decoded.b, b);
                    }
                }
            }
        }
    }
}

TEST(ColorTransformTest, ForwardTransformUsesHighBitCompatibilityLayout)
{
    const auto code = mffv1::syntax::forward_jpeg2000_rct(
        900, 700, 100, 10, false);

    EXPECT_EQ(code.y, 450);
    EXPECT_EQ(code.cb, 1624);
    EXPECT_EQ(code.cr, 1824);
}

} // namespace
