#include "codec/slice_encode_executor.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

mffv1::syntax::StreamParameters make_stream(
    std::uint8_t bits_per_raw_sample = 8)
{
    mffv1::syntax::StreamParameters stream;
    stream.version = 3;
    stream.micro_version = 4;
    stream.width = 4;
    stream.height = 2;
    stream.bits_per_raw_sample = bits_per_raw_sample;
    stream.chroma_planes = false;
    stream.num_h_slices = 2;
    stream.num_v_slices = 2;
    stream.quant_table_sets.push_back(
        mffv1::syntax::make_zero_quant_table_set());
    stream.intra_only = true;
    return stream;
}

TEST(SliceEncodeExecutorTest, ResolvesAndCapsWorkerCount)
{
    const auto stream = make_stream();
    const mffv1::codec::SliceEncodeExecutor automatic(stream, 0);
    const mffv1::codec::SliceEncodeExecutor serial(stream, 1);
    const mffv1::codec::SliceEncodeExecutor parallel(stream, 8);

    EXPECT_GE(automatic.thread_count(), 1u);
    EXPECT_EQ(serial.thread_count(), 1u);
    EXPECT_EQ(parallel.thread_count(), 8u);
    EXPECT_EQ(parallel.worker_count_for(0), 0u);
    EXPECT_EQ(parallel.worker_count_for(1), 1u);
    EXPECT_EQ(parallel.worker_count_for(4), 4u);
    EXPECT_EQ(parallel.worker_count_for(12), 8u);
}

TEST(SliceEncodeExecutorTest, ParallelEncodingMatchesSerialBitstream)
{
    const auto stream = make_stream();
    const std::array<std::uint8_t, 8> storage{
        0, 17, 93, 255,
        71, 19, 201, 3,
    };
    const mffv1::PlaneView plane{
        storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 2, 4},
    };
    const mffv1::FrameView input{&plane, 1};
    std::vector<std::byte> serial_frame;
    std::vector<std::byte> parallel_frame;

    const mffv1::codec::SliceEncodeExecutor serial(stream, 1);
    const mffv1::codec::SliceEncodeExecutor parallel(stream, 3);
    ASSERT_TRUE(serial.encode(input, serial_frame).ok());
    ASSERT_TRUE(parallel.encode(input, parallel_frame).ok());

    EXPECT_EQ(parallel_frame, serial_frame);
}

TEST(SliceEncodeExecutorTest, ParallelFailureReportsFirstSliceInInputOrder)
{
    const auto stream = make_stream(10);
    const std::array<std::uint16_t, 8> storage{
        1024, 0, 1024, 0,
        0, 0, 0, 0,
    };
    const mffv1::PlaneView plane{
        storage.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt16, 4, 2, 8},
    };
    const mffv1::FrameView input{&plane, 1};
    std::vector<std::byte> frame{std::byte{0xaa}};
    const mffv1::codec::SliceEncodeExecutor executor(stream, 4);

    const auto status = executor.encode(input, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    EXPECT_EQ(frame, (std::vector<std::byte>{std::byte{0xaa}}));
}

} // namespace
