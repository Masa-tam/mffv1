#include "mffv1/codec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

std::array<std::byte, 3> minimal_v0_y_only_configuration_record()
{
    return {
        std::byte{0x95},
        std::byte{0x36},
        std::byte{0xe9},
    };
}

std::array<std::byte, 3> minimal_v0_golomb_rice_y_only_configuration_record()
{
    return {
        std::byte{0xe7},
        std::byte{0x7a},
        std::byte{0xe2},
    };
}

std::array<std::byte, 2> minimal_v0_rgb_configuration_record()
{
    return {
        std::byte{0x86},
        std::byte{0x92},
    };
}

std::array<std::byte, 2> minimal_v0_golomb_rice_rgb_configuration_record()
{
    return {
        std::byte{0xc9},
        std::byte{0x41},
    };
}

std::array<std::byte, 18> minimal_v3_y_only_configuration_record()
{
    return {
        std::byte{0x56}, std::byte{0x00}, std::byte{0x2f}, std::byte{0xa3},
        std::byte{0x67}, std::byte{0x6a}, std::byte{0x28}, std::byte{0x5e},
        std::byte{0x8f}, std::byte{0x6f}, std::byte{0x2b}, std::byte{0x13},
        std::byte{0x3d}, std::byte{0x00}, std::byte{0x6a}, std::byte{0x49},
        std::byte{0x41}, std::byte{0xa4},
    };
}

std::array<std::byte, 2> zero_scalar_payload()
{
    return {
        std::byte{0xff},
        std::byte{0x00},
    };
}

std::array<std::byte, 16> zero_frame_payload()
{
    return {
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
    };
}

mffv1::Status configure_minimal_v0_y_only(mffv1::IDecoder& decoder)
{
    const auto configuration_record = minimal_v0_y_only_configuration_record();
    return decoder.configure(configuration_record);
}

mffv1::Status configure_minimal_v0_golomb_rice_y_only(mffv1::IDecoder& decoder)
{
    const auto configuration_record = minimal_v0_golomb_rice_y_only_configuration_record();
    return decoder.configure(configuration_record);
}

mffv1::Status configure_minimal_v0_rgb(mffv1::IDecoder& decoder)
{
    const auto configuration_record = minimal_v0_rgb_configuration_record();
    return decoder.configure(configuration_record);
}

mffv1::Status configure_minimal_v0_golomb_rice_rgb(mffv1::IDecoder& decoder)
{
    const auto configuration_record = minimal_v0_golomb_rice_rgb_configuration_record();
    return decoder.configure(configuration_record);
}

mffv1::Status configure_minimal_v3_y_only(mffv1::IDecoder& decoder)
{
    const auto configuration_record = minimal_v3_y_only_configuration_record();
    return decoder.configure(configuration_record);
}

mffv1::MutablePlaneView make_y_plane(std::uint8_t* data,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    std::ptrdiff_t stride_bytes)
{
    mffv1::MutablePlaneView plane;
    plane.data = data;
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt8;
    plane.info.width = width;
    plane.info.height = height;
    plane.info.stride_bytes = stride_bytes;
    return plane;
}

TEST(DecoderTest, FactoryCreatesDecoder)
{
    const auto result = mffv1::create_decoder({});

    EXPECT_TRUE(result.status.ok());
    EXPECT_NE(result.decoder, nullptr);
}

TEST(DecoderTest, FactoryAcceptsExternalFrameDimensions)
{
    mffv1::DecoderOptions options;
    options.frame_width = 16;
    options.frame_height = 8;

    const auto result = mffv1::create_decoder(options);

    EXPECT_TRUE(result.status.ok());
    EXPECT_NE(result.decoder, nullptr);
}

TEST(DecoderTest, FactoryRejectsIncompleteExternalFrameDimensions)
{
    mffv1::DecoderOptions options;
    options.frame_width = 16;

    const auto result = mffv1::create_decoder(options);

    EXPECT_FALSE(result.status.ok());
    EXPECT_EQ(result.status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(result.status.message,
              "decoder frame dimensions must be both set or both zero");
    EXPECT_EQ(result.decoder, nullptr);
}

TEST(DecoderTest, FactoryRejectsNegativeThreadCount)
{
    mffv1::DecoderOptions options;
    options.thread_count = -1;

    const auto result = mffv1::create_decoder(options);

    EXPECT_FALSE(result.status.ok());
    EXPECT_EQ(result.status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(result.status.message, "decoder thread count must not be negative");
    EXPECT_EQ(result.decoder, nullptr);
}

TEST(DecoderTest, ConfigureRejectsEmptyConfigurationRecord)
{
    const auto result = mffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const mffv1::ByteSpan empty;
    const auto status = result.decoder->configure(empty);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "configuration record is empty");
}

TEST(DecoderTest, ConfigureRejectsTooShortRangeCoderPayload)
{
    const auto result = mffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const std::array<std::byte, 1> record{std::byte{0x00}};
    const auto status = result.decoder->configure(record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(DecoderTest, ConfigureRejectsVersionThreeRecordTooSmallForCrc)
{
    const auto result = mffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    auto configuration_record = minimal_v3_y_only_configuration_record();
    const mffv1::ByteSpan truncated_record{configuration_record.data(), 3};

    const auto status = result.decoder->configure(truncated_record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "configuration record is too small for CRC parity");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(DecoderTest, ConfigureAcceptsVersionThreeRecordWithValidCrc)
{
    const auto result = mffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const auto status = configure_minimal_v3_y_only(*result.decoder);

    EXPECT_TRUE(status.ok()) << status.message;
}

TEST(DecoderTest, ConfigureRejectsVersionThreeRecordWithCrcMismatch)
{
    const auto result = mffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    auto configuration_record = minimal_v3_y_only_configuration_record();
    configuration_record.back() ^= std::byte{0x01};

    const auto status = result.decoder->configure(configuration_record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::CrcMismatch);
    EXPECT_EQ(status.message, "configuration record CRC remainder is non-zero");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 14u);
}

TEST(DecoderTest, ConfigureChecksVersionThreeCrcBeforeParameterSyntax)
{
    const auto original = minimal_v3_y_only_configuration_record();
    constexpr std::size_t range_coder_initial_bytes = 2;
    constexpr std::size_t crc_size = 4;

    for (std::size_t offset = range_coder_initial_bytes;
         offset < original.size() - crc_size;
         ++offset) {
        const auto result = mffv1::create_decoder({});
        ASSERT_TRUE(result.status.ok());
        ASSERT_NE(result.decoder, nullptr);
        auto damaged = original;
        damaged[offset] ^= std::byte{0xff};

        const auto status = result.decoder->configure(damaged);

        EXPECT_FALSE(status.ok()) << "offset=" << offset;
        EXPECT_EQ(status.code, mffv1::ErrorCode::CrcMismatch) << "offset=" << offset;
        EXPECT_EQ(status.message, "configuration record CRC remainder is non-zero") << "offset=" << offset;
        EXPECT_TRUE(status.location.has_byte_offset) << "offset=" << offset;
        EXPECT_EQ(status.location.byte_offset, original.size() - crc_size) << "offset=" << offset;
    }
}

TEST(DecoderTest, FailedReconfigurePreservesPreviousConfiguration)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());
    auto damaged_record = minimal_v3_y_only_configuration_record();
    damaged_record.back() ^= std::byte{0x01};

    const auto configure_status = result.decoder->configure(damaged_record);

    EXPECT_FALSE(configure_status.ok());
    EXPECT_EQ(configure_status.code, mffv1::ErrorCode::CrcMismatch);

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    mffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_scalar_payload();
    const auto decode_status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(decode_status.ok()) << decode_status.message;
    EXPECT_EQ(storage[0], 0u);
}

TEST(DecoderTest, DecodeRequiresConfiguration)
{
    const auto result = mffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const std::array<std::byte, 1> payload{std::byte{0x00}};
    mffv1::MutableFrameView output{};
    const auto status = result.decoder->decode_frame(payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "decoder is not configured");
}

TEST(DecoderTest, InspectFrameRequiresConfiguration)
{
    const auto result = mffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const std::array<std::byte, 1> payload{std::byte{0x00}};
    mffv1::FrameInfo info;
    const auto status = result.decoder->inspect_frame(payload, info);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "decoder is not configured");
}

TEST(DecoderTest, InspectFrameRequiresExternalDimensions)
{
    const auto result = mffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    const std::array<std::byte, 1> frame_payload{std::byte{0x00}};
    mffv1::FrameInfo info;
    const auto status = result.decoder->inspect_frame(frame_payload, info);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "decoder frame dimensions are not configured");
}

TEST(DecoderTest, InspectFrameRejectsEmptyPayload)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    const mffv1::ByteSpan frame_payload;
    mffv1::FrameInfo info;
    const auto status = result.decoder->inspect_frame(frame_payload, info);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "frame payload is empty");
}

TEST(DecoderTest, InspectFrameFailurePreservesOutputInfo)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    mffv1::FrameInfo info;
    info.width = 99;
    info.height = 77;
    info.version = 2;
    info.micro_version = 3;
    info.entropy_mode = mffv1::EntropyMode::GolombRice;
    info.bits_per_raw_sample = 16;
    info.plane_count = 4;
    info.planes[0].role = mffv1::PlaneRole::Alpha;
    info.planes[0].width = 5;
    info.color_space = mffv1::ColorSpace::Rgb;
    info.has_chroma_planes = true;
    info.has_extra_plane = true;
    info.keyframe = true;
    info.slice_count = 6;
    const mffv1::ByteSpan frame_payload;

    const auto status = result.decoder->inspect_frame(frame_payload, info);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "frame payload is empty");
    EXPECT_EQ(info.width, 99u);
    EXPECT_EQ(info.height, 77u);
    EXPECT_EQ(info.version, 2u);
    EXPECT_EQ(info.micro_version, 3u);
    EXPECT_EQ(info.entropy_mode, mffv1::EntropyMode::GolombRice);
    EXPECT_EQ(info.bits_per_raw_sample, 16u);
    EXPECT_EQ(info.plane_count, 4u);
    EXPECT_EQ(info.planes[0].role, mffv1::PlaneRole::Alpha);
    EXPECT_EQ(info.planes[0].width, 5u);
    EXPECT_EQ(info.color_space, mffv1::ColorSpace::Rgb);
    EXPECT_TRUE(info.has_chroma_planes);
    EXPECT_TRUE(info.has_extra_plane);
    EXPECT_TRUE(info.keyframe);
    EXPECT_EQ(info.slice_count, 6u);
}

TEST(DecoderTest, DecodeFrameRequiresExternalDimensions)
{
    const auto result = mffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{};
    auto plane = make_y_plane(storage.data(), 1, 1, 1);
    mffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "decoder frame dimensions are not configured");
}

TEST(DecoderTest, DecodeFrameRejectsMissingOutputPlanes)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    mffv1::MutableFrameView output;
    output.planes = nullptr;
    output.plane_count = 1;
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "output plane array is null");
}

TEST(DecoderTest, DecodeFrameRejectsNullOutputPlaneData)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    auto plane = make_y_plane(nullptr, options.frame_width, options.frame_height, 1);
    mffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "output plane data pointer is null");
}

TEST(DecoderTest, DecodeFrameRejectsShortOutputStride)
{
    mffv1::DecoderOptions options;
    options.frame_width = 2;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 2> storage{0xee, 0xdd};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    mffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "plane stride is smaller than the stream requires");
    EXPECT_EQ(storage[0], 0xee);
    EXPECT_EQ(storage[1], 0xdd);
}

TEST(DecoderTest, DecodeFrameRejectsNegativeOutputStride)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, -1);
    mffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "plane stride is smaller than the stream requires");
    EXPECT_EQ(storage[0], 0xee);
}

TEST(DecoderTest, DecodeFrameRejectsWrongOutputPlaneRole)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    plane.info.role = mffv1::PlaneRole::Cb;
    mffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "plane role does not match stream plane order");
    EXPECT_EQ(storage[0], 0xee);
}

TEST(DecoderTest, DecodeFrameRejectsSmallOutputPlaneDimensions)
{
    mffv1::DecoderOptions options;
    options.frame_width = 2;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 2> storage{0xee, 0xdd};
    auto plane = make_y_plane(storage.data(), 1, options.frame_height, 2);
    mffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "plane dimensions are smaller than the stream requires");
    EXPECT_EQ(storage[0], 0xee);
    EXPECT_EQ(storage[1], 0xdd);
}

TEST(DecoderTest, DecodeFrameRejectsWrongOutputSampleFormat)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint16_t, 1> storage{0xeedd};
    mffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = mffv1::PlaneRole::Y;
    plane.info.sample_format = mffv1::SampleFormat::UInt16;
    plane.info.width = options.frame_width;
    plane.info.height = options.frame_height;
    plane.info.stride_bytes = 2;
    mffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "plane sample format does not match stream bit depth");
    EXPECT_EQ(storage[0], 0xeeddu);
}

TEST(DecoderTest, DecodeFrameRejectsMissingRequiredPlaneCount)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    mffv1::MutableFrameView output;
    output.planes = nullptr;
    output.plane_count = 0;
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "output frame does not have enough planes");
}

TEST(DecoderTest, DecodeFrameIgnoresExtraOutputPlanes)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> y_storage{0xee};
    std::array<std::uint8_t, 1> extra_storage{0xdd};
    std::array<mffv1::MutablePlaneView, 2> planes{};
    planes[0] = make_y_plane(y_storage.data(), options.frame_width, options.frame_height, 1);
    planes[1].data = extra_storage.data();
    planes[1].info.role = mffv1::PlaneRole::Alpha;
    planes[1].info.sample_format = mffv1::SampleFormat::UInt8;
    planes[1].info.width = options.frame_width;
    planes[1].info.height = options.frame_height;
    planes[1].info.stride_bytes = 1;
    mffv1::MutableFrameView output{planes.data(), planes.size()};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(y_storage[0], 0u);
    EXPECT_EQ(extra_storage[0], 0xdd);
}

TEST(DecoderTest, DecodeFrameIgnoresMalformedExtraOutputPlanes)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> y_storage{0xee};
    std::array<mffv1::MutablePlaneView, 2> planes{};
    planes[0] = make_y_plane(y_storage.data(), options.frame_width, options.frame_height, 1);
    planes[1].data = nullptr;
    planes[1].info.role = mffv1::PlaneRole::Cb;
    planes[1].info.sample_format = mffv1::SampleFormat::UInt16;
    planes[1].info.width = 0;
    planes[1].info.height = 0;
    planes[1].info.stride_bytes = -1;
    mffv1::MutableFrameView output{planes.data(), planes.size()};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(y_storage[0], 0u);
}

TEST(DecoderTest, DecodeFrameRejectsEmptyPayload)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    mffv1::MutableFrameView output{&plane, 1};
    const mffv1::ByteSpan frame_payload;

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "frame payload is empty");
    EXPECT_EQ(storage[0], 0xee);
}

TEST(DecoderTest, DecodeFrameRejectsTooShortSliceRangePayload)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 1> frame_payload{std::byte{0xff}};

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_FALSE(status.location.has_slice_index);
    EXPECT_EQ(storage[0], 0xee);
}

TEST(DecoderTest, DecodeFramePreservesSliceLocationFromRangeHeaderFailure)
{
    mffv1::StreamInfo stream;
    stream.width = 16;
    stream.height = 8;
    stream.has_chroma_planes = false;
    stream.num_h_slices = 2;
    stream.num_v_slices = 1;
    auto encoder = mffv1::create_encoder({});
    ASSERT_TRUE(encoder.status.ok());
    ASSERT_NE(encoder.encoder, nullptr);
    mffv1::ConfigurationRecord record;
    ASSERT_TRUE(encoder.encoder->configure(stream, record).ok());

    mffv1::DecoderOptions options;
    options.frame_width = stream.width;
    options.frame_height = stream.height;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    ASSERT_TRUE(result.decoder->configure(record.bytes).ok());

    std::array<std::uint8_t, 128> storage{};
    storage.fill(0xee);
    auto plane = make_y_plane(storage.data(), stream.width, stream.height, 16);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array malformed_frame_payload{
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x03},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x03},
    };

    const auto status = result.decoder->decode_frame(malformed_frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    EXPECT_TRUE(status.location.has_byte_offset);
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0xee);
    }
}

TEST(DecoderTest, InspectFrameUsesExternalDimensions)
{
    mffv1::DecoderOptions options;
    options.frame_width = 16;
    options.frame_height = 8;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    const auto frame_payload = zero_scalar_payload();
    mffv1::FrameInfo info;
    const auto status = result.decoder->inspect_frame(frame_payload, info);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(info.width, options.frame_width);
    EXPECT_EQ(info.height, options.frame_height);
    EXPECT_EQ(info.version, 0u);
    EXPECT_EQ(info.micro_version, 0u);
    EXPECT_EQ(info.entropy_mode, mffv1::EntropyMode::Range);
    EXPECT_EQ(info.bits_per_raw_sample, 8u);
    EXPECT_EQ(info.plane_count, 1u);
    EXPECT_EQ(info.planes.size(), mffv1::kMaxFramePlanes);
    EXPECT_EQ(info.planes[0].role, mffv1::PlaneRole::Y);
    EXPECT_EQ(info.planes[0].sample_format, mffv1::SampleFormat::UInt8);
    EXPECT_EQ(info.planes[0].width, options.frame_width);
    EXPECT_EQ(info.planes[0].height, options.frame_height);
    EXPECT_EQ(info.planes[0].stride_bytes, 16);
    EXPECT_EQ(info.color_space, mffv1::ColorSpace::YCbCr);
    EXPECT_FALSE(info.has_chroma_planes);
    EXPECT_FALSE(info.has_extra_plane);
    EXPECT_EQ(info.log2_h_chroma_subsample, 0u);
    EXPECT_EQ(info.log2_v_chroma_subsample, 0u);
    EXPECT_FALSE(info.error_status_enabled);
    EXPECT_FALSE(info.intra_only);
    EXPECT_TRUE(info.keyframe);
    EXPECT_EQ(info.slice_count, 1u);
}

TEST(DecoderTest, DecodesMinimalVersionThreeFrameThroughPublicApi)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    ASSERT_TRUE(configure_minimal_v3_y_only(*result.decoder).ok());
    const std::array<std::byte, 5> frame_payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };

    mffv1::FrameInfo info;
    auto status = result.decoder->inspect_frame(frame_payload, info);
    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(info.version, 3u);
    EXPECT_EQ(info.micro_version, 4u);
    EXPECT_EQ(info.entropy_mode, mffv1::EntropyMode::Range);
    EXPECT_EQ(info.width, 1u);
    EXPECT_EQ(info.height, 1u);
    EXPECT_EQ(info.bits_per_raw_sample, 8u);
    EXPECT_EQ(info.plane_count, 1u);
    EXPECT_EQ(info.planes[0].role, mffv1::PlaneRole::Y);
    EXPECT_EQ(info.planes[0].sample_format, mffv1::SampleFormat::UInt8);
    EXPECT_EQ(info.planes[0].width, 1u);
    EXPECT_EQ(info.planes[0].height, 1u);
    EXPECT_EQ(info.planes[0].stride_bytes, 1);
    EXPECT_EQ(info.color_space, mffv1::ColorSpace::YCbCr);
    EXPECT_FALSE(info.has_chroma_planes);
    EXPECT_FALSE(info.has_extra_plane);
    EXPECT_EQ(info.log2_h_chroma_subsample, 0u);
    EXPECT_EQ(info.log2_v_chroma_subsample, 0u);
    EXPECT_FALSE(info.error_status_enabled);
    EXPECT_TRUE(info.intra_only);
    EXPECT_TRUE(info.keyframe);
    EXPECT_EQ(info.slice_count, 1u);

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), 1, 1, 1);
    mffv1::MutableFrameView output{&plane, 1};
    status = result.decoder->decode_frame(frame_payload, output);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
}

TEST(DecoderTest, InspectFrameReportsLegacyNonKeyframe)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    const std::array keyframe_payload{std::byte{0xff}, std::byte{0x00}};
    const std::array non_keyframe_payload{std::byte{0x70}, std::byte{0x00}};
    mffv1::FrameInfo info;

    ASSERT_TRUE(result.decoder->inspect_frame(keyframe_payload, info).ok());
    EXPECT_TRUE(info.keyframe);
    ASSERT_TRUE(result.decoder->inspect_frame(non_keyframe_payload, info).ok());
    EXPECT_FALSE(info.keyframe);
    EXPECT_FALSE(info.intra_only);
}

TEST(DecoderTest, InspectFrameDoesNotSeedReferenceState)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    const std::array keyframe_payload{std::byte{0xff}, std::byte{0x00}};
    mffv1::FrameInfo info;
    ASSERT_TRUE(result.decoder->inspect_frame(keyframe_payload, info).ok());
    EXPECT_TRUE(info.keyframe);

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), 1, 1, 1);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array non_keyframe_payload{std::byte{0x70}, std::byte{0x00}};

    const auto status = result.decoder->decode_frame(non_keyframe_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidState);
    EXPECT_EQ(status.message, "non-keyframe requires reference slice states");
    EXPECT_EQ(storage[0], 0xee);
}

TEST(DecoderTest, DecodeFrameWritesZeroYOnlyFrame)
{
    mffv1::DecoderOptions options;
    options.frame_width = 4;
    options.frame_height = 2;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 4);
    mffv1::MutableFrameView output{&plane, 1};

    const auto frame_payload = zero_frame_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(DecoderTest, DecodeFrameWritesRgbFrameThroughPublicApi)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    ASSERT_TRUE(configure_minimal_v0_rgb(*result.decoder).ok());

    const auto frame_payload = zero_scalar_payload();
    mffv1::FrameInfo info;
    auto status = result.decoder->inspect_frame(frame_payload, info);
    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(info.version, 0u);
    EXPECT_EQ(info.bits_per_raw_sample, 8u);
    EXPECT_EQ(info.plane_count, 3u);
    EXPECT_EQ(info.planes[0].role, mffv1::PlaneRole::R);
    EXPECT_EQ(info.planes[1].role, mffv1::PlaneRole::G);
    EXPECT_EQ(info.planes[2].role, mffv1::PlaneRole::B);
    EXPECT_EQ(info.planes[0].sample_format, mffv1::SampleFormat::UInt8);
    EXPECT_EQ(info.planes[1].sample_format, mffv1::SampleFormat::UInt8);
    EXPECT_EQ(info.planes[2].sample_format, mffv1::SampleFormat::UInt8);
    EXPECT_EQ(info.planes[0].width, 1u);
    EXPECT_EQ(info.planes[1].width, 1u);
    EXPECT_EQ(info.planes[2].width, 1u);
    EXPECT_EQ(info.planes[0].height, 1u);
    EXPECT_EQ(info.planes[1].height, 1u);
    EXPECT_EQ(info.planes[2].height, 1u);
    EXPECT_EQ(info.planes[0].stride_bytes, 1);
    EXPECT_EQ(info.planes[1].stride_bytes, 1);
    EXPECT_EQ(info.planes[2].stride_bytes, 1);
    EXPECT_EQ(info.color_space, mffv1::ColorSpace::Rgb);
    EXPECT_TRUE(info.has_chroma_planes);
    EXPECT_FALSE(info.has_extra_plane);
    EXPECT_EQ(info.log2_h_chroma_subsample, 0u);
    EXPECT_EQ(info.log2_v_chroma_subsample, 0u);

    std::array<std::uint8_t, 1> r{0xee};
    std::array<std::uint8_t, 1> g{0xee};
    std::array<std::uint8_t, 1> b{0xee};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    mffv1::MutableFrameView output{planes.data(), planes.size()};

    status = result.decoder->decode_frame(frame_payload, output);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(r[0], 128u);
    EXPECT_EQ(g[0], 128u);
    EXPECT_EQ(b[0], 128u);
}

TEST(DecoderTest, DecodeFrameWritesGolombRiceRgbThroughPublicApi)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    ASSERT_TRUE(configure_minimal_v0_golomb_rice_rgb(*result.decoder).ok());

    std::array<std::uint8_t, 1> r{0xee};
    std::array<std::uint8_t, 1> g{0xee};
    std::array<std::uint8_t, 1> b{0xee};
    std::array<mffv1::MutablePlaneView, 3> planes{};
    planes[0] = {r.data(), {mffv1::PlaneRole::R, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[1] = {g.data(), {mffv1::PlaneRole::G, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    planes[2] = {b.data(), {mffv1::PlaneRole::B, mffv1::SampleFormat::UInt8, 1, 1, 1}};
    mffv1::MutableFrameView output{planes.data(), planes.size()};
    const std::array frame_payload{std::byte{0xf0}};

    const auto status = result.decoder->decode_frame(frame_payload, output);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(r[0], 128u);
    EXPECT_EQ(g[0], 128u);
    EXPECT_EQ(b[0], 128u);
}

TEST(DecoderTest, DecodeFrameWritesLegacyGolombRiceFrame)
{
    mffv1::DecoderOptions options;
    options.frame_width = 4;
    options.frame_height = 2;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    ASSERT_TRUE(configure_minimal_v0_golomb_rice_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 10> storage{};
    storage.fill(0xee);
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 5);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array frame_payload{std::byte{0xfe}};

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    for (std::size_t y = 0; y < 2; ++y) {
        for (std::size_t x = 0; x < 4; ++x) {
            EXPECT_EQ(storage[y * 5 + x], 0u);
        }
        EXPECT_EQ(storage[y * 5 + 4], 0xee);
    }
}

TEST(DecoderTest, DecodeFrameContinuesLegacyRangeNonKeyframe)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), 1, 1, 1);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array keyframe_payload{std::byte{0xff}, std::byte{0x00}};
    ASSERT_TRUE(result.decoder->decode_frame(keyframe_payload, output).ok());
    const std::array non_keyframe_payload{std::byte{0x70}, std::byte{0x00}};

    const auto status = result.decoder->decode_frame(non_keyframe_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
}

TEST(DecoderTest, FailedOutputValidationPreservesReferenceState)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), 1, 1, 1);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array keyframe_payload{std::byte{0xff}, std::byte{0x00}};
    ASSERT_TRUE(result.decoder->decode_frame(keyframe_payload, output).ok());

    std::array<std::uint8_t, 1> rejected_storage{0xdd};
    auto rejected_plane = make_y_plane(rejected_storage.data(), 1, 1, 1);
    rejected_plane.info.role = mffv1::PlaneRole::Cb;
    mffv1::MutableFrameView rejected_output{&rejected_plane, 1};
    const auto failed_status =
        result.decoder->decode_frame(keyframe_payload, rejected_output);
    ASSERT_FALSE(failed_status.ok());
    EXPECT_EQ(failed_status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(failed_status.message, "plane role does not match stream plane order");
    EXPECT_EQ(rejected_storage[0], 0xdd);

    const std::array non_keyframe_payload{std::byte{0x70}, std::byte{0x00}};
    const auto status = result.decoder->decode_frame(non_keyframe_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
}

TEST(DecoderTest, DecodeFrameContinuesLegacyGolombRiceNonKeyframe)
{
    mffv1::DecoderOptions options;
    options.frame_width = 4;
    options.frame_height = 2;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);
    ASSERT_TRUE(configure_minimal_v0_golomb_rice_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 8> storage{};
    auto plane = make_y_plane(storage.data(), 4, 2, 4);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array keyframe_payload{std::byte{0xfe}};
    ASSERT_TRUE(result.decoder->decode_frame(keyframe_payload, output).ok());
    const std::array non_keyframe_payload{std::byte{0x70}};

    const auto status = result.decoder->decode_frame(non_keyframe_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(DecoderTest, DecodeFrameReconstructsPositiveDifference)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> frame_payload{
        std::byte{0x7f},
        std::byte{0x80},
    };

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
}

TEST(DecoderTest, DecodeFrameWrapsNegativeDifference)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> frame_payload{
        std::byte{0x8f},
        std::byte{0x70},
    };

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 255u);
}

TEST(DecoderTest, DecodeFrameUsesLeftPrediction)
{
    mffv1::DecoderOptions options;
    options.frame_width = 2;
    options.frame_height = 1;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 2> storage{0xee, 0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 2);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> frame_payload{
        std::byte{0x8b},
        std::byte{0x38},
    };

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 1u);
}

TEST(DecoderTest, DecodeFrameUsesTopPrediction)
{
    mffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 2;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 2> storage{0xee, 0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    mffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> frame_payload{
        std::byte{0x8b},
        std::byte{0x38},
    };

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 1u);
}

TEST(DecoderTest, DecodeFramePreservesStridePadding)
{
    mffv1::DecoderOptions options;
    options.frame_width = 2;
    options.frame_height = 2;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 4);
    mffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_frame_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
    EXPECT_EQ(storage[1], 0u);
    EXPECT_EQ(storage[2], 0xee);
    EXPECT_EQ(storage[3], 0xee);
    EXPECT_EQ(storage[4], 0u);
    EXPECT_EQ(storage[5], 0u);
    EXPECT_EQ(storage[6], 0xee);
    EXPECT_EQ(storage[7], 0xee);
}

TEST(DecoderTest, DecodeFrameWritesOnlyStreamRectangleInLargerOutputPlane)
{
    mffv1::DecoderOptions options;
    options.frame_width = 2;
    options.frame_height = 2;
    const auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 12> storage{};
    storage.fill(0xee);
    auto plane = make_y_plane(storage.data(), 4, 3, 4);
    mffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_frame_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 0u);
    EXPECT_EQ(storage[1], 0u);
    EXPECT_EQ(storage[2], 0xee);
    EXPECT_EQ(storage[3], 0xee);
    EXPECT_EQ(storage[4], 0u);
    EXPECT_EQ(storage[5], 0u);
    EXPECT_EQ(storage[6], 0xee);
    EXPECT_EQ(storage[7], 0xee);
    EXPECT_EQ(storage[8], 0xee);
    EXPECT_EQ(storage[9], 0xee);
    EXPECT_EQ(storage[10], 0xee);
    EXPECT_EQ(storage[11], 0xee);
}

} // namespace
