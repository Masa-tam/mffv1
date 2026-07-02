#include "entropy/golomb_rice_context.hpp"
#include "entropy/golomb_rice_reader.hpp"
#include "entropy/golomb_rice_writer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::vector<std::byte> finalize(mffv1::bitstream::BitWriter& writer)
{
    EXPECT_TRUE(writer.byte_align_zero().ok());
    std::vector<std::byte> bytes;
    EXPECT_TRUE(writer.finalize(bytes).ok());
    return bytes;
}

TEST(GolombRiceWriterTest, WritesRfcSignedCodewords)
{
    struct Example {
        std::uint8_t k;
        std::int32_t value;
        std::byte expected;
    };
    const std::array examples{
        Example{0, 0, std::byte{0x80}},
        Example{0, 1, std::byte{0x20}},
        Example{2, 0, std::byte{0x80}},
        Example{2, 1, std::byte{0xc0}},
        Example{2, -3, std::byte{0x50}},
    };

    for (const auto& example : examples) {
        mffv1::bitstream::BitWriter bits;
        mffv1::entropy::GolombRiceWriter writer(bits);

        ASSERT_TRUE(writer.write_signed(
            example.k, 8, example.value).ok());
        const auto bytes = finalize(bits);

        ASSERT_EQ(bytes.size(), 1u);
        EXPECT_EQ(bytes[0], example.expected);
    }
}

TEST(GolombRiceWriterTest, WritesCanonicalEscape)
{
    mffv1::bitstream::BitWriter bits;
    mffv1::entropy::GolombRiceWriter writer(bits);

    ASSERT_TRUE(writer.write_signed(0, 8, -70).ok());
    const auto bytes = finalize(bits);

    EXPECT_EQ(bytes, (std::vector<std::byte>{
        std::byte{0x00}, std::byte{0x08}, std::byte{0x00}}));
}

TEST(GolombRiceWriterTest, RoundTripsSignedValues)
{
    for (std::uint8_t k = 0; k <= 6; ++k) {
        for (std::int32_t value = -128; value <= 127; ++value) {
            mffv1::bitstream::BitWriter output_bits;
            mffv1::entropy::GolombRiceWriter writer(output_bits);
            ASSERT_TRUE(writer.write_signed(k, 8, value).ok());
            const auto bytes = finalize(output_bits);

            mffv1::bitstream::BitReader input_bits(bytes);
            mffv1::entropy::GolombRiceReader reader(input_bits);
            std::int32_t decoded = 0;
            ASSERT_TRUE(reader.read_signed(k, 8, decoded).ok());
            EXPECT_EQ(decoded, value);
        }
    }
}

TEST(GolombRiceWriterTest, ContextRoundTripMatchesState)
{
    const std::array<std::int32_t, 12> values{
        0, -1, 3, -4, 127, -128, 1, 1, -2, 64, -63, 0};
    mffv1::bitstream::BitWriter output_bits;
    mffv1::entropy::GolombRiceWriter writer(output_bits);
    mffv1::entropy::GolombRiceContextState encode_state;
    for (const auto value : values) {
        ASSERT_TRUE(mffv1::entropy::write_golomb_rice_symbol(
            writer, encode_state, 8, value).ok());
    }
    const auto bytes = finalize(output_bits);

    mffv1::bitstream::BitReader input_bits(bytes);
    mffv1::entropy::GolombRiceReader reader(input_bits);
    mffv1::entropy::GolombRiceContextState decode_state;
    for (const auto expected : values) {
        std::int32_t decoded = 0;
        ASSERT_TRUE(mffv1::entropy::read_golomb_rice_symbol(
            reader, decode_state, 8, decoded).ok());
        EXPECT_EQ(decoded, expected);
    }
    EXPECT_EQ(decode_state.drift, encode_state.drift);
    EXPECT_EQ(decode_state.error_sum, encode_state.error_sum);
    EXPECT_EQ(decode_state.bias, encode_state.bias);
    EXPECT_EQ(decode_state.count, encode_state.count);
}

TEST(GolombRiceWriterTest, RunInterruptionRoundTripsNonzeroValues)
{
    const std::array<std::int32_t, 6> values{1, -1, 2, -2, 127, -128};
    mffv1::bitstream::BitWriter output_bits;
    mffv1::entropy::GolombRiceWriter writer(output_bits);
    mffv1::entropy::GolombRiceContextState encode_state;
    for (const auto value : values) {
        ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run_interruption(
            writer, encode_state, 8, value).ok());
    }
    const auto bytes = finalize(output_bits);

    mffv1::bitstream::BitReader input_bits(bytes);
    mffv1::entropy::GolombRiceReader reader(input_bits);
    mffv1::entropy::GolombRiceContextState decode_state;
    for (const auto expected : values) {
        std::int32_t decoded = 0;
        ASSERT_TRUE(mffv1::entropy::read_golomb_rice_run_interruption(
            reader, decode_state, 8, decoded).ok());
        EXPECT_EQ(decoded, expected);
    }
}

TEST(GolombRiceWriterTest, RejectsInvalidInputsWithoutWriting)
{
    mffv1::bitstream::BitWriter bits;
    mffv1::entropy::GolombRiceWriter writer(bits);

    auto status = writer.write_signed(32, 8, 0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "Golomb-Rice parameter k must be less than 32");
    EXPECT_EQ(bits.bit_position(), 0u);

    status = writer.write_signed(0, 0, 0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message,
              "Golomb-Rice raw sample width must be in the range 1..31");
    EXPECT_EQ(bits.bit_position(), 0u);

    mffv1::entropy::GolombRiceContextState state;
    status = mffv1::entropy::write_golomb_rice_run_interruption(
        writer, state, 8, 0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message,
              "Golomb-Rice run interruption difference must be nonzero");
    EXPECT_EQ(bits.bit_position(), 0u);
}

TEST(GolombRiceWriterTest, InvalidContextLeavesStateUnchanged)
{
    mffv1::bitstream::BitWriter bits;
    mffv1::entropy::GolombRiceWriter writer(bits);
    mffv1::entropy::GolombRiceContextState state;
    state.count = 0;
    const auto original = state;

    const auto status = mffv1::entropy::write_golomb_rice_symbol(
        writer, state, 8, 1);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "Golomb-Rice context count is invalid");
    EXPECT_EQ(bits.bit_position(), 0u);
    EXPECT_EQ(state.drift, original.drift);
    EXPECT_EQ(state.error_sum, original.error_sum);
    EXPECT_EQ(state.bias, original.bias);
    EXPECT_EQ(state.count, original.count);
}

} // namespace
