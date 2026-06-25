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
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    ASSERT_EQ(record.bytes.size(), 1u);
    EXPECT_EQ(record.bytes[0], std::byte{0xaa});
}

TEST(EncoderTest, FailedReconfigurePreservesPreviousConfiguration)
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

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::NotImplemented);
    ASSERT_EQ(frame.bytes.size(), 1u);
    EXPECT_EQ(frame.bytes[0], std::byte{0xaa});
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

TEST(EncoderTest, EncodeFrameAcceptsValidInputBeforeCoding)
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

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::NotImplemented);
    ASSERT_EQ(frame.bytes.size(), 1u);
    EXPECT_EQ(frame.bytes[0], std::byte{0xaa});
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
