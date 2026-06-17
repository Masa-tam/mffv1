#include "codec/slice_header_parser.hpp"

#include <cstdint>
#include <deque>
#include <utility>

#include <gtest/gtest.h>

namespace {

class ScriptedUnsignedReader final : public ffv1::entropy::SymbolReader {
public:
    explicit ScriptedUnsignedReader(std::deque<std::uint64_t> values, std::uint64_t bytes_per_read = 0)
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
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());
    stream.quant_table_sets.push_back(ffv1::syntax::make_zero_quant_table_set());
    return stream;
}

TEST(SliceHeaderParserTest, AppliesValidHeaderValues)
{
    const auto stream = make_stream();
    ffv1::codec::SliceHeaderValues values;
    values.x = 4;
    values.y = 2;
    values.width = 8;
    values.height = 4;
    values.quant_table_set_indexes = {1};
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.x, 4u);
    EXPECT_EQ(descriptor.y, 2u);
    EXPECT_EQ(descriptor.width, 8u);
    EXPECT_EQ(descriptor.height, 4u);
    ASSERT_EQ(descriptor.quant_table_set_indexes.size(), 1u);
    EXPECT_EQ(descriptor.quant_table_set_indexes[0], 1u);
}

TEST(SliceHeaderParserTest, ReadsHeaderValuesFromSymbolReader)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({4, 2, 8, 4, 1, 1});
    ffv1::codec::SliceHeaderValues values;

    const ffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(values.x, 4u);
    EXPECT_EQ(values.y, 2u);
    EXPECT_EQ(values.width, 8u);
    EXPECT_EQ(values.height, 4u);
    ASSERT_EQ(values.quant_table_set_indexes.size(), 1u);
    EXPECT_EQ(values.quant_table_set_indexes[0], 1u);
}

TEST(SliceHeaderParserTest, ReadRejectsUnsupportedQuantTableIndexCount)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({0, 0, 16, 8, 4}, 2);
    ffv1::codec::SliceHeaderValues values;

    const ffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read(reader, stream, values);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::UnsupportedFeature);
    EXPECT_TRUE(status.location.has_byte_offset);
    EXPECT_EQ(status.location.byte_offset, 10u);
}

TEST(SliceHeaderParserTest, ReadDescriptorSetsHeaderAndContentOffsets)
{
    const auto stream = make_stream();
    ScriptedUnsignedReader reader({4, 2, 8, 4, 1, 1}, 2);
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SliceHeaderParser parser;
    const auto status = parser.read_descriptor(reader, stream, descriptor);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(descriptor.x, 4u);
    EXPECT_EQ(descriptor.y, 2u);
    EXPECT_EQ(descriptor.width, 8u);
    EXPECT_EQ(descriptor.height, 4u);
    ASSERT_EQ(descriptor.quant_table_set_indexes.size(), 1u);
    EXPECT_EQ(descriptor.quant_table_set_indexes[0], 1u);
    EXPECT_EQ(descriptor.header_byte_offset, 0u);
    EXPECT_EQ(descriptor.content_byte_offset, 12u);
    EXPECT_EQ(descriptor.payload_byte_offset, 0u);
}

TEST(SliceHeaderParserTest, RejectsOutOfFrameRectangle)
{
    const auto stream = make_stream();
    ffv1::codec::SliceHeaderValues values;
    values.x = 15;
    values.y = 0;
    values.width = 2;
    values.height = 1;
    values.quant_table_set_indexes = {0};
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

TEST(SliceHeaderParserTest, RejectsMissingQuantTableIndex)
{
    const auto stream = make_stream();
    ffv1::codec::SliceHeaderValues values;
    values.width = 16;
    values.height = 8;
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

TEST(SliceHeaderParserTest, RejectsOutOfRangeQuantTableIndex)
{
    const auto stream = make_stream();
    ffv1::codec::SliceHeaderValues values;
    values.width = 16;
    values.height = 8;
    values.quant_table_set_indexes = {2};
    ffv1::syntax::SliceDescriptor descriptor;

    const ffv1::codec::SliceHeaderParser parser;
    const auto status = parser.apply(stream, values, descriptor);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

} // namespace
