#include "codec/slice_decoder.hpp"
#include "ffv1/configuration_parser.hpp"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

ffv1::syntax::StreamParameters make_stream()
{
    ffv1::syntax::StreamParameters stream;
    stream.width = 4;
    stream.height = 2;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());
    return stream;
}

ffv1::MutablePlaneView make_plane(std::array<std::uint8_t, 8>& storage)
{
    ffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = ffv1::PlaneRole::Y;
    plane.info.sample_format = ffv1::SampleFormat::UInt8;
    plane.info.width = 4;
    plane.info.height = 2;
    plane.info.stride_bytes = 4;
    return plane;
}

ffv1::MutablePlaneView make_u16_plane(std::array<std::uint16_t, 8>& storage)
{
    ffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = ffv1::PlaneRole::Y;
    plane.info.sample_format = ffv1::SampleFormat::UInt16;
    plane.info.width = 4;
    plane.info.height = 2;
    plane.info.stride_bytes = 8;
    return plane;
}

TEST(LineStateTest, ResetsAndSwapsLines)
{
    ffv1::syntax::LineState line;
    ASSERT_TRUE(line.reset(3).ok());
    ASSERT_EQ(line.width(), 3u);

    line.mutable_current()[1] = 42;
    line.swap_lines();

    EXPECT_EQ(line.previous()[1], 42);
    EXPECT_EQ(line.current()[1], 0);
    line.mutable_current()[2] = 7;
    line.swap_lines();
    EXPECT_EQ(line.second_previous()[1], 42);
    EXPECT_EQ(line.previous()[2], 7);
}

TEST(SliceStateTest, ResetsOneLineStatePerCodedPlane)
{
    const auto stream = make_stream();
    ffv1::codec::SliceState state;

    const auto status = state.reset(stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(state.plane_count(), 1u);
    EXPECT_EQ(state.line_state(0).width(), stream.width);
}

TEST(SliceDecoderTest, RejectsEmptyPayload)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(SliceDecoderTest, RejectsContentOffsetOutsidePayload)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 3;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, slice.content_byte_offset);
}

TEST(SliceDecoderTest, DecodesZeroDifferencesForYOnly8BitSlice)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 16> payload{
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
    };

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(SliceDecoderTest, DecodesZeroDifferencesForYOnly16BitSlice)
{
    auto stream = make_stream();
    stream.bits_per_raw_sample = 16;
    std::array<std::uint16_t, 8> storage{};
    storage.fill(0xffff);
    auto plane = make_u16_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 16> payload{
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
    };

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(SliceDecoderTest, DecodesPositiveDifferenceForYOnly16BitSlice)
{
    auto stream = make_stream();
    stream.bits_per_raw_sample = 16;
    std::array<std::uint16_t, 8> storage{};
    storage.fill(0xffff);
    auto plane = make_u16_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{
        std::byte{0x14},
        std::byte{0x46},
    };

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 0xffffu);
}

TEST(SliceDecoderTest, DecodesWrappedNegativeDifferenceForYOnly16BitSlice)
{
    auto stream = make_stream();
    stream.bits_per_raw_sample = 16;
    std::array<std::uint16_t, 8> storage{};
    storage.fill(0);
    auto plane = make_u16_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{
        std::byte{0x21},
        std::byte{0xcf},
    };

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 65535u);
    EXPECT_EQ(storage[1], 0u);
}

TEST(SliceDecoderTest, ReportsUnsupportedChromaPath)
{
    auto stream = make_stream();
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;

    std::array<std::uint8_t, 8> y{};
    std::array<std::uint8_t, 2> cb{};
    std::array<std::uint8_t, 2> cr{};
    std::array<ffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {ffv1::PlaneRole::Y, ffv1::SampleFormat::UInt8, 4, 2, 4};
    planes[1].data = cb.data();
    planes[1].info = {ffv1::PlaneRole::Cb, ffv1::SampleFormat::UInt8, 2, 1, 2};
    planes[2].data = cr.data();
    planes[2].info = {ffv1::PlaneRole::Cr, ffv1::SampleFormat::UInt8, 2, 1, 2};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::NotImplemented);
}

TEST(SliceDecoderTest, RejectsUnsupportedBitDepth)
{
    auto stream = make_stream();
    stream.bits_per_raw_sample = 17;
    std::array<std::uint16_t, 8> storage{};
    auto plane = make_u16_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::UnsupportedFeature);
}

TEST(SliceDecoderTest, RejectsMissingQuantTableSetIndex)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

TEST(SliceDecoderTest, RejectsOutOfRangeQuantTableSetIndex)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 0;
    slice.quant_table_set_indexes.push_back(1);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

} // namespace
