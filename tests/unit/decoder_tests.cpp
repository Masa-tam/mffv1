#include "ffv1/codec.hpp"

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

ffv1::Status configure_minimal_v0_y_only(ffv1::IDecoder& decoder)
{
    const auto configuration_record = minimal_v0_y_only_configuration_record();
    return decoder.configure(configuration_record);
}

ffv1::MutablePlaneView make_y_plane(std::uint8_t* data,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    std::ptrdiff_t stride_bytes)
{
    ffv1::MutablePlaneView plane;
    plane.data = data;
    plane.info.role = ffv1::PlaneRole::Y;
    plane.info.sample_format = ffv1::SampleFormat::UInt8;
    plane.info.width = width;
    plane.info.height = height;
    plane.info.stride_bytes = stride_bytes;
    return plane;
}

TEST(DecoderTest, FactoryCreatesDecoder)
{
    const auto result = ffv1::create_decoder({});

    EXPECT_TRUE(result.status.ok());
    EXPECT_NE(result.decoder, nullptr);
}

TEST(DecoderTest, FactoryAcceptsExternalFrameDimensions)
{
    ffv1::DecoderOptions options;
    options.frame_width = 16;
    options.frame_height = 8;

    const auto result = ffv1::create_decoder(options);

    EXPECT_TRUE(result.status.ok());
    EXPECT_NE(result.decoder, nullptr);
}

TEST(DecoderTest, FactoryRejectsIncompleteExternalFrameDimensions)
{
    ffv1::DecoderOptions options;
    options.frame_width = 16;

    const auto result = ffv1::create_decoder(options);

    EXPECT_FALSE(result.status.ok());
    EXPECT_EQ(result.status.code, ffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(result.decoder, nullptr);
}

TEST(DecoderTest, ConfigureRejectsEmptyConfigurationRecord)
{
    const auto result = ffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const ffv1::ByteSpan empty;
    const auto status = result.decoder->configure(empty);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(DecoderTest, ConfigureRejectsTooShortRangeCoderPayload)
{
    const auto result = ffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const std::array<std::byte, 1> record{std::byte{0x00}};
    const auto status = result.decoder->configure(record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

TEST(DecoderTest, DecodeRequiresConfiguration)
{
    const auto result = ffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const std::array<std::byte, 1> payload{std::byte{0x00}};
    ffv1::MutableFrameView output{};
    const auto status = result.decoder->decode_frame(payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidState);
}

TEST(DecoderTest, InspectFrameRequiresConfiguration)
{
    const auto result = ffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const std::array<std::byte, 1> payload{std::byte{0x00}};
    ffv1::FrameInfo info;
    const auto status = result.decoder->inspect_frame(payload, info);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidState);
}

TEST(DecoderTest, InspectFrameRequiresExternalDimensions)
{
    const auto result = ffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    const std::array<std::byte, 1> frame_payload{std::byte{0x00}};
    ffv1::FrameInfo info;
    const auto status = result.decoder->inspect_frame(frame_payload, info);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidState);
}

TEST(DecoderTest, InspectFrameRejectsEmptyPayload)
{
    ffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    const ffv1::ByteSpan frame_payload;
    ffv1::FrameInfo info;
    const auto status = result.decoder->inspect_frame(frame_payload, info);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(DecoderTest, DecodeFrameRequiresExternalDimensions)
{
    const auto result = ffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{};
    auto plane = make_y_plane(storage.data(), 1, 1, 1);
    ffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidState);
}

TEST(DecoderTest, DecodeFrameRejectsMissingOutputPlanes)
{
    ffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    ffv1::MutableFrameView output;
    output.planes = nullptr;
    output.plane_count = 1;
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(DecoderTest, DecodeFrameRejectsNullOutputPlaneData)
{
    ffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    auto plane = make_y_plane(nullptr, options.frame_width, options.frame_height, 1);
    ffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(DecoderTest, DecodeFrameRejectsShortOutputStride)
{
    ffv1::DecoderOptions options;
    options.frame_width = 2;
    options.frame_height = 1;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 2> storage{};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    ffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(DecoderTest, DecodeFrameRejectsWrongOutputSampleFormat)
{
    ffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint16_t, 1> storage{};
    ffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = ffv1::PlaneRole::Y;
    plane.info.sample_format = ffv1::SampleFormat::UInt16;
    plane.info.width = options.frame_width;
    plane.info.height = options.frame_height;
    plane.info.stride_bytes = 2;
    ffv1::MutableFrameView output{&plane, 1};
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(DecoderTest, DecodeFrameRejectsMissingRequiredPlaneCount)
{
    ffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    ffv1::MutableFrameView output;
    output.planes = nullptr;
    output.plane_count = 0;
    const auto frame_payload = zero_scalar_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(DecoderTest, DecodeFrameRejectsEmptyPayload)
{
    ffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    ffv1::MutableFrameView output{&plane, 1};
    const ffv1::ByteSpan frame_payload;

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(DecoderTest, DecodeFrameRejectsTooShortSliceRangePayload)
{
    ffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    ffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 1> frame_payload{std::byte{0xff}};

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(DecoderTest, InspectFrameUsesExternalDimensions)
{
    ffv1::DecoderOptions options;
    options.frame_width = 16;
    options.frame_height = 8;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    const std::array<std::byte, 1> frame_payload{std::byte{0x00}};
    ffv1::FrameInfo info;
    const auto status = result.decoder->inspect_frame(frame_payload, info);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(info.width, options.frame_width);
    EXPECT_EQ(info.height, options.frame_height);
    EXPECT_EQ(info.version, 0u);
    EXPECT_EQ(info.bits_per_raw_sample, 8u);
    EXPECT_EQ(info.plane_count, 1u);
}

TEST(DecoderTest, DecodeFrameWritesZeroYOnlyFrame)
{
    ffv1::DecoderOptions options;
    options.frame_width = 4;
    options.frame_height = 2;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 4);
    ffv1::MutableFrameView output{&plane, 1};

    const auto frame_payload = zero_frame_payload();

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0u);
    }
}

TEST(DecoderTest, DecodeFrameReconstructsPositiveDifference)
{
    ffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    ffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> frame_payload{
        std::byte{0x14},
        std::byte{0x46},
    };

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
}

TEST(DecoderTest, DecodeFrameWrapsNegativeDifference)
{
    ffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 1;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 1> storage{0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    ffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> frame_payload{
        std::byte{0x21},
        std::byte{0xcf},
    };

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 255u);
}

TEST(DecoderTest, DecodeFrameUsesLeftPrediction)
{
    ffv1::DecoderOptions options;
    options.frame_width = 2;
    options.frame_height = 1;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 2> storage{0xee, 0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 2);
    ffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> frame_payload{
        std::byte{0x1c},
        std::byte{0xf0},
    };

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 1u);
}

TEST(DecoderTest, DecodeFrameUsesTopPrediction)
{
    ffv1::DecoderOptions options;
    options.frame_width = 1;
    options.frame_height = 2;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 2> storage{0xee, 0xee};
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 1);
    ffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> frame_payload{
        std::byte{0x1c},
        std::byte{0xf0},
    };

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(storage[0], 1u);
    EXPECT_EQ(storage[1], 1u);
}

TEST(DecoderTest, DecodeFramePreservesStridePadding)
{
    ffv1::DecoderOptions options;
    options.frame_width = 2;
    options.frame_height = 2;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    ASSERT_TRUE(configure_minimal_v0_y_only(*result.decoder).ok());

    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    auto plane = make_y_plane(storage.data(), options.frame_width, options.frame_height, 4);
    ffv1::MutableFrameView output{&plane, 1};
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

} // namespace
