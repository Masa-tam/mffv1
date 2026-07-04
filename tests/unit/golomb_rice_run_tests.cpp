#include "entropy/golomb_rice_run.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

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

TEST(GolombRiceRunTest, ReadsFullSegmentEndingAtRowEndAndIncrementsIndex)
{
    const std::array bytes{std::byte{0x80}};
    mffv1::bitstream::BitReader reader(bytes);
    mffv1::entropy::GolombRiceRunState state{4};
    mffv1::entropy::GolombRiceRunSegment segment;

    const auto status = mffv1::entropy::read_golomb_rice_run_segment(reader,
                                                                    state,
                                                                    4,
                                                                    6,
                                                                    segment);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(segment.count, 2u);
    EXPECT_FALSE(segment.interrupted);
    EXPECT_EQ(state.run_index, 5u);
    EXPECT_EQ(state.pending_count, 0u);
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
    EXPECT_EQ(segment.count, 1u);
    EXPECT_FALSE(segment.interrupted);
    EXPECT_EQ(state.run_index, 4u);
    EXPECT_EQ(state.pending_count, 1u);
}

TEST(GolombRiceRunTest, CarriesFullSegmentRemainderAcrossRows)
{
    const std::array bytes{std::byte{0x80}};
    mffv1::bitstream::BitReader reader(bytes);
    mffv1::entropy::GolombRiceRunState state{4};
    mffv1::entropy::GolombRiceRunSegment segment;

    ASSERT_TRUE(mffv1::entropy::read_golomb_rice_run_segment(
        reader, state, 4, 5, segment).ok());
    EXPECT_EQ(segment.count, 1u);
    EXPECT_FALSE(segment.interrupted);
    EXPECT_EQ(state.pending_count, 1u);

    ASSERT_TRUE(mffv1::entropy::read_golomb_rice_run_segment(
        reader, state, 0, 5, segment).ok());
    EXPECT_EQ(segment.count, 1u);
    EXPECT_FALSE(segment.interrupted);
    EXPECT_EQ(state.pending_count, 0u);
    EXPECT_EQ(reader.bit_position(), 1u);
}

TEST(GolombRiceRunTest, ResetClearsPendingRunCount)
{
    mffv1::entropy::GolombRiceRunState state{4, 7};

    state.reset();

    EXPECT_EQ(state.run_index, 0u);
    EXPECT_EQ(state.pending_count, 0u);
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
    EXPECT_EQ(status.message, "bitstream underflow while reading bits");
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
    EXPECT_EQ(status.message, "Golomb-Rice run index is out of range");
    EXPECT_EQ(reader.bit_position(), 0u);

    state.reset();
    status = mffv1::entropy::read_golomb_rice_run_segment(reader, state, 9, 8, segment);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "Golomb-Rice run position is outside the row");
    EXPECT_EQ(reader.bit_position(), 0u);
}

TEST(GolombRiceRunTest, WritesFullSegmentAndIncrementsIndex)
{
    mffv1::bitstream::BitWriter writer;
    mffv1::entropy::GolombRiceRunState state;

    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
        writer, state, 0, 1, 1).ok());
    ASSERT_TRUE(writer.byte_align_zero().ok());
    std::vector<std::byte> bytes;
    ASSERT_TRUE(writer.finalize(bytes).ok());

    EXPECT_EQ(bytes, (std::vector<std::byte>{std::byte{0x80}}));
    EXPECT_EQ(state.run_index, 1u);
}

TEST(GolombRiceRunTest, WritesRemainderBeforeInterruption)
{
    mffv1::bitstream::BitWriter writer;
    mffv1::entropy::GolombRiceRunState state{4};

    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
        writer, state, 0, 8, 1).ok());
    ASSERT_TRUE(writer.byte_align_zero().ok());
    std::vector<std::byte> bytes;
    ASSERT_TRUE(writer.finalize(bytes).ok());

    EXPECT_EQ(bytes, (std::vector<std::byte>{std::byte{0x40}}));
    EXPECT_EQ(state.run_index, 3u);
}

TEST(GolombRiceRunTest, RoundTripsCompleteRuns)
{
    for (std::uint32_t width = 1; width <= 64; ++width) {
        for (std::uint32_t count = 0; count <= width; ++count) {
            mffv1::bitstream::BitWriter output;
            mffv1::entropy::GolombRiceRunState encode_state;
            ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
                output, encode_state, 0, width, count).ok());
            ASSERT_TRUE(output.byte_align_zero().ok());
            std::vector<std::byte> bytes;
            ASSERT_TRUE(output.finalize(bytes).ok());

            mffv1::bitstream::BitReader input(bytes);
            mffv1::entropy::GolombRiceRunState decode_state;
            std::uint32_t decoded_count = 0;
            bool interrupted = false;
            while (decoded_count < width && !interrupted) {
                mffv1::entropy::GolombRiceRunSegment segment;
                ASSERT_TRUE(mffv1::entropy::read_golomb_rice_run_segment(
                    input,
                    decode_state,
                    decoded_count,
                    width,
                    segment).ok());
                decoded_count += std::min(
                    segment.count, width - decoded_count);
                interrupted = segment.interrupted;
            }

            EXPECT_EQ(decoded_count, count);
            if (count < width) {
                EXPECT_TRUE(interrupted);
            }
            EXPECT_EQ(decode_state.run_index, encode_state.run_index);
        }
    }
}

TEST(GolombRiceRunTest, ContinuesRunIndexAcrossRows)
{
    mffv1::bitstream::BitWriter output;
    mffv1::entropy::GolombRiceRunState encode_state;
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
        output, encode_state, 0, 8, 8).ok());
    ASSERT_TRUE(mffv1::entropy::write_golomb_rice_run(
        output, encode_state, 0, 8, 3).ok());
    ASSERT_TRUE(output.byte_align_zero().ok());
    std::vector<std::byte> bytes;
    ASSERT_TRUE(output.finalize(bytes).ok());

    mffv1::bitstream::BitReader input(bytes);
    mffv1::entropy::GolombRiceRunState decode_state;
    for (const auto expected : {8u, 3u}) {
        std::uint32_t decoded = 0;
        bool interrupted = false;
        while (decoded < 8 && !interrupted) {
            mffv1::entropy::GolombRiceRunSegment segment;
            ASSERT_TRUE(mffv1::entropy::read_golomb_rice_run_segment(
                input, decode_state, decoded, 8, segment).ok());
            decoded += std::min(segment.count, 8u - decoded);
            interrupted = segment.interrupted;
        }
        EXPECT_EQ(decoded, expected);
    }
    EXPECT_EQ(decode_state.run_index, encode_state.run_index);
}

TEST(GolombRiceRunTest, RejectsInvalidRunWithoutWriting)
{
    mffv1::bitstream::BitWriter writer;
    mffv1::entropy::GolombRiceRunState state;

    auto status = mffv1::entropy::write_golomb_rice_run(
        writer, state, 9, 8, 0);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "Golomb-Rice run is outside the row");
    EXPECT_EQ(writer.bit_position(), 0u);

    status = mffv1::entropy::write_golomb_rice_run(
        writer, state, 7, 8, 2);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "Golomb-Rice run is outside the row");
    EXPECT_EQ(writer.bit_position(), 0u);

    state.run_index = 40;
    status = mffv1::entropy::write_golomb_rice_run(
        writer, state, 0, std::uint32_t{1} << 24, std::uint32_t{1} << 24);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "Golomb-Rice run index exceeds the supported table");
    EXPECT_EQ(writer.bit_position(), 0u);
    EXPECT_EQ(state.run_index, 40u);
}

} // namespace
