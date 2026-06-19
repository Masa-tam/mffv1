#include "entropy/golomb_rice_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

TEST(GolombRiceReaderTest, AppliesSignedMappingToRfcCodewords)
{
    struct Example {
        std::uint8_t k;
        std::array<std::byte, 2> bytes;
        std::int32_t expected;
    };
    const std::array examples{
        Example{0, {std::byte{0x80}, std::byte{0x00}}, 0},
        Example{0, {std::byte{0x20}, std::byte{0x00}}, 1},
        Example{2, {std::byte{0x80}, std::byte{0x00}}, 0},
        Example{2, {std::byte{0xc0}, std::byte{0x00}}, 1},
        Example{2, {std::byte{0x50}, std::byte{0x00}}, -3},
    };

    for (const auto& example : examples) {
        mffv1::bitstream::BitReader bits(example.bytes);
        mffv1::entropy::GolombRiceReader reader(bits);
        std::int32_t value = 0;

        const auto status = reader.read_signed(example.k, 8, value);

        EXPECT_TRUE(status.ok()) << status.message;
        EXPECT_EQ(value, example.expected);
    }
}

TEST(GolombRiceReaderTest, DecodesRfcEscapeExample)
{
    const std::array bytes{
        std::byte{0x00},
        std::byte{0x08},
        std::byte{0x00},
    };
    mffv1::bitstream::BitReader bits(bytes);
    mffv1::entropy::GolombRiceReader reader(bits);
    std::int32_t value = 0;

    const auto status = reader.read_signed(0, 8, value);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(value, -70);
    EXPECT_EQ(bits.bit_position(), 20u);
}

TEST(GolombRiceReaderTest, RejectsNonCanonicalEscape)
{
    const std::array bytes{
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
    };
    mffv1::bitstream::BitReader bits(bytes);
    mffv1::entropy::GolombRiceReader reader(bits);
    std::int32_t value = 0;

    const auto status = reader.read_signed(0, 8, value);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(GolombRiceReaderTest, ReportsTruncatedCodeAtCodeStart)
{
    const std::array bytes{std::byte{0x80}};
    mffv1::bitstream::BitReader bits(bytes);
    ASSERT_TRUE(bits.skip_bits(6).ok());
    mffv1::entropy::GolombRiceReader reader(bits);
    std::int32_t value = 0;

    const auto status = reader.read_signed(2, 8, value);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(GolombRiceReaderTest, RejectsUnsupportedParametersWithoutConsumingBits)
{
    const std::array bytes{std::byte{0xff}};
    mffv1::bitstream::BitReader bits(bytes);
    mffv1::entropy::GolombRiceReader reader(bits);
    std::int32_t value = 0;

    auto status = reader.read_signed(32, 8, value);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(bits.bit_position(), 0u);

    status = reader.read_signed(0, 0, value);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(bits.bit_position(), 0u);
}

} // namespace
