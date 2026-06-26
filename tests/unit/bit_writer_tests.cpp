#include "bitstream/bit_writer.hpp"

#include "bitstream/bit_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

TEST(BitWriterTest, WritesBitsMostSignificantBitFirst)
{
    mffv1::bitstream::BitWriter writer;

    ASSERT_TRUE(writer.write_bits(0b1010, 4).ok());
    ASSERT_TRUE(writer.write_bits(0b0101'11, 6).ok());
    ASSERT_TRUE(writer.write_bits(0b00'0011, 6).ok());
    EXPECT_EQ(writer.bit_position(), 16u);
    EXPECT_EQ(writer.byte_position(), 2u);

    std::vector<std::byte> bytes;
    ASSERT_TRUE(writer.finalize(bytes).ok());
    const std::vector<std::byte> expected{
        std::byte{0b1010'0101},
        std::byte{0b1100'0011},
    };
    EXPECT_EQ(bytes, expected);
}

TEST(BitWriterTest, RoundTripsSixtyFourBitsThroughBitReader)
{
    constexpr std::uint64_t expected = 0x0123'4567'89ab'cdefu;
    mffv1::bitstream::BitWriter writer;
    ASSERT_TRUE(writer.write_bits(expected, 64).ok());

    std::vector<std::byte> bytes;
    ASSERT_TRUE(writer.finalize(bytes).ok());
    mffv1::bitstream::BitReader reader(bytes);
    std::uint64_t actual = 0;

    ASSERT_TRUE(reader.read_bits(64, actual).ok());
    EXPECT_EQ(actual, expected);
}

TEST(BitWriterTest, RejectsInvalidValuesWithoutAdvancing)
{
    mffv1::bitstream::BitWriter writer;
    ASSERT_TRUE(writer.write_bits(0b101, 3).ok());

    auto status = writer.write_bit(2);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "bit value must be zero or one");
    EXPECT_EQ(writer.bit_position(), 3u);

    status = writer.write_bits(0b1000, 3);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "value does not fit in the requested bit count");
    EXPECT_EQ(writer.bit_position(), 3u);

    status = writer.write_bits(0, 65);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "cannot write more than 64 bits at once");
    EXPECT_EQ(writer.bit_position(), 3u);
}

TEST(BitWriterTest, ZeroBitFieldRequiresZeroValue)
{
    mffv1::bitstream::BitWriter writer;

    EXPECT_TRUE(writer.write_bits(0, 0).ok());
    EXPECT_EQ(writer.bit_position(), 0u);

    const auto status = writer.write_bits(1, 0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "value does not fit in the requested bit count");
    EXPECT_EQ(writer.bit_position(), 0u);
}

TEST(BitWriterTest, AlignsWithZeroPadding)
{
    mffv1::bitstream::BitWriter writer;
    ASSERT_TRUE(writer.write_bits(0b101, 3).ok());
    auto status = writer.require_byte_aligned();
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "bitstream is not byte aligned");

    ASSERT_TRUE(writer.byte_align_zero().ok());
    EXPECT_TRUE(writer.require_byte_aligned().ok());
    EXPECT_EQ(writer.bit_position(), 8u);

    std::vector<std::byte> bytes;
    ASSERT_TRUE(writer.finalize(bytes).ok());
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], std::byte{0b1010'0000});
}

TEST(BitWriterTest, UnalignedFinalizePreservesOutput)
{
    mffv1::bitstream::BitWriter writer;
    ASSERT_TRUE(writer.write_bit(1).ok());
    std::vector<std::byte> bytes{std::byte{0xaa}};

    const auto status = writer.finalize(bytes);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "bitstream is not byte aligned");
    EXPECT_FALSE(writer.finalized());
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], std::byte{0xaa});
}

TEST(BitWriterTest, FinalizeSealsWriterUntilReset)
{
    mffv1::bitstream::BitWriter writer;
    ASSERT_TRUE(writer.write_bits(0xab, 8).ok());
    std::vector<std::byte> bytes;
    ASSERT_TRUE(writer.finalize(bytes).ok());
    EXPECT_TRUE(writer.finalized());

    auto status = writer.write_bit(0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "cannot write to a finalized bitstream");
    status = writer.byte_align_zero();
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "cannot align a finalized bitstream");
    status = writer.finalize(bytes);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "bitstream is already finalized");

    writer.reset();
    EXPECT_FALSE(writer.finalized());
    EXPECT_EQ(writer.bit_position(), 0u);
    ASSERT_TRUE(writer.write_bits(0xcd, 8).ok());
    ASSERT_TRUE(writer.finalize(bytes).ok());
    ASSERT_EQ(bytes.size(), 1u);
    EXPECT_EQ(bytes[0], std::byte{0xcd});
}

TEST(BitWriterTest, FinalizesEmptyStream)
{
    mffv1::bitstream::BitWriter writer;
    std::vector<std::byte> bytes{std::byte{0xaa}};

    ASSERT_TRUE(writer.finalize(bytes).ok());
    EXPECT_TRUE(bytes.empty());
}

} // namespace
