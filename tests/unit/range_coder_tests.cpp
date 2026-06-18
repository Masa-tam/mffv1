#include "entropy/range_coder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

TEST(RangeCoderTest, RejectsTooShortPayload)
{
    const std::array<std::byte, 1> payload{std::byte{0}};
    ffv1::entropy::RangeCoder coder;

    const auto status = coder.reset(payload);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(RangeCoderTest, DecodesLowInitialStateAsFalseBinary)
{
    const std::array<std::byte, 2> payload{
        std::byte{0x00},
        std::byte{0x00},
    };
    ffv1::entropy::RangeCoder coder;
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
    ffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload).ok());

    bool value = false;
    EXPECT_TRUE(coder.read_bool(value).ok());
    EXPECT_TRUE(value);
}

TEST(RangeCoderTest, CustomStateTransitionChangesBinaryDecoding)
{
    auto custom_transition = ffv1::syntax::kDefaultStateTransition;
    custom_transition[128] = 0;
    bool observed_difference = false;

    for (std::uint32_t low = 0; low < 0xff00 && !observed_difference; low += 257) {
        const std::array<std::byte, 2> payload{
            static_cast<std::byte>((low >> 8) & 0xffu),
            static_cast<std::byte>(low & 0xffu),
        };
        ffv1::entropy::RangeCoder default_coder;
        ffv1::entropy::RangeCoder custom_coder;
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
    ffv1::entropy::RangeCoder coder;
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
    ffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload).ok());

    bool value = true;
    for (int i = 0; i < 64 && coder.byte_position() == 2; ++i) {
        EXPECT_TRUE(coder.read_bool(value).ok());
    }

    EXPECT_GT(coder.byte_position(), 2u);
}

TEST(RangeCoderTest, DecodesHighInitialStateAsZeroSignedSymbol)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    ffv1::entropy::RangeCoder coder;
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
    ffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload, 1).ok());

    std::uint64_t value = 0;
    const auto status = coder.read_unsigned(1, value);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(RangeCoderTest, RejectsExcessiveScalarContextCount)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    ffv1::entropy::RangeCoder coder;

    const auto status = coder.reset(
        payload, ffv1::entropy::RangeCoder::kMaxScalarContextCount + 1);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::ResourceExhausted);
}

TEST(RangeCoderTest, DecodesFromSelectedContextBank)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const std::array<std::size_t, 2> context_counts{1, 2};
    ffv1::entropy::RangeCoder coder;
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
    std::array<ffv1::entropy::RangeCoder::ScalarContextStates, 1> default_states{};
    default_states[0].fill(ffv1::entropy::RangeCoder::kDefaultInitialState);
    std::array<ffv1::entropy::RangeCoder::ScalarContextStates, 1> custom_states = default_states;
    custom_states[0][0] = 255;
    const std::array<std::span<const ffv1::entropy::RangeCoder::ScalarContextStates>, 2> state_banks{
        default_states,
        custom_states,
    };
    ffv1::entropy::RangeCoder coder;
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
    std::array<ffv1::entropy::RangeCoder::ScalarContextStates, 1> states{};
    const std::array<std::span<const ffv1::entropy::RangeCoder::ScalarContextStates>, 1> state_banks{states};
    ffv1::entropy::RangeCoder coder;

    const auto status = coder.reset(payload, context_counts, state_banks);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(RangeCoderTest, RejectsOutOfRangeContextBank)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const std::array<std::size_t, 2> context_counts{1, 2};
    ffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload, context_counts).ok());

    std::uint64_t value = 0;
    const auto status = coder.read_unsigned(2, 0, value);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(RangeCoderTest, RejectsContextOutsideSelectedBank)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const std::array<std::size_t, 2> context_counts{1, 2};
    ffv1::entropy::RangeCoder coder;
    ASSERT_TRUE(coder.reset(payload, context_counts).ok());

    std::uint64_t value = 0;
    const auto status = coder.read_unsigned(0, 1, value);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(RangeCoderTest, RejectsExcessiveContextBankCount)
{
    const std::array<std::byte, 2> payload{
        std::byte{0xff},
        std::byte{0x00},
    };
    const std::array<std::size_t, ffv1::entropy::RangeCoder::kMaxContextBankCount + 1> context_counts{
        1, 1, 1, 1, 1,
    };
    ffv1::entropy::RangeCoder coder;

    const auto status = coder.reset(payload, context_counts);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::ResourceExhausted);
}

} // namespace
