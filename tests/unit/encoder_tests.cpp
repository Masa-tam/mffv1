#include "mffv1/codec.hpp"

#include "codec/configuration_record_parser.hpp"
#include "codec/frame_decode_context.hpp"
#include "codec/frame_parser.hpp"
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

void expect_encode_input_rejection(mffv1::FrameView input,
                                   mffv1::ErrorCode code,
                                   const char* message)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(result.encoder->configure(stream, record).ok());
    mffv1::EncodedFrame frame;
    frame.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->encode_frame(input, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, code);
    EXPECT_EQ(status.message, message);
    EXPECT_EQ(frame.bytes, (std::vector<std::byte>{std::byte{0xaa}}));
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
    EXPECT_EQ(result.status.message, "encoder thread count must not be negative");
    EXPECT_EQ(result.encoder, nullptr);
}

TEST(EncoderTest, FactoryRejectsZeroKeyframeInterval)
{
    mffv1::EncoderOptions options;
    options.keyframe_interval = 0;

    const auto result = mffv1::create_encoder(options);

    EXPECT_FALSE(result.status.ok());
    EXPECT_EQ(result.status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(result.status.message, "encoder keyframe interval must be non-zero");
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

TEST(EncoderTest, ConfigureWritesNonIntraRecordForInterFrameCadence)
{
    mffv1::EncoderOptions options;
    options.keyframe_interval = 2;
    auto result = mffv1::create_encoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;

    const auto status = result.encoder->configure(stream, record);

    ASSERT_TRUE(status.ok()) << status.message;
    mffv1::syntax::StreamParameters parsed;
    const mffv1::codec::ConfigurationRecordParser parser;
    ASSERT_TRUE(parser.parse(record.bytes, parsed).ok());
    EXPECT_FALSE(parsed.intra_only);
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
    EXPECT_EQ(status.message,
              "encoder option version does not match stream version");
    ASSERT_EQ(record.bytes.size(), 1u);
    EXPECT_EQ(record.bytes[0], std::byte{0xaa});
}

TEST(EncoderTest, ConfigureRejectsUnsupportedVersionWithoutChangingOutput)
{
    mffv1::EncoderOptions options;
    options.version = 2;
    auto result = mffv1::create_encoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.version = 2;
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(status.message, "encoder supports only FFV1 version 3");
    EXPECT_EQ(record.bytes, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(EncoderTest, ConfigureRejectsUnsupportedEntropyModeWithoutChangingOutput)
{
    mffv1::EncoderOptions options;
    options.entropy_mode = static_cast<mffv1::EntropyMode>(99);
    auto result = mffv1::create_encoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(status.message, "encoder entropy mode is unsupported");
    EXPECT_EQ(record.bytes, (std::vector<std::byte>{std::byte{0xaa}}));
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
    EXPECT_EQ(status.message, "stream dimensions must be non-zero");
    ASSERT_EQ(record.bytes.size(), 1u);
    EXPECT_EQ(record.bytes[0], std::byte{0xaa});
}

TEST(EncoderTest, ConfigureRejectsZeroSliceGridWithoutChangingOutput)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.num_v_slices = 0;
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "encoder slice grid dimensions must be non-zero");
    ASSERT_EQ(record.bytes.size(), 1u);
    EXPECT_EQ(record.bytes[0], std::byte{0xaa});
}

TEST(EncoderTest, ConfigureAcceptsMultipleSlices)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.num_h_slices = 2;
    mffv1::ConfigurationRecord record;
    const auto status = result.encoder->configure(stream, record);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_FALSE(record.bytes.empty());
}

TEST(EncoderTest, ConfigureRejectsSliceGridWithEmptyChromaRegion)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.width = 3;
    stream.height = 2;
    stream.has_chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.num_h_slices = 3;
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "encoder slice grid would create an empty plane region");
    EXPECT_EQ(record.bytes, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(EncoderTest, ConfigureRejectsLargeFrameWithTooFewSlices)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.width = 353;
    stream.height = 288;
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message,
              "version 3 frames larger than CIF require at least four slices");
    EXPECT_EQ(record.bytes, (std::vector<std::byte>{std::byte{0xaa}}));
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
    EXPECT_EQ(status.message,
              "encoder supports only 4:4:4, 4:2:2, and 4:2:0 chroma geometry");
    ASSERT_EQ(record.bytes.size(), 1u);
    EXPECT_EQ(record.bytes[0], std::byte{0xaa});
}

TEST(EncoderTest, ConfigureRejectsUnsupportedBitDepthWithoutChangingOutput)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.bits_per_raw_sample = 17;
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(status.message,
              "encoder supports only 8-16 bit planar YCbCr or RGB streams, with an optional extra plane");
    EXPECT_EQ(record.bytes, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(EncoderTest, ConfigureRejectsUnsupportedColorSpaceWithoutChangingOutput)
{
    auto result = mffv1::create_encoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.color_space = static_cast<mffv1::ColorSpace>(99);
    mffv1::ConfigurationRecord record;
    record.bytes.push_back(std::byte{0xaa});

    const auto status = result.encoder->configure(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(status.message, "encoder color space is unsupported");
    EXPECT_EQ(record.bytes, (std::vector<std::byte>{std::byte{0xaa}}));
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
    EXPECT_EQ(status.message, "chroma subsampling requires chroma planes");
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
    EXPECT_EQ(status.message,
              "RGB streams require three full-resolution color planes");
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
    std::array<std::uint8_t, 128> storage{};
    auto plane = make_input_plane(storage);
    plane.info.stride_bytes = 15;
    const mffv1::FrameView input{&plane, 1};

    expect_encode_input_rejection(
        input,
        mffv1::ErrorCode::InvalidArgument,
        "plane stride is smaller than the stream requires");
}

TEST(EncoderTest, EncodeFrameRejectsMissingInputPlaneCount)
{
    const mffv1::FrameView input{nullptr, 0};

    expect_encode_input_rejection(
        input,
        mffv1::ErrorCode::InvalidArgument,
        "input frame plane count does not match the stream");
}

TEST(EncoderTest, EncodeFrameRejectsExtraInputPlaneCount)
{
    std::array<std::uint8_t, 128> y_storage{};
    std::array<std::uint8_t, 128> extra_storage{};
    std::array<mffv1::PlaneView, 2> planes{
        make_input_plane(y_storage),
        make_input_plane(extra_storage),
    };
    const mffv1::FrameView input{planes.data(), planes.size()};

    expect_encode_input_rejection(
        input,
        mffv1::ErrorCode::InvalidArgument,
        "input frame plane count does not match the stream");
}

TEST(EncoderTest, EncodeFrameRejectsNullInputPlaneArray)
{
    const mffv1::FrameView input{nullptr, 1};

    expect_encode_input_rejection(
        input,
        mffv1::ErrorCode::InvalidArgument,
        "input plane array is null");
}

TEST(EncoderTest, EncodeFrameRejectsNullInputPlaneData)
{
    std::array<std::uint8_t, 128> storage{};
    auto plane = make_input_plane(storage);
    plane.data = nullptr;
    const mffv1::FrameView input{&plane, 1};

    expect_encode_input_rejection(
        input,
        mffv1::ErrorCode::InvalidArgument,
        "input plane data pointer is null");
}

TEST(EncoderTest, EncodeFrameRejectsWrongInputPlaneRole)
{
    std::array<std::uint8_t, 128> storage{};
    auto plane = make_input_plane(storage);
    plane.info.role = mffv1::PlaneRole::Cb;
    const mffv1::FrameView input{&plane, 1};

    expect_encode_input_rejection(
        input,
        mffv1::ErrorCode::InvalidArgument,
        "plane role does not match stream plane order");
}

TEST(EncoderTest, EncodeFrameRejectsWrongInputSampleFormat)
{
    std::array<std::uint16_t, 128> storage{};
    std::array<std::uint8_t, 128> plane_info_storage{};
    auto plane = make_input_plane(plane_info_storage);
    plane.data = storage.data();
    plane.info.sample_format = mffv1::SampleFormat::UInt16;
    plane.info.stride_bytes = 32;
    const mffv1::FrameView input{&plane, 1};

    expect_encode_input_rejection(
        input,
        mffv1::ErrorCode::InvalidArgument,
        "plane sample format does not match stream bit depth");
}

TEST(EncoderTest, EncodeFrameRejectsInputPlaneDimensionMismatch)
{
    std::array<std::uint8_t, 128> storage{};
    auto plane = make_input_plane(storage);
    plane.info.width = 15;
    const mffv1::FrameView input{&plane, 1};

    expect_encode_input_rejection(
        input,
        mffv1::ErrorCode::InvalidArgument,
        "input plane dimensions do not match the stream");
}

TEST(EncoderTest, EncodeFrameRejectsNegativeInputStride)
{
    std::array<std::uint8_t, 128> storage{};
    auto plane = make_input_plane(storage);
    plane.info.stride_bytes = -1;
    const mffv1::FrameView input{&plane, 1};

    expect_encode_input_rejection(
        input,
        mffv1::ErrorCode::UnsupportedFeature,
        "negative input plane stride is not supported");
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
    EXPECT_EQ(info.micro_version, 4u);
    EXPECT_EQ(info.entropy_mode, mffv1::EntropyMode::Range);
    EXPECT_EQ(info.bits_per_raw_sample, stream.bits_per_raw_sample);
    EXPECT_EQ(info.plane_count, 1u);
    EXPECT_EQ(info.planes[0].role, mffv1::PlaneRole::Y);
    EXPECT_EQ(info.planes[0].sample_format, mffv1::SampleFormat::UInt8);
    EXPECT_EQ(info.planes[0].width, stream.width);
    EXPECT_EQ(info.planes[0].height, stream.height);
    EXPECT_EQ(info.planes[0].stride_bytes,
              static_cast<std::ptrdiff_t>(stream.width));
    EXPECT_EQ(info.color_space, stream.color_space);
    EXPECT_EQ(info.has_chroma_planes, stream.has_chroma_planes);
    EXPECT_EQ(info.has_extra_plane, stream.has_extra_plane);
    EXPECT_EQ(info.log2_h_chroma_subsample,
              stream.log2_h_chroma_subsample);
    EXPECT_EQ(info.log2_v_chroma_subsample,
              stream.log2_v_chroma_subsample);
    EXPECT_FALSE(info.error_status_enabled);
    EXPECT_TRUE(info.intra_only);
    EXPECT_TRUE(info.keyframe);
    EXPECT_EQ(info.slice_count, 1u);

    std::array<std::uint8_t, 128> decoded{};
    decoded.fill(0xee);
    auto output_plane = make_output_plane(decoded);
    mffv1::MutableFrameView output{&output_plane, 1};

    const auto status = decoder.decoder->decode_frame(frame.bytes, output);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decoded, source);
}

TEST(EncoderTest, PublicEncoderRoundTripsPaddedInputStride)
{
    auto encoder = mffv1::create_encoder({});
    ASSERT_TRUE(encoder.status.ok());
    ASSERT_NE(encoder.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(encoder.encoder->configure(stream, record).ok());

    std::array<std::uint8_t, 136> padded_source{};
    padded_source.fill(0xcc);
    std::array<std::uint8_t, 128> expected{};
    for (std::uint32_t y = 0; y < stream.height; ++y) {
        for (std::uint32_t x = 0; x < stream.width; ++x) {
            const auto value = static_cast<std::uint8_t>(
                (x * 17u + y * 41u + (x ^ y) * 9u) & 0xffu);
            padded_source[y * 17u + x] = value;
            expected[y * stream.width + x] = value;
        }
    }

    mffv1::PlaneView input_plane;
    input_plane.data = padded_source.data();
    input_plane.info.role = mffv1::PlaneRole::Y;
    input_plane.info.sample_format = mffv1::SampleFormat::UInt8;
    input_plane.info.width = stream.width;
    input_plane.info.height = stream.height;
    input_plane.info.stride_bytes = 17;
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

    std::array<std::uint8_t, 128> decoded{};
    decoded.fill(0xee);
    auto output_plane = make_output_plane(decoded);
    mffv1::MutableFrameView output{&output_plane, 1};

    const auto status = decoder.decoder->decode_frame(frame.bytes, output);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decoded, expected);
}

void expect_public_multi_slice_y_round_trip(mffv1::EntropyMode entropy_mode)
{
    mffv1::EncoderOptions encoder_options;
    encoder_options.entropy_mode = entropy_mode;
    auto encoder = mffv1::create_encoder(encoder_options);
    ASSERT_TRUE(encoder.status.ok());
    ASSERT_NE(encoder.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.num_h_slices = 2;
    stream.num_v_slices = 2;
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(encoder.encoder->configure(stream, record).ok());

    std::array<std::uint8_t, 128> source{};
    for (std::size_t index = 0; index < source.size(); ++index) {
        source[index] = static_cast<std::uint8_t>(
            (index * 43u + (index / 16u) * 61u) & 0xffu);
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
    EXPECT_EQ(info.entropy_mode, entropy_mode);
    EXPECT_EQ(info.color_space, stream.color_space);
    EXPECT_EQ(info.has_chroma_planes, stream.has_chroma_planes);
    EXPECT_EQ(info.has_extra_plane, stream.has_extra_plane);
    EXPECT_EQ(info.log2_h_chroma_subsample,
              stream.log2_h_chroma_subsample);
    EXPECT_EQ(info.log2_v_chroma_subsample,
              stream.log2_v_chroma_subsample);
    EXPECT_EQ(info.planes[0].role, mffv1::PlaneRole::Y);
    EXPECT_EQ(info.planes[0].width, stream.width);
    EXPECT_EQ(info.planes[0].height, stream.height);
    EXPECT_EQ(info.planes[0].stride_bytes,
              static_cast<std::ptrdiff_t>(stream.width));
    EXPECT_EQ(info.slice_count, 4u);

    std::array<std::uint8_t, 128> decoded{};
    decoded.fill(0xee);
    auto output_plane = make_output_plane(decoded);
    mffv1::MutableFrameView output{&output_plane, 1};

    const auto status = decoder.decoder->decode_frame(frame.bytes, output);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(decoded, source);
}

TEST(EncoderTest, PublicEncoderRoundTripsMultiSliceRangeFrame)
{
    expect_public_multi_slice_y_round_trip(mffv1::EntropyMode::Range);
}

TEST(EncoderTest, PublicEncoderRoundTripsMultiSliceGolombRiceFrame)
{
    expect_public_multi_slice_y_round_trip(mffv1::EntropyMode::GolombRice);
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

void expect_public_high_bit_y_round_trip(
    std::uint8_t bits_per_raw_sample,
    mffv1::EntropyMode entropy_mode = mffv1::EntropyMode::Range)
{
    mffv1::EncoderOptions options;
    options.entropy_mode = entropy_mode;
    auto encoder = mffv1::create_encoder(options);
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

TEST(EncoderTest, PublicEncoderRoundTripsSixteenBitGolombRiceSamples)
{
    expect_public_high_bit_y_round_trip(
        16, mffv1::EntropyMode::GolombRice);
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
    EXPECT_EQ(status.message, "input sample exceeds configured bit depth");
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    EXPECT_EQ(frame.bytes, (std::vector<std::byte>{std::byte{0xaa}}));
}

void expect_public_subsampled_round_trip(
    std::uint8_t log2_v_chroma_subsample,
    bool has_extra_plane = false,
    mffv1::EntropyMode entropy_mode = mffv1::EntropyMode::Range,
    std::uint32_t num_h_slices = 1,
    std::uint32_t num_v_slices = 1)
{
    mffv1::EncoderOptions options;
    options.entropy_mode = entropy_mode;
    auto encoder = mffv1::create_encoder(options);
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
    stream.num_h_slices = num_h_slices;
    stream.num_v_slices = num_v_slices;
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

    mffv1::FrameInfo info;
    ASSERT_TRUE(decoder.decoder->inspect_frame(frame.bytes, info).ok());
    EXPECT_EQ(info.color_space, stream.color_space);
    EXPECT_EQ(info.has_chroma_planes, stream.has_chroma_planes);
    EXPECT_EQ(info.has_extra_plane, stream.has_extra_plane);
    EXPECT_EQ(info.log2_h_chroma_subsample,
              stream.log2_h_chroma_subsample);
    EXPECT_EQ(info.log2_v_chroma_subsample,
              stream.log2_v_chroma_subsample);
    EXPECT_EQ(info.plane_count, has_extra_plane ? 4u : 3u);
    EXPECT_EQ(info.planes[0].role, mffv1::PlaneRole::Y);
    EXPECT_EQ(info.planes[1].role, mffv1::PlaneRole::Cb);
    EXPECT_EQ(info.planes[2].role, mffv1::PlaneRole::Cr);
    EXPECT_EQ(info.planes[0].sample_format, mffv1::SampleFormat::UInt8);
    EXPECT_EQ(info.planes[1].sample_format, mffv1::SampleFormat::UInt8);
    EXPECT_EQ(info.planes[2].sample_format, mffv1::SampleFormat::UInt8);
    EXPECT_EQ(info.planes[0].width, stream.width);
    EXPECT_EQ(info.planes[0].height, stream.height);
    EXPECT_EQ(info.planes[1].width, chroma_width);
    EXPECT_EQ(info.planes[1].height, chroma_height);
    EXPECT_EQ(info.planes[2].width, chroma_width);
    EXPECT_EQ(info.planes[2].height, chroma_height);
    EXPECT_EQ(info.planes[0].stride_bytes,
              static_cast<std::ptrdiff_t>(stream.width));
    EXPECT_EQ(info.planes[1].stride_bytes,
              static_cast<std::ptrdiff_t>(chroma_width));
    EXPECT_EQ(info.planes[2].stride_bytes,
              static_cast<std::ptrdiff_t>(chroma_width));
    if (has_extra_plane) {
        EXPECT_EQ(info.planes[3].role, mffv1::PlaneRole::Alpha);
        EXPECT_EQ(info.planes[3].sample_format, mffv1::SampleFormat::UInt8);
        EXPECT_EQ(info.planes[3].width, stream.width);
        EXPECT_EQ(info.planes[3].height, stream.height);
        EXPECT_EQ(info.planes[3].stride_bytes,
                  static_cast<std::ptrdiff_t>(stream.width));
    }

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

TEST(EncoderTest, PublicEncoderRoundTripsGolombRiceYcbcr420WithExtraPlane)
{
    expect_public_subsampled_round_trip(
        1, true, mffv1::EntropyMode::GolombRice);
}

TEST(EncoderTest, PublicEncoderRoundTripsOddSizedMultiSliceYcbcr420)
{
    expect_public_subsampled_round_trip(
        1, false, mffv1::EntropyMode::Range, 2, 2);
}

void expect_public_ten_bit_ycbcr420_with_extra_plane(
    mffv1::EntropyMode entropy_mode)
{
    mffv1::EncoderOptions options;
    options.entropy_mode = entropy_mode;
    auto encoder = mffv1::create_encoder(options);
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

TEST(EncoderTest, PublicEncoderRoundTripsTenBitYcbcr420WithExtraPlane)
{
    expect_public_ten_bit_ycbcr420_with_extra_plane(
        mffv1::EntropyMode::Range);
}

TEST(EncoderTest, PublicEncoderRoundTripsTenBitGolombRiceYcbcr420WithExtraPlane)
{
    expect_public_ten_bit_ycbcr420_with_extra_plane(
        mffv1::EntropyMode::GolombRice);
}

void expect_public_rgb_round_trip(std::uint8_t bits_per_raw_sample,
                                  bool has_extra_plane,
                                  mffv1::EntropyMode entropy_mode =
                                      mffv1::EntropyMode::Range)
{
    mffv1::EncoderOptions options;
    options.entropy_mode = entropy_mode;
    auto encoder = mffv1::create_encoder(options);
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

    mffv1::FrameInfo info;
    ASSERT_TRUE(decoder.decoder->inspect_frame(frame.bytes, info).ok());
    EXPECT_EQ(info.color_space, stream.color_space);
    EXPECT_EQ(info.has_chroma_planes, stream.has_chroma_planes);
    EXPECT_EQ(info.has_extra_plane, stream.has_extra_plane);
    EXPECT_EQ(info.log2_h_chroma_subsample,
              stream.log2_h_chroma_subsample);
    EXPECT_EQ(info.log2_v_chroma_subsample,
              stream.log2_v_chroma_subsample);
    EXPECT_EQ(info.plane_count, has_extra_plane ? 4u : 3u);
    EXPECT_EQ(info.planes[0].role, mffv1::PlaneRole::R);
    EXPECT_EQ(info.planes[1].role, mffv1::PlaneRole::G);
    EXPECT_EQ(info.planes[2].role, mffv1::PlaneRole::B);
    EXPECT_EQ(info.planes[0].sample_format, format);
    EXPECT_EQ(info.planes[1].sample_format, format);
    EXPECT_EQ(info.planes[2].sample_format, format);
    EXPECT_EQ(info.planes[0].width, stream.width);
    EXPECT_EQ(info.planes[1].width, stream.width);
    EXPECT_EQ(info.planes[2].width, stream.width);
    EXPECT_EQ(info.planes[0].height, stream.height);
    EXPECT_EQ(info.planes[1].height, stream.height);
    EXPECT_EQ(info.planes[2].height, stream.height);
    EXPECT_EQ(info.planes[0].stride_bytes, stride);
    EXPECT_EQ(info.planes[1].stride_bytes, stride);
    EXPECT_EQ(info.planes[2].stride_bytes, stride);
    if (has_extra_plane) {
        EXPECT_EQ(info.planes[3].role, mffv1::PlaneRole::Alpha);
        EXPECT_EQ(info.planes[3].sample_format, format);
        EXPECT_EQ(info.planes[3].width, stream.width);
        EXPECT_EQ(info.planes[3].height, stream.height);
        EXPECT_EQ(info.planes[3].stride_bytes, stride);
    }

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

TEST(EncoderTest, PublicEncoderRoundTripsEightBitGolombRiceRgb)
{
    expect_public_rgb_round_trip(
        8, false, mffv1::EntropyMode::GolombRice);
}

TEST(EncoderTest, PublicEncoderRoundTripsTenBitGolombRiceRgb)
{
    expect_public_rgb_round_trip(
        10, false, mffv1::EntropyMode::GolombRice);
}

TEST(EncoderTest, PublicEncoderRoundTripsTenBitGolombRiceRgba)
{
    expect_public_rgb_round_trip(
        10, true, mffv1::EntropyMode::GolombRice);
}

TEST(EncoderTest, PublicEncoderRoundTripsSixteenBitGolombRiceRgb)
{
    expect_public_rgb_round_trip(
        16, false, mffv1::EntropyMode::GolombRice);
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

TEST(EncoderTest, EncodesConfiguredNonKeyframes)
{
    mffv1::EncoderOptions options;
    options.keyframe_interval = 2;
    auto result = mffv1::create_encoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.num_h_slices = 2;
    stream.num_v_slices = 2;
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(result.encoder->configure(stream, record).ok());

    std::array<std::uint8_t, 128> first{};
    std::array<std::uint8_t, 128> second{};
    for (std::size_t index = 0; index < first.size(); ++index) {
        first[index] = static_cast<std::uint8_t>(
            (index * 37u + index / 16u) & 0xffu);
        second[index] = static_cast<std::uint8_t>(
            (255u - index * 19u + index / 8u) & 0xffu);
    }
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

    mffv1::FrameInfo info;
    ASSERT_TRUE(decoder.decoder->inspect_frame(first_frame.bytes, info).ok());
    EXPECT_EQ(info.entropy_mode, mffv1::EntropyMode::Range);
    EXPECT_FALSE(info.error_status_enabled);
    EXPECT_FALSE(info.intra_only);
    EXPECT_TRUE(info.keyframe);
    EXPECT_EQ(info.slice_count, 4u);
    ASSERT_TRUE(decoder.decoder->inspect_frame(second_frame.bytes, info).ok());
    EXPECT_FALSE(info.keyframe);
    EXPECT_EQ(info.slice_count, 4u);

    mffv1::syntax::StreamParameters parsed_stream;
    const mffv1::codec::ConfigurationRecordParser record_parser;
    ASSERT_TRUE(record_parser.parse(record.bytes, parsed_stream).ok());
    parsed_stream.width = stream.width;
    parsed_stream.height = stream.height;
    const mffv1::codec::FrameParser frame_parser(parsed_stream);
    mffv1::codec::FrameDecodeContext parsed_first;
    mffv1::codec::FrameDecodeContext parsed_second;
    ASSERT_TRUE(frame_parser.parse_with_range_header(
        first_frame.bytes, parsed_first).ok());
    ASSERT_TRUE(frame_parser.parse_with_range_header(
        second_frame.bytes, parsed_second).ok());
    EXPECT_TRUE(parsed_first.keyframe);
    EXPECT_FALSE(parsed_second.keyframe);

    std::array<std::uint8_t, 128> decoded{};
    auto output_plane = make_output_plane(decoded);
    mffv1::MutableFrameView output{&output_plane, 1};
    ASSERT_TRUE(decoder.decoder->decode_frame(first_frame.bytes, output).ok());
    EXPECT_EQ(decoded, first);

    decoded.fill(0);
    ASSERT_TRUE(decoder.decoder->decode_frame(second_frame.bytes, output).ok());
    EXPECT_EQ(decoded, second);
}

TEST(EncoderTest, EncodesConfiguredGolombRiceNonKeyframes)
{
    mffv1::EncoderOptions options;
    options.entropy_mode = mffv1::EntropyMode::GolombRice;
    options.keyframe_interval = 2;
    auto result = mffv1::create_encoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    stream.num_h_slices = 2;
    stream.num_v_slices = 2;
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(result.encoder->configure(stream, record).ok());

    std::array<std::uint8_t, 128> first{};
    std::array<std::uint8_t, 128> second{};
    for (std::size_t index = 0; index < first.size(); ++index) {
        first[index] = static_cast<std::uint8_t>(
            index < 32 ? 0 : (index * 13u) & 0xffu);
        second[index] = static_cast<std::uint8_t>(
            index < 48 ? 7 : (255u - index * 11u) & 0xffu);
    }
    const auto first_plane = make_input_plane(first);
    const auto second_plane = make_input_plane(second);
    const mffv1::FrameView first_input{&first_plane, 1};
    const mffv1::FrameView second_input{&second_plane, 1};
    mffv1::EncodedFrame first_frame;
    mffv1::EncodedFrame second_frame;

    ASSERT_TRUE(result.encoder->encode_frame(first_input, first_frame).ok());
    ASSERT_TRUE(result.encoder->encode_frame(second_input, second_frame).ok());

    mffv1::syntax::StreamParameters parsed_stream;
    const mffv1::codec::ConfigurationRecordParser record_parser;
    ASSERT_TRUE(record_parser.parse(record.bytes, parsed_stream).ok());
    parsed_stream.width = stream.width;
    parsed_stream.height = stream.height;
    const mffv1::codec::FrameParser frame_parser(parsed_stream);
    mffv1::codec::FrameDecodeContext parsed_first;
    mffv1::codec::FrameDecodeContext parsed_second;
    ASSERT_TRUE(frame_parser.parse_with_range_header(
        first_frame.bytes, parsed_first).ok());
    ASSERT_TRUE(frame_parser.parse_with_range_header(
        second_frame.bytes, parsed_second).ok());
    EXPECT_TRUE(parsed_first.keyframe);
    EXPECT_FALSE(parsed_second.keyframe);

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

TEST(EncoderTest, KeyframeIntervalReturnsToKeyframe)
{
    mffv1::EncoderOptions options;
    options.keyframe_interval = 2;
    auto result = mffv1::create_encoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(result.encoder->configure(stream, record).ok());

    std::array<std::uint8_t, 128> first{};
    std::array<std::uint8_t, 128> second{};
    std::array<std::uint8_t, 128> third{};
    for (std::size_t index = 0; index < first.size(); ++index) {
        first[index] = static_cast<std::uint8_t>(index & 0xffu);
        second[index] = static_cast<std::uint8_t>((index * 3u) & 0xffu);
        third[index] = static_cast<std::uint8_t>((255u - index * 5u) & 0xffu);
    }
    const auto first_plane = make_input_plane(first);
    const auto second_plane = make_input_plane(second);
    const auto third_plane = make_input_plane(third);
    const mffv1::FrameView first_input{&first_plane, 1};
    const mffv1::FrameView second_input{&second_plane, 1};
    const mffv1::FrameView third_input{&third_plane, 1};
    mffv1::EncodedFrame first_frame;
    mffv1::EncodedFrame second_frame;
    mffv1::EncodedFrame third_frame;

    ASSERT_TRUE(result.encoder->encode_frame(first_input, first_frame).ok());
    ASSERT_TRUE(result.encoder->encode_frame(second_input, second_frame).ok());
    ASSERT_TRUE(result.encoder->encode_frame(third_input, third_frame).ok());

    mffv1::syntax::StreamParameters parsed_stream;
    const mffv1::codec::ConfigurationRecordParser record_parser;
    ASSERT_TRUE(record_parser.parse(record.bytes, parsed_stream).ok());
    parsed_stream.width = stream.width;
    parsed_stream.height = stream.height;
    const mffv1::codec::FrameParser frame_parser(parsed_stream);
    mffv1::codec::FrameDecodeContext parsed_first;
    mffv1::codec::FrameDecodeContext parsed_second;
    mffv1::codec::FrameDecodeContext parsed_third;
    ASSERT_TRUE(frame_parser.parse_with_range_header(
        first_frame.bytes, parsed_first).ok());
    ASSERT_TRUE(frame_parser.parse_with_range_header(
        second_frame.bytes, parsed_second).ok());
    ASSERT_TRUE(frame_parser.parse_with_range_header(
        third_frame.bytes, parsed_third).ok());

    EXPECT_TRUE(parsed_first.keyframe);
    EXPECT_FALSE(parsed_second.keyframe);
    EXPECT_TRUE(parsed_third.keyframe);
}

TEST(EncoderTest, SuccessfulReconfigureResetsKeyframeCadence)
{
    mffv1::EncoderOptions options;
    options.keyframe_interval = 2;
    auto result = mffv1::create_encoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(result.encoder->configure(stream, record).ok());

    std::array<std::uint8_t, 128> first{};
    std::array<std::uint8_t, 128> second{};
    std::array<std::uint8_t, 128> after_reconfigure{};
    for (std::size_t index = 0; index < first.size(); ++index) {
        first[index] = static_cast<std::uint8_t>(index & 0xffu);
        second[index] = static_cast<std::uint8_t>((index * 3u) & 0xffu);
        after_reconfigure[index] =
            static_cast<std::uint8_t>((255u - index * 7u) & 0xffu);
    }
    const auto first_plane = make_input_plane(first);
    const auto second_plane = make_input_plane(second);
    const auto after_reconfigure_plane =
        make_input_plane(after_reconfigure);
    const mffv1::FrameView first_input{&first_plane, 1};
    const mffv1::FrameView second_input{&second_plane, 1};
    const mffv1::FrameView after_reconfigure_input{
        &after_reconfigure_plane, 1};
    mffv1::EncodedFrame first_frame;
    mffv1::EncodedFrame second_frame;
    mffv1::EncodedFrame reset_frame;
    ASSERT_TRUE(result.encoder->encode_frame(first_input, first_frame).ok());
    ASSERT_TRUE(result.encoder->encode_frame(second_input, second_frame).ok());

    ASSERT_TRUE(result.encoder->configure(stream, record).ok());
    ASSERT_TRUE(result.encoder->encode_frame(
        after_reconfigure_input, reset_frame).ok());

    mffv1::syntax::StreamParameters parsed_stream;
    const mffv1::codec::ConfigurationRecordParser record_parser;
    ASSERT_TRUE(record_parser.parse(record.bytes, parsed_stream).ok());
    parsed_stream.width = stream.width;
    parsed_stream.height = stream.height;
    const mffv1::codec::FrameParser frame_parser(parsed_stream);
    mffv1::codec::FrameDecodeContext parsed_second;
    mffv1::codec::FrameDecodeContext parsed_reset;
    ASSERT_TRUE(frame_parser.parse_with_range_header(
        second_frame.bytes, parsed_second).ok());
    ASSERT_TRUE(frame_parser.parse_with_range_header(
        reset_frame.bytes, parsed_reset).ok());

    EXPECT_FALSE(parsed_second.keyframe);
    EXPECT_TRUE(parsed_reset.keyframe);
}

TEST(EncoderTest, FailedReconfigurePreservesKeyframeCadence)
{
    mffv1::EncoderOptions options;
    options.keyframe_interval = 2;
    auto result = mffv1::create_encoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(result.encoder->configure(stream, record).ok());
    const auto original_record = record.bytes;

    std::array<std::uint8_t, 128> first{};
    std::array<std::uint8_t, 128> second{};
    for (std::size_t index = 0; index < first.size(); ++index) {
        first[index] = static_cast<std::uint8_t>(index & 0xffu);
        second[index] = static_cast<std::uint8_t>((index * 5u) & 0xffu);
    }
    const auto first_plane = make_input_plane(first);
    const auto second_plane = make_input_plane(second);
    const mffv1::FrameView first_input{&first_plane, 1};
    const mffv1::FrameView second_input{&second_plane, 1};
    mffv1::EncodedFrame first_frame;
    mffv1::EncodedFrame second_frame;
    ASSERT_TRUE(result.encoder->encode_frame(first_input, first_frame).ok());

    stream.bits_per_raw_sample = 17;
    ASSERT_FALSE(result.encoder->configure(stream, record).ok());
    EXPECT_EQ(record.bytes, original_record);
    ASSERT_TRUE(result.encoder->encode_frame(second_input, second_frame).ok());

    mffv1::syntax::StreamParameters parsed_stream;
    const mffv1::codec::ConfigurationRecordParser record_parser;
    ASSERT_TRUE(record_parser.parse(record.bytes, parsed_stream).ok());
    parsed_stream.width = stream.width;
    parsed_stream.height = stream.height;
    const mffv1::codec::FrameParser frame_parser(parsed_stream);
    mffv1::codec::FrameDecodeContext parsed_second;
    ASSERT_TRUE(frame_parser.parse_with_range_header(
        second_frame.bytes, parsed_second).ok());
    EXPECT_FALSE(parsed_second.keyframe);

    mffv1::DecoderOptions decoder_options;
    decoder_options.frame_width = parsed_stream.width;
    decoder_options.frame_height = parsed_stream.height;
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

TEST(EncoderTest, FailedEncodeDoesNotAdvanceKeyframeCadence)
{
    mffv1::EncoderOptions options;
    options.keyframe_interval = 2;
    auto result = mffv1::create_encoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(result.encoder->configure(stream, record).ok());

    std::array<std::uint8_t, 128> first{};
    std::array<std::uint8_t, 128> second{};
    second.fill(0x55);
    const auto first_plane = make_input_plane(first);
    auto second_plane = make_input_plane(second);
    const mffv1::FrameView first_input{&first_plane, 1};
    mffv1::FrameView second_input{&second_plane, 1};
    mffv1::EncodedFrame frame;
    ASSERT_TRUE(result.encoder->encode_frame(first_input, frame).ok());

    second_plane.info.stride_bytes = 15;
    frame.bytes.assign({std::byte{0xaa}});
    const auto failed_status = result.encoder->encode_frame(second_input, frame);
    ASSERT_FALSE(failed_status.ok());
    EXPECT_EQ(failed_status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(failed_status.message, "plane stride is smaller than the stream requires");
    EXPECT_FALSE(failed_status.location.has_slice_index);
    EXPECT_EQ(frame.bytes, (std::vector<std::byte>{std::byte{0xaa}}));

    second_plane.info.stride_bytes = 16;
    ASSERT_TRUE(result.encoder->encode_frame(second_input, frame).ok());

    mffv1::syntax::StreamParameters parsed_stream;
    const mffv1::codec::ConfigurationRecordParser record_parser;
    ASSERT_TRUE(record_parser.parse(record.bytes, parsed_stream).ok());
    parsed_stream.width = stream.width;
    parsed_stream.height = stream.height;
    const mffv1::codec::FrameParser frame_parser(parsed_stream);
    mffv1::codec::FrameDecodeContext parsed;
    ASSERT_TRUE(frame_parser.parse_with_range_header(frame.bytes, parsed).ok());
    EXPECT_FALSE(parsed.keyframe);
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
    EXPECT_EQ(status.message, "encoder is not configured");
    ASSERT_EQ(frame.bytes.size(), 1u);
    EXPECT_EQ(frame.bytes[0], std::byte{0xaa});
}

TEST(EncoderTest, PublicEncoderRoundTripsGolombRiceFrame)
{
    mffv1::EncoderOptions options;
    options.entropy_mode = mffv1::EntropyMode::GolombRice;
    auto result = mffv1::create_encoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.encoder, nullptr);
    const auto stream = make_initial_profile();
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(result.encoder->configure(stream, record).ok());
    std::array<std::uint8_t, 128> source{};
    for (std::size_t index = 0; index < source.size(); ++index) {
        source[index] = index < 24
            ? 0
            : static_cast<std::uint8_t>((index * 31u) & 0xffu);
    }
    const auto plane = make_input_plane(source);
    const mffv1::FrameView input{&plane, 1};
    mffv1::EncodedFrame frame;
    ASSERT_TRUE(result.encoder->encode_frame(input, frame).ok());

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

    ASSERT_TRUE(decoder.decoder->decode_frame(frame.bytes, output).ok());
    EXPECT_EQ(decoded, source);
}

TEST(EncoderTest, ConfigureRejectsInvalidGolombRiceRgbGeometry)
{
    mffv1::EncoderOptions options;
    options.entropy_mode = mffv1::EntropyMode::GolombRice;
    auto result = mffv1::create_encoder(options);
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
    EXPECT_EQ(status.message,
              "RGB streams require three full-resolution color planes");
    EXPECT_EQ(record.bytes, (std::vector<std::byte>{std::byte{0xaa}}));
}

} // namespace
