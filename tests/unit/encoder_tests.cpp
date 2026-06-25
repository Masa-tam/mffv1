#include "mffv1/codec.hpp"

#include "util/crc32.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

mffv1::StreamInfo make_initial_profile()
{
    mffv1::StreamInfo stream;
    stream.width = 16;
    stream.height = 8;
    stream.version = 3;
    stream.bits_per_raw_sample = 8;
    stream.log2_h_chroma_subsample = 0;
    stream.log2_v_chroma_subsample = 0;
    stream.has_chroma_planes = false;
    stream.has_extra_plane = false;
    return stream;
}

mffv1::PlaneView make_input_plane(const std::array<std::uint8_t, 128>& storage)
{
    mffv1::PlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 16;
    plane.info.height = 8;
    plane.info.stride_bytes = 16;
    return plane;
}

mffv1::MutablePlaneView make_output_plane(
    std::array<std::uint8_t, 128>& storage)
{
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = 16;
    plane.info.height = 8;
    plane.info.stride_bytes = 16;
    return plane;
}

TEST(EncoderTest, FactoryCreatesEncoder)
{
    const auto result = mffv1::create_encoder({});

    EXPECT_TRUE(result.status.ok());
    EXPECT_NE(result.encoder, nullptr);
}

TEST(EncoderTest, FactoryRejectsNegativeThreadCount)
{
    mffv1::EncoderOptions options;
    options.thread_count = -1;

    const auto result = mffv1::create_encoder(options);

    EXPECT_FALSE(result.status.ok());
    EXPECT_EQ(result.status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(result.encoder, nullptr);
}

TEST(EncoderTest, ConfigureWritesRecordAcceptedByPublicDecoder)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;

    const auto status = result.encoder->configure(stream, record);

    ASSERT_TRUE(status.ok()) << status.message;
    ASSERT_FALSE(record.bytes.empty());
    EXPECT_EQ(mffv1::util::crc32_ieee_msb(record.bytes), 0u);

    mffv1::DecoderOptions decoder_options;
    decoder_options.frame_width = stream.width;
    decoder_options.frame_height = stream.height;
    auto decoder = mffv1::create_decoder(decoder_options);
    ASSERT_TRUE(decoder.status.ok());
    ASSERT_NE(decoder.decoder, nullptr);
    EXPECT_TRUE(decoder.decoder->configure(record.bytes).ok());
}

TEST(EncoderTest, ConfigureRejectsVersionMismatchWithoutChangingOutput)
{
    mffv1::EncoderOptions options;
    options.version = 3;
    auto result = mffv1::create_encoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.version = 1;
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    ASSERT_EQ(record.bytes.size(), 1u);
    EXPECT_EQ(record.bytes[0], std::byte{0xaa});
}

TEST(EncoderTest, ConfigureRejectsZeroDimensionsWithoutChangingOutput)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.width = 0;
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    ASSERT_EQ(record.bytes.size(), 1u);
    EXPECT_EQ(record.bytes[0], std::byte{0xaa});
}

TEST(EncoderTest, ConfigureRejectsUnsupportedProfileWithoutChangingOutput)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.has_chroma_planes = true;
    stream.log2_h_chroma_subsample = 2;
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    ASSERT_EQ(record.bytes.size(), 1u);
    EXPECT_EQ(record.bytes[0], std::byte{0xaa});
}

TEST(EncoderTest, ConfigureRejectsSubsamplingWithoutChroma)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.log2_h_chroma_subsample = 1;
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(record.bytes, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(EncoderTest, ConfigureRejectsInvalidRgbPlaneGeometry)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.color_space = mffv1::ColorSpace::Rgb;
    stream.has_chroma_planes = false;
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(record.bytes, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(EncoderTest, FailedReconfigurePreservesUsablePreviousConfiguration)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(result.encoder->configure(stream, record).ok());
    const auto original_record = record.bytes;

    stream.bits_per_raw_sample = 17;
    ASSERT_FALSE(result.encoder->configure(stream, record).ok());
    EXPECT_EQ(record.bytes, original_record);
    mffv1::EncodedFrame frame;
    frame.bytes.push_back(std::byte{0xaa});
    std::array<std::uint8_t, 128> storage{};
    const auto plane = make_input_plane(storage);
    const mffv1::FrameView input{&plane, 1};

    const auto status = result.encoder->encode_frame(input, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_GT(frame.bytes.size(), 3u);
}

TEST(EncoderTest, EncodeFrameRejectsInvalidInputWithoutChangingOutput)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(result.encoder->configure(stream, record).ok());
    std::array<std::uint8_t, 128> storage{};
    auto plane = make_input_plane(storage);
    plane.info.stride_bytes = 15;
    const mffv1::FrameView input{&plane, 1};
    mffv1::EncodedFrame frame;
    frame.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->encode_frame(input, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    ASSERT_EQ(frame.bytes.size(), 1u);
    EXPECT_EQ(frame.bytes[0], std::byte{0xaa});
}

TEST(EncoderTest, EncodeFrameProducesCompleteFrame)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(result.encoder->configure(stream, record).ok());
    std::array<std::uint8_t, 128> storage{};
    const auto plane = make_input_plane(storage);
    const mffv1::FrameView input{&plane, 1};
    mffv1::EncodedFrame frame;
    frame.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->encode_frame(input, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_GT(frame.bytes.size(), 3u);
    const auto encoded_size = frame.bytes.size();
    EXPECT_EQ(
        (static_cast<std::uint32_t>(frame.bytes[encoded_size - 3]) << 16)
            | (static_cast<std::uint32_t>(frame.bytes[encoded_size - 2]) << 8)
            | static_cast<std::uint32_t>(frame.bytes[encoded_size - 1]),
        static_cast<std::uint32_t>(encoded_size));
}

TEST(EncoderTest, PublicEncoderRoundTripsThroughPublicDecoder)
{
    auto encoder = mffv1::create_encoder({});
    ASSERT_TRUE(encoder.status.ok());
    ASSERT_NE(encoder.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(encoder.encoder->configure(stream, record).ok());

    std::array<std::uint8_t, 128> source{};
    for (std::size_t index = 0; index < source.size(); ++index) {
        source[index] = static_cast<std::uint8_t>(
            (index * 73u + (index / 16u) * 29u) & 0xffu);
    }
    const auto input_plane = make_input_plane(source);
    const mffv1::FrameView input{&input_plane, 1};
    mffv1::EncodedFrame frame;
    ASSERT_TRUE(encoder.encoder->encode_frame(input, frame).ok());

    mffv1::DecoderOptions decoder_options;
    decoder_options.frame_width = stream.width;
    decoder_options.frame_height = stream.height;
    auto decoder = mffv1::create_decoder(decoder_options);
    ASSERT_TRUE(decoder.status.ok());
    ASSERT_NE(decoder.decoder, nullptr);
    ASSERT_TRUE(decoder.decoder->configure(record.bytes).ok());

    mffv1::FrameInfo info;
    ASSERT_TRUE(decoder.decoder->inspect_frame(frame.bytes, info).ok());
    EXPECT_EQ(info.width, stream.width);
    EXPECT_EQ(info.height, stream.height);
    EXPECT_EQ(info.version, stream.version);
    EXPECT_EQ(info.bits_per_raw_sample, stream.bits_per_raw_sample);
    EXPECT_EQ(info.plane_count, 1u);

    std::array<std::uint8_t, 128> decoded{};
    decoded.fill(0xee);
    auto output_plane = make_output_plane(decoded);
    mffv1::MutableFrameView output{&output_plane, 1};

    const auto status = decoder.decoder->decode_frame(frame.bytes, output);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decoded, source);
}

TEST(EncoderTest, PublicEncoderRoundTripsPlanarYcbcr444)
{
    auto encoder = mffv1::create_encoder({});
    ASSERT_TRUE(encoder.status.ok());
    ASSERT_NE(encoder.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.has_chroma_planes = true;
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(encoder.encoder->configure(stream, record).ok());

    std::array<std::uint8_t, 128> y{};
    std::array<std::uint8_t, 128> cb{};
    std::array<std::uint8_t, 128> cr{};
    for (std::size_t index = 0; index < y.size(); ++index) {
        y[index] = static_cast<std::uint8_t>((index * 73u) & 0xffu);
        cb[index] = static_cast<std::uint8_t>((128u + index * 29u) & 0xffu);
        cr[index] = static_cast<std::uint8_t>((255u - index * 17u) & 0xffu);
    }
    std::array<mffv1::PlaneView, 3> input_planes{};
    input_planes[0] = {
        y.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 16, 8, 16},
    };
    input_planes[1] = {
        cb.data(),
        {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 16, 8, 16},
    };
    input_planes[2] = {
        cr.data(),
        {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 16, 8, 16},
    };
    const mffv1::FrameView input{
        input_planes.data(), input_planes.size()};
    mffv1::EncodedFrame frame;
    ASSERT_TRUE(encoder.encoder->encode_frame(input, frame).ok());

    mffv1::DecoderOptions decoder_options;
    decoder_options.frame_width = stream.width;
    decoder_options.frame_height = stream.height;
    auto decoder = mffv1::create_decoder(decoder_options);
    ASSERT_TRUE(decoder.status.ok());
    ASSERT_NE(decoder.decoder, nullptr);
    ASSERT_TRUE(decoder.decoder->configure(record.bytes).ok());

    std::array<std::uint8_t, 128> decoded_y{};
    std::array<std::uint8_t, 128> decoded_cb{};
    std::array<std::uint8_t, 128> decoded_cr{};
    std::array<mffv1::MutablePlaneView, 3> output_planes{};
    output_planes[0] = {
        decoded_y.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 16, 8, 16},
    };
    output_planes[1] = {
        decoded_cb.data(),
        {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt8, 16, 8, 16},
    };
    output_planes[2] = {
        decoded_cr.data(),
        {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt8, 16, 8, 16},
    };
    mffv1::MutableFrameView output{
        output_planes.data(), output_planes.size()};

    const auto status =
        decoder.decoder->decode_frame(frame.bytes, output);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decoded_y, y);
    EXPECT_EQ(decoded_cb, cb);
    EXPECT_EQ(decoded_cr, cr);
}

TEST(EncoderTest, PublicEncoderRoundTripsYWithExtraPlane)
{
    auto encoder = mffv1::create_encoder({});
    ASSERT_TRUE(encoder.status.ok());
    ASSERT_NE(encoder.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.has_extra_plane = true;
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(encoder.encoder->configure(stream, record).ok());

    std::array<std::uint8_t, 128> y{};
    std::array<std::uint8_t, 128> alpha{};
    for (std::size_t index = 0; index < y.size(); ++index) {
        y[index] = static_cast<std::uint8_t>((index * 43u) & 0xffu);
        alpha[index] =
            static_cast<std::uint8_t>((255u - index * 31u) & 0xffu);
    }
    std::array<mffv1::PlaneView, 2> input_planes{};
    input_planes[0] = {
        y.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 16, 8, 16},
    };
    input_planes[1] = {
        alpha.data(),
        {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt8, 16, 8, 16},
    };
    const mffv1::FrameView input{
        input_planes.data(), input_planes.size()};
    mffv1::EncodedFrame frame;
    ASSERT_TRUE(encoder.encoder->encode_frame(input, frame).ok());

    mffv1::DecoderOptions decoder_options;
    decoder_options.frame_width = stream.width;
    decoder_options.frame_height = stream.height;
    auto decoder = mffv1::create_decoder(decoder_options);
    ASSERT_TRUE(decoder.status.ok());
    ASSERT_NE(decoder.decoder, nullptr);
    ASSERT_TRUE(decoder.decoder->configure(record.bytes).ok());

    std::array<std::uint8_t, 128> decoded_y{};
    std::array<std::uint8_t, 128> decoded_alpha{};
    std::array<mffv1::MutablePlaneView, 2> output_planes{};
    output_planes[0] = {
        decoded_y.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 16, 8, 16},
    };
    output_planes[1] = {
        decoded_alpha.data(),
        {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt8, 16, 8, 16},
    };
    mffv1::MutableFrameView output{
        output_planes.data(), output_planes.size()};

    ASSERT_TRUE(decoder.decoder->decode_frame(frame.bytes, output).ok());
    EXPECT_EQ(decoded_y, y);
    EXPECT_EQ(decoded_alpha, alpha);
}

void expect_public_high_bit_y_round_trip(std::uint8_t bits_per_raw_sample)
{
    auto encoder = mffv1::create_encoder({});
    ASSERT_TRUE(encoder.status.ok());
    ASSERT_NE(encoder.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.width = 7;
    stream.height = 3;
    stream.bits_per_raw_sample = bits_per_raw_sample;
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(encoder.encoder->configure(stream, record).ok());

    const std::uint32_t maximum_sample =
        bits_per_raw_sample == 16
        ? 0xffffu
        : (std::uint32_t{1} << bits_per_raw_sample) - 1u;
    std::array<std::uint16_t, 21> source{};
    for (std::size_t index = 0; index < source.size(); ++index) {
        source[index] = static_cast<std::uint16_t>(
            (index * 8191u + index * index * 17u) & maximum_sample);
    }
    source[1] = static_cast<std::uint16_t>(maximum_sample);
    source[2] = static_cast<std::uint16_t>((maximum_sample + 1u) / 2u);
    const mffv1::PlaneView input_plane{
        source.data(),
        {mffv1::PlaneRole::Y,
         mffv1::SampleFormat::UInt16,
         7,
         3,
         14},
    };
    const mffv1::FrameView input{&input_plane, 1};
    mffv1::EncodedFrame frame;
    ASSERT_TRUE(encoder.encoder->encode_frame(input, frame).ok());

    mffv1::DecoderOptions decoder_options;
    decoder_options.frame_width = stream.width;
    decoder_options.frame_height = stream.height;
    auto decoder = mffv1::create_decoder(decoder_options);
    ASSERT_TRUE(decoder.status.ok());
    ASSERT_NE(decoder.decoder, nullptr);
    ASSERT_TRUE(decoder.decoder->configure(record.bytes).ok());

    std::array<std::uint16_t, 21> decoded{};
    mffv1::MutablePlaneView output_plane{
        decoded.data(),
        {mffv1::PlaneRole::Y,
         mffv1::SampleFormat::UInt16,
         7,
         3,
         14},
    };
    mffv1::MutableFrameView output{&output_plane, 1};

    ASSERT_TRUE(decoder.decoder->decode_frame(frame.bytes, output).ok());
    EXPECT_EQ(decoded, source);
}

TEST(EncoderTest, PublicEncoderRoundTripsTenBitPlanarSamples)
{
    expect_public_high_bit_y_round_trip(10);
}

TEST(EncoderTest, PublicEncoderRoundTripsSixteenBitSignedPredictionBoundaries)
{
    expect_public_high_bit_y_round_trip(16);
}

TEST(EncoderTest, RejectsInputSampleAboveConfiguredBitDepth)
{
    auto encoder = mffv1::create_encoder({});
    ASSERT_TRUE(encoder.status.ok());
    ASSERT_NE(encoder.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.width = 2;
    stream.height = 1;
    stream.bits_per_raw_sample = 10;
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(encoder.encoder->configure(stream, record).ok());
    const std::array<std::uint16_t, 2> source{0, 1024};
    const mffv1::PlaneView input_plane{
        source.data(),
        {mffv1::PlaneRole::Y,
         mffv1::SampleFormat::UInt16,
         2,
         1,
         4},
    };
    const mffv1::FrameView input{&input_plane, 1};
    mffv1::EncodedFrame frame;
    frame.bytes.push_back(std::byte{0xaa});

    const auto status = encoder.encoder->encode_frame(input, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(frame.bytes, (std::vector<std::byte>{std::byte{0xaa}}));
}

void expect_public_subsampled_round_trip(
    std::uint8_t log2_v_chroma_subsample,
    bool has_extra_plane = false)
{
    auto encoder = mffv1::create_encoder({});
    ASSERT_TRUE(encoder.status.ok());
    ASSERT_NE(encoder.encoder, nullptr);
    mffv1::StreamInfo stream;
    stream.width = 5;
    stream.height = 3;
    stream.version = 3;
    stream.bits_per_raw_sample = 8;
    stream.has_chroma_planes = true;
    stream.has_extra_plane = has_extra_plane;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = log2_v_chroma_subsample;
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(encoder.encoder->configure(stream, record).ok());

    const std::uint32_t chroma_width = 3;
    const std::uint32_t chroma_height =
        log2_v_chroma_subsample == 0 ? 3u : 2u;
    std::vector<std::uint8_t> y(15);
    std::vector<std::uint8_t> cb(chroma_width * chroma_height);
    std::vector<std::uint8_t> cr(chroma_width * chroma_height);
    std::vector<std::uint8_t> alpha(15);
    for (std::size_t index = 0; index < y.size(); ++index) {
        y[index] = static_cast<std::uint8_t>((index * 37u) & 0xffu);
        alpha[index] =
            static_cast<std::uint8_t>((255u - index * 11u) & 0xffu);
    }
    for (std::size_t index = 0; index < cb.size(); ++index) {
        cb[index] = static_cast<std::uint8_t>((128u + index * 19u) & 0xffu);
        cr[index] = static_cast<std::uint8_t>((255u - index * 23u) & 0xffu);
    }
    std::array<mffv1::PlaneView, 4> input_planes{};
    input_planes[0] = {
        y.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 5, 3, 5},
    };
    input_planes[1] = {
        cb.data(),
        {mffv1::PlaneRole::Cb,
         mffv1::SampleFormat::UInt8,
         chroma_width,
         chroma_height,
         static_cast<std::ptrdiff_t>(chroma_width)},
    };
    input_planes[2] = {
        cr.data(),
        {mffv1::PlaneRole::Cr,
         mffv1::SampleFormat::UInt8,
         chroma_width,
         chroma_height,
         static_cast<std::ptrdiff_t>(chroma_width)},
    };
    input_planes[3] = {
        alpha.data(),
        {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt8, 5, 3, 5},
    };
    const mffv1::FrameView input{
        input_planes.data(), has_extra_plane ? 4u : 3u};
    mffv1::EncodedFrame frame;
    ASSERT_TRUE(encoder.encoder->encode_frame(input, frame).ok());

    mffv1::DecoderOptions decoder_options;
    decoder_options.frame_width = stream.width;
    decoder_options.frame_height = stream.height;
    auto decoder = mffv1::create_decoder(decoder_options);
    ASSERT_TRUE(decoder.status.ok());
    ASSERT_NE(decoder.decoder, nullptr);
    ASSERT_TRUE(decoder.decoder->configure(record.bytes).ok());

    std::vector<std::uint8_t> decoded_y(y.size());
    std::vector<std::uint8_t> decoded_cb(cb.size());
    std::vector<std::uint8_t> decoded_cr(cr.size());
    std::vector<std::uint8_t> decoded_alpha(alpha.size());
    std::array<mffv1::MutablePlaneView, 4> output_planes{};
    output_planes[0] = {
        decoded_y.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt8, 5, 3, 5},
    };
    output_planes[1] = {
        decoded_cb.data(),
        {mffv1::PlaneRole::Cb,
         mffv1::SampleFormat::UInt8,
         chroma_width,
         chroma_height,
         static_cast<std::ptrdiff_t>(chroma_width)},
    };
    output_planes[2] = {
        decoded_cr.data(),
        {mffv1::PlaneRole::Cr,
         mffv1::SampleFormat::UInt8,
         chroma_width,
         chroma_height,
         static_cast<std::ptrdiff_t>(chroma_width)},
    };
    output_planes[3] = {
        decoded_alpha.data(),
        {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt8, 5, 3, 5},
    };
    mffv1::MutableFrameView output{
        output_planes.data(), has_extra_plane ? 4u : 3u};

    ASSERT_TRUE(decoder.decoder->decode_frame(frame.bytes, output).ok());
    EXPECT_EQ(decoded_y, y);
    EXPECT_EQ(decoded_cb, cb);
    EXPECT_EQ(decoded_cr, cr);
    if (has_extra_plane) {
        EXPECT_EQ(decoded_alpha, alpha);
    }
}

TEST(EncoderTest, PublicEncoderRoundTripsOddSizedYcbcr422)
{
    expect_public_subsampled_round_trip(0);
}

TEST(EncoderTest, PublicEncoderRoundTripsOddSizedYcbcr420)
{
    expect_public_subsampled_round_trip(1);
}

TEST(EncoderTest, PublicEncoderRoundTripsOddSizedYcbcr420WithExtraPlane)
{
    expect_public_subsampled_round_trip(1, true);
}

TEST(EncoderTest, PublicEncoderRoundTripsTenBitYcbcr420WithExtraPlane)
{
    auto encoder = mffv1::create_encoder({});
    ASSERT_TRUE(encoder.status.ok());
    ASSERT_NE(encoder.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.width = 5;
    stream.height = 3;
    stream.bits_per_raw_sample = 10;
    stream.has_chroma_planes = true;
    stream.has_extra_plane = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(encoder.encoder->configure(stream, record).ok());

    std::array<std::uint16_t, 15> y{};
    std::array<std::uint16_t, 6> cb{};
    std::array<std::uint16_t, 6> cr{};
    std::array<std::uint16_t, 15> alpha{};
    for (std::size_t index = 0; index < y.size(); ++index) {
        y[index] = static_cast<std::uint16_t>((index * 251u) & 0x3ffu);
        alpha[index] =
            static_cast<std::uint16_t>((1023u - index * 47u) & 0x3ffu);
    }
    for (std::size_t index = 0; index < cb.size(); ++index) {
        cb[index] =
            static_cast<std::uint16_t>((512u + index * 83u) & 0x3ffu);
        cr[index] =
            static_cast<std::uint16_t>((1023u - index * 101u) & 0x3ffu);
    }
    std::array<mffv1::PlaneView, 4> input_planes{};
    input_planes[0] = {
        y.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt16, 5, 3, 10},
    };
    input_planes[1] = {
        cb.data(),
        {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt16, 3, 2, 6},
    };
    input_planes[2] = {
        cr.data(),
        {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt16, 3, 2, 6},
    };
    input_planes[3] = {
        alpha.data(),
        {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt16, 5, 3, 10},
    };
    const mffv1::FrameView input{
        input_planes.data(), input_planes.size()};
    mffv1::EncodedFrame frame;
    ASSERT_TRUE(encoder.encoder->encode_frame(input, frame).ok());

    mffv1::DecoderOptions decoder_options;
    decoder_options.frame_width = stream.width;
    decoder_options.frame_height = stream.height;
    auto decoder = mffv1::create_decoder(decoder_options);
    ASSERT_TRUE(decoder.status.ok());
    ASSERT_NE(decoder.decoder, nullptr);
    ASSERT_TRUE(decoder.decoder->configure(record.bytes).ok());

    std::array<std::uint16_t, 15> decoded_y{};
    std::array<std::uint16_t, 6> decoded_cb{};
    std::array<std::uint16_t, 6> decoded_cr{};
    std::array<std::uint16_t, 15> decoded_alpha{};
    std::array<mffv1::MutablePlaneView, 4> output_planes{};
    output_planes[0] = {
        decoded_y.data(),
        {mffv1::PlaneRole::Y, mffv1::SampleFormat::UInt16, 5, 3, 10},
    };
    output_planes[1] = {
        decoded_cb.data(),
        {mffv1::PlaneRole::Cb, mffv1::SampleFormat::UInt16, 3, 2, 6},
    };
    output_planes[2] = {
        decoded_cr.data(),
        {mffv1::PlaneRole::Cr, mffv1::SampleFormat::UInt16, 3, 2, 6},
    };
    output_planes[3] = {
        decoded_alpha.data(),
        {mffv1::PlaneRole::Alpha, mffv1::SampleFormat::UInt16, 5, 3, 10},
    };
    mffv1::MutableFrameView output{
        output_planes.data(), output_planes.size()};

    ASSERT_TRUE(decoder.decoder->decode_frame(frame.bytes, output).ok());
    EXPECT_EQ(decoded_y, y);
    EXPECT_EQ(decoded_cb, cb);
    EXPECT_EQ(decoded_cr, cr);
    EXPECT_EQ(decoded_alpha, alpha);
}

void expect_public_rgb_round_trip(std::uint8_t bits_per_raw_sample,
                                  bool has_extra_plane)
{
    auto encoder = mffv1::create_encoder({});
    ASSERT_TRUE(encoder.status.ok());
    ASSERT_NE(encoder.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.width = 5;
    stream.height = 3;
    stream.bits_per_raw_sample = bits_per_raw_sample;
    stream.has_chroma_planes = true;
    stream.has_extra_plane = has_extra_plane;
    stream.color_space = mffv1::ColorSpace::Rgb;
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(encoder.encoder->configure(stream, record).ok());

    const std::uint32_t maximum =
        bits_per_raw_sample == 16
        ? 0xffffu
        : (std::uint32_t{1} << bits_per_raw_sample) - 1u;
    std::array<std::uint16_t, 15> r{};
    std::array<std::uint16_t, 15> g{};
    std::array<std::uint16_t, 15> b{};
    std::array<std::uint16_t, 15> alpha{};
    for (std::size_t index = 0; index < r.size(); ++index) {
        r[index] =
            static_cast<std::uint16_t>((index * 251u) & maximum);
        g[index] =
            static_cast<std::uint16_t>((maximum - index * 47u) & maximum);
        b[index] =
            static_cast<std::uint16_t>((index * index * 73u) & maximum);
        alpha[index] =
            static_cast<std::uint16_t>((maximum - index * 19u) & maximum);
    }

    const auto format = bits_per_raw_sample <= 8
        ? mffv1::SampleFormat::UInt8
        : mffv1::SampleFormat::UInt16;
    const std::ptrdiff_t stride =
        bits_per_raw_sample <= 8 ? 5 : 10;
    std::array<std::uint8_t, 15> r8{};
    std::array<std::uint8_t, 15> g8{};
    std::array<std::uint8_t, 15> b8{};
    std::array<std::uint8_t, 15> alpha8{};
    if (bits_per_raw_sample <= 8) {
        for (std::size_t index = 0; index < r.size(); ++index) {
            r8[index] = static_cast<std::uint8_t>(r[index]);
            g8[index] = static_cast<std::uint8_t>(g[index]);
            b8[index] = static_cast<std::uint8_t>(b[index]);
            alpha8[index] = static_cast<std::uint8_t>(alpha[index]);
        }
    }
    std::array<mffv1::PlaneView, 4> input_planes{};
    input_planes[0] = {
        bits_per_raw_sample <= 8
            ? static_cast<const void*>(r8.data())
            : static_cast<const void*>(r.data()),
        {mffv1::PlaneRole::R, format, 5, 3, stride},
    };
    input_planes[1] = {
        bits_per_raw_sample <= 8
            ? static_cast<const void*>(g8.data())
            : static_cast<const void*>(g.data()),
        {mffv1::PlaneRole::G, format, 5, 3, stride},
    };
    input_planes[2] = {
        bits_per_raw_sample <= 8
            ? static_cast<const void*>(b8.data())
            : static_cast<const void*>(b.data()),
        {mffv1::PlaneRole::B, format, 5, 3, stride},
    };
    input_planes[3] = {
        bits_per_raw_sample <= 8
            ? static_cast<const void*>(alpha8.data())
            : static_cast<const void*>(alpha.data()),
        {mffv1::PlaneRole::Alpha, format, 5, 3, stride},
    };
    const mffv1::FrameView input{
        input_planes.data(), has_extra_plane ? 4u : 3u};
    mffv1::EncodedFrame frame;
    ASSERT_TRUE(encoder.encoder->encode_frame(input, frame).ok());

    mffv1::DecoderOptions decoder_options;
    decoder_options.frame_width = stream.width;
    decoder_options.frame_height = stream.height;
    auto decoder = mffv1::create_decoder(decoder_options);
    ASSERT_TRUE(decoder.status.ok());
    ASSERT_NE(decoder.decoder, nullptr);
    ASSERT_TRUE(decoder.decoder->configure(record.bytes).ok());

    std::array<std::uint16_t, 15> decoded_r{};
    std::array<std::uint16_t, 15> decoded_g{};
    std::array<std::uint16_t, 15> decoded_b{};
    std::array<std::uint16_t, 15> decoded_alpha{};
    std::array<std::uint8_t, 15> decoded_r8{};
    std::array<std::uint8_t, 15> decoded_g8{};
    std::array<std::uint8_t, 15> decoded_b8{};
    std::array<std::uint8_t, 15> decoded_alpha8{};
    std::array<mffv1::MutablePlaneView, 4> output_planes{};
    output_planes[0] = {
        bits_per_raw_sample <= 8
            ? static_cast<void*>(decoded_r8.data())
            : static_cast<void*>(decoded_r.data()),
        {mffv1::PlaneRole::R, format, 5, 3, stride},
    };
    output_planes[1] = {
        bits_per_raw_sample <= 8
            ? static_cast<void*>(decoded_g8.data())
            : static_cast<void*>(decoded_g.data()),
        {mffv1::PlaneRole::G, format, 5, 3, stride},
    };
    output_planes[2] = {
        bits_per_raw_sample <= 8
            ? static_cast<void*>(decoded_b8.data())
            : static_cast<void*>(decoded_b.data()),
        {mffv1::PlaneRole::B, format, 5, 3, stride},
    };
    output_planes[3] = {
        bits_per_raw_sample <= 8
            ? static_cast<void*>(decoded_alpha8.data())
            : static_cast<void*>(decoded_alpha.data()),
        {mffv1::PlaneRole::Alpha, format, 5, 3, stride},
    };
    mffv1::MutableFrameView output{
        output_planes.data(), has_extra_plane ? 4u : 3u};
    ASSERT_TRUE(decoder.decoder->decode_frame(frame.bytes, output).ok());

    if (bits_per_raw_sample <= 8) {
        EXPECT_EQ(decoded_r8, r8);
        EXPECT_EQ(decoded_g8, g8);
        EXPECT_EQ(decoded_b8, b8);
        if (has_extra_plane) {
            EXPECT_EQ(decoded_alpha8, alpha8);
        }
    } else {
        EXPECT_EQ(decoded_r, r);
        EXPECT_EQ(decoded_g, g);
        EXPECT_EQ(decoded_b, b);
        if (has_extra_plane) {
            EXPECT_EQ(decoded_alpha, alpha);
        }
    }
}

TEST(EncoderTest, PublicEncoderRoundTripsEightBitRgb)
{
    expect_public_rgb_round_trip(8, false);
}

TEST(EncoderTest, PublicEncoderRoundTripsTenBitRgbCompatibilityTransform)
{
    expect_public_rgb_round_trip(10, false);
}

TEST(EncoderTest, PublicEncoderRoundTripsTenBitRgba)
{
    expect_public_rgb_round_trip(10, true);
}

TEST(EncoderTest, PublicEncoderRoundTripsSixteenBitRgb)
{
    expect_public_rgb_round_trip(16, false);
}

TEST(EncoderTest, EncodesSuccessiveFramesAsIndependentKeyframes)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(result.encoder->configure(stream, record).ok());

    std::array<std::uint8_t, 128> first{};
    std::array<std::uint8_t, 128> second{};
    second.fill(0xff);
    const auto first_plane = make_input_plane(first);
    const auto second_plane = make_input_plane(second);
    const mffv1::FrameView first_input{&first_plane, 1};
    const mffv1::FrameView second_input{&second_plane, 1};
    mffv1::EncodedFrame first_frame;
    mffv1::EncodedFrame second_frame;

    ASSERT_TRUE(result.encoder->encode_frame(first_input, first_frame).ok());
    ASSERT_TRUE(result.encoder->encode_frame(second_input, second_frame).ok());

    mffv1::DecoderOptions decoder_options;
    decoder_options.frame_width = stream.width;
    decoder_options.frame_height = stream.height;
    auto decoder = mffv1::create_decoder(decoder_options);
    ASSERT_TRUE(decoder.status.ok());
    ASSERT_NE(decoder.decoder, nullptr);
    ASSERT_TRUE(decoder.decoder->configure(record.bytes).ok());

    std::array<std::uint8_t, 128> decoded{};
    auto output_plane = make_output_plane(decoded);
    mffv1::MutableFrameView output{&output_plane, 1};
    ASSERT_TRUE(decoder.decoder->decode_frame(first_frame.bytes, output).ok());
    EXPECT_EQ(decoded, first);

    decoded.fill(0);
    ASSERT_TRUE(decoder.decoder->decode_frame(second_frame.bytes, output).ok());
    EXPECT_EQ(decoded, second);
}

TEST(EncoderTest, EncodeFrameRequiresConfigurationWithoutChangingOutput)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    mffv1::EncodedFrame frame;
    frame.bytes.push_back(std::byte{0xaa});
    const mffv1::FrameView input{};

    const auto status = result.encoder->encode_frame(input, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    ASSERT_EQ(frame.bytes.size(), 1u);
    EXPECT_EQ(frame.bytes[0], std::byte{0xaa});
}

TEST(EncoderTest, ConfigureRejectsGolombRiceOption)
{
    mffv1::EncoderOptions options;
    options.entropy_mode = mffv1::EntropyMode::GolombRice;
    auto result = mffv1::create_encoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_TRUE(record.bytes.empty());
}

} // namespace
