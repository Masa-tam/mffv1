#include "codec/slice_encoder.hpp"

#include "codec/slice_decoder.hpp"
#include "codec/frame_decode_context.hpp"
#include "codec/frame_parser.hpp"
#include "codec/slice_output_window.hpp"
#include "codec/slice_state.hpp"
#include "mffv1/configuration_parser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

mffv1::syntax::StreamParameters make_stream()
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 4;
    stream.height = 2;
    stream.version = 3;
    stream.micro_version = 4;
    stream.entropy_mode = mffv1::EntropyMode::Range;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    stream.quant_table_sets.push_back(
        mffv1::syntax::make_zero_quant_table_set());
    stream.intra_only = true;
    return stream;
}

mffv1::PlaneView make_input_plane(
    const std::array<std::uint8_t, 8>& storage)
{
    mffv1::PlaneView plane;
    plane.data = storage.data();
    plane.info = {
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt8,
        4,
        2,
        4,
    };
    return plane;
}

mffv1::MutablePlaneView make_output_plane(
    std::array<std::uint8_t, 8>& storage)
{
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info = {
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt8,
        4,
        2,
        4,
    };
    return plane;
}

void expect_round_trip(
    const std::array<std::uint8_t, 8>& source,
    const mffv1::syntax::StreamParameters& stream = make_stream())
{
    const auto input_plane = make_input_plane(source);
    const mffv1::FrameView input{&input_plane, 1};
    std::vector<std::byte> payload;
    const mffv1::codec::SliceEncoder encoder(stream);
    ASSERT_TRUE(encoder.encode_content(input, payload).ok());
    ASSERT_GE(payload.size(), 2u);

    std::array<std::uint8_t, 8> decoded{};
    decoded.fill(0xee);
    auto output_plane = make_output_plane(decoded);
    mffv1::MutableFrameView output{&output_plane, 1};
    mffv1::syntax::SliceDescriptor slice;
    slice.width = stream.width;
    slice.height = stream.height;
    slice.raster_width = 1;
    slice.raster_height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes = {0, 0};

    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, output, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());
    const mffv1::codec::SliceDecoder decoder(stream);

    const auto status = decoder.decode(slice, window, state);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decoded, source);
}

TEST(SliceEncoderTest, RoundTripsZeroPlane)
{
    expect_round_trip({});
}

TEST(SliceEncoderTest, RoundTripsPredictionAndModuloBoundaries)
{
    expect_round_trip({0, 255, 128, 1, 255, 0, 127, 254});
}

TEST(SliceEncoderTest, RoundTripsInvertedContextWithCustomInitialState)
{
    auto stream = make_stream();
    auto& tables = stream.quant_table_sets[0];
    tables.context_count = 2;
    tables.tables[0][10] = -1;
    stream.initial_states.resize(1);
    stream.initial_states[0].contexts.resize(2);
    stream.initial_states[0].contexts[0].fill(128);
    stream.initial_states[0].contexts[1].fill(96);

    expect_round_trip({10, 20, 20, 20, 20, 20, 20, 20}, stream);
}

TEST(SliceEncoderTest, AssemblesVersionThreeSliceAcceptedByFrameParser)
{
    const auto stream = make_stream();
    const std::array<std::uint8_t, 8> source{
        0, 255, 128, 1, 255, 0, 127, 254,
    };
    const auto input_plane = make_input_plane(source);
    const mffv1::FrameView input{&input_plane, 1};
    std::vector<std::byte> payload;
    const mffv1::codec::SliceEncoder encoder(stream);
    ASSERT_TRUE(encoder.encode_slice(input, true, payload).ok());

    mffv1::codec::FrameDecodeContext frame;
    const mffv1::codec::FrameParser parser(stream);
    ASSERT_TRUE(parser.parse_with_range_header(payload, frame).ok());
    ASSERT_TRUE(frame.keyframe);
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].slice_size, payload.size());
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_y, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 1u);
    EXPECT_EQ(frame.slices[0].raster_height, 1u);
    EXPECT_LT(frame.slices[0].content_byte_offset,
              frame.slices[0].footer_byte_offset);

    std::array<std::uint8_t, 8> decoded{};
    auto output_plane = make_output_plane(decoded);
    mffv1::MutableFrameView output{&output_plane, 1};
    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(
        window.validate(stream, output, frame.slices[0]).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());
    const mffv1::codec::SliceDecoder decoder(stream);

    const auto status =
        decoder.decode(frame.slices[0], window, state);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decoded, source);
}

TEST(SliceEncoderTest, RejectsNonKeyframeForIntraStream)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    const auto plane = make_input_plane(storage);
    const mffv1::FrameView input{&plane, 1};
    std::vector<std::byte> payload{std::byte{0xaa}};
    const mffv1::codec::SliceEncoder encoder(stream);

    const auto status = encoder.encode_slice(input, false, payload);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(payload, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(SliceEncoderTest, ReadsPaddedInputStride)
{
    auto stream = make_stream();
    std::array<std::uint8_t, 10> storage{
        1, 2, 3, 4, 0xee,
        5, 6, 7, 8, 0xee,
    };
    mffv1::PlaneView plane;
    plane.data = storage.data();
    plane.info = {
        mffv1::PlaneRole::Y,
        mffv1::SampleFormat::UInt8,
        4,
        2,
        5,
    };
    const mffv1::FrameView input{&plane, 1};
    std::vector<std::byte> payload;
    const mffv1::codec::SliceEncoder encoder(stream);

    ASSERT_TRUE(encoder.encode_content(input, payload).ok());

    std::array<std::uint8_t, 8> decoded{};
    auto output_plane = make_output_plane(decoded);
    mffv1::MutableFrameView output{&output_plane, 1};
    mffv1::syntax::SliceDescriptor slice;
    slice.width = 4;
    slice.height = 2;
    slice.raster_width = 1;
    slice.raster_height = 1;
    slice.payload = payload;
    slice.quant_table_set_indexes = {0, 0};
    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(window.validate(stream, output, slice).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());
    const mffv1::codec::SliceDecoder decoder(stream);
    ASSERT_TRUE(decoder.decode(slice, window, state).ok());
    EXPECT_EQ(decoded, (std::array<std::uint8_t, 8>{1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST(SliceEncoderTest, RejectsUnsupportedStreamWithoutChangingOutput)
{
    auto stream = make_stream();
    stream.bits_per_raw_sample = 16;
    std::array<std::uint8_t, 8> storage{};
    const auto plane = make_input_plane(storage);
    const mffv1::FrameView input{&plane, 1};
    std::vector<std::byte> payload{std::byte{0xaa}};
    const mffv1::codec::SliceEncoder encoder(stream);

    const auto status = encoder.encode_content(input, payload);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(payload, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(SliceEncoderTest, RejectsInvalidInputWithoutChangingOutput)
{
    const auto stream = make_stream();
    std::array<std::uint8_t, 8> storage{};
    auto plane = make_input_plane(storage);
    plane.info.stride_bytes = 3;
    const mffv1::FrameView input{&plane, 1};
    std::vector<std::byte> payload{std::byte{0xaa}};
    const mffv1::codec::SliceEncoder encoder(stream);

    const auto status = encoder.encode_content(input, payload);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(payload, (std::vector<std::byte>{std::byte{0xaa}}));
}

} // namespace
