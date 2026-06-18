#include "codec/frame_parser.hpp"
#include "util/crc32.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

#include <gtest/gtest.h>

namespace {

class ScriptedUnsignedReader final : public ffv1::entropy::SymbolReader {
public:
    explicit ScriptedUnsignedReader(std::deque<std::uint64_t> values,
                                    std::uint64_t bytes_per_read,
                                    bool keyframe = true)
        : values_(std::move(values))
        , bytes_per_read_(bytes_per_read)
        , keyframe_(keyframe)
    {
    }

    ffv1::Status read_bool(bool& out_value) override
    {
        if (bool_read_) {
            return ffv1::make_error(ffv1::ErrorCode::InternalError, "unexpected second bool read");
        }
        out_value = keyframe_;
        bool_read_ = true;
        byte_position_ += bytes_per_read_;
        return ffv1::ok_status();
    }

    ffv1::Status read_unsigned(std::uint64_t& out_value) override
    {
        if (values_.empty()) {
            return ffv1::make_error(ffv1::ErrorCode::SyntaxError, "scripted reader underflow");
        }
        out_value = values_.front();
        values_.pop_front();
        byte_position_ += bytes_per_read_;
        return ffv1::ok_status();
    }

    ffv1::Status read_signed(std::int64_t&) override
    {
        return ffv1::make_error(ffv1::ErrorCode::InternalError, "unexpected signed read");
    }

    std::uint64_t byte_position() const noexcept override
    {
        return byte_position_;
    }

private:
    std::deque<std::uint64_t> values_;
    std::uint64_t bytes_per_read_ = 0;
    std::uint64_t byte_position_ = 0;
    bool keyframe_ = true;
    bool bool_read_ = false;
};

ffv1::syntax::StreamParameters make_stream()
{
    ffv1::syntax::StreamParameters stream;
    stream.width = 16;
    stream.height = 8;
    stream.bits_per_raw_sample = 8;
    stream.chroma_planes = false;
    stream.num_h_slices = 1;
    stream.num_v_slices = 1;
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());
    return stream;
}

template <std::size_t Size>
void write_crc_parity(std::array<std::byte, Size>& payload)
{
    static_assert(Size >= 4);
    const auto crc = ffv1::util::crc32_ieee_msb(ffv1::ByteSpan(payload.data(), Size - 4));
    payload[Size - 4] = static_cast<std::byte>((crc >> 24) & 0xffu);
    payload[Size - 3] = static_cast<std::byte>((crc >> 16) & 0xffu);
    payload[Size - 2] = static_cast<std::byte>((crc >> 8) & 0xffu);
    payload[Size - 1] = static_cast<std::byte>(crc & 0xffu);
}

TEST(FrameParserTest, RejectsEmptyPayload)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;

    const ffv1::ByteSpan empty;
    const auto status = parser.parse(empty, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::InvalidArgument);
}

TEST(FrameParserTest, CreatesSingleSliceDescriptor)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 4> payload{
        std::byte{1},
        std::byte{2},
        std::byte{3},
        std::byte{4},
    };

    const auto status = parser.parse(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].index, 0u);
    EXPECT_EQ(frame.slices[0].x, 0u);
    EXPECT_EQ(frame.slices[0].y, 0u);
    EXPECT_EQ(frame.slices[0].width, stream.width);
    EXPECT_EQ(frame.slices[0].height, stream.height);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_y, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 1u);
    EXPECT_EQ(frame.slices[0].raster_height, 1u);
    EXPECT_EQ(frame.slices[0].payload.size(), payload.size());
    EXPECT_EQ(frame.slices[0].header_byte_offset, 0u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 0u);
    EXPECT_EQ(frame.slices[0].payload_byte_offset, 0u);
    ASSERT_EQ(frame.slices[0].quant_table_set_indexes.size(), 1u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[0], 0u);
    EXPECT_EQ(frame.frame_info.width, stream.width);
    EXPECT_EQ(frame.frame_info.height, stream.height);
    EXPECT_EQ(frame.frame_info.plane_count, 1u);
}

TEST(FrameParserTest, ParsesLegacyRangeKeyframeAndContentOffset)
{
    auto stream = make_stream();
    stream.version = 0;
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array payload{std::byte{0xff}, std::byte{0x00}};

    const auto status = parser.parse(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_TRUE(frame.keyframe);
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].header_byte_offset, 0u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 2u);
    EXPECT_EQ(frame.slices[0].payload_byte_offset, 0u);
    EXPECT_TRUE(frame.slices[0].continues_frame_range_state);
}

TEST(FrameParserTest, RejectsLegacyRangeNonKeyframe)
{
    auto stream = make_stream();
    stream.version = 0;
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array payload{std::byte{0x00}, std::byte{0x00}};

    const auto status = parser.parse(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::UnsupportedFeature);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_TRUE(frame.slices.empty());
}

TEST(FrameParserTest, CreatesSingleSliceDescriptorFromHeaderReader)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 0, 3, 4, 3}, 1);
    const std::array<std::byte, 16> payload{
        std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3},
        std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7},
        std::byte{8}, std::byte{9}, std::byte{10}, std::byte{11},
        std::byte{12}, std::byte{13}, std::byte{14}, std::byte{15},
    };

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].x, 0u);
    EXPECT_EQ(frame.slices[0].y, 0u);
    EXPECT_EQ(frame.slices[0].width, stream.width);
    EXPECT_EQ(frame.slices[0].height, stream.height);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_y, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 1u);
    EXPECT_EQ(frame.slices[0].raster_height, 1u);
    EXPECT_EQ(frame.slices[0].header_byte_offset, 1u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 10u);
    EXPECT_EQ(frame.slices[0].picture_structure, 3u);
    EXPECT_EQ(frame.slices[0].sar_num, 4u);
    EXPECT_EQ(frame.slices[0].sar_den, 3u);
    EXPECT_TRUE(frame.keyframe);
    EXPECT_EQ(frame.slices[0].payload.size(), payload.size());
    ASSERT_EQ(frame.slices[0].quant_table_set_indexes.size(), 2u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[0], 0u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[1], 0u);
}

TEST(FrameParserTest, RejectsHeaderReaderThatConsumesPastPayload)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 0, 0, 0, 0}, 2);
    const std::array<std::byte, 8> payload{
        std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3},
        std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7},
    };

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 20u);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
}

TEST(FrameParserTest, RejectsNonKeyframeForIntraOnlyStream)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 0}, 1, false);
    const std::array<std::byte, 16> payload{};

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_FALSE(frame.keyframe);
    EXPECT_TRUE(frame.slices.empty());
}

TEST(FrameParserTest, RejectsTooShortRangeHeaderPayload)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 1> payload{std::byte{0}};

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
}

TEST(FrameParserTest, RejectsInvalidSliceHeaderThroughRangeCoder)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 4> payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

TEST(FrameParserTest, RejectsRangeCodedNonKeyframeForIntraOnlyStream)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 5> payload{
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    EXPECT_FALSE(frame.keyframe);
    EXPECT_TRUE(frame.slices.empty());
}

TEST(FrameParserTest, CreatesSingleSliceDescriptorFromRangeHeader)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 7> payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_EQ(frame.slices[0].x, 0u);
    EXPECT_EQ(frame.slices[0].y, 0u);
    EXPECT_EQ(frame.slices[0].width, stream.width);
    EXPECT_EQ(frame.slices[0].height, stream.height);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_y, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 1u);
    EXPECT_EQ(frame.slices[0].raster_height, 1u);
    EXPECT_EQ(frame.slices[0].header_byte_offset, 2u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 2u);
    EXPECT_EQ(frame.slices[0].footer_byte_offset, 4u);
    EXPECT_EQ(frame.slices[0].slice_size, payload.size());
    EXPECT_EQ(frame.slices[0].payload.size(), payload.size());
    EXPECT_TRUE(frame.keyframe);
    ASSERT_EQ(frame.slices[0].quant_table_set_indexes.size(), 2u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[0], 0u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[1], 0u);
}

TEST(FrameParserTest, VerifiesSingleSliceCrc)
{
    auto stream = make_stream();
    stream.error_status_enabled = true;
    ffv1::codec::FrameParser parser(stream, true);
    ffv1::codec::FrameDecodeContext frame;
    std::array<std::byte, 12> payload{
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x0c}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00},
    };
    write_crc_parity(payload);

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_TRUE(frame.slices[0].has_crc);
    EXPECT_EQ(frame.slices[0].footer_byte_offset, 4u);
    EXPECT_EQ(frame.slices[0].slice_size, payload.size());
}

TEST(FrameParserTest, RejectsSingleSliceCrcMismatch)
{
    auto stream = make_stream();
    stream.error_status_enabled = true;
    ffv1::codec::FrameParser parser(stream, true);
    ffv1::codec::FrameDecodeContext frame;
    std::array<std::byte, 12> payload{
        std::byte{0xff}, std::byte{0x00}, std::byte{0xff}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x0c}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00},
    };
    write_crc_parity(payload);
    payload.back() ^= std::byte{0x01};

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::CrcMismatch);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 8u);
    EXPECT_TRUE(frame.slices.empty());
}

TEST(FrameParserTest, ReportsMultiSliceAsNotImplemented)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 1> payload{std::byte{0}};

    const auto status = parser.parse(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::NotImplemented);
}

TEST(FrameParserTest, RejectsMultiSliceRangePayloadTooSmallForFooter)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 2> payload{
        std::byte{0},
        std::byte{0},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(FrameParserTest, RejectsMalformedSingleSliceInMultiCellRaster)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array payload{
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x05},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 2u);
    EXPECT_TRUE(frame.slices.empty());
}

TEST(FrameParserTest, CreatesMultiSliceDescriptorsFromLocatedRangeHeaders)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
        std::byte{0x3d},
        std::byte{0x34},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 2u);
    EXPECT_TRUE(frame.keyframe);
    EXPECT_EQ(frame.slices[0].index, 0u);
    EXPECT_EQ(frame.slices[0].x, 0u);
    EXPECT_EQ(frame.slices[0].y, 0u);
    EXPECT_EQ(frame.slices[0].width, 8u);
    EXPECT_EQ(frame.slices[0].height, stream.height);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_y, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 1u);
    EXPECT_EQ(frame.slices[0].raster_height, 1u);
    EXPECT_EQ(frame.slices[0].payload_byte_offset, 0u);
    EXPECT_EQ(frame.slices[0].header_byte_offset, 2u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 2u);
    EXPECT_EQ(frame.slices[0].footer_byte_offset, 4u);
    EXPECT_EQ(frame.slices[0].slice_size, 7u);
    EXPECT_EQ(frame.slices[0].payload.size(), 7u);

    EXPECT_EQ(frame.slices[1].index, 1u);
    EXPECT_EQ(frame.slices[1].x, 8u);
    EXPECT_EQ(frame.slices[1].y, 0u);
    EXPECT_EQ(frame.slices[1].width, 8u);
    EXPECT_EQ(frame.slices[1].height, stream.height);
    EXPECT_EQ(frame.slices[1].raster_x, 1u);
    EXPECT_EQ(frame.slices[1].raster_y, 0u);
    EXPECT_EQ(frame.slices[1].raster_width, 1u);
    EXPECT_EQ(frame.slices[1].raster_height, 1u);
    EXPECT_EQ(frame.slices[1].payload_byte_offset, 7u);
    EXPECT_EQ(frame.slices[1].header_byte_offset, 9u);
    EXPECT_EQ(frame.slices[1].content_byte_offset, 10u);
    EXPECT_EQ(frame.slices[1].footer_byte_offset, 11u);
    EXPECT_EQ(frame.slices[1].slice_size, 7u);
    EXPECT_EQ(frame.slices[1].payload.size(), 7u);
}

TEST(FrameParserTest, AcceptsSingleSliceCoveringMultipleRasterCells)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array payload{
        std::byte{0xe3},
        std::byte{0xfe},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(frame.slices.size(), 1u);
    EXPECT_TRUE(frame.keyframe);
    EXPECT_EQ(frame.slices[0].raster_x, 0u);
    EXPECT_EQ(frame.slices[0].raster_y, 0u);
    EXPECT_EQ(frame.slices[0].raster_width, 2u);
    EXPECT_EQ(frame.slices[0].raster_height, 1u);
    EXPECT_EQ(frame.slices[0].x, 0u);
    EXPECT_EQ(frame.slices[0].width, stream.width);
}

TEST(FrameParserTest, RejectsMultiSliceRangeHeaderWithSliceLocation)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array payload{
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x03},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x03},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.code, ffv1::ErrorCode::NotImplemented);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
    EXPECT_TRUE(status.location.has_byte_offset);
}

TEST(FrameParserTest, DoesNotExposeParsedPrefixWhenLaterSliceHeaderFails)
{
    auto stream = make_stream();
    stream.num_h_slices = 2;
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array payload{
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0xff},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x03},
    };

    const auto status = parser.parse_with_range_header(payload, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 1u);
    EXPECT_TRUE(frame.slices.empty());
}

} // namespace
