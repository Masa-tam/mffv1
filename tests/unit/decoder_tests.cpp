#include "ffv1/codec.hpp"

#include <array>
#include <cstddef>

#include <gtest/gtest.h>

namespace {

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

TEST(DecoderTest, InspectFrameRequiresExternalDimensions)
{
    const auto result = ffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const std::array<std::byte, 3> configuration_record{
        std::byte{0x95},
        std::byte{0x36},
        std::byte{0xe9},
    };
    ASSERT_TRUE(result.decoder->configure(configuration_record).ok());

    const std::array<std::byte, 1> frame_payload{std::byte{0x00}};
    ffv1::FrameInfo info;
    const auto status = result.decoder->inspect_frame(frame_payload, info);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidState);
}

TEST(DecoderTest, DecodeFrameRequiresExternalDimensions)
{
    const auto result = ffv1::create_decoder({});
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const std::array<std::byte, 3> configuration_record{
        std::byte{0x95},
        std::byte{0x36},
        std::byte{0xe9},
    };
    ASSERT_TRUE(result.decoder->configure(configuration_record).ok());

    std::array<std::uint8_t, 1> storage{};
    ffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = ffv1::PlaneRole::Y;
    plane.info.sample_format = ffv1::SampleFormat::UInt8;
    plane.info.width = 1;
    plane.info.height = 1;
    plane.info.stride_bytes = 1;
    ffv1::MutableFrameView output{&plane, 1};
    const std::array<std::byte, 2> frame_payload{std::byte{0xff}, std::byte{0x00}};

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidState);
}

TEST(DecoderTest, InspectFrameUsesExternalDimensions)
{
    ffv1::DecoderOptions options;
    options.frame_width = 16;
    options.frame_height = 8;
    const auto result = ffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const std::array<std::byte, 3> configuration_record{
        std::byte{0x95},
        std::byte{0x36},
        std::byte{0xe9},
    };
    ASSERT_TRUE(result.decoder->configure(configuration_record).ok());

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

    const std::array<std::byte, 3> configuration_record{
        std::byte{0x95},
        std::byte{0x36},
        std::byte{0xe9},
    };
    ASSERT_TRUE(result.decoder->configure(configuration_record).ok());

    std::array<std::uint8_t, 8> storage{};
    storage.fill(0xee);
    ffv1::MutablePlaneView plane;
    plane.data = storage.data();
    plane.info.role = ffv1::PlaneRole::Y;
    plane.info.sample_format = ffv1::SampleFormat::UInt8;
    plane.info.width = options.frame_width;
    plane.info.height = options.frame_height;
    plane.info.stride_bytes = 4;
    ffv1::MutableFrameView output{&plane, 1};

    const std::array<std::byte, 16> frame_payload{
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
        std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
    };

    const auto status = result.decoder->decode_frame(frame_payload, output);

    EXPECT_TRUE(status.ok()) << status.message;
    for (const auto sample : storage) {
        EXPECT_EQ(sample, 0u);
    }
}

} // namespace
