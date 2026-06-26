#include "entropy/range_encoder.hpp"

#include "entropy/range_coder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
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
    EXPECT_EQ(status.message, "binary value has a zero-width range in the current state");
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
    EXPECT_EQ(status.message, "range encoder is not initialized");
    status = encoder.write_unsigned(0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "range encoder is not initialized");
    status = encoder.write_signed(0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "range encoder is not initialized");
    status = encoder.finalize(payload);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "range encoder is not initialized");
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
    EXPECT_EQ(status.message, "range encoder is finalized");
    status = encoder.write_unsigned(0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "range encoder is finalized");
    status = encoder.write_signed(0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "range encoder is finalized");
    status = encoder.finalize(payload);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "range encoder is already finalized");

    ASSERT_TRUE(encoder.reset().ok());
    EXPECT_FALSE(encoder.finalized());
    EXPECT_EQ(encoder.byte_count(), 2u);
    ASSERT_TRUE(encoder.write_bool(false).ok());
    ASSERT_TRUE(encoder.finalize(payload).ok());
}

TEST(RangeEncoderTest, RoundTripsUnsignedScalarValues)
{
    const std::array<std::uint64_t, 12> values{
        0,
        1,
        2,
        3,
        7,
        8,
        255,
        256,
        65'535,
        65'536,
        std::uint64_t{1} << 40,
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
    };
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset().ok());
    for (const auto value : values) {
        ASSERT_TRUE(encoder.write_unsigned(value).ok()) << value;
    }

    std::vector<std::byte> payload;
    ASSERT_TRUE(encoder.finalize(payload).ok());
    mffv1::entropy::RangeCoder decoder;
    ASSERT_TRUE(decoder.reset(payload).ok());
    for (const auto expected : values) {
        std::uint64_t actual = 0;
        ASSERT_TRUE(decoder.read_unsigned(actual).ok()) << expected;
        EXPECT_EQ(actual, expected);
    }
}

TEST(RangeEncoderTest, RoundTripsSignedScalarValues)
{
    const std::array<std::int64_t, 15> values{
        0,
        1,
        -1,
        2,
        -2,
        255,
        -255,
        256,
        -256,
        65'535,
        -65'535,
        std::int64_t{1} << 40,
        -(std::int64_t{1} << 40),
        std::numeric_limits<std::int64_t>::max(),
        -std::numeric_limits<std::int64_t>::max(),
    };
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset().ok());
    for (const auto value : values) {
        ASSERT_TRUE(encoder.write_signed(value).ok()) << value;
    }

    std::vector<std::byte> payload;
    ASSERT_TRUE(encoder.finalize(payload).ok());
    mffv1::entropy::RangeCoder decoder;
    ASSERT_TRUE(decoder.reset(payload).ok());
    for (const auto expected : values) {
        std::int64_t actual = 0;
        ASSERT_TRUE(decoder.read_signed(actual).ok()) << expected;
        EXPECT_EQ(actual, expected);
    }
}

TEST(RangeEncoderTest, EncodesSelectedContextBanks)
{
    const std::array<std::size_t, 2> context_counts{1, 2};
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset(context_counts).ok());
    ASSERT_TRUE(encoder.write_signed(1, 1, -17).ok());
    ASSERT_TRUE(encoder.write_unsigned(0, 0, 23).ok());
    ASSERT_TRUE(encoder.write_signed(1, 0, 0).ok());

    std::vector<std::byte> payload;
    ASSERT_TRUE(encoder.finalize(payload).ok());
    mffv1::entropy::RangeCoder decoder;
    ASSERT_TRUE(decoder.reset(payload, context_counts).ok());
    std::int64_t signed_value = 0;
    std::uint64_t unsigned_value = 0;
    ASSERT_TRUE(decoder.read_signed(1, 1, signed_value).ok());
    EXPECT_EQ(signed_value, -17);
    ASSERT_TRUE(decoder.read_unsigned(0, 0, unsigned_value).ok());
    EXPECT_EQ(unsigned_value, 23u);
    ASSERT_TRUE(decoder.read_signed(1, 0, signed_value).ok());
    EXPECT_EQ(signed_value, 0);
}

TEST(RangeEncoderTest, UsesCustomInitialStates)
{
    const std::array<std::size_t, 1> context_counts{1};
    std::array<mffv1::entropy::RangeEncoder::ScalarContextStates, 1> states{};
    states[0].fill(mffv1::entropy::RangeEncoder::kDefaultInitialState);
    states[0][0] = 200;
    states[0][1] = 180;
    states[0][22] = 160;
    const std::array<
        std::span<const mffv1::entropy::RangeEncoder::ScalarContextStates>,
        1> state_banks{states};
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset(
        context_counts, state_banks, mffv1::syntax::kDefaultStateTransition).ok());
    ASSERT_TRUE(encoder.write_unsigned(3).ok());

    std::vector<std::byte> payload;
    ASSERT_TRUE(encoder.finalize(payload).ok());
    mffv1::entropy::RangeCoder decoder;
    ASSERT_TRUE(decoder.reset(payload, context_counts, state_banks).ok());
    std::uint64_t value = 0;
    ASSERT_TRUE(decoder.read_unsigned(value).ok());
    EXPECT_EQ(value, 3u);
}

TEST(RangeEncoderTest, ReconfiguresContextsWithoutResettingArithmeticState)
{
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset().ok());
    ASSERT_TRUE(encoder.write_bool(true).ok());
    const auto byte_count = encoder.byte_count();
    const std::array<std::size_t, 2> context_counts{1, 2};
    ASSERT_TRUE(encoder.reconfigure_contexts(context_counts).ok());
    EXPECT_EQ(encoder.byte_count(), byte_count);
    ASSERT_TRUE(encoder.write_signed(1, 1, -9).ok());

    std::vector<std::byte> payload;
    ASSERT_TRUE(encoder.finalize(payload).ok());
    mffv1::entropy::RangeCoder decoder;
    ASSERT_TRUE(decoder.reset(payload).ok());
    bool flag = false;
    ASSERT_TRUE(decoder.read_bool(flag).ok());
    EXPECT_TRUE(flag);
    ASSERT_TRUE(decoder.reconfigure_contexts(context_counts).ok());
    std::int64_t value = 0;
    ASSERT_TRUE(decoder.read_signed(1, 1, value).ok());
    EXPECT_EQ(value, -9);
}

TEST(RangeEncoderTest, CopiesUpdatedContextBanks)
{
    const std::array<std::size_t, 2> context_counts{1, 2};
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset(context_counts).ok());
    ASSERT_TRUE(encoder.write_signed(1, 1, -17).ok());
    mffv1::entropy::RangeEncoder::ContextStateBanks context_banks;

    const auto status = encoder.copy_contexts(context_banks);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(context_banks.size(), 2u);
    ASSERT_EQ(context_banks[0].size(), 1u);
    ASSERT_EQ(context_banks[1].size(), 2u);
    EXPECT_EQ(context_banks[0][0][0],
              mffv1::entropy::RangeEncoder::kDefaultInitialState);
    EXPECT_NE(context_banks[1][1][0],
              mffv1::entropy::RangeEncoder::kDefaultInitialState);
}

TEST(RangeEncoderTest, RejectsUnrepresentableScalarValuesWithoutChangingState)
{
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset().ok());
    ASSERT_TRUE(encoder.write_bool(false).ok());
    const auto byte_count = encoder.byte_count();
    mffv1::entropy::RangeEncoder::ContextStateBanks contexts_before;
    ASSERT_TRUE(encoder.copy_contexts(contexts_before).ok());

    auto status = encoder.write_unsigned(
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "unsigned value exceeds the range coder scalar limit");
    status = encoder.write_signed(std::numeric_limits<std::int64_t>::min());
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "signed value magnitude exceeds the range coder scalar limit");
    EXPECT_EQ(encoder.byte_count(), byte_count);
    mffv1::entropy::RangeEncoder::ContextStateBanks contexts_after;
    ASSERT_TRUE(encoder.copy_contexts(contexts_after).ok());
    EXPECT_EQ(contexts_after, contexts_before);
}

TEST(RangeEncoderTest, RejectsInvalidContextSelectionWithoutChangingState)
{
    const std::array<std::size_t, 2> context_counts{1, 2};
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset(context_counts).ok());
    ASSERT_TRUE(encoder.write_bool(false).ok());
    const auto byte_count = encoder.byte_count();

    auto status = encoder.write_unsigned(2, 0, 1);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "range encoder scalar context bank is out of range");
    status = encoder.write_signed(0, 1, -1);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "range encoder scalar context is out of range");
    EXPECT_EQ(encoder.byte_count(), byte_count);
}

TEST(RangeEncoderTest, FailedScalarWriteRollsBackArithmeticAndContextState)
{
    const std::array<std::size_t, 1> context_counts{1};
    std::array<mffv1::entropy::RangeEncoder::ScalarContextStates, 1> states{};
    states[0].fill(mffv1::entropy::RangeEncoder::kDefaultInitialState);
    states[0][1] = 0;
    const std::array<
        std::span<const mffv1::entropy::RangeEncoder::ScalarContextStates>,
        1> state_banks{states};
    mffv1::entropy::RangeEncoder encoder;
    ASSERT_TRUE(encoder.reset(
        context_counts, state_banks, mffv1::syntax::kDefaultStateTransition).ok());
    mffv1::entropy::RangeEncoder::ContextStateBanks contexts_before;
    ASSERT_TRUE(encoder.copy_contexts(contexts_before).ok());

    const auto status = encoder.write_unsigned(2);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message,
              "scalar symbol enters a zero-width range in the current context state");
    mffv1::entropy::RangeEncoder::ContextStateBanks contexts_after;
    ASSERT_TRUE(encoder.copy_contexts(contexts_after).ok());
    EXPECT_EQ(contexts_after, contexts_before);
    ASSERT_TRUE(encoder.write_unsigned(0).ok());

    std::vector<std::byte> payload;
    ASSERT_TRUE(encoder.finalize(payload).ok());
    mffv1::entropy::RangeCoder decoder;
    ASSERT_TRUE(decoder.reset(payload, context_counts, state_banks).ok());
    std::uint64_t value = 99;
    ASSERT_TRUE(decoder.read_unsigned(value).ok());
    EXPECT_EQ(value, 0u);
}

} // namespace
