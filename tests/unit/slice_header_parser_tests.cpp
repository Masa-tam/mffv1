#include "codec/slice_header_parser.hpp"

#include <gtest/gtest.h>

namespace {

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

