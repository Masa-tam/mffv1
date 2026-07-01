#include "codec/slice_payload_locator.hpp"
#include "codec/slice_footer_writer.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace {

constexpr std::array kSentinelPayload{
    std::byte{0xde},
    std::byte{0xad},
};

mffv1::syntax::SliceDescriptor make_sentinel_descriptor()
{
    mffv1::syntax::SliceDescriptor descriptor;
    descriptor.index = 17;
    descriptor.payload = kSentinelPayload;
    descriptor.payload_byte_offset = 123;
    descriptor.footer_byte_offset = 456;
    descriptor.slice_size = 789;
    descriptor.error_status = 2;
    descriptor.expected_crc = 0xabcdef01u;
    descriptor.has_crc = true;
    return descriptor;
}

void expect_descriptor_equal(const mffv1::syntax::SliceDescriptor& actual,
                             const mffv1::syntax::SliceDescriptor& expected)
{
    EXPECT_EQ(actual.index, expected.index);
    EXPECT_EQ(actual.payload.data(), expected.payload.data());
    EXPECT_EQ(actual.payload.size(), expected.payload.size());
    EXPECT_EQ(actual.payload_byte_offset, expected.payload_byte_offset);
    EXPECT_EQ(actual.footer_byte_offset, expected.footer_byte_offset);
    EXPECT_EQ(actual.slice_size, expected.slice_size);
    EXPECT_EQ(actual.error_status, expected.error_status);
    EXPECT_EQ(actual.expected_crc, expected.expected_crc);
    EXPECT_EQ(actual.has_crc, expected.has_crc);
}

TEST(SlicePayloadLocatorTest, LocatesWholeFrameAsTrailingSlice)
{
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };
    mffv1::syntax::StreamParameters stream;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SlicePayloadLocator locator;
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
    mffv1::syntax::StreamParameters stream;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SlicePayloadLocator locator;
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
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.slice_size, 10u);
    EXPECT_EQ(descriptor.payload_byte_offset, 1u);
    EXPECT_EQ(descriptor.footer_byte_offset, 3u);
    EXPECT_EQ(descriptor.error_status, 1u);
    EXPECT_TRUE(descriptor.has_crc);
    EXPECT_EQ(descriptor.expected_crc, 0x12345678u);
}

TEST(SlicePayloadLocatorTest, LocatesMinimumEcTrailingSlice)
{
    const std::array payload{
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x08},
        std::byte{0x02},
        std::byte{0x12},
        std::byte{0x34},
        std::byte{0x56},
        std::byte{0x78},
    };
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.slice_size, payload.size());
    EXPECT_EQ(descriptor.payload_byte_offset, 0u);
    EXPECT_EQ(descriptor.footer_byte_offset, 0u);
    EXPECT_EQ(descriptor.error_status, 2u);
    EXPECT_TRUE(descriptor.has_crc);
    EXPECT_EQ(descriptor.expected_crc, 0x12345678u);
    EXPECT_EQ(descriptor.payload.size(), payload.size());
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
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SlicePayloadLocator locator;
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
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor, true);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::CrcMismatch);
    EXPECT_EQ(status.message, "slice CRC remainder is non-zero");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 7u);
}

TEST(SlicePayloadLocatorTest, FailedTrailingSlicePreservesDescriptor)
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
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    auto descriptor = make_sentinel_descriptor();
    const auto original = descriptor;

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor, true);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::CrcMismatch);
    EXPECT_EQ(status.message, "slice CRC remainder is non-zero");
    expect_descriptor_equal(descriptor, original);
}

TEST(SlicePayloadLocatorTest, RejectsFrameTooSmallForFooter)
{
    const std::array payload{
        std::byte{0x00},
        std::byte{0x05},
    };
    mffv1::syntax::StreamParameters stream;
    auto descriptor = make_sentinel_descriptor();
    const auto original = descriptor;

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "frame payload is too small to contain a slice footer");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    expect_descriptor_equal(descriptor, original);
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
    mffv1::syntax::StreamParameters stream;
    auto descriptor = make_sentinel_descriptor();
    const auto original = descriptor;

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice footer size is smaller than the footer");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 2u);
    expect_descriptor_equal(descriptor, original);
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
    mffv1::syntax::StreamParameters stream;
    auto descriptor = make_sentinel_descriptor();
    const auto original = descriptor;

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_trailing_slice(payload, stream, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice footer size is larger than the frame payload");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 2u);
    expect_descriptor_equal(descriptor, original);
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
    mffv1::syntax::StreamParameters stream;
    std::vector<mffv1::syntax::SliceDescriptor> descriptors;

    const mffv1::codec::SlicePayloadLocator locator;
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

TEST(SlicePayloadLocatorTest, LocatesMultipleEcSlicesInPayloadOrder)
{
    const std::array payload{
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x08},
        std::byte{0x01},
        std::byte{0x11},
        std::byte{0x22},
        std::byte{0x33},
        std::byte{0x44},
        std::byte{0xba},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x09},
        std::byte{0x02},
        std::byte{0x55},
        std::byte{0x66},
        std::byte{0x77},
        std::byte{0x88},
    };
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    std::vector<mffv1::syntax::SliceDescriptor> descriptors;

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_slices(payload, stream, 2, descriptors);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(descriptors.size(), 2u);
    EXPECT_EQ(descriptors[0].index, 0u);
    EXPECT_EQ(descriptors[0].slice_size, 8u);
    EXPECT_EQ(descriptors[0].payload_byte_offset, 0u);
    EXPECT_EQ(descriptors[0].footer_byte_offset, 0u);
    EXPECT_EQ(descriptors[0].error_status, 1u);
    EXPECT_TRUE(descriptors[0].has_crc);
    EXPECT_EQ(descriptors[0].expected_crc, 0x11223344u);
    EXPECT_EQ(descriptors[1].index, 1u);
    EXPECT_EQ(descriptors[1].slice_size, 9u);
    EXPECT_EQ(descriptors[1].payload_byte_offset, 8u);
    EXPECT_EQ(descriptors[1].footer_byte_offset, 9u);
    EXPECT_EQ(descriptors[1].payload[0], std::byte{0xba});
    EXPECT_EQ(descriptors[1].error_status, 2u);
    EXPECT_TRUE(descriptors[1].has_crc);
    EXPECT_EQ(descriptors[1].expected_crc, 0x55667788u);
}

TEST(SlicePayloadLocatorTest, RejectsZeroMaximumSliceCount)
{
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };
    mffv1::syntax::StreamParameters stream;
    std::vector<mffv1::syntax::SliceDescriptor> descriptors(1);

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_slices(payload, stream, 0, descriptors);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "maximum slice count must be non-zero");
    EXPECT_EQ(descriptors.size(), 1u);
}

TEST(SlicePayloadLocatorTest, RejectsMoreSlicesThanMaximum)
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
    mffv1::syntax::StreamParameters stream;
    std::vector<mffv1::syntax::SliceDescriptor> descriptors(1);

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_slices(payload, stream, 1, descriptors);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "frame contains more slices than raster cells");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    EXPECT_EQ(descriptors.size(), 1u);
}

TEST(SlicePayloadLocatorTest, FailedLocateSlicesPreservesDescriptors)
{
    const std::array payload{
        std::byte{0xff},
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };
    mffv1::syntax::StreamParameters stream;
    std::vector<mffv1::syntax::SliceDescriptor> descriptors(1);
    descriptors[0].index = 9;
    descriptors[0].slice_size = 99;
    descriptors[0].payload_byte_offset = 77;
    descriptors[0].footer_byte_offset = 88;
    const auto original = descriptors;

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_slices(payload, stream, 2, descriptors);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "frame payload is too small to contain a slice footer");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    ASSERT_EQ(descriptors.size(), original.size());
    EXPECT_EQ(descriptors[0].index, original[0].index);
    EXPECT_EQ(descriptors[0].slice_size, original[0].slice_size);
    EXPECT_EQ(descriptors[0].payload_byte_offset, original[0].payload_byte_offset);
    EXPECT_EQ(descriptors[0].footer_byte_offset, original[0].footer_byte_offset);
}

TEST(SlicePayloadLocatorTest, FailedLocateAfterTrailingSuccessPreservesDescriptors)
{
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    const mffv1::codec::SliceFooterWriter footer_writer;
    std::vector<std::byte> first_slice{std::byte{0xa0}, std::byte{0xa1}};
    ASSERT_TRUE(footer_writer.append(stream, 0, first_slice).ok());
    first_slice.back() ^= std::byte{0x01};
    std::vector<std::byte> second_slice{std::byte{0xb0}};
    ASSERT_TRUE(footer_writer.append(stream, 0, second_slice).ok());
    std::vector<std::byte> payload = first_slice;
    payload.insert(payload.end(), second_slice.begin(), second_slice.end());
    std::vector<mffv1::syntax::SliceDescriptor> descriptors{
        make_sentinel_descriptor(),
    };
    const auto original = descriptors;

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_slices(
        payload, stream, 2, descriptors, true);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::CrcMismatch);
    EXPECT_EQ(status.message, "slice CRC remainder is non-zero");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 6u);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    ASSERT_EQ(descriptors.size(), original.size());
    expect_descriptor_equal(descriptors[0], original[0]);
}

TEST(SlicePayloadLocatorTest, ReportsTrailingCrcMismatchAfterEarlierSliceIndex)
{
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    const mffv1::codec::SliceFooterWriter footer_writer;
    std::vector<std::byte> first_slice{std::byte{0xa0}, std::byte{0xa1}};
    ASSERT_TRUE(footer_writer.append(stream, 0, first_slice).ok());
    std::vector<std::byte> second_slice{std::byte{0xb0}, std::byte{0xb1}};
    ASSERT_TRUE(footer_writer.append(stream, 0, second_slice).ok());
    second_slice.back() ^= std::byte{0x01};
    std::vector<std::byte> payload = first_slice;
    payload.insert(payload.end(), second_slice.begin(), second_slice.end());
    std::vector<mffv1::syntax::SliceDescriptor> descriptors{
        make_sentinel_descriptor(),
    };
    const auto original = descriptors;

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_slices(
        payload, stream, 2, descriptors, true);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::CrcMismatch);
    EXPECT_EQ(status.message, "slice CRC remainder is non-zero");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 16u);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 1u);
    ASSERT_EQ(descriptors.size(), original.size());
    expect_descriptor_equal(descriptors[0], original[0]);
}

TEST(SlicePayloadLocatorTest, DiscoversFewerSlicesThanMaximum)
{
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };
    mffv1::syntax::StreamParameters stream;
    std::vector<mffv1::syntax::SliceDescriptor> descriptors;

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_slices(payload, stream, 2, descriptors);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(descriptors.size(), 1u);
    EXPECT_EQ(descriptors[0].index, 0u);
    EXPECT_EQ(descriptors[0].slice_size, payload.size());
    EXPECT_EQ(descriptors[0].payload_byte_offset, 0u);
}

TEST(SlicePayloadLocatorTest, RejectsUnrepresentableMaximumBeforeAllocation)
{
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };
    mffv1::syntax::StreamParameters stream;
    std::vector<mffv1::syntax::SliceDescriptor> descriptors(1);

    const mffv1::codec::SlicePayloadLocator locator;
    const auto status = locator.locate_slices(
        payload,
        stream,
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1,
        descriptors);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::ResourceExhausted);
    EXPECT_EQ(status.message, "maximum slice count exceeds the supported index range");
    EXPECT_EQ(descriptors.size(), 1u);
}

} // namespace
