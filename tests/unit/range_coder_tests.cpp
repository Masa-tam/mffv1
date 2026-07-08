#include "entropy/range_coder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

TEST(RangeCoderTest, RejectsTooShortPayload)
{
    const std::array<std::byte, 1> payload{std::byte{0}};
    mffv1::entropy::RangeCoder coder;

    const auto status = coder.reset(payload);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "range coder payload must contain at least two bytes");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(RangeCoderTest, DecodesLowInitialStateAsFalseBinary)
{
    const std::array<std::byte, 2> payload{
        std::byte{0x00},
        std::byte{0x00},
    };
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload).ok());

    bool value = true;
    EXPECT_TRUE(coder.read_bool(value).ok());
    EXPECT_FALSE(value);
    EXPECT_EQ(coder.byte_position(), 2u);
}

TEST(RangeCoderTest, DecodesHighInitialStateAsTrueBinary)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload).ok());

    bool value = false;
    EXPECT_TRUE(coder.read_bool(value).ok());
    EXPECT_TRUE(value);
}

TEST(RangeCoderTest, CustomStateTransitionChangesBinaryDecoding)
{
    auto custom_transition = mffv1::syntax::kDefaultStateTransition;
    custom_transition[128] = 0;
    bool observed_difference = false;

    for (std::uint32_t low = 0; low < 0xff00 && !observed_difference; low += 257) {
        const std::array<std::byte, 2> payload{
            static_cast<std::byte>((low >> 8) & 0xffu),
            static_cast<std::byte>(low & 0xffu),
        };
        mffv1::entropy::RangeCoder default_coder;
        mffv1::entropy::RangeCoder custom_coder;
        ASSERT_TRUE(default_coder.reset(payload).ok());
        ASSERT_TRUE(custom_coder.reset(payload, custom_transition).ok());

        for (int bit_index = 0; bit_index < 8; ++bit_index) {
            bool default_bit = false;
            bool custom_bit = false;
            ASSERT_TRUE(default_coder.read_bool(default_bit).ok());
            ASSERT_TRUE(custom_coder.read_bool(custom_bit).ok());
            if (default_bit != custom_bit) {
                observed_difference = true;
                break;
            }
        }
    }

    EXPECT_TRUE(observed_difference);
}

TEST(RangeCoderTest, DecodesHighInitialStateAsZeroUnsignedSymbol)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload).ok());

    std::uint64_t value = 99;
    EXPECT_TRUE(coder.read_unsigned(value).ok());
    EXPECT_EQ(value, 0u);
    EXPECT_EQ(coder.byte_position(), 2u);
}

TEST(RangeCoderTest, BytePositionAdvancesAfterRefill)
{
    const std::array<std::byte, 4> payload{
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0xaa},
        std::byte{0xbb},
    };
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload).ok());

    bool value = true;
    for (int i = 0; i < 64 && coder.byte_position() == 2; ++i) {
        EXPECT_TRUE(coder.read_bool(value).ok());
    }

    EXPECT_GT(coder.byte_position(), 2u);
}

TEST(RangeCoderTest, ExposesArithmeticStateForDiagnostics)
{
    const std::array<std::byte, 3> payload{
        std::byte{0x7f},
        std::byte{0x80},
        std::byte{0x55},
    };
    mffv1::entropy::RangeCoder coder;

    const auto initial_state = coder.arithmetic_state();

    EXPECT_FALSE(initial_state.initialized);
    EXPECT_EQ(initial_state.range, 0u);
    EXPECT_EQ(initial_state.low, 0u);
    EXPECT_EQ(initial_state.byte_position, 0u);
    EXPECT_FALSE(initial_state.end);

    ASSERT_TRUE(coder.reset(payload).ok());
    const auto reset_state = coder.arithmetic_state();

    EXPECT_TRUE(reset_state.initialized);
    EXPECT_EQ(reset_state.range, 0xff00u);
    EXPECT_EQ(reset_state.low, 0x7f80u);
    EXPECT_EQ(reset_state.byte_position, 2u);
    EXPECT_FALSE(reset_state.end);

    bool value = false;
    ASSERT_TRUE(coder.read_bool(value).ok());
    const auto advanced_state = coder.arithmetic_state();

    EXPECT_TRUE(advanced_state.initialized);
    EXPECT_NE(advanced_state, reset_state);
    EXPECT_EQ(advanced_state.byte_position, coder.byte_position());
}

TEST(RangeCoderTest, ContextReconfigurationKeepsArithmeticState)
{
    const std::array<std::byte, 4> payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0xaa},
        std::byte{0xbb},
    };
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload).ok());

    bool frame_flag = false;
    ASSERT_TRUE(coder.read_bool(frame_flag).ok());
    const auto before_reconfigure = coder.arithmetic_state();
    const std::array<std::size_t, 2> context_counts{1, 2};

    ASSERT_TRUE(coder.reconfigure_contexts(context_counts).ok());
    const auto after_reconfigure = coder.arithmetic_state();

    EXPECT_EQ(after_reconfigure, before_reconfigure);
}

TEST(RangeCoderTest, DecodesHighInitialStateAsZeroSignedSymbol)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload).ok());

    std::int64_t value = 99;
    EXPECT_TRUE(coder.read_signed(value).ok());
    EXPECT_EQ(value, 0);
}

TEST(RangeCoderTest, RejectsOutOfRangeScalarContext)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload, 1).ok());

    std::uint64_t value = 0;
    const auto status = coder.read_unsigned(1, value);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "range coder scalar context is out of range");
}

TEST(RangeCoderTest, RejectsExcessiveScalarContextCount)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    mffv1::entropy::RangeCoder coder;

    const auto status = coder.reset(
        payload, mffv1::entropy::RangeCoder::kMaxScalarContextCount + 1);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "range coder scalar context count exceeds the supported limit");
}

TEST(RangeCoderTest, DecodesFromSelectedContextBank)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const std::array<std::size_t, 2> context_counts{1, 2};
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload, context_counts).ok());

    std::int64_t value = 99;
    EXPECT_TRUE(coder.read_signed(1, 1, value).ok());
    EXPECT_EQ(value, 0);
}

TEST(RangeCoderTest, UsesCustomInitialStatesForSelectedContextBank)
{
    const std::array<std::byte, 2> payload{
        std::byte{0x01},
        std::byte{0x00},
    };
    const std::array<std::size_t, 2> context_counts{1, 1};
    std::array<mffv1::entropy::RangeCoder::ScalarContextStates, 1> default_states{};
    default_states[0].fill(mffv1::entropy::RangeCoder::kDefaultInitialState);
    std::array<mffv1::entropy::RangeCoder::ScalarContextStates, 1> custom_states = default_states;
    custom_states[0][0] = 255;
    const std::array<std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>, 2> state_banks{
        default_states,
        custom_states,
    };
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload, context_counts, state_banks).ok());

    std::uint64_t value = 99;
    EXPECT_TRUE(coder.read_unsigned(1, 0, value).ok());
    EXPECT_EQ(value, 0u);
}

TEST(RangeCoderTest, RejectsMismatchedInitialStateCount)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const std::array<std::size_t, 1> context_counts{2};
    std::array<mffv1::entropy::RangeCoder::ScalarContextStates, 1> states{};
    const std::array<std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>, 1> state_banks{states};
    mffv1::entropy::RangeCoder coder;

    const auto status = coder.reset(payload, context_counts, state_banks);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message,
              "range coder initial state count does not match scalar context count");
}

TEST(RangeCoderTest, RejectsOutOfRangeContextBank)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const std::array<std::size_t, 2> context_counts{1, 2};
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload, context_counts).ok());

    std::uint64_t value = 0;
    const auto status = coder.read_unsigned(2, 0, value);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "range coder scalar context bank is out of range");
}

TEST(RangeCoderTest, RejectsContextOutsideSelectedBank)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const std::array<std::size_t, 2> context_counts{1, 2};
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload, context_counts).ok());

    std::uint64_t value = 0;
    const auto status = coder.read_unsigned(0, 1, value);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "range coder scalar context is out of range");
}

TEST(RangeCoderTest, RejectsExcessiveContextBankCount)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const std::array<std::size_t, mffv1::entropy::RangeCoder::kMaxContextBankCount + 1> context_counts{
        1, 1, 1, 1, 1,
    };
    mffv1::entropy::RangeCoder coder;

    const auto status = coder.reset(payload, context_counts);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "range coder context bank count exceeds the supported limit");
}

TEST(RangeCoderTest, ReconfiguresContextsWithoutResettingArithmeticPosition)
{
    const std::array<std::byte, 4> payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0xaa},
        std::byte{0xbb},
    };
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload).ok());

    bool frame_flag = false;
    ASSERT_TRUE(coder.read_bool(frame_flag).ok());
    const auto position = coder.byte_position();
    const std::array<std::size_t, 2> context_counts{1, 2};

    const auto status = coder.reconfigure_contexts(context_counts);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(coder.byte_position(), position);
    std::int64_t value = 99;
    EXPECT_TRUE(coder.read_signed(1, 1, value).ok());
}

TEST(RangeCoderTest, BinarySymbolsShareScalarContextZero)
{
    const std::array<std::byte, 2> payload{
        std::byte{0x7f},
        std::byte{0x80},
    };
    mffv1::entropy::RangeCoder bool_coder;
    mffv1::entropy::RangeCoder scalar_coder;
    ASSERT_TRUE(bool_coder.reset(payload).ok());
    ASSERT_TRUE(scalar_coder.reset(payload).ok());

    bool bool_value = false;
    std::uint64_t scalar_value = 0;
    ASSERT_TRUE(bool_coder.read_bool(bool_value).ok());
    ASSERT_TRUE(scalar_coder.read_unsigned(scalar_value).ok());

    EXPECT_TRUE(bool_value);
    EXPECT_EQ(scalar_value, 0u);
    mffv1::entropy::RangeCoder::ContextStateBanks bool_contexts;
    mffv1::entropy::RangeCoder::ContextStateBanks scalar_contexts;
    ASSERT_TRUE(bool_coder.copy_contexts(bool_contexts).ok());
    ASSERT_TRUE(scalar_coder.copy_contexts(scalar_contexts).ok());
    ASSERT_EQ(bool_contexts.size(), 1u);
    ASSERT_EQ(scalar_contexts.size(), 1u);
    EXPECT_EQ(bool_contexts[0][0], scalar_contexts[0][0]);
}

TEST(RangeCoderTest, IndependentScalarContextScopeRestoresOuterContexts)
{
    const std::array<std::byte, 4> payload{
        std::byte{0x7f},
        std::byte{0x80},
        std::byte{0x00},
        std::byte{0x00},
    };
    mffv1::entropy::RangeCoder scoped;
    mffv1::entropy::RangeCoder reference;
    ASSERT_TRUE(scoped.reset(payload).ok());
    ASSERT_TRUE(reference.reset(payload).ok());

    bool scoped_value = false;
    bool reference_value = false;
    ASSERT_TRUE(scoped.read_bool(scoped_value).ok());
    ASSERT_TRUE(reference.read_bool(reference_value).ok());
    ASSERT_TRUE(scoped.begin_independent_scalar_contexts(1).ok());
    bool ignored = false;
    ASSERT_TRUE(scoped.read_bool(ignored).ok());
    ASSERT_TRUE(scoped.end_independent_scalar_contexts().ok());
    ASSERT_TRUE(reference.begin_independent_scalar_contexts(1).ok());
    ASSERT_TRUE(reference.read_bool(ignored).ok());
    ASSERT_TRUE(reference.end_independent_scalar_contexts().ok());

    mffv1::entropy::RangeCoder::ContextStateBanks scoped_contexts;
    mffv1::entropy::RangeCoder::ContextStateBanks reference_contexts;
    ASSERT_TRUE(scoped.copy_contexts(scoped_contexts).ok());
    ASSERT_TRUE(reference.copy_contexts(reference_contexts).ok());
    EXPECT_EQ(scoped_contexts, reference_contexts);
    EXPECT_EQ(scoped.byte_position(), reference.byte_position());
}

TEST(RangeCoderTest, RejectsContextReconfigurationBeforeReset)
{
    mffv1::entropy::RangeCoder coder;
    const std::array<std::size_t, 1> context_counts{1};

    const auto status = coder.reconfigure_contexts(context_counts);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "range coder is not initialized");
}

TEST(RangeCoderTest, FailedContextReconfigurationPreservesExistingBanks)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const std::array<std::size_t, 1> original_counts{1};
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload, original_counts).ok());
    const std::array<std::size_t, 1> invalid_counts{2};
    std::array<mffv1::entropy::RangeCoder::ScalarContextStates, 1> states{};
    const std::array<std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>, 1> state_banks{states};

    const auto status = coder.reconfigure_contexts(invalid_counts, state_banks);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message,
              "range coder initial state count does not match scalar context count");
    std::int64_t value = 99;
    EXPECT_TRUE(coder.read_signed(0, 0, value).ok());
}

TEST(RangeCoderTest, CopiesUpdatedContextBanksForContinuation)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const std::array<std::size_t, 2> context_counts{1, 2};
    mffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload, context_counts).ok());

    std::int64_t value = 99;
    ASSERT_TRUE(coder.read_signed(1, 1, value).ok());
    mffv1::entropy::RangeCoder::ContextStateBanks context_banks;

    const auto status = coder.copy_contexts(context_banks);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(context_banks.size(), 2u);
    ASSERT_EQ(context_banks[0].size(), 1u);
    ASSERT_EQ(context_banks[1].size(), 2u);
    EXPECT_EQ(context_banks[0][0][0], mffv1::entropy::RangeCoder::kDefaultInitialState);
    EXPECT_EQ(context_banks[1][0][0], mffv1::entropy::RangeCoder::kDefaultInitialState);
    EXPECT_NE(context_banks[1][1][0], mffv1::entropy::RangeCoder::kDefaultInitialState);
}

TEST(RangeCoderTest, RejectsContextCopyBeforeResetWithoutChangingOutput)
{
    mffv1::entropy::RangeCoder coder;
    mffv1::entropy::RangeCoder::ContextStateBanks context_banks(1);
    context_banks[0].resize(2);

    const auto status = coder.copy_contexts(context_banks);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "range coder is not initialized");
    ASSERT_EQ(context_banks.size(), 1u);
    EXPECT_EQ(context_banks[0].size(), 2u);
}

TEST(RangeCoderTest, RestoresCopiedContextBanksForNewPayload)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const std::array<std::size_t, 2> context_counts{1, 2};
    mffv1::entropy::RangeCoder source;
    ASSERT_TRUE(source.reset(payload, context_counts).ok());
    std::int64_t value = 99;
    ASSERT_TRUE(source.read_signed(1, 1, value).ok());
    mffv1::entropy::RangeCoder::ContextStateBanks saved_contexts;
    ASSERT_TRUE(source.copy_contexts(saved_contexts).ok());

    mffv1::entropy::RangeCoder restored;
    const auto status = restored.reset_from_contexts(payload, saved_contexts);
    ASSERT_TRUE(status.ok()) << status.message;
    mffv1::entropy::RangeCoder::ContextStateBanks restored_contexts;
    ASSERT_TRUE(restored.copy_contexts(restored_contexts).ok());

    EXPECT_EQ(restored_contexts, saved_contexts);
    EXPECT_EQ(restored.byte_position(), 2u);
}

TEST(RangeCoderTest, RestoresArithmeticStateForContinuation)
{
    const std::array<std::byte, 4> payload{
        std::byte{0x7f},
        std::byte{0x80},
        std::byte{0xaa},
        std::byte{0xbb},
    };
    const std::array<std::size_t, 1> context_counts{1};
    mffv1::entropy::RangeCoder source;
    ASSERT_TRUE(source.reset(payload, context_counts).ok());
    bool first_bit = false;
    ASSERT_TRUE(source.read_bool(first_bit).ok());
    const auto saved_state = source.arithmetic_state();
    mffv1::entropy::RangeCoder::ContextStateBanks saved_contexts;
    ASSERT_TRUE(source.copy_contexts(saved_contexts).ok());

    mffv1::entropy::RangeCoder restored;
    const std::array<std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>, 1>
        initial_state_banks{std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>(
            saved_contexts[0].data(), saved_contexts[0].size())};
    ASSERT_TRUE(restored.reset_from_arithmetic_state(
        payload,
        context_counts,
        initial_state_banks,
        mffv1::syntax::kDefaultStateTransition,
        saved_state).ok());

    bool source_next = false;
    bool restored_next = false;
    ASSERT_TRUE(source.read_bool(source_next).ok());
    ASSERT_TRUE(restored.read_bool(restored_next).ok());

    EXPECT_EQ(restored_next, source_next);
    EXPECT_EQ(restored.arithmetic_state(), source.arithmetic_state());
}

TEST(RangeCoderTest, RejectsInvalidArithmeticStateRestore)
{
    const std::array<std::byte, 2> payload{
        std::byte{0x7f},
        std::byte{0x80},
    };
    const std::array<std::size_t, 1> context_counts{1};
    mffv1::entropy::RangeCoder coder;

    auto status = coder.reset_from_arithmetic_state(
        payload,
        context_counts,
        {},
        mffv1::syntax::kDefaultStateTransition,
        {});

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "range coder arithmetic state is not initialized");

    mffv1::entropy::RangeCoder::ArithmeticState state;
    state.initialized = true;
    state.range = 1;
    state.byte_position = 3;
    status = coder.reset_from_arithmetic_state(
        payload,
        context_counts,
        {},
        mffv1::syntax::kDefaultStateTransition,
        state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message,
              "range coder arithmetic byte position is outside the payload");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 3u);
}

TEST(RangeCoderTest, RejectsLegacyV0ArithmeticModeBeforeReset)
{
    mffv1::entropy::RangeCoder coder;

    const auto status = coder.set_legacy_v0_arithmetic(true);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "range coder is not initialized");
}

TEST(RangeCoderTest, RejectsLegacyZeroSymbolCarryModeBeforeReset)
{
    mffv1::entropy::RangeCoder coder;

    const auto status = coder.set_legacy_zero_symbol_carry(true);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "range coder is not initialized");
}

TEST(RangeCoderTest, LegacyZeroSymbolCarryAdvancesLowAfterZeroScalar)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xfe},
        std::byte{0xff},
    };

    mffv1::entropy::RangeCoder normal;
    ASSERT_TRUE(normal.reset(payload).ok());
    std::int64_t normal_value = 1;
    ASSERT_TRUE(normal.read_signed(normal_value).ok());

    mffv1::entropy::RangeCoder legacy;
    ASSERT_TRUE(legacy.reset(payload).ok());
    ASSERT_TRUE(legacy.set_legacy_zero_symbol_carry(true).ok());
    std::int64_t legacy_value = 1;
    ASSERT_TRUE(legacy.read_signed(legacy_value).ok());

    EXPECT_EQ(normal_value, 0);
    EXPECT_EQ(legacy_value, 0);
    EXPECT_EQ(legacy.arithmetic_state().range, normal.arithmetic_state().range);
    EXPECT_EQ(legacy.arithmetic_state().byte_position,
              normal.arithmetic_state().byte_position);
    EXPECT_EQ(legacy.arithmetic_state().low, normal.arithmetic_state().low + 1);
}

TEST(RangeCoderTest, LegacyV0ArithmeticModeUsesCeilSplitAndOnePathCarry)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xd3},
        std::byte{0x20},
    };
    const std::array<std::size_t, 1> context_counts{1};
    mffv1::entropy::RangeCoder::ScalarContextStates states{};
    states.fill(mffv1::entropy::RangeCoder::kDefaultInitialState);
    states[0] = 255;
    const std::array initial_state_banks{
        std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>{&states, 1},
    };
    mffv1::entropy::RangeCoder::ArithmeticState state;
    state.range = 0xd3fu;
    state.low = 0x20u;
    state.byte_position = 2;
    state.initialized = true;

    mffv1::entropy::RangeCoder normal;
    ASSERT_TRUE(normal.reset_from_arithmetic_state(
        payload,
        context_counts,
        initial_state_banks,
        mffv1::syntax::kDefaultStateTransition,
        state).ok());
    bool normal_bit = false;
    ASSERT_TRUE(normal.read_bool(normal_bit).ok());

    mffv1::entropy::RangeCoder legacy;
    ASSERT_TRUE(legacy.reset_from_arithmetic_state(
        payload,
        context_counts,
        initial_state_banks,
        mffv1::syntax::kDefaultStateTransition,
        state).ok());
    ASSERT_TRUE(legacy.set_legacy_v0_arithmetic(true).ok());
    bool legacy_bit = false;
    ASSERT_TRUE(legacy.read_bool(legacy_bit).ok());

    EXPECT_TRUE(normal_bit);
    EXPECT_TRUE(legacy_bit);
    EXPECT_EQ(normal.arithmetic_state().range, 0xd31u);
    EXPECT_EQ(normal.arithmetic_state().low, 0x12u);
    EXPECT_EQ(legacy.arithmetic_state().range, 0xd32u);
    EXPECT_EQ(legacy.arithmetic_state().low, 0x15u);
}

TEST(RangeCoderTest, RejectsEmptyRestoredContextBank)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const mffv1::entropy::RangeCoder::ContextStateBanks context_banks(1);
    mffv1::entropy::RangeCoder coder;

    const auto status = coder.reset_from_contexts(payload, context_banks);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message,
              "range coder context banks must have at least one scalar context");
}

} // namespace
