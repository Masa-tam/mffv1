#include "codec/slice_executor.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

ffv1::syntax::StreamParameters make_stream()
{
    ffv1::syntax::StreamParameters stream;
    stream.width = 1;
    stream.height = 1;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());
    return stream;
}

ffv1::MutablePlaneView make_y_plane(std::array<std::uint8_t, 1>& storage)
{
    ffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = ffv1::PlaneRole::Y;
    plane.info.sample_format = ffv1::SampleFormat::UInt8;
    plane.info.width = 1;
    plane.info.height = 1;
    plane.info.stride_bytes = 1;
    return plane;
}

TEST(SliceExecutorTest, AcceptsEmptySliceList)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage);
    ffv1::MutableFrameView output{&plane, 1};
    const std::vector<ffv1::syntax::SliceDescriptor> slices;

    const ffv1::codec::SliceExecutor executor(stream);
    const auto status = executor.decode(output, slices);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0xee);
}

TEST(SliceExecutorTest, NormalizesNonPositiveThreadCountToSerial)
{
    const auto stream = make_stream();

    const ffv1::codec::SliceExecutor automatic_executor(stream, 0);
    const ffv1::codec::SliceExecutor serial_executor(stream, 1);

    EXPECT_EQ(automatic_executor.thread_count(), 1u);
    EXPECT_EQ(serial_executor.thread_count(), 1u);
}

TEST(SliceExecutorTest, KeepsRequestedPositiveThreadCount)
{
    const auto stream = make_stream();

    const ffv1::codec::SliceExecutor executor(stream, 4);

    EXPECT_EQ(executor.thread_count(), 4u);
}

TEST(SliceExecutorTest, CapsWorkerCountToSliceCount)
{
    const auto stream = make_stream();
    const ffv1::codec::SliceExecutor executor(stream, 8);

    EXPECT_EQ(executor.worker_count_for(0), 0u);
    EXPECT_EQ(executor.worker_count_for(1), 1u);
    EXPECT_EQ(executor.worker_count_for(3), 3u);
    EXPECT_EQ(executor.worker_count_for(9), 8u);
}

TEST(SliceExecutorTest, AddsSliceIndexToDecodeFailure)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage);
    ffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0xff}};

    ffv1::syntax::SliceDescriptor slice;
    slice.index = 7;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);
    const std::array slices{slice};

    const ffv1::codec::SliceExecutor executor(stream);
    const auto status = executor.decode(output, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 7u);
}

TEST(SliceExecutorTest, ParallelDecodeReportsFirstFailingSliceInInputOrder)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage);
    ffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0xff}};

    ffv1::syntax::SliceDescriptor first;
    first.index = 3;
    first.width = 1;
    first.height = 1;
    first.payload = payload;
    first.quant_table_set_indexes.push_back(0);

    ffv1::syntax::SliceDescriptor second = first;
    second.index = 9;

    const std::array slices{first, second};

    const ffv1::codec::SliceExecutor executor(stream, 2);
    const auto status = executor.decode(output, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 3u);
}

} // namespace
