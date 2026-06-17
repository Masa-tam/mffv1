#include "bitstream/bit_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

TEST(BitReaderTest, ReadsBitsMostSignificantBitFirst)
{
    const std::array<std::byte, 2> data{
        std::byte{0b1010'0101},
        std::byte{0b1100'0011},
    };

    ffv1::bitstream::BitReader reader(data);

    std::uint64_t value = 0;
    EXPECT_TRUE(reader.read_bits(4, value).ok());
    EXPECT_EQ(value, 0b1010u);

    EXPECT_TRUE(reader.read_bits(6, value).ok());
    EXPECT_EQ(value, 0b0101'11u);

    EXPECT_EQ(reader.bit_position(), 10u);
}

TEST(BitReaderTest, ReportsUnderflow)
{
    const std::array<std::byte, 1> data{std::byte{0xff}};
    ffv1::bitstream::BitReader reader(data);

    std::uint64_t value = 0;
    EXPECT_TRUE(reader.read_bits(8, value).ok());

    const auto status = reader.read_bits(1, value);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

} // namespace

