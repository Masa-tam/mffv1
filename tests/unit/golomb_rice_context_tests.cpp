#include "entropy/golomb_rice_context.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

TEST(GolombRiceContextTest, DecodesWithInitialKAndUpdatesState)
{
    const std::array bytes{std::byte{0x80}}; // k=2: 1 00
    mffv1::bitstream::BitReader bits(bytes);
    mffv1::entropy::GolombRiceReader reader(bits);
    mffv1::entropy::GolombRiceContextState state;
    std::int32_t value = 1;

    const auto status = mffv1::entropy::read_golomb_rice_symbol(reader, state, 8, value);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(value, 0);
    EXPECT_EQ(bits.bit_position(), 3u);
    EXPECT_EQ(state.drift, 0);
    EXPECT_EQ(state.error_sum, 4);
    EXPECT_EQ(state.bias, 0);
    EXPECT_EQ(state.count, 2);
}

TEST(GolombRiceContextTest, AppliesDriftInversionAndBiasCorrection)
{
    const std::array bytes{std::byte{0x80}}; // k=0: 1
    mffv1::bitstream::BitReader bits(bytes);
    mffv1::entropy::GolombRiceReader reader(bits);
    mffv1::entropy::GolombRiceContextState state{-4, 4, 0, 4};
    std::int32_t value = 0;

    const auto status = mffv1::entropy::read_golomb_rice_symbol(reader, state, 8, value);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(value, -1);
    EXPECT_EQ(state.drift, 0);
    EXPECT_EQ(state.error_sum, 5);
    EXPECT_EQ(state.bias, -1);
    EXPECT_EQ(state.count, 5);
}

TEST(GolombRiceContextTest, SignExtendsDecodedValueToSampleWidth)
{
    const std::array bytes{std::byte{0x24}}; // k=2: 001 00 -> 8
    mffv1::bitstream::BitReader bits(bytes);
    mffv1::entropy::GolombRiceReader reader(bits);
    mffv1::entropy::GolombRiceContextState state;
    std::int32_t value = 0;

    const auto status = mffv1::entropy::read_golomb_rice_symbol(reader, state, 3, value);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(value, -4);
}

TEST(GolombRiceContextTest, RescalesStateAtCountLimit)
{
    const std::array bytes{std::byte{0x80}}; // k=1: 1 0
    mffv1::bitstream::BitReader bits(bytes);
    mffv1::entropy::GolombRiceReader reader(bits);
    mffv1::entropy::GolombRiceContextState state{-64, 256, 0, 128};
    std::int32_t value = 0;

    const auto status = mffv1::entropy::read_golomb_rice_symbol(reader, state, 8, value);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(value, 0);
    EXPECT_EQ(state.drift, -32);
    EXPECT_EQ(state.error_sum, 128);
    EXPECT_EQ(state.bias, 0);
    EXPECT_EQ(state.count, 65);
}

TEST(GolombRiceContextTest, LeavesStateAndOutputUnchangedOnUnderflow)
{
    const std::array<std::byte, 0> bytes{};
    mffv1::bitstream::BitReader bits(bytes);
    mffv1::entropy::GolombRiceReader reader(bits);
    mffv1::entropy::GolombRiceContextState state;
    const auto original = state;
    std::int32_t value = 7;

    const auto status = mffv1::entropy::read_golomb_rice_symbol(reader, state, 8, value);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(value, 7);
    EXPECT_EQ(state.drift, original.drift);
    EXPECT_EQ(state.error_sum, original.error_sum);
    EXPECT_EQ(state.bias, original.bias);
    EXPECT_EQ(state.count, original.count);
}

TEST(GolombRiceContextTest, RejectsInvalidStateWithoutReading)
{
    const std::array bytes{std::byte{0xff}};
    mffv1::bitstream::BitReader bits(bytes);
    mffv1::entropy::GolombRiceReader reader(bits);
    mffv1::entropy::GolombRiceContextState state;
    state.count = 0;
    std::int32_t value = 0;

    const auto status = mffv1::entropy::read_golomb_rice_symbol(reader, state, 8, value);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(bits.bit_position(), 0u);
}

TEST(GolombRiceContextTest, RemovesZeroFromRunInterruptionDifference)
{
    const std::array bytes{std::byte{0x80}}; // initial k=2: 1 00 -> 0
    mffv1::bitstream::BitReader bits(bytes);
    mffv1::entropy::GolombRiceReader reader(bits);
    mffv1::entropy::GolombRiceContextState state;
    std::int32_t value = 0;

    const auto status = mffv1::entropy::read_golomb_rice_run_interruption(reader,
                                                                         state,
                                                                         8,
                                                                         value);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(value, 1);
}

TEST(GolombRiceContextTest, RejectsUnrepresentableKWithoutReading)
{
    const std::array bytes{std::byte{0xff}};
    mffv1::bitstream::BitReader bits(bytes);
    mffv1::entropy::GolombRiceReader reader(bits);
    mffv1::entropy::GolombRiceContextState state{0, std::int64_t{1} << 40, 0, 1};
    std::int32_t value = 0;

    const auto status = mffv1::entropy::read_golomb_rice_symbol(reader, state, 8, value);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(bits.bit_position(), 0u);
}

} // namespace
