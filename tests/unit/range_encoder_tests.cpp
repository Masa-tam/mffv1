#include "entropy/range_encoder.hpp"

#include "entropy/range_coder.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

void expect_round_trip(const std::vector<bool>& values,
                       const mffv1::syntax::StateTransitionTable& state_transition =
                           mffv1::syntax::kDefaultStateTransition)
{
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset(state_transition).ok());
    for (const bool value : values) {
        ASSERT_TRUE(encoder.write_bool(value).ok());
    }

    std::vector<std::byte> payload;
    ASSERT_TRUE(encoder.finalize(payload).ok());
    ASSERT_GE(payload.size(), 2u);

    mffv1::entropy::RangeCoder decoder;
    ASSERT_TRUE(decoder.reset(payload, state_transition).ok());
    for (const bool expected : values) {
        bool actual = !expected;
        ASSERT_TRUE(decoder.read_bool(actual).ok());
        EXPECT_EQ(actual, expected);
    }
}

TEST(RangeEncoderTest, EncodesRfcInitialBinarySubranges)
{
    mffv1::entropy::RangeEncoder false_encoder;
    ASSERT_TRUE(false_encoder.reset().ok());
    ASSERT_TRUE(false_encoder.write_bool(false).ok());
    std::vector<std::byte> false_payload;
    ASSERT_TRUE(false_encoder.finalize(false_payload).ok());
    const std::vector<std::byte> expected_false{
        std::byte{0x00},
        std::byte{0x00},
    };
    EXPECT_EQ(false_payload, expected_false);

    mffv1::entropy::RangeEncoder true_encoder;
    ASSERT_TRUE(true_encoder.reset().ok());
    ASSERT_TRUE(true_encoder.write_bool(true).ok());
    std::vector<std::byte> true_payload;
    ASSERT_TRUE(true_encoder.finalize(true_payload).ok());
    const std::vector<std::byte> expected_true{
        std::byte{0x7f},
        std::byte{0x80},
    };
    EXPECT_EQ(true_payload, expected_true);
}

TEST(RangeEncoderTest, RoundTripsEveryBinarySequenceThroughTwelveBits)
{
    for (std::size_t bit_count = 0; bit_count <= 12; ++bit_count) {
        const std::uint64_t sequence_count = std::uint64_t{1} << bit_count;
        for (std::uint64_t sequence = 0; sequence < sequence_count; ++sequence) {
            std::vector<bool> values;
            values.reserve(bit_count);
            for (std::size_t bit = bit_count; bit != 0; --bit) {
                values.push_back(((sequence >> (bit - 1)) & 1u) != 0);
            }
            expect_round_trip(values);
        }
    }
}

TEST(RangeEncoderTest, RoundTripsLongAdaptiveSequence)
{
    std::vector<bool> values;
    values.reserve(10'000);
    for (std::uint32_t index = 0; index < 10'000; ++index) {
        values.push_back(((index * 73u) % 11u) < 5u);
    }

    expect_round_trip(values);
}

TEST(RangeEncoderTest, RoundTripsCustomStateTransition)
{
    auto state_transition = mffv1::syntax::kDefaultStateTransition;
    for (std::size_t state = 1; state < state_transition.size(); ++state) {
        if (state_transition[state] != 0) {
            state_transition[state] =
                static_cast<std::uint8_t>(state_transition[state] - 1);
        }
    }
    const std::vector<bool> values{
        true, false, true, true, false, false, true, false,
        false, true, true, true, false, true, false, false,
    };

    expect_round_trip(values, state_transition);
}

TEST(RangeEncoderTest, RejectsZeroWidthSubrangeWithoutChangingState)
{
    auto state_transition = mffv1::syntax::kDefaultStateTransition;
    state_transition[128] = 0;
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset(state_transition).ok());
    ASSERT_TRUE(encoder.write_bool(true).ok());
    const auto byte_count = encoder.byte_count();

    const auto status = encoder.write_bool(true);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(encoder.byte_count(), byte_count);
    ASSERT_TRUE(encoder.write_bool(false).ok());
    std::vector<std::byte> payload;
    ASSERT_TRUE(encoder.finalize(payload).ok());
    mffv1::entropy::RangeCoder decoder;
    ASSERT_TRUE(decoder.reset(payload, state_transition).ok());
    bool value = false;
    ASSERT_TRUE(decoder.read_bool(value).ok());
    EXPECT_TRUE(value);
    ASSERT_TRUE(decoder.read_bool(value).ok());
    EXPECT_FALSE(value);
}

TEST(RangeEncoderTest, RequiresReset)
{
    mffv1::entropy::RangeEncoder encoder;
    std::vector<std::byte> payload{std::byte{0xaa}};

    auto status = encoder.write_bool(false);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    status = encoder.write_unsigned(0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    status = encoder.write_signed(0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    status = encoder.finalize(payload);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    ASSERT_EQ(payload.size(), 1u);
    EXPECT_EQ(payload[0], std::byte{0xaa});
}

TEST(RangeEncoderTest, FinalizeSealsEncoderUntilReset)
{
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset().ok());
    ASSERT_TRUE(encoder.write_bool(true).ok());
    std::vector<std::byte> payload;
    ASSERT_TRUE(encoder.finalize(payload).ok());
    EXPECT_TRUE(encoder.finalized());

    auto status = encoder.write_bool(false);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    status = encoder.write_unsigned(0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    status = encoder.write_signed(0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    status = encoder.finalize(payload);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);

    ASSERT_TRUE(encoder.reset().ok());
    EXPECT_FALSE(encoder.finalized());
    EXPECT_EQ(encoder.byte_count(), 2u);
    ASSERT_TRUE(encoder.write_bool(false).ok());
    ASSERT_TRUE(encoder.finalize(payload).ok());
}

TEST(RangeEncoderTest, UnsupportedScalarWritesPreserveBinaryState)
{
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset().ok());
    ASSERT_TRUE(encoder.write_bool(false).ok());
    const auto byte_count = encoder.byte_count();

    auto status = encoder.write_unsigned(42);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::NotImplemented);
    EXPECT_EQ(encoder.byte_count(), byte_count);
    status = encoder.write_signed(-42);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::NotImplemented);
    EXPECT_EQ(encoder.byte_count(), byte_count);
    ASSERT_TRUE(encoder.write_bool(true).ok());

    std::vector<std::byte> payload;
    ASSERT_TRUE(encoder.finalize(payload).ok());
    mffv1::entropy::RangeCoder decoder;
    ASSERT_TRUE(decoder.reset(payload).ok());
    bool value = true;
    ASSERT_TRUE(decoder.read_bool(value).ok());
    EXPECT_FALSE(value);
    ASSERT_TRUE(decoder.read_bool(value).ok());
    EXPECT_TRUE(value);
}

} // namespace
