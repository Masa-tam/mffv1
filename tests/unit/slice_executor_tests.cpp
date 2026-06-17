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

} // namespace
