#include "codec/slice_executor.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

mffv1::syntax::StreamParameters make_stream(std::uint32_t width = 1, std::uint32_t height = 1)
{
    mffv1::syntax::StreamParameters stream;
    stream.version = 0;
    stream.width = width;
    stream.height = height;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());
    return stream;
}

mffv1::MutablePlaneView make_y_plane(std::array<std::uint8_t, 1>& storage)
{
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 1;
    plane.info.height = 1;
    plane.info.stride_bytes = 1;
    return plane;
}

mffv1::MutablePlaneView make_y_plane(std::array<std::uint8_t, 2>& storage)
{
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 2;
    plane.info.height = 1;
    plane.info.stride_bytes = 2;
    return plane;
}

mffv1::MutablePlaneView make_y_plane(std::array<std::uint8_t, 3>& storage)
{
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
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
    mffv1::MutableFrameView output{&plane, 1};
    const std::vector<mffv1::syntax::SliceDescriptor> slices;

    mffv1::codec::SliceExecutor executor(stream);
    const auto status = executor.decode(output, slices);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0xee);
}

TEST(SliceExecutorTest, ResolvesAutomaticThreadCount)
{
    const auto stream = make_stream();

    const mffv1::codec::SliceExecutor automatic_executor(stream, 0);

    EXPECT_GE(automatic_executor.thread_count(), 1u);
}

TEST(SliceExecutorTest, KeepsSerialThreadCount)
{
    const auto stream = make_stream();

    const mffv1::codec::SliceExecutor serial_executor(stream, 1);

    EXPECT_EQ(serial_executor.thread_count(), 1u);
}

TEST(SliceExecutorTest, KeepsRequestedPositiveThreadCount)
{
    const auto stream = make_stream();

    const mffv1::codec::SliceExecutor executor(stream, 4);

    EXPECT_EQ(executor.thread_count(), 4u);
}

TEST(SliceExecutorTest, CapsWorkerCountToSliceCount)
{
    const auto stream = make_stream();
    const mffv1::codec::SliceExecutor executor(stream, 8);

    EXPECT_EQ(executor.worker_count_for(0), 0u);
    EXPECT_EQ(executor.worker_count_for(1), 1u);
    EXPECT_EQ(executor.worker_count_for(3), 3u);
    EXPECT_EQ(executor.worker_count_for(9), 8u);
}

TEST(SliceExecutorTest, RejectsNonKeyframeWithoutReferenceState)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};
    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);
    const std::array slices{slice};
    mffv1::codec::SliceExecutor executor(stream);

    const auto status = executor.decode(output, slices, false);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "non-keyframe requires reference slice states");
    EXPECT_FALSE(executor.has_reference_state());
    EXPECT_EQ(storage[0], 0xee);
}

TEST(SliceExecutorTest, FailedFramePreservesReferenceState)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> valid_payload{std::byte{0xff}, std::byte{0x00}};
    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = valid_payload;
    slice.quant_table_set_indexes.push_back(0);
    std::array slices{slice};
    mffv1::codec::SliceExecutor executor(stream);
    ASSERT_TRUE(executor.decode(output, slices, true).ok());
    ASSERT_TRUE(executor.has_reference_state());

    const std::array<std::byte, 1> invalid_payload{std::byte{0xff}};
    slices[0].payload = invalid_payload;
    const auto status = executor.decode(output, slices, false);

    EXPECT_FALSE(status.ok());
    EXPECT_TRUE(executor.has_reference_state());
}

TEST(SliceExecutorTest, NonKeyframeContinuesRangeContexts)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 1> continued_storage{0xee};
    auto continued_plane = make_y_plane(continued_storage);
    mffv1::MutableFrameView continued_output{&continued_plane, 1};
    const std::array<std::byte, 2> keyframe_payload{std::byte{0xff}, std::byte{0x00}};
    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = keyframe_payload;
    slice.quant_table_set_indexes.push_back(0);
    std::array slices{slice};
    mffv1::codec::SliceExecutor executor(stream);
    ASSERT_TRUE(executor.decode(continued_output, slices, true).ok());

    const std::array<std::byte, 2> next_payload{std::byte{0x70}, std::byte{0x00}};
    slices[0].payload = next_payload;
    ASSERT_TRUE(executor.decode(continued_output, slices, false).ok());

    std::array<std::uint8_t, 1> fresh_storage{0xee};
    auto fresh_plane = make_y_plane(fresh_storage);
    mffv1::MutableFrameView fresh_output{&fresh_plane, 1};
    mffv1::codec::SliceExecutor fresh_executor(stream);
    ASSERT_TRUE(fresh_executor.decode(fresh_output, slices, true).ok());

    EXPECT_NE(fresh_storage[0], continued_storage[0]);
}

TEST(SliceExecutorTest, DecodesVersionThreeGolombRiceReadAheadBoundary)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x80}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.content_byte_offset = 1;
    slice.footer_byte_offset = 1;
    slice.slice_size = 1;
    slice.quant_table_set_indexes = {0, 0};
    const std::array slices{slice};

    mffv1::codec::SliceExecutor executor(stream);
    const auto status = executor.decode(output, slices, true);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
    EXPECT_TRUE(executor.has_reference_state());
}

TEST(SliceExecutorTest, DecodesGolombRiceReadAheadBoundaryForOffsetSlice)
{
    auto stream = make_stream(2, 1);
    stream.version = 3;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.num_h_slices = 2;
    std::array<std::uint8_t, 2> storage{0xee, 0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x80}};

    mffv1::syntax::SliceDescriptor slice;
    slice.index = 1;
    slice.x = 1;
    slice.width = 1;
    slice.height = 1;
    slice.raster_x = 1;
    slice.raster_width = 1;
    slice.raster_height = 1;
    slice.payload = payload;
    slice.content_byte_offset = 1;
    slice.footer_byte_offset = 1;
    slice.slice_size = 1;
    slice.quant_table_set_indexes = {0, 0};
    const std::array slices{slice};

    mffv1::codec::SliceExecutor executor(stream);
    const auto status = executor.decode(output, slices, true);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0xee);
    EXPECT_EQ(storage[1], 0u);
    EXPECT_TRUE(executor.has_reference_state());
}

TEST(SliceExecutorTest, DecodesLegacyGolombRiceEmbeddedReadAheadBoundary)
{
    auto stream = make_stream();
    stream.version = 1;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x80}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.content_byte_offset = 1;
    slice.content_bit_offset = 0;
    slice.quant_table_set_indexes = {0};
    const std::array slices{slice};

    mffv1::codec::SliceExecutor executor(stream);
    const auto status = executor.decode(output, slices, true);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
    EXPECT_TRUE(executor.has_reference_state());
}

TEST(SliceExecutorTest, ParallelDecodeRejectsOverlappingRasterSlices)
{
    auto stream = make_stream(2, 1);
    stream.num_h_slices = 2;
    std::array<std::uint8_t, 2> storage{0xee, 0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor first;
    first.index = 3;
    first.width = 1;
    first.height = 1;
    first.raster_width = 1;
    first.raster_height = 1;
    first.payload = payload;
    first.quant_table_set_indexes.push_back(0);

    mffv1::syntax::SliceDescriptor second = first;
    second.index = 9;

    const std::array slices{first, second};

    mffv1::codec::SliceExecutor executor(stream, 2);
    const auto status = executor.decode(output, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice raster rectangles overlap");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 9u);
    EXPECT_EQ(storage[0], 0xee);
    EXPECT_EQ(storage[1], 0xee);
}

TEST(SliceExecutorTest, PrefersPrimaryGolombRiceBoundaryWithReadAheadFallback)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 3> payload{
        std::byte{0xaa},
        std::byte{0x40},
        std::byte{0x55},
    };

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.content_byte_offset = 1;
    slice.footer_byte_offset = 3;
    slice.slice_size = 3;
    slice.quant_table_set_indexes = {0, 0};
    const std::array slices{slice};

    mffv1::codec::SliceExecutor executor(stream);
    const auto status = executor.decode(output, slices, true);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_TRUE(executor.has_reference_state());
}

TEST(SliceExecutorTest, ReportsGolombRiceReadAheadCandidateOnFailure)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x00}};

    mffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.content_byte_offset = 1;
    slice.footer_byte_offset = 1;
    slice.slice_size = 1;
    slice.quant_table_set_indexes = {0, 0};
    const std::array slices{slice};

    mffv1::codec::SliceExecutor executor(stream);
    const auto status = executor.decode(output, slices, true);

    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.message.find(
                  "while decoding Golomb-Rice content candidate at byte offset 1"),
              std::string::npos);
    EXPECT_EQ(storage[0], 0xee);
    EXPECT_FALSE(executor.has_reference_state());
}

TEST(SliceExecutorTest, RejectsChangedNonKeyframeSliceLayout)
{
    auto stream = make_stream(2, 1);
    stream.num_h_slices = 2;
    std::array<std::uint8_t, 2> storage{0xee, 0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};
    mffv1::syntax::SliceDescriptor slice;
    slice.index = 4;
    slice.width = 1;
    slice.height = 1;
    slice.raster_width = 1;
    slice.raster_height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);
    std::array slices{slice};
    mffv1::codec::SliceExecutor executor(stream);
    ASSERT_TRUE(executor.decode(output, slices, true).ok());

    slices[0].raster_x = 1;
    storage.fill(0xee);
    const auto status = executor.decode(output, slices, false);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message,
              "non-keyframe slice layout differs from the reference frame");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 4u);
    EXPECT_EQ(storage[0], 0xee);
    EXPECT_EQ(storage[1], 0xee);
    EXPECT_TRUE(executor.has_reference_state());
}

TEST(SliceExecutorTest, RejectsChangedNonKeyframeSliceCountBeforeWritingOutput)
{
    const auto stream = make_stream(2, 1);
    std::array<std::uint8_t, 2> storage{0xee, 0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor first;
    first.index = 0;
    first.width = 1;
    first.height = 1;
    first.raster_width = 1;
    first.raster_height = 1;
    first.payload = payload;
    first.quant_table_set_indexes.push_back(0);
    std::array keyframe_slices{first};
    mffv1::codec::SliceExecutor executor(stream);
    ASSERT_TRUE(executor.decode(output, keyframe_slices, true).ok());
    ASSERT_TRUE(executor.has_reference_state());

    auto second = first;
    second.index = 1;
    second.x = 1;
    second.raster_x = 1;
    const std::array non_keyframe_slices{first, second};
    storage.fill(0xee);
    const auto status = executor.decode(output, non_keyframe_slices, false);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "non-keyframe slice count differs from the reference frame");
    EXPECT_FALSE(status.location.has_slice_index);
    EXPECT_EQ(storage[0], 0xee);
    EXPECT_EQ(storage[1], 0xee);
    EXPECT_TRUE(executor.has_reference_state());
}

TEST(SliceExecutorTest, MatchesNonKeyframeStatesBySliceLayout)
{
    auto stream = make_stream(2, 1);
    stream.num_h_slices = 2;
    std::array<std::uint8_t, 2> storage{0xee, 0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};
    mffv1::syntax::SliceDescriptor left;
    left.index = 0;
    left.width = 1;
    left.height = 1;
    left.raster_width = 1;
    left.raster_height = 1;
    left.payload = payload;
    left.quant_table_set_indexes.push_back(0);
    auto right = left;
    right.index = 1;
    right.x = 1;
    right.raster_x = 1;
    std::array slices{left, right};
    mffv1::codec::SliceExecutor executor(stream, 2);
    ASSERT_TRUE(executor.decode(output, slices, true).ok());

    std::swap(slices[0], slices[1]);
    const auto status = executor.decode(output, slices, false);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(executor.has_reference_state());
}

TEST(SliceExecutorTest, AddsSliceIndexToDecodeFailure)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0xff}};

    mffv1::syntax::SliceDescriptor slice;
    slice.index = 7;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);
    const std::array slices{slice};

    mffv1::codec::SliceExecutor executor(stream);
    const auto status = executor.decode(output, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "range coder payload must contain at least two bytes");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 7u);
}

TEST(SliceExecutorTest, ParallelDecodeReportsFirstFailingSliceInInputOrder)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0xff}};

    mffv1::syntax::SliceDescriptor first;
    first.index = 3;
    first.width = 1;
    first.height = 1;
    first.payload = payload;
    first.quant_table_set_indexes.push_back(0);

    mffv1::syntax::SliceDescriptor second = first;
    second.index = 9;

    const std::array slices{first, second};

    mffv1::codec::SliceExecutor executor(stream, 2);
    const auto status = executor.decode(output, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "range coder payload must contain at least two bytes");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 3u);
}

TEST(SliceExecutorTest, ParallelDecodeProcessesAllBatches)
{
    const auto stream = make_stream(3, 1);
    std::array<std::uint8_t, 3> storage{0xee, 0xee, 0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    std::array<mffv1::syntax::SliceDescriptor, 3> slices;
    for (std::uint32_t i = 0; i < slices.size(); ++i) {
        auto& slice = slices[i];
        slice.index = i;
        slice.x = i;
        slice.width = 1;
        slice.height = 1;
        slice.payload = payload;
        slice.quant_table_set_indexes.push_back(0);
    }

    mffv1::codec::SliceExecutor executor(stream, 2);
    const auto status = executor.decode(output, slices);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
    EXPECT_EQ(storage[1], 0u);
    EXPECT_EQ(storage[2], 0u);
    EXPECT_TRUE(executor.has_reference_state());
}

TEST(SliceExecutorTest, ValidatesAllSlicesBeforeWritingOutput)
{
    const auto stream = make_stream(2, 1);
    std::array<std::uint8_t, 2> storage{0xee, 0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    mffv1::syntax::SliceDescriptor first;
    first.index = 0;
    first.width = 1;
    first.height = 1;
    first.payload = payload;
    first.quant_table_set_indexes.push_back(0);

    mffv1::syntax::SliceDescriptor second = first;
    second.index = 1;
    second.x = 1;
    second.quant_table_set_indexes[0] = 1;
    const std::array slices{first, second};

    mffv1::codec::SliceExecutor executor(stream);
    const auto status = executor.decode(output, slices);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice quantization table set index is out of range");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 1u);
    EXPECT_EQ(storage[0], 0xee);
    EXPECT_EQ(storage[1], 0xee);
}

} // namespace
