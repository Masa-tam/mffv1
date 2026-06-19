#include "codec/slice_footer_parser.hpp"

#include <array>
#include <cstddef>

#include <gtest/gtest.h>

namespace {

TEST(SliceFooterParserTest, ReadsSliceSize)
{
    const std::array payload{
        std::byte{0x00},
        std::byte{0x12},
        std::byte{0x34},
    };
    mffv1::bitstream::BitReader reader(payload);
    mffv1::syntax::StreamParameters stream;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceFooterParser parser;
    const auto status = parser.read(reader, stream, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.slice_size, 0x1234u);
    EXPECT_FALSE(descriptor.has_crc);
    EXPECT_EQ(reader.byte_position(), payload.size());
}

TEST(SliceFooterParserTest, ReadsFooterFromSliceEnd)
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
    descriptor.payload_byte_offset = 100;

    const mffv1::codec::SliceFooterParser parser;
    const auto status = parser.read_from_end(payload, stream, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.slice_size, payload.size());
    EXPECT_EQ(descriptor.footer_byte_offset, 102u);
    EXPECT_FALSE(descriptor.has_crc);
}

TEST(SliceFooterParserTest, ReadsErrorStatusAndCrcWhenEnabled)
{
    const std::array payload{
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
        std::byte{0x01},
        std::byte{0x12},
        std::byte{0x34},
        std::byte{0x56},
        std::byte{0x78},
    };
    mffv1::bitstream::BitReader reader(payload);
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceFooterParser parser;
    const auto status = parser.read(reader, stream, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.slice_size, 7u);
    EXPECT_EQ(descriptor.error_status, 1u);
    EXPECT_TRUE(descriptor.has_crc);
    EXPECT_EQ(descriptor.expected_crc, 0x12345678u);
    EXPECT_EQ(reader.byte_position(), payload.size());
}

TEST(SliceFooterParserTest, ReadsEcFooterFromSliceEnd)
{
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x0a},
        std::byte{0x02},
        std::byte{0x12},
        std::byte{0x34},
        std::byte{0x56},
        std::byte{0x78},
    };
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    mffv1::syntax::SliceDescriptor descriptor;
    descriptor.payload_byte_offset = 50;

    const mffv1::codec::SliceFooterParser parser;
    const auto status = parser.read_from_end(payload, stream, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.slice_size, payload.size());
    EXPECT_EQ(descriptor.error_status, 2u);
    EXPECT_TRUE(descriptor.has_crc);
    EXPECT_EQ(descriptor.expected_crc, 0x12345678u);
    EXPECT_EQ(descriptor.footer_byte_offset, 52u);
}

TEST(SliceFooterParserTest, VerifiesCrcFromSliceEnd)
{
    const std::array payload{
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
    descriptor.payload_byte_offset = 50;

    const mffv1::codec::SliceFooterParser parser;
    const auto status = parser.read_from_end(payload, stream, descriptor, true);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(descriptor.has_crc);
    EXPECT_EQ(descriptor.expected_crc, 0x1ffeb9e9u);
}

TEST(SliceFooterParserTest, RejectsCrcMismatchFromSliceEnd)
{
    const std::array payload{
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
    descriptor.payload_byte_offset = 50;

    const mffv1::codec::SliceFooterParser parser;
    const auto status = parser.read_from_end(payload, stream, descriptor, true);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::CrcMismatch);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 56u);
}

TEST(SliceFooterParserTest, RejectsReservedErrorStatus)
{
    const std::array payload{
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
        std::byte{0x03},
        std::byte{0x12},
        std::byte{0x34},
        std::byte{0x56},
        std::byte{0x78},
    };
    mffv1::bitstream::BitReader reader(payload);
    mffv1::syntax::StreamParameters stream;
    stream.error_status_enabled = true;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceFooterParser parser;
    const auto status = parser.read(reader, stream, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 3u);
}

TEST(SliceFooterParserTest, RejectsSliceSizeMismatchFromEnd)
{
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x04},
    };
    mffv1::syntax::StreamParameters stream;
    mffv1::syntax::SliceDescriptor descriptor;
    descriptor.payload_byte_offset = 20;

    const mffv1::codec::SliceFooterParser parser;
    const auto status = parser.read_from_end(payload, stream, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 22u);
}

TEST(SliceFooterParserTest, RejectsPayloadTooSmallForFooter)
{
    const std::array payload{
        std::byte{0x00},
        std::byte{0x05},
    };
    mffv1::syntax::StreamParameters stream;
    mffv1::syntax::SliceDescriptor descriptor;
    descriptor.payload_byte_offset = 30;

    const mffv1::codec::SliceFooterParser parser;
    const auto status = parser.read_from_end(payload, stream, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 30u);
}

TEST(SliceFooterParserTest, RejectsUnalignedFooter)
{
    const std::array payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
    };
    mffv1::bitstream::BitReader reader(payload);
    ASSERT_TRUE(reader.skip_bits(1).ok());
    mffv1::syntax::StreamParameters stream;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceFooterParser parser;
    const auto status = parser.read(reader, stream, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(SliceFooterParserTest, ReportsUnderflowAtFooterOffset)
{
    const std::array payload{
        std::byte{0x00},
        std::byte{0x12},
    };
    mffv1::bitstream::BitReader reader(payload);
    mffv1::syntax::StreamParameters stream;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceFooterParser parser;
    const auto status = parser.read(reader, stream, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

} // namespace
