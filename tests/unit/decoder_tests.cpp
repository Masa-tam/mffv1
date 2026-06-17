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

} // namespace
