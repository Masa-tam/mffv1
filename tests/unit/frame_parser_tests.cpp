#include "codec/frame_parser.hpp"

#include <array>
#include <cstddef>

#include <gtest/gtest.h>

namespace {

ffv1::syntax::StreamParameters make_stream()
{
    ffv1::syntax::StreamParameters stream;
    stream.width = 16;
    stream.height = 8;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    stream.num_h_slices = 1;
    stream.num_v_slices = 1;
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());
    return stream;
}

TEST(FrameParserTest, RejectsEmptyPayload)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;

    const ffv1::ByteSpan empty;
    const auto status = parser.parse(empty, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(FrameParserTest, CreatesSingleSliceDescriptor)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 4> payload{
        std::byte{1},
        std::byte{2},
        std::byte{3},
        std::byte{4},
    };

    const auto status = parser.parse(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].index, 0u);
    EXPECT_EQ(frame.slices[0].x, 0u);
    EXPECT_EQ(frame.slices[0].y, 0u);
    EXPECT_EQ(frame.slices[0].width, stream.width);
    EXPECT_EQ(frame.slices[0].height, stream.height);
    EXPECT_EQ(frame.slices[0].payload.size(), payload.size());
    ASSERT_EQ(frame.slices[0].quant_table_set_indexes.size(), 1u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[0], 0u);
    EXPECT_EQ(frame.frame_info.width, stream.width);
    EXPECT_EQ(frame.frame_info.height, stream.height);
    EXPECT_EQ(frame.frame_info.plane_count, 1u);
}

TEST(FrameParserTest, ReportsMultiSliceAsNotImplemented)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 1> payload{std::byte{0}};

    const auto status = parser.parse(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::NotImplemented);
}

} // namespace
