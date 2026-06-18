#include "codec/slice_executor.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

ffv1::syntax::StreamParameters make_stream(std::uint32_t width = 1, std::uint32_t height = 1)
{
    ffv1::syntax::StreamParameters stream;
    stream.width = width;
    stream.height = height;
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

ffv1::MutablePlaneView make_y_plane(std::array<std::uint8_t, 2>& storage)
{
    ffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = ffv1::PlaneRole::Y;
    plane.info.sample_format = ffv1::SampleFormat::UInt8;
    plane.info.width = 2;
    plane.info.height = 1;
    plane.info.stride_bytes = 2;
    return plane;
}

ffv1::MutablePlaneView make_y_plane(std::array<std::uint8_t, 3>& storage)
{
    ffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = ffv1::PlaneRole::Y;
    plane.info.sample_format = ffv1::SampleFormat::UInt8;
    plane.info.width = 3;
    plane.info.height = 1;
    plane.info.stride_bytes = 3;
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

TEST(SliceExecutorTest, ResolvesAutomaticThreadCount)
{
    const auto stream = make_stream();

    const ffv1::codec::SliceExecutor automatic_executor(stream, 0);

    EXPECT_GE(automatic_executor.thread_count(), 1u);
}

TEST(SliceExecutorTest, KeepsSerialThreadCount)
{
    const auto stream = make_stream();

    const ffv1::codec::SliceExecutor serial_executor(stream, 1);

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

TEST(SliceExecutorTest, ParallelDecodeProcessesAllBatches)
{
    const auto stream = make_stream(3, 1);
    std::array<std::uint8_t, 3> storage{0xee, 0xee, 0xee};
    auto plane = make_y_plane(storage);
    ffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    std::array<ffv1::syntax::SliceDescriptor, 3> slices;
    for (std::uint32_t i = 0; i < slices.size(); ++i) {
        auto& slice = slices[i];
        slice.index = i;
        slice.x = i;
        slice.width = 1;
        slice.height = 1;
        slice.payload = payload;
        slice.quant_table_set_indexes.push_back(0);
    }

    const ffv1::codec::SliceExecutor executor(stream, 2);
    const auto status = executor.decode(output, slices);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
    EXPECT_EQ(storage[1], 0u);
    EXPECT_EQ(storage[2], 0u);
}

TEST(SliceExecutorTest, ValidatesAllSlicesBeforeWritingOutput)
{
    const auto stream = make_stream(2, 1);
    std::array<std::uint8_t, 2> storage{0xee, 0xee};
    auto plane = make_y_plane(storage);
    ffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    ffv1::syntax::SliceDescriptor first;
    first.index = 0;
    first.width = 1;
    first.height = 1;
    first.payload = payload;
    first.quant_table_set_indexes.push_back(0);

    ffv1::syntax::SliceDescriptor second = first;
    second.index = 1;
    second.x = 1;
    second.quant_table_set_indexes[0] = 1;
    const std::array slices{first, second};

    const ffv1::codec::SliceExecutor executor(stream);
    const auto status = executor.decode(output, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 1u);
    EXPECT_EQ(storage[0], 0xee);
    EXPECT_EQ(storage[1], 0xee);
}

} // namespace
