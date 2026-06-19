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
    stream.version = 0;
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

TEST(LineStateTest, DerivesRfcSliceBorderNeighbors)
{
    ffv1::syntax::LineState line;
    ASSERT_TRUE(line.reset(3).ok());
    line.mutable_current() = {10, 20, 30};
    line.swap_lines();

    line.mutable_current()[0] = 40;
    auto neighbors = line.neighbors(0);
    EXPECT_EQ(neighbors.far_left, 0);
    EXPECT_EQ(neighbors.left, 10);
    EXPECT_EQ(neighbors.top, 10);
    EXPECT_EQ(neighbors.top_left, 0);
    EXPECT_EQ(neighbors.top_right, 20);
    EXPECT_EQ(neighbors.top_top, 0);

    neighbors = line.neighbors(1);
    EXPECT_EQ(neighbors.far_left, 10);
    EXPECT_EQ(neighbors.left, 40);
    EXPECT_EQ(neighbors.top, 20);
    EXPECT_EQ(neighbors.top_left, 10);
    EXPECT_EQ(neighbors.top_right, 30);
    EXPECT_EQ(neighbors.top_top, 0);

    line.mutable_current()[1] = 50;
    line.mutable_current()[2] = 60;
    line.swap_lines();
    neighbors = line.neighbors(0);
    EXPECT_EQ(neighbors.left, 40);
    EXPECT_EQ(neighbors.top_left, 10);
    EXPECT_EQ(neighbors.top_top, 10);
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

TEST(SliceStateTest, KeepsExtraPlaneFullWidthWhenChromaIsAbsent)
{
    auto stream = make_stream();
    stream.extra_plane = true;
    stream.log2_h_chroma_subsample = 1;
    ffv1::codec::SliceState state;

    const auto status = state.reset(stream);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(state.plane_count(), 2u);
    EXPECT_EQ(state.line_state(0).width(), stream.width);
    EXPECT_EQ(state.line_state(1).width(), stream.width);
}

TEST(SliceStateTest, UsesSliceOutputPlaneWidths)
{
    auto stream = make_stream();
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;

    std::array<std::uint8_t, 8> y{};
    std::array<std::uint8_t, 4> cb{};
    std::array<std::uint8_t, 4> cr{};
    std::array<ffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {ffv1::PlaneRole::Y, ffv1::SampleFormat::UInt8, 4, 2, 4};
    planes[1].data = cb.data();
    planes[1].info = {ffv1::PlaneRole::Cb, ffv1::SampleFormat::UInt8, 2, 2, 2};
    planes[2].data = cr.data();
    planes[2].info = {ffv1::PlaneRole::Cr, ffv1::SampleFormat::UInt8, 2, 2, 2};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 2;
    slice.height = 2;
    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());

    ffv1::codec::SliceState state;
    const auto status = state.reset(window);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(state.plane_count(), 3u);
    EXPECT_EQ(state.line_state(0).width(), 2u);
    EXPECT_EQ(state.line_state(1).width(), 1u);
    EXPECT_EQ(state.line_state(2).width(), 1u);
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

TEST(SliceDecoderTest, RejectsInitialStateCountThatDoesNotMatchQuantizationContexts)
{
    auto stream = make_stream();
    stream.initial_states.resize(1);
    stream.initial_states[0].contexts.resize(2);
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidState);
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

TEST(SliceDecoderTest, RejectsContentOffsetBeforePayload)
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
    slice.payload_byte_offset = 10;
    slice.content_byte_offset = 9;
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

TEST(SliceDecoderTest, DecodesWithAbsoluteContentOffset)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 4> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xff},
        std::byte{0x00},
    };

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.payload_byte_offset = 10;
    slice.content_byte_offset = 12;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
    EXPECT_EQ(storage[1], 0xee);
}

TEST(SliceDecoderTest, ReportsRangeCoderResetErrorAtAbsoluteContentOffset)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 3> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xff},
    };

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.payload_byte_offset = 10;
    slice.content_byte_offset = 12;
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

TEST(SliceDecoderTest, DecodesOnlyContentBeforeFooter)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 7> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
    };

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.payload_byte_offset = 10;
    slice.content_byte_offset = 12;
    slice.footer_byte_offset = 14;
    slice.slice_size = static_cast<std::uint32_t>(payload.size());
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
    EXPECT_EQ(storage[1], 0xee);
}

TEST(SliceDecoderTest, RejectsFooterOffsetBeforePayload)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 4> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xff},
        std::byte{0x00},
    };

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.payload_byte_offset = 10;
    slice.content_byte_offset = 12;
    slice.footer_byte_offset = 9;
    slice.slice_size = static_cast<std::uint32_t>(payload.size());
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
    EXPECT_EQ(status.location.byte_offset, slice.footer_byte_offset);
}

TEST(SliceDecoderTest, RejectsFooterOffsetBeforeContent)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 4> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xff},
        std::byte{0x00},
    };

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.payload_byte_offset = 10;
    slice.content_byte_offset = 13;
    slice.footer_byte_offset = 12;
    slice.slice_size = static_cast<std::uint32_t>(payload.size());
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
    EXPECT_EQ(status.location.byte_offset, slice.footer_byte_offset);
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

TEST(SliceDecoderTest, DecodesZeroDifferencesFor8BitChromaSlice)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.chroma_planes = true;
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());
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
    slice.quant_table_set_indexes = {0, 1};

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : y) {
        EXPECT_EQ(sample, 0u);
    }
    for (const auto sample : cb) {
        EXPECT_EQ(sample, 0u);
    }
    for (const auto sample : cr) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(SliceDecoderTest, DecodesZeroDifferencesFor16BitChromaSlice)
{
    auto stream = make_stream();
    stream.bits_per_raw_sample = 16;
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;

    std::array<std::uint16_t, 8> y{};
    std::array<std::uint16_t, 2> cb{};
    std::array<std::uint16_t, 2> cr{};
    y.fill(0xffff);
    cb.fill(0xffff);
    cr.fill(0xffff);
    std::array<ffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {ffv1::PlaneRole::Y, ffv1::SampleFormat::UInt16, 4, 2, 8};
    planes[1].data = cb.data();
    planes[1].info = {ffv1::PlaneRole::Cb, ffv1::SampleFormat::UInt16, 2, 1, 4};
    planes[2].data = cr.data();
    planes[2].info = {ffv1::PlaneRole::Cr, ffv1::SampleFormat::UInt16, 2, 1, 4};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};
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
    for (const auto sample : y) {
        EXPECT_EQ(sample, 0u);
    }
    for (const auto sample : cb) {
        EXPECT_EQ(sample, 0u);
    }
    for (const auto sample : cr) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(SliceDecoderTest, DecodesZeroDifferencesForExtraPlaneSlice)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.extra_plane = true;
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());

    std::array<std::uint8_t, 8> y{};
    std::array<std::uint8_t, 8> alpha{};
    std::array<ffv1::MutablePlaneView, 2> planes{};
    planes[0].data = y.data();
    planes[0].info = {ffv1::PlaneRole::Y, ffv1::SampleFormat::UInt8, 4, 2, 4};
    planes[1].data = alpha.data();
    planes[1].info = {ffv1::PlaneRole::Alpha, ffv1::SampleFormat::UInt8, 4, 2, 4};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};
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
    slice.quant_table_set_indexes = {0, 1, 2};

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(stream).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : y) {
        EXPECT_EQ(sample, 0u);
    }
    for (const auto sample : alpha) {
        EXPECT_EQ(sample, 0u);
    }
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

TEST(SliceDecoderTest, DecodesZeroDifferenceRgbRangeSlice)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    std::array<std::uint8_t, 1> r{0xee};
    std::array<std::uint8_t, 1> g{0xee};
    std::array<std::uint8_t, 1> b{0xee};
    std::array<ffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {ffv1::PlaneRole::R, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[1] = {g.data(), {ffv1::PlaneRole::G, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[2] = {b.data(), {ffv1::PlaneRole::B, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(r[0], 128u);
    EXPECT_EQ(g[0], 128u);
    EXPECT_EQ(b[0], 128u);
}

TEST(SliceDecoderTest, DecodesNonzeroRgbRangeSliceInLineOrder)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    std::array<std::uint8_t, 1> r{};
    std::array<std::uint8_t, 1> g{};
    std::array<std::uint8_t, 1> b{};
    std::array<ffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {ffv1::PlaneRole::R, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[1] = {g.data(), {ffv1::PlaneRole::G, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[2] = {b.data(), {ffv1::PlaneRole::B, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0x14}, std::byte{0x46}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(state.line_state(0).previous().size(), 1u);
    EXPECT_EQ(state.line_state(0).previous()[0], 1);
    EXPECT_EQ(state.line_state(1).previous()[0], 0);
    EXPECT_EQ(state.line_state(2).previous()[0], 2);
    EXPECT_EQ(r[0], 131u);
    EXPECT_EQ(g[0], 129u);
    EXPECT_EQ(b[0], 129u);
}

TEST(SliceDecoderTest, DecodesRgbExtraPlaneAfterColorLines)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.extra_plane = true;
    std::array<std::uint8_t, 1> r{};
    std::array<std::uint8_t, 1> g{};
    std::array<std::uint8_t, 1> b{};
    std::array<std::uint8_t, 1> alpha{};
    std::array<ffv1::MutablePlaneView, 4> planes{};
    planes[0] = {r.data(), {ffv1::PlaneRole::R, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[1] = {g.data(), {ffv1::PlaneRole::G, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[2] = {b.data(), {ffv1::PlaneRole::B, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[3] = {alpha.data(), {ffv1::PlaneRole::Alpha, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0x14}, std::byte{0x46}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(state.line_state(3).previous().size(), 1u);
    EXPECT_EQ(state.line_state(3).previous()[0], 0);
    EXPECT_EQ(r[0], 131u);
    EXPECT_EQ(g[0], 129u);
    EXPECT_EQ(b[0], 129u);
    EXPECT_EQ(alpha[0], 0u);
}

TEST(SliceDecoderTest, RejectsGolombRiceRgbBeforeWritingOutput)
{
    auto stream = make_stream();
    stream.width = 1;
    stream.height = 1;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 1> r{0xee};
    std::array<std::uint8_t, 1> g{0xee};
    std::array<std::uint8_t, 1> b{0xee};
    std::array<ffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {ffv1::PlaneRole::R, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[1] = {g.data(), {ffv1::PlaneRole::G, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[2] = {b.data(), {ffv1::PlaneRole::B, ffv1::SampleFormat::UInt8, 1, 1, 1}};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 1> payload{std::byte{0xff}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(r[0], 0xee);
    EXPECT_EQ(g[0], 0xee);
    EXPECT_EQ(b[0], 0xee);
}

TEST(SliceDecoderTest, DecodesGolombRiceZeroRun)
{
    auto stream = make_stream();
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0xfc}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
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

TEST(SliceDecoderTest, DecodesGolombRiceChromaPlanesInOrder)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.width = 1;
    stream.height = 1;
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    stream.chroma_planes = true;
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());

    std::array<std::uint8_t, 1> y{0xee};
    std::array<std::uint8_t, 1> cb{0xee};
    std::array<std::uint8_t, 1> cr{0xee};
    std::array<ffv1::MutablePlaneView, 3> planes{};
    planes[0].data = y.data();
    planes[0].info = {ffv1::PlaneRole::Y, ffv1::SampleFormat::UInt8, 1, 1, 1};
    planes[1].data = cb.data();
    planes[1].info = {ffv1::PlaneRole::Cb, ffv1::SampleFormat::UInt8, 1, 1, 1};
    planes[2].data = cr.data();
    planes[2].info = {ffv1::PlaneRole::Cr, ffv1::SampleFormat::UInt8, 1, 1, 1};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 2> payload{std::byte{0x45}, std::byte{0x60}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes = {0, 1};

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(y[0], 1u);
    EXPECT_EQ(cb[0], 255u);
    EXPECT_EQ(cr[0], 2u);
}

TEST(SliceDecoderTest, DecodesGolombRice16BitExtraPlane)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.width = 1;
    stream.height = 1;
    stream.bits_per_raw_sample = 16;
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    stream.extra_plane = true;
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());

    std::array<std::uint16_t, 1> y{0xeeee};
    std::array<std::uint16_t, 1> alpha{0xeeee};
    std::array<ffv1::MutablePlaneView, 2> planes{};
    planes[0].data = y.data();
    planes[0].info = {ffv1::PlaneRole::Y, ffv1::SampleFormat::UInt16, 1, 1, 2};
    planes[1].data = alpha.data();
    planes[1].info = {ffv1::PlaneRole::Alpha, ffv1::SampleFormat::UInt16, 1, 1, 2};
    ffv1::MutableFrameView frame{planes.data(), planes.size()};
    const std::array<std::byte, 1> payload{std::byte{0x45}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes = {0, 1, 2};

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(y[0], 1u);
    EXPECT_EQ(alpha[0], 65535u);
}

TEST(SliceDecoderTest, DecodesPositiveGolombRiceRunInterruption)
{
    auto stream = make_stream();
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x40}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 0xee);
}

TEST(SliceDecoderTest, DecodesGolombRiceFromContentBitOffset)
{
    auto stream = make_stream();
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0xa0}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.content_bit_offset = 1;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 0xee);
}

TEST(SliceDecoderTest, RejectsGolombRiceContentBitOffsetOutsideByte)
{
    auto stream = make_stream();
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x00}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.content_bit_offset = 8;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(SliceDecoderTest, DecodesNegativeGolombRiceRunInterruption)
{
    auto stream = make_stream();
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x50}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 1;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 255u);
    EXPECT_EQ(storage[1], 0xee);
}

TEST(SliceDecoderTest, DecodesGolombRiceScalarContext)
{
    auto stream = make_stream();
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    stream.quant_table_sets[0].context_count = 2;
    stream.quant_table_sets[0].tables[0][1] = 1;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x48}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 2;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 1u);
    EXPECT_EQ(storage[2], 0xee);
}

TEST(SliceDecoderTest, InvertsGolombRiceDifferenceForNegativeContext)
{
    auto stream = make_stream();
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    stream.quant_table_sets[0].context_count = 2;
    stream.quant_table_sets[0].tables[0][1] = -1;
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0x4c}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 2;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 0u);
    EXPECT_EQ(storage[2], 0xee);
}

TEST(SliceDecoderTest, DecodesGolombRiceRunRemainder)
{
    auto stream = make_stream();
    stream.width = 6;
    stream.height = 1;
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 6> storage{};
    storage.fill(0xee);
    ffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = ffv1::PlaneRole::Y;
    plane.info.sample_format = ffv1::SampleFormat::UInt8;
    plane.info.width = 6;
    plane.info.height = 1;
    plane.info.stride_bytes = 6;
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xf6}, std::byte{0x00}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 6;
    slice.height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes.push_back(0);

    ffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, frame, slice).ok());
    ffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());

    const ffv1::codec::SliceDecoder decoder(stream);
    const auto status = decoder.decode(slice, window, state);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage, (std::array<std::uint8_t, 6>{0, 0, 0, 0, 0, 1}));
}

TEST(SliceDecoderTest, RejectsNonzeroGolombRicePadding)
{
    auto stream = make_stream();
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 1> payload{std::byte{0xff}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
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

TEST(SliceDecoderTest, RejectsTrailingGolombRiceByte)
{
    auto stream = make_stream();
    stream.entropy_mode = ffv1::EntropyMode::GolombRice;
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 4> payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xfc},
        std::byte{0x00},
    };

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.content_byte_offset = 2;
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
    EXPECT_EQ(status.location.byte_offset, 3u);
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

TEST(SliceDecoderTest, AcceptsUnusedVersionThreeChromaCompatibilityIndex)
{
    auto stream = make_stream();
    stream.version = 3;
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());
    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_plane(storage);
    ffv1::MutableFrameView frame{&plane, 1};
    const std::array<std::byte, 2> payload{std::byte{0xff}, std::byte{0x00}};

    ffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.payload = payload;
    slice.quant_table_set_indexes = {0, 1};

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
