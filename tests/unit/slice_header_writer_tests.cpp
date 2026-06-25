#include "codec/slice_header_writer.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

class RecordingWriter final : public mffv1::entropy::SymbolWriter {
public:
    mffv1::Status write_bool(bool) override
    {
        return mffv1::make_error(
            mffv1::ErrorCode::InternalError,
            "unexpected bool symbol");
    }

    mffv1::Status write_unsigned(std::uint64_t value) override
    {
        values.push_back(value);
        return mffv1::ok_status();
    }

    mffv1::Status write_signed(std::int64_t) override
    {
        return mffv1::make_error(
            mffv1::ErrorCode::InternalError,
            "unexpected signed symbol");
    }

    std::vector<std::uint64_t> values;
};

mffv1::syntax::StreamParameters make_stream()
{
    mffv1::syntax::StreamParameters stream;
    stream.width = 16;
    stream.height = 8;
    stream.version = 3;
    stream.num_h_slices = 2;
    stream.num_v_slices = 2;
    stream.chroma_planes = false;
    stream.quant_table_sets.push_back(
        mffv1::syntax::make_zero_quant_table_set());
    return stream;
}

TEST(SliceHeaderWriterTest, WritesSyntaxInRfcOrder)
{
    const auto stream = make_stream();
    mffv1::codec::SliceHeaderValues values;
    values.x = 1;
    values.y = 0;
    values.width = 1;
    values.height = 2;
    values.quant_table_set_indexes = {0, 0};
    values.picture_structure = 2;
    values.sar_num = 4;
    values.sar_den = 3;
    RecordingWriter symbols;
    const mffv1::codec::SliceHeaderWriter writer;

    const auto status = writer.write(symbols, stream, values);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(
        symbols.values,
        (std::vector<std::uint64_t>{1, 0, 0, 1, 0, 0, 2, 4, 3}));
}

TEST(SliceHeaderWriterTest, RejectsWrongIndexCountBeforeWriting)
{
    const auto stream = make_stream();
    mffv1::codec::SliceHeaderValues values;
    values.width = 1;
    values.height = 1;
    values.quant_table_set_indexes = {0};
    RecordingWriter symbols;
    const mffv1::codec::SliceHeaderWriter writer;

    const auto status = writer.write(symbols, stream, values);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_TRUE(symbols.values.empty());
}

TEST(SliceHeaderWriterTest, RejectsIncompleteAspectRatioBeforeWriting)
{
    const auto stream = make_stream();
    mffv1::codec::SliceHeaderValues values;
    values.width = 1;
    values.height = 1;
    values.quant_table_set_indexes = {0, 0};
    values.sar_num = 1;
    RecordingWriter symbols;
    const mffv1::codec::SliceHeaderWriter writer;

    const auto status = writer.write(symbols, stream, values);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_TRUE(symbols.values.empty());
}

} // namespace
