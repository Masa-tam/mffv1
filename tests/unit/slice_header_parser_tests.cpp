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
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply_raster(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
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

TEST(SliceHeaderParserTest, ReadRejectsOutOfRasterRectangleWithByteLocation)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({0, 0, 1, 0}, 2);
    mffv1::codec::SliceHeaderValues values;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 8u);
}

TEST(SliceHeaderParserTest, ReadRejectsOutOfRangeQuantTableIndexWithByteLocation)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 2}, 2);
    mffv1::codec::SliceHeaderValues values;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 12u);
}

TEST(SliceHeaderParserTest, ReadRejectsReservedPictureStructureWithByteLocation)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({0, 0, 0, 0, 0, 0, 4}, 2);
    mffv1::codec::SliceHeaderValues values;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 14u);
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
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
}

TEST(SliceHeaderParserTest, RejectsMissingQuantTableIndex)
{
    const auto stream = make_stream();
    mffv1::codec::SliceHeaderValues values;
    values.width = 16;
    values.height = 8;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
}

TEST(SliceHeaderParserTest, RejectsOutOfRangeQuantTableIndex)
{
    const auto stream = make_stream();
    mffv1::codec::SliceHeaderValues values;
    values.width = 16;
    values.height = 8;
    values.quant_table_set_indexes = {2};
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
}

TEST(SliceHeaderParserTest, RejectsReservedPictureStructureWhenApplyingValues)
{
    const auto stream = make_stream();
    mffv1::codec::SliceHeaderValues values;
    values.width = 16;
    values.height = 8;
    values.quant_table_set_indexes = {0};
    values.picture_structure = 4;
    mffv1::syntax::SliceDescriptor descriptor;

    const mffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
}

} // namespace
