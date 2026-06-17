#include "codec/frame_parser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

#include <gtest/gtest.h>

namespace {

class ScriptedUnsignedReader final : public ffv1::entropy::SymbolReader {
public:
    explicit ScriptedUnsignedReader(std::deque<std::uint64_t> values, std::uint64_t bytes_per_read)
        : values_(std::move(values))
        , bytes_per_read_(bytes_per_read)
    {
    }

    ffv1::Status read_bool(bool&) override
    {
        return ffv1::make_error(ffv1::ErrorCode::InternalError, "unexpected bool read");
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

TEST(FrameParserTest, CreatesSingleSliceDescriptorFromHeaderReader)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    ScriptedUnsignedReader reader({0, 0, 1, 1, 1, 0}, 1);
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
    EXPECT_EQ(frame.slices[0].header_byte_offset, 0u);
    EXPECT_EQ(frame.slices[0].content_byte_offset, 6u);
    EXPECT_EQ(frame.slices[0].payload.size(), payload.size());
    ASSERT_EQ(frame.slices[0].quant_table_set_indexes.size(), 1u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[0], 0u);
}

TEST(FrameParserTest, RejectsHeaderReaderThatConsumesPastPayload)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    ScriptedUnsignedReader reader({0, 0, 1, 1, 1, 0}, 2);
    const std::array<std::byte, 8> payload{
        std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3},
        std::byte{4}, std::byte{5}, std::byte{6}, std::byte{7},
    };

    const auto status = parser.parse_with_header_reader(payload, reader, frame);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 12u);
    EXPECT_TRUE(status.location.has_slice_index);
    EXPECT_EQ(status.location.slice_index, 0u);
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

TEST(FrameParserTest, CreatesSingleSliceDescriptorFromRangeHeader)
{
    const auto stream = make_stream();
    ffv1::codec::FrameParser parser(stream);
    ffv1::codec::FrameDecodeContext frame;
    const std::array<std::byte, 7> payload{
        std::byte{0xbc},
        std::byte{0xd3},
        std::byte{0x3d},
        std::byte{0x65},
        std::byte{0x43},
        std::byte{0x7d},
        std::byte{0x38},
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
    EXPECT_EQ(frame.slices[0].content_byte_offset, 3u);
    EXPECT_EQ(frame.slices[0].payload.size(), payload.size());
    ASSERT_EQ(frame.slices[0].quant_table_set_indexes.size(), 1u);
    EXPECT_EQ(frame.slices[0].quant_table_set_indexes[0], 0u);
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

} // namespace
