#include "codec/slice_payload_locator.hpp"

#include <array>
#include <cstddef>

#include <gtest/gtest.h>

namespace {

TEST(SlicePayloadLocatorTest, LocatesWholeFrameAsTrailingSlice)
{
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };
    ffv1::syntax::StreamParameters stream;
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.slice_size, 5u);
    EXPECT_EQ(descriptor.payload_byte_offset, 0u);
    EXPECT_EQ(descriptor.footer_byte_offset, 2u);
    EXPECT_EQ(descriptor.payload.size(), payload.size());
    EXPECT_FALSE(descriptor.has_crc);
}

TEST(SlicePayloadLocatorTest, LocatesTrailingSliceAfterEarlierBytes)
{
    const std::array payload{
        std::byte{0x11},
        std::byte{0x22},
        std::byte{0x33},
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };
    ffv1::syntax::StreamParameters stream;
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.slice_size, 5u);
    EXPECT_EQ(descriptor.payload_byte_offset, 3u);
    EXPECT_EQ(descriptor.footer_byte_offset, 5u);
    EXPECT_EQ(descriptor.payload.size(), 5u);
    EXPECT_EQ(descriptor.payload[0], std::byte{0xaa});
}

TEST(SlicePayloadLocatorTest, LocatesEcTrailingSlice)
{
    const std::array payload{
        std::byte{0x99},
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x0a},
        std::byte{0x01},
        std::byte{0x12},
        std::byte{0x34},
        std::byte{0x56},
        std::byte{0x78},
    };
    ffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.slice_size, 10u);
    EXPECT_EQ(descriptor.payload_byte_offset, 1u);
    EXPECT_EQ(descriptor.footer_byte_offset, 3u);
    EXPECT_EQ(descriptor.error_status, 1u);
    EXPECT_TRUE(descriptor.has_crc);
    EXPECT_EQ(descriptor.expected_crc, 0x12345678u);
}

TEST(SlicePayloadLocatorTest, RejectsFrameTooSmallForFooter)
{
    const std::array payload{
        std::byte{0x00},
        std::byte{0x05},
    };
    ffv1::syntax::StreamParameters stream;
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(SlicePayloadLocatorTest, RejectsSliceSizeSmallerThanFooter)
{
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x02},
    };
    ffv1::syntax::StreamParameters stream;
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 2u);
}

TEST(SlicePayloadLocatorTest, RejectsSliceSizeLargerThanFrame)
{
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x06},
    };
    ffv1::syntax::StreamParameters stream;
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 2u);
}

} // namespace
