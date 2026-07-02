#include "codec/frame_parser.hpp"
#include "codec/slice_executor.hpp"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

mffv1::syntax::StreamParameters make_two_slice_stream()
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 2;
    stream.height = 1;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    stream.num_h_slices = 2;
    stream.num_v_slices = 1;
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());
    return stream;
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

TEST(MultiSliceDecodeTest, ContinuesRangeStateFromEachSliceHeader)
{
    const auto stream = make_two_slice_stream();
    const std::array frame_payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x04},
        std::byte{0x3d},
        std::byte{0x34},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x04},
    };

    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    auto status = parser.parse_with_range_header(frame_payload, frame);

    ASSERT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 2u);
    EXPECT_TRUE(frame.keyframe);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 2u);
    EXPECT_EQ(frame.slices[0].footer_byte_offset, 4u);
    EXPECT_EQ(frame.slices[1].content_byte_offset, 10u);
    EXPECT_EQ(frame.slices[1].footer_byte_offset, 11u);

    std::array<std::uint8_t, 2> storage{0xee, 0xee};
    auto plane = make_y_plane(storage);
    mffv1::MutableFrameView output{&plane, 1};
    mffv1::codec::SliceExecutor executor(stream, 2);
    status = executor.decode(output, frame.slices);
    ASSERT_TRUE(status.ok()) << status.message;

    EXPECT_EQ(storage[0], 0u);
    EXPECT_EQ(storage[1], 1u);
}

TEST(MultiSliceDecodeTest, DecodesBufferedSymbolWhenHeaderConsumesAllEntropyBytes)
{
    auto stream = make_two_slice_stream();
    stream.width = 1;
    stream.num_h_slices = 1;
    const std::array frame_payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x02},
    };

    mffv1::codec::FrameParser parser(stream);
    mffv1::codec::FrameDecodeContext frame;
    auto status = parser.parse_with_range_header(frame_payload, frame);

    ASSERT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, frame.slices[0].footer_byte_offset);

    std::array<std::uint8_t, 1> storage{0xee};
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 1;
    plane.info.height = 1;
    plane.info.stride_bytes = 1;
    mffv1::MutableFrameView output{&plane, 1};
    mffv1::codec::SliceExecutor executor(stream, 1);

    status = executor.decode(output, frame.slices);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
}

} // namespace
