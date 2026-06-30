#include "codec/slice_header_parser.hpp"

#include <cstdint>
#include <deque>
#include <utility>

#include <gtest/gtest.h>

namespace {

class ScriptedUnsignedReader final : public mffv1::entropy::SymbolReader {
public:
    explicit ScriptedUnsignedReader(std::deque<std::uint64_t> values, std::uint64_t bytes_per_read = 0)
        : values_(std::move(values))
        , bytes_per_read_(bytes_per_read)
    {
    }

    mffv1::Status read_bool(bool&) override
    {
        return mffv1::make_error(mffv1::ErrorCode::InternalError, "unexpected bool read");
    }

    mffv1::Status read_unsigned(std::uint64_t& out_value) override
    {
        if (values_.empty()) {
            return mffv1::make_error(mffv1::ErrorCode::SyntaxError, "scripted reader underflow");
        }
        out_value = values_.front();
        values_.pop_front();
        byte_position_ += bytes_per_read_;
        return mffv1::ok_status();
    }

    mffv1::Status read_signed(std::int64_t&) override
    {
        return mffv1::make_error(mffv1::ErrorCode::InternalError, "unexpected signed read");
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

mffv1::syntax::StreamParameters make_stream()
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 16;
    stream.height = 8;
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());
    return stream;
}

mffv1::codec::SliceHeaderValues make_sentinel_values()
{
    mffv1::codec::SliceHeaderValues values;
    values.x = 11;
    values.y = 12;
    values.width = 13;
    values.height = 14;
    values.quant_table_set_indexes = {1, 0};
    values.picture_structure = 2;
    values.sar_num = 3;
    values.sar_den = 4;
    return values;
}

void expect_values_equal(const mffv1::codec::SliceHeaderValues& actual,
                         const mffv1::codec::SliceHeaderValues& expected)
{
    EXPECT_EQ(actual.x, expected.x);
    EXPECT_EQ(actual.y, expected.y);
    EXPECT_EQ(actual.width, expected.width);
    EXPECT_EQ(actual.height, expected.height);
    EXPECT_EQ(actual.quant_table_set_indexes, expected.quant_table_set_indexes);
    EXPECT_EQ(actual.picture_structure, expected.picture_structure);
    EXPECT_EQ(actual.sar_num, expected.sar_num);
    EXPECT_EQ(actual.sar_den, expected.sar_den);
}

mffv1::syntax::SliceDescriptor make_sentinel_descriptor()
{
    mffv1::syntax::SliceDescriptor descriptor;
    descriptor.index = 5;
    descriptor.x = 11;
    descriptor.y = 12;
    descriptor.width = 13;
    descriptor.height = 14;
    descriptor.raster_x = 1;
    descriptor.raster_y = 2;
    descriptor.raster_width = 3;
    descriptor.raster_height = 4;
    descriptor.header_byte_offset = 101;
    descriptor.content_byte_offset = 102;
    descriptor.content_bit_offset = 3;
    descriptor.payload_byte_offset = 103;
    descriptor.footer_byte_offset = 104;
    descriptor.slice_size = 105;
    descriptor.quant_table_set_indexes = {1, 0};
    descriptor.picture_structure = 2;
    descriptor.sar_num = 3;
    descriptor.sar_den = 4;
    descriptor.error_status = 1;
    descriptor.expected_crc = 0xabcdef01u;
    descriptor.has_crc = true;
    descriptor.continues_frame_range_state = true;
    return descriptor;
}

void expect_descriptor_equal(const mffv1::syntax::SliceDescriptor& actual,
                             const mffv1::syntax::SliceDescriptor& expected)
{
    EXPECT_EQ(actual.index, expected.index);
    EXPECT_EQ(actual.x, expected.x);
    EXPECT_EQ(actual.y, expected.y);
    EXPECT_EQ(actual.width, expected.width);
    EXPECT_EQ(actual.height, expected.height);
    EXPECT_EQ(actual.raster_x, expected.raster_x);
    EXPECT_EQ(actual.raster_y, expected.raster_y);
    EXPECT_EQ(actual.raster_width, expected.raster_width);
    EXPECT_EQ(actual.raster_height, expected.raster_height);
    EXPECT_EQ(actual.header_byte_offset, expected.header_byte_offset);
    EXPECT_EQ(actual.content_byte_offset, expected.content_byte_offset);
    EXPECT_EQ(actual.content_bit_offset, expected.content_bit_offset);
    EXPECT_EQ(actual.payload_byte_offset, expected.payload_byte_offset);
    EXPECT_EQ(actual.footer_byte_offset, expected.footer_byte_offset);
    EXPECT_EQ(actual.slice_size, expected.slice_size);
    EXPECT_EQ(actual.quant_table_set_indexes, expected.quant_table_set_indexes);
    EXPECT_EQ(actual.picture_structure, expected.picture_structure);
    EXPECT_EQ(actual.sar_num, expected.sar_num);
    EXPECT_EQ(actual.sar_den, expected.sar_den);
    EXPECT_EQ(actual.error_status, expected.error_status);
    EXPECT_EQ(actual.expected_crc, expected.expected_crc);
    EXPECT_EQ(actual.has_crc, expected.has_crc);
    EXPECT_EQ(actual.continues_frame_range_state, expected.continues_frame_range_state);
}

TEST(SliceHeaderParserTest, AppliesValidHeaderValues)
{
    const auto stream = make_stream();
    mffv1::codec::SliceHeaderValues values;
    values.x = 4;
    values.y = 2;
    values.width = 8;
    values.height = 4;
    values.quant_table_set_indexes = {1};
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.x, 4u);
    EXPECT_EQ(descriptor.y, 2u);
    EXPECT_EQ(descriptor.width, 8u);
    EXPECT_EQ(descriptor.height, 4u);
    ASSERT_EQ(descriptor.quant_table_set_indexes.size(), 1u);
    EXPECT_EQ(descriptor.quant_table_set_indexes[0], 1u);
}

TEST(SliceHeaderParserTest, AppliesRasterHeaderValuesAsPixelRectangle)
{
    auto stream = make_stream();
    stream.width = 17;
    stream.height = 10;
    stream.num_h_slices = 4;
    stream.num_v_slices = 3;
    mffv1::codec::SliceHeaderValues values;
    values.x = 1;
    values.y = 1;
    values.width = 2;
    values.height = 2;
    values.quant_table_set_indexes = {1};
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply_raster(stream, values, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.x, 4u);
    EXPECT_EQ(descriptor.y, 3u);
    EXPECT_EQ(descriptor.width, 8u);
    EXPECT_EQ(descriptor.height, 7u);
    EXPECT_EQ(descriptor.raster_x, 1u);
    EXPECT_EQ(descriptor.raster_y, 1u);
    EXPECT_EQ(descriptor.raster_width, 2u);
    EXPECT_EQ(descriptor.raster_height, 2u);
    ASSERT_EQ(descriptor.quant_table_set_indexes.size(), 1u);
    EXPECT_EQ(descriptor.quant_table_set_indexes[0], 1u);
}

TEST(SliceHeaderParserTest, RejectsOutOfRasterRectangle)
{
    auto stream = make_stream();
    stream.num_h_slices = 4;
    stream.num_v_slices = 3;
    mffv1::codec::SliceHeaderValues values;
    values.x = 3;
    values.y = 0;
    values.width = 2;
    values.height = 1;
    values.quant_table_set_indexes = {0};
    auto descriptor = make_sentinel_descriptor();
    const auto original = descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply_raster(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice header rectangle is outside the slice raster");
    expect_descriptor_equal(descriptor, original);
}

TEST(SliceHeaderParserTest, ReadsHeaderValuesFromSymbolReader)
{
    auto stream = make_stream();
    stream.num_h_slices = 16;
    stream.num_v_slices = 8;
    ScriptedUnsignedReader reader({4, 2, 7, 3, 1, 0, 3, 4, 3});
    mffv1::codec::SliceHeaderValues values;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(values.x, 4u);
    EXPECT_EQ(values.y, 2u);
    EXPECT_EQ(values.width, 8u);
    EXPECT_EQ(values.height, 4u);
    ASSERT_EQ(values.quant_table_set_indexes.size(), 2u);
    EXPECT_EQ(values.quant_table_set_indexes[0], 1u);
    EXPECT_EQ(values.quant_table_set_indexes[1], 0u);
    EXPECT_EQ(values.picture_structure, 3u);
    EXPECT_EQ(values.sar_num, 4u);
    EXPECT_EQ(values.sar_den, 3u);
}

TEST(SliceHeaderParserTest, NormalizesIncompleteSampleAspectRatio)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 0, 0, 16, 0});
    mffv1::codec::SliceHeaderValues values;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(values.sar_num, 0u);
    EXPECT_EQ(values.sar_den, 0u);
}

TEST(SliceHeaderParserTest, ReadsQuantTableIndexCountFromStreamParameters)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({0, 0, 0, 0, 1, 0, 0, 0, 0}, 2);
    mffv1::codec::SliceHeaderValues values;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(values.quant_table_set_indexes.size(), 2u);
    EXPECT_EQ(values.quant_table_set_indexes[0], 1u);
    EXPECT_EQ(values.quant_table_set_indexes[1], 0u);
    EXPECT_EQ(reader.byte_position(), 18u);
}

TEST(SliceHeaderParserTest, ReadRejectsSliceXOutsideRasterWithByteLocation)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({2}, 2);
    mffv1::codec::SliceHeaderValues values;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice_x is outside the slice raster");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 2u);
}

TEST(SliceHeaderParserTest, ReadRejectsSliceYOutsideRasterWithByteLocation)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({0, 2}, 2);
    mffv1::codec::SliceHeaderValues values;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice_y is outside the slice raster");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 4u);
}

TEST(SliceHeaderParserTest, ReadRejectsOutOfRasterRectangleWithByteLocation)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({0, 0, 1, 0}, 2);
    mffv1::codec::SliceHeaderValues values;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice header rectangle is outside the slice raster");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 8u);
}

TEST(SliceHeaderParserTest, ReadRejectsOutOfRangeQuantTableIndexWithByteLocation)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 2}, 2);
    auto values = make_sentinel_values();
    const auto original = values;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice header quantization table set index is out of range");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 12u);
    expect_values_equal(values, original);
}

TEST(SliceHeaderParserTest, ReadRejectsReservedPictureStructureWithByteLocation)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 0, 4}, 2);
    auto values = make_sentinel_values();
    const auto original = values;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice header picture_structure is reserved");
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 14u);
    expect_values_equal(values, original);
}

TEST(SliceHeaderParserTest, ReadDescriptorSetsHeaderAndContentOffsets)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({0, 0, 0, 0, 1, 0, 3, 4, 3}, 2);
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read_descriptor(reader, stream, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.x, 0u);
    EXPECT_EQ(descriptor.y, 0u);
    EXPECT_EQ(descriptor.width, stream.width);
    EXPECT_EQ(descriptor.height, stream.height);
    EXPECT_EQ(descriptor.raster_x, 0u);
    EXPECT_EQ(descriptor.raster_y, 0u);
    EXPECT_EQ(descriptor.raster_width, 1u);
    EXPECT_EQ(descriptor.raster_height, 1u);
    ASSERT_EQ(descriptor.quant_table_set_indexes.size(), 2u);
    EXPECT_EQ(descriptor.quant_table_set_indexes[0], 1u);
    EXPECT_EQ(descriptor.quant_table_set_indexes[1], 0u);
    EXPECT_EQ(descriptor.header_byte_offset, 0u);
    EXPECT_EQ(descriptor.picture_structure, 3u);
    EXPECT_EQ(descriptor.sar_num, 4u);
    EXPECT_EQ(descriptor.sar_den, 3u);
    EXPECT_EQ(descriptor.content_byte_offset, 18u);
    EXPECT_EQ(descriptor.payload_byte_offset, 0u);
}

TEST(SliceHeaderParserTest, RejectsOutOfFrameRectangle)
{
    const auto stream = make_stream();
    mffv1::codec::SliceHeaderValues values;
    values.x = 15;
    values.y = 0;
    values.width = 2;
    values.height = 1;
    values.quant_table_set_indexes = {0};
    auto descriptor = make_sentinel_descriptor();
    const auto original = descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice header rectangle is outside the frame");
    expect_descriptor_equal(descriptor, original);
}

TEST(SliceHeaderParserTest, RejectsMissingQuantTableIndex)
{
    const auto stream = make_stream();
    mffv1::codec::SliceHeaderValues values;
    values.width = 16;
    values.height = 8;
    auto descriptor = make_sentinel_descriptor();
    const auto original = descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice header has no quantization table set indexes");
    expect_descriptor_equal(descriptor, original);
}

TEST(SliceHeaderParserTest, RejectsOutOfRangeQuantTableIndex)
{
    const auto stream = make_stream();
    mffv1::codec::SliceHeaderValues values;
    values.width = 16;
    values.height = 8;
    values.quant_table_set_indexes = {2};
    auto descriptor = make_sentinel_descriptor();
    const auto original = descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice header quantization table set index is out of range");
    expect_descriptor_equal(descriptor, original);
}

TEST(SliceHeaderParserTest, RejectsReservedPictureStructureWhenApplyingValues)
{
    const auto stream = make_stream();
    mffv1::codec::SliceHeaderValues values;
    values.width = 16;
    values.height = 8;
    values.quant_table_set_indexes = {0};
    values.picture_structure = 4;
    auto descriptor = make_sentinel_descriptor();
    const auto original = descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "slice header picture_structure is reserved");
    expect_descriptor_equal(descriptor, original);
}

} // namespace
