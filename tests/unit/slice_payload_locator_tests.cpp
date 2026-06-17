#include "codec/slice_payload_locator.hpp"

#include <array>
#include <cstddef>
#include <vector>

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

TEST(SlicePayloadLocatorTest, VerifiesEcTrailingSliceCrc)
{
    const std::array payload{
        std::byte{0x99},
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x0a},
        std::byte{0x00},
        std::byte{0x1f},
        std::byte{0xfe},
        std::byte{0xb9},
        std::byte{0xe9},
    };
    ffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor, true);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.payload_byte_offset, 1u);
    EXPECT_EQ(descriptor.footer_byte_offset, 3u);
    EXPECT_EQ(descriptor.expected_crc, 0x1ffeb9e9u);
}

TEST(SlicePayloadLocatorTest, RejectsEcTrailingSliceCrcMismatch)
{
    const std::array payload{
        std::byte{0x99},
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x0a},
        std::byte{0x00},
        std::byte{0x1f},
        std::byte{0xfe},
        std::byte{0xb9},
        std::byte{0xe8},
    };
    ffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor, true);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::CrcMismatch);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 7u);
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

TEST(SlicePayloadLocatorTest, LocatesMultipleSlicesInPayloadOrder)
{
    const std::array payload{
        std::byte{0xa0},
        std::byte{0xa1},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
        std::byte{0xb0},
        std::byte{0xb1},
        std::byte{0xb2},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x06},
    };
    ffv1::syntax::StreamParameters stream;
    std::vector<ffv1::syntax::SliceDescriptor> descriptors;

    const ffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_slices(payload, stream, 2, descriptors);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(descriptors.size(), 2u);
    EXPECT_EQ(descriptors[0].index, 0u);
    EXPECT_EQ(descriptors[0].slice_size, 5u);
    EXPECT_EQ(descriptors[0].payload_byte_offset, 0u);
    EXPECT_EQ(descriptors[0].footer_byte_offset, 2u);
    EXPECT_EQ(descriptors[0].payload[0], std::byte{0xa0});
    EXPECT_EQ(descriptors[1].index, 1u);
    EXPECT_EQ(descriptors[1].slice_size, 6u);
    EXPECT_EQ(descriptors[1].payload_byte_offset, 5u);
    EXPECT_EQ(descriptors[1].footer_byte_offset, 8u);
    EXPECT_EQ(descriptors[1].payload[0], std::byte{0xb0});
}

TEST(SlicePayloadLocatorTest, RejectsZeroExpectedSliceCount)
{
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };
    ffv1::syntax::StreamParameters stream;
    std::vector<ffv1::syntax::SliceDescriptor> descriptors(1);

    const ffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_slices(payload, stream, 0, descriptors);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(descriptors.size(), 1u);
}

TEST(SlicePayloadLocatorTest, RejectsUncoveredPrefixWhenExpectedCountIsTooSmall)
{
    const std::array payload{
        std::byte{0xa0},
        std::byte{0xa1},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
        std::byte{0xb0},
        std::byte{0xb1},
        std::byte{0xb2},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x06},
    };
    ffv1::syntax::StreamParameters stream;
    std::vector<ffv1::syntax::SliceDescriptor> descriptors(1);

    const ffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_slices(payload, stream, 1, descriptors);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_EQ(descriptors.size(), 1u);
}

TEST(SlicePayloadLocatorTest, RejectsExpectedCountLargerThanPayload)
{
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };
    ffv1::syntax::StreamParameters stream;
    std::vector<ffv1::syntax::SliceDescriptor> descriptors(1);

    const ffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_slices(payload, stream, 2, descriptors);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_EQ(descriptors.size(), 1u);
}

} // namespace
