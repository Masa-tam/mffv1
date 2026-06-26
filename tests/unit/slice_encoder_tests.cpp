#include "codec/slice_encoder.hpp"

#include "codec/slice_decoder.hpp"
#include "codec/frame_decode_context.hpp"
#include "codec/frame_parser.hpp"
#include "codec/slice_output_window.hpp"
#include "codec/slice_state.hpp"
#include "codec/slice_footer_writer.hpp"
#include "codec/slice_header_writer.hpp"
#include "entropy/range_encoder.hpp"
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

TEST(SliceEncoderTest, LocatesGolombRiceContentAfterRangeHeader)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    mffv1::entropy::RangeEncoder header;
    ASSERT_TRUE(header.reset().ok());
    ASSERT_TRUE(header.write_bool(true).ok());
    mffv1::codec::SliceHeaderValues values;
    values.width = 1;
    values.height = 1;
    values.quant_table_set_indexes = {0, 0};
    const mffv1::codec::SliceHeaderWriter header_writer;
    ASSERT_TRUE(header_writer.write(header, stream, values).ok());
    std::vector<std::byte> payload;
    ASSERT_TRUE(header.finalize(payload).ok());
    const auto expected_content_offset = payload.size();
    payload.push_back(std::byte{0x80});
    const mffv1::codec::SliceFooterWriter footer_writer;
    ASSERT_TRUE(footer_writer.append(stream, 0, payload).ok());

    mffv1::codec::FrameDecodeContext frame;
    const mffv1::codec::FrameParser parser(stream);
    ASSERT_TRUE(parser.parse_with_range_header(payload, frame).ok());

    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_TRUE(frame.keyframe);
    EXPECT_EQ(
        frame.slices[0].content_byte_offset,
        expected_content_offset);
    EXPECT_EQ(frame.slices[0].content_bit_offset, 0u);
}

TEST(SliceEncoderTest, RoundTripsGolombRiceContent)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    const std::array<std::uint8_t, 8> source{
        0, 0, 4, 4, 4, 9, 9, 0,
    };
    const auto input_plane = make_input_plane(source);
    const mffv1::FrameView input{&input_plane, 1};
    std::vector<std::byte> payload;
    const mffv1::codec::SliceEncoder encoder(stream);
    ASSERT_TRUE(encoder.encode_content(input, payload).ok());

    std::array<std::uint8_t, 8> decoded{};
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

    ASSERT_TRUE(decoder.decode(slice, window, state).ok());
    EXPECT_EQ(decoded, source);
}

TEST(SliceEncoderTest, AssemblesGolombRiceSliceAcceptedByFrameParser)
{
    auto stream = make_stream();
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    const std::array<std::uint8_t, 8> source{
        0, 0, 4, 4, 4, 9, 9, 0,
    };
    const auto input_plane = make_input_plane(source);
    const mffv1::FrameView input{&input_plane, 1};
    std::vector<std::byte> payload;
    const mffv1::codec::SliceEncoder encoder(stream);
    ASSERT_TRUE(encoder.encode_slice(input, true, payload).ok());

    mffv1::codec::FrameDecodeContext frame;
    const mffv1::codec::FrameParser parser(stream);
    ASSERT_TRUE(parser.parse_with_range_header(payload, frame).ok());
    ASSERT_EQ(frame.slices.size(), 1u);

    std::array<std::uint8_t, 8> decoded{};
    auto output_plane = make_output_plane(decoded);
    mffv1::MutableFrameView output{&output_plane, 1};
    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(
        window.validate(stream, output, frame.slices[0]).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());
    const mffv1::codec::SliceDecoder decoder(stream);

    ASSERT_TRUE(decoder.decode(frame.slices[0], window, state).ok());
    EXPECT_EQ(decoded, source);
}

TEST(SliceEncoderTest, RoundTripsPlanarYcbcr444)
{
    auto stream = make_stream();
    stream.chroma_planes = true;
    const std::array<std::uint8_t, 8> y{
        0, 255, 128, 1, 255, 0, 127, 254,
    };
    const std::array<std::uint8_t, 8> cb{
        128, 129, 127, 0, 255, 64, 192, 32,
    };
    const std::array<std::uint8_t, 8> cr{
        255, 0, 1, 254, 2, 253, 3, 252,
    };
    std::array<mffv1::PlaneView, 3> input_planes{};
    input_planes[0] = {
        y.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 2, 4},
    };
    input_planes[1] = {
        cb.data(),
        {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 4, 2, 4},
    };
    input_planes[2] = {
        cr.data(),
        {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 4, 2, 4},
    };
    const mffv1::FrameView input{
        input_planes.data(), input_planes.size()};
    std::vector<std::byte> payload;
    const mffv1::codec::SliceEncoder encoder(stream);
    ASSERT_TRUE(encoder.encode_slice(input, true, payload).ok());

    mffv1::codec::FrameDecodeContext frame;
    const mffv1::codec::FrameParser parser(stream);
    ASSERT_TRUE(parser.parse_with_range_header(payload, frame).ok());
    ASSERT_EQ(frame.slices.size(), 1u);

    std::array<std::uint8_t, 8> decoded_y{};
    std::array<std::uint8_t, 8> decoded_cb{};
    std::array<std::uint8_t, 8> decoded_cr{};
    std::array<mffv1::MutablePlaneView, 3> output_planes{};
    output_planes[0] = {
        decoded_y.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 4, 2, 4},
    };
    output_planes[1] = {
        decoded_cb.data(),
        {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 4, 2, 4},
    };
    output_planes[2] = {
        decoded_cr.data(),
        {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 4, 2, 4},
    };
    mffv1::MutableFrameView output{
        output_planes.data(), output_planes.size()};
    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(
        window.validate(stream, output, frame.slices[0]).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());
    const mffv1::codec::SliceDecoder decoder(stream);

    ASSERT_TRUE(
        decoder.decode(frame.slices[0], window, state).ok());
    EXPECT_EQ(decoded_y, y);
    EXPECT_EQ(decoded_cb, cb);
    EXPECT_EQ(decoded_cr, cr);
}

TEST(SliceEncoderTest, RoundTripsOddSizedYcbcr420)
{
    auto stream = make_stream();
    stream.width = 5;
    stream.height = 3;
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;
    const std::array<std::uint8_t, 15> y{
        0, 1, 2, 3, 4,
        5, 6, 7, 8, 9,
        10, 11, 12, 13, 14,
    };
    const std::array<std::uint8_t, 6> cb{
        128, 129, 130, 131, 132, 133,
    };
    const std::array<std::uint8_t, 6> cr{
        255, 254, 253, 252, 251, 250,
    };
    std::array<mffv1::PlaneView, 3> input_planes{};
    input_planes[0] = {
        y.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 5, 3, 5},
    };
    input_planes[1] = {
        cb.data(),
        {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 3, 2, 3},
    };
    input_planes[2] = {
        cr.data(),
        {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 3, 2, 3},
    };
    const mffv1::FrameView input{
        input_planes.data(), input_planes.size()};
    std::vector<std::byte> payload;
    const mffv1::codec::SliceEncoder encoder(stream);
    ASSERT_TRUE(encoder.encode_slice(input, true, payload).ok());

    mffv1::codec::FrameDecodeContext frame;
    const mffv1::codec::FrameParser parser(stream);
    ASSERT_TRUE(parser.parse_with_range_header(payload, frame).ok());
    ASSERT_EQ(frame.slices.size(), 1u);

    std::array<std::uint8_t, 15> decoded_y{};
    std::array<std::uint8_t, 6> decoded_cb{};
    std::array<std::uint8_t, 6> decoded_cr{};
    std::array<mffv1::MutablePlaneView, 3> output_planes{};
    output_planes[0] = {
        decoded_y.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 5, 3, 5},
    };
    output_planes[1] = {
        decoded_cb.data(),
        {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 3, 2, 3},
    };
    output_planes[2] = {
        decoded_cr.data(),
        {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 3, 2, 3},
    };
    mffv1::MutableFrameView output{
        output_planes.data(), output_planes.size()};
    mffv1::codec::SliceOutputWindow window;
    ASSERT_TRUE(
        window.validate(stream, output, frame.slices[0]).ok());
    mffv1::codec::SliceState state;
    ASSERT_TRUE(state.reset(window).ok());
    const mffv1::codec::SliceDecoder decoder(stream);

    ASSERT_TRUE(
        decoder.decode(frame.slices[0], window, state).ok());
    EXPECT_EQ(decoded_y, y);
    EXPECT_EQ(decoded_cb, cb);
    EXPECT_EQ(decoded_cr, cr);
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

TEST(SliceEncoderTest, StatefulEncodingRoundTripsNonKeyframe)
{
    for (const auto entropy_mode : {
             mffv1::EntropyMode::Range,
             mffv1::EntropyMode::GolombRice,
         }) {
        auto stream = make_stream();
        stream.entropy_mode = entropy_mode;
        stream.intra_only = false;
        const std::array<std::uint8_t, 8> first{
            0, 1, 2, 3, 4, 5, 6, 7};
        const std::array<std::uint8_t, 8> second{
            7, 6, 5, 4, 3, 2, 1, 0};
        const auto first_plane = make_input_plane(first);
        const auto second_plane = make_input_plane(second);
        const mffv1::FrameView first_input{&first_plane, 1};
        const mffv1::FrameView second_input{&second_plane, 1};
        mffv1::codec::SliceHeaderValues header;
        header.width = 1;
        header.height = 1;
        header.quant_table_set_indexes = {0, 0};

        const mffv1::codec::SliceEncoder encoder(stream);
        mffv1::codec::SliceState encode_state;
        std::vector<std::byte> first_payload;
        std::vector<std::byte> second_payload;
        ASSERT_TRUE(encoder.encode_slice(
            first_input,
            header,
            true,
            true,
            encode_state,
            first_payload).ok());
        ASSERT_TRUE(encoder.encode_slice(
            second_input,
            header,
            true,
            false,
            encode_state,
            second_payload).ok());

        mffv1::codec::FrameDecodeContext first_frame;
        mffv1::codec::FrameDecodeContext second_frame;
        const mffv1::codec::FrameParser parser(stream);
        ASSERT_TRUE(parser.parse_with_range_header(
            first_payload, first_frame).ok());
        ASSERT_TRUE(parser.parse_with_range_header(
            second_payload, second_frame).ok());
        ASSERT_TRUE(first_frame.keyframe);
        ASSERT_FALSE(second_frame.keyframe);
        ASSERT_EQ(first_frame.slices.size(), 1u);
        ASSERT_EQ(second_frame.slices.size(), 1u);

        std::array<std::uint8_t, 8> decoded{};
        auto output_plane = make_output_plane(decoded);
        mffv1::MutableFrameView output{&output_plane, 1};
        mffv1::codec::SliceState decode_state;
        const mffv1::codec::SliceDecoder decoder(stream);

        mffv1::codec::SliceOutputWindow first_window;
        ASSERT_TRUE(first_window.validate(
            stream, output, first_frame.slices[0]).ok());
        ASSERT_TRUE(decode_state.reset(first_window).ok());
        ASSERT_TRUE(decoder.decode(
            first_frame.slices[0], first_window, decode_state).ok());
        EXPECT_EQ(decoded, first);

        decoded.fill(0);
        mffv1::codec::SliceOutputWindow second_window;
        ASSERT_TRUE(second_window.validate(
            stream, output, second_frame.slices[0]).ok());
        ASSERT_TRUE(decode_state.reset(second_window).ok());
        ASSERT_TRUE(decoder.decode(
            second_frame.slices[0], second_window, decode_state).ok());
        EXPECT_EQ(decoded, second);
    }
}

TEST(SliceEncoderTest, StatefulNonKeyframeRequiresReferenceState)
{
    auto stream = make_stream();
    stream.intra_only = false;
    std::array<std::uint8_t, 8> storage{};
    const auto plane = make_input_plane(storage);
    const mffv1::FrameView input{&plane, 1};
    mffv1::codec::SliceHeaderValues header;
    header.width = 1;
    header.height = 1;
    header.quant_table_set_indexes = {0, 0};
    mffv1::codec::SliceState state;
    std::vector<std::byte> payload{std::byte{0xaa}};
    const mffv1::codec::SliceEncoder encoder(stream);

    const auto status = encoder.encode_slice(
        input, header, true, false, state, payload);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(payload, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(SliceEncoderTest, StatefulKeyframeResetsReferenceState)
{
    for (const auto entropy_mode : {
             mffv1::EntropyMode::Range,
             mffv1::EntropyMode::GolombRice,
         }) {
        auto stream = make_stream();
        stream.entropy_mode = entropy_mode;
        stream.intra_only = false;
        const std::array<std::uint8_t, 8> first{
            0, 1, 2, 3, 4, 5, 6, 7};
        const std::array<std::uint8_t, 8> second{
            7, 6, 5, 4, 3, 2, 1, 0};
        const auto first_plane = make_input_plane(first);
        const auto second_plane = make_input_plane(second);
        const mffv1::FrameView first_input{&first_plane, 1};
        const mffv1::FrameView second_input{&second_plane, 1};
        mffv1::codec::SliceHeaderValues header;
        header.width = 1;
        header.height = 1;
        header.quant_table_set_indexes = {0, 0};
        const mffv1::codec::SliceEncoder encoder(stream);
        mffv1::codec::SliceState continued_state;
        mffv1::codec::SliceState fresh_state;
        std::vector<std::byte> first_payload;
        std::vector<std::byte> continued_payload;
        std::vector<std::byte> fresh_payload;

        ASSERT_TRUE(encoder.encode_slice(
            first_input,
            header,
            true,
            true,
            continued_state,
            first_payload).ok());
        ASSERT_TRUE(encoder.encode_slice(
            second_input,
            header,
            true,
            true,
            continued_state,
            continued_payload).ok());
        ASSERT_TRUE(encoder.encode_slice(
            second_input,
            header,
            true,
            true,
            fresh_state,
            fresh_payload).ok());

        EXPECT_EQ(continued_payload, fresh_payload);
    }
}

TEST(SliceEncoderTest, FailedStatefulEncodePreservesReferenceState)
{
    auto stream = make_stream();
    stream.intra_only = false;
    const std::array<std::uint8_t, 8> first{
        0, 1, 2, 3, 4, 5, 6, 7};
    const std::array<std::uint8_t, 8> second{
        7, 6, 5, 4, 3, 2, 1, 0};
    const auto first_plane = make_input_plane(first);
    const auto second_plane = make_input_plane(second);
    const mffv1::FrameView first_input{&first_plane, 1};
    const mffv1::FrameView second_input{&second_plane, 1};
    mffv1::codec::SliceHeaderValues header;
    header.width = 1;
    header.height = 1;
    header.quant_table_set_indexes = {0, 0};
    const mffv1::codec::SliceEncoder encoder(stream);
    mffv1::codec::SliceState state;
    std::vector<std::byte> first_payload;
    ASSERT_TRUE(encoder.encode_slice(
        first_input,
        header,
        true,
        true,
        state,
        first_payload).ok());
    const auto reference_state = state;

    auto invalid_plane = second_plane;
    invalid_plane.data = nullptr;
    const mffv1::FrameView invalid_input{&invalid_plane, 1};
    std::vector<std::byte> failed_payload{std::byte{0xaa}};
    const auto failed = encoder.encode_slice(
        invalid_input,
        header,
        true,
        false,
        state,
        failed_payload);
    EXPECT_FALSE(failed.ok());
    EXPECT_EQ(failed_payload, (std::vector<std::byte>{std::byte{0xaa}}));

    std::vector<std::byte> actual_payload;
    std::vector<std::byte> expected_payload;
    auto expected_state = reference_state;
    ASSERT_TRUE(encoder.encode_slice(
        second_input,
        header,
        true,
        false,
        state,
        actual_payload).ok());
    ASSERT_TRUE(encoder.encode_slice(
        second_input,
        header,
        true,
        false,
        expected_state,
        expected_payload).ok());
    EXPECT_EQ(actual_payload, expected_payload);
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
    stream.bits_per_raw_sample = 17;
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
