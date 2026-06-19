#include "entropy/golomb_rice_run.hpp"

#include <array>
#include <cstddef>

#include <gtest/gtest.h>

namespace {

TEST(GolombRiceRunTest, ReadsFullSegmentAndIncrementsIndex)
{
    const std::array bytes{std::byte{0x80}};
    mffv1::bitstream::BitReader reader(bytes);
    mffv1::entropy::GolombRiceRunState state;
    mffv1::entropy::GolombRiceRunSegment segment;

    const auto status = mffv1::entropy::read_golomb_rice_run_segment(reader,
                                                                    state,
                                                                    0,
                                                                    8,
                                                                    segment);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(segment.count, 1u);
    EXPECT_FALSE(segment.interrupted);
    EXPECT_EQ(state.run_index, 1u);
    EXPECT_EQ(reader.bit_position(), 1u);
}

TEST(GolombRiceRunTest, DoesNotIncrementIndexWhenFullSegmentCrossesRowEnd)
{
    const std::array bytes{std::byte{0x80}};
    mffv1::bitstream::BitReader reader(bytes);
    mffv1::entropy::GolombRiceRunState state{4};
    mffv1::entropy::GolombRiceRunSegment segment;

    const auto status = mffv1::entropy::read_golomb_rice_run_segment(reader,
                                                                    state,
                                                                    4,
                                                                    5,
                                                                    segment);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(segment.count, 2u);
    EXPECT_FALSE(segment.interrupted);
    EXPECT_EQ(state.run_index, 4u);
}

TEST(GolombRiceRunTest, ReadsRemainderAndLeavesRunMode)
{
    const std::array bytes{std::byte{0x40}}; // index 4: 0 1
    mffv1::bitstream::BitReader reader(bytes);
    mffv1::entropy::GolombRiceRunState state{4};
    mffv1::entropy::GolombRiceRunSegment segment;

    const auto status = mffv1::entropy::read_golomb_rice_run_segment(reader,
                                                                    state,
                                                                    0,
                                                                    8,
                                                                    segment);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(segment.count, 1u);
    EXPECT_TRUE(segment.interrupted);
    EXPECT_EQ(state.run_index, 3u);
    EXPECT_EQ(reader.bit_position(), 2u);
}

TEST(GolombRiceRunTest, ReadsZeroRemainderAtInitialIndex)
{
    const std::array bytes{std::byte{0x00}};
    mffv1::bitstream::BitReader reader(bytes);
    mffv1::entropy::GolombRiceRunState state;
    mffv1::entropy::GolombRiceRunSegment segment{7, false};

    const auto status = mffv1::entropy::read_golomb_rice_run_segment(reader,
                                                                    state,
                                                                    0,
                                                                    8,
                                                                    segment);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(segment.count, 0u);
    EXPECT_TRUE(segment.interrupted);
    EXPECT_EQ(state.run_index, 0u);
    EXPECT_EQ(reader.bit_position(), 1u);
}

TEST(GolombRiceRunTest, LeavesStateAndOutputUnchangedOnTruncatedRemainder)
{
    const std::array bytes{std::byte{0x00}};
    mffv1::bitstream::BitReader reader(bytes);
    ASSERT_TRUE(reader.skip_bits(7).ok());
    mffv1::entropy::GolombRiceRunState state{4};
    mffv1::entropy::GolombRiceRunSegment segment{7, false};

    const auto status = mffv1::entropy::read_golomb_rice_run_segment(reader,
                                                                    state,
                                                                    0,
                                                                    8,
                                                                    segment);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(state.run_index, 4u);
    EXPECT_EQ(segment.count, 7u);
    EXPECT_FALSE(segment.interrupted);
}

TEST(GolombRiceRunTest, RejectsInvalidArgumentsWithoutReading)
{
    const std::array bytes{std::byte{0xff}};
    mffv1::bitstream::BitReader reader(bytes);
    mffv1::entropy::GolombRiceRunSegment segment;

    mffv1::entropy::GolombRiceRunState state{41};
    auto status = mffv1::entropy::read_golomb_rice_run_segment(reader, state, 0, 8, segment);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(reader.bit_position(), 0u);

    state.reset();
    status = mffv1::entropy::read_golomb_rice_run_segment(reader, state, 9, 8, segment);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(reader.bit_position(), 0u);
}

} // namespace
