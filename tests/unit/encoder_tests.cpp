#include "mffv1/codec.hpp"

#include "util/crc32.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

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
    stream.log2_h_chroma_subsample = 1;
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    ASSERT_EQ(record.bytes.size(), 1u);
    EXPECT_EQ(record.bytes[0], std::byte{0xaa});
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

    stream.has_extra_plane = true;
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
