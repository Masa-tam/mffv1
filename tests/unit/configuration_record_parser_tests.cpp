#include "codec/configuration_record_parser.hpp"

#include "codec/configuration_record_writer.hpp"
#include "entropy/range_encoder.hpp"
#include "util/crc32.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

mffv1::syntax::StreamParameters make_initial_profile()
{
    mffv1::syntax::StreamParameters stream;
    stream.version = 3;
    stream.micro_version = 4;
    stream.entropy_mode = mffv1::EntropyMode::Range;
    stream.width = 16;
    stream.height = 8;
    stream.bits_per_raw_sample = 8;
    stream.colorspace_type = 0;
    stream.chroma_planes = false;
    stream.extra_plane = false;
    stream.log2_h_chroma_subsample = 0;
    stream.log2_v_chroma_subsample = 0;
    stream.num_h_slices = 1;
    stream.num_v_slices = 1;
    stream.quant_table_sets.push_back(mffv1::syntax::make_zero_quant_table_set());
    stream.intra_only = true;
    return stream;
}

std::vector<std::byte> make_configuration_record()
{
    const mffv1::codec::ConfigurationRecordWriter writer;
    std::vector<std::byte> record;
    const auto status = writer.write(make_initial_profile(), record);
    EXPECT_TRUE(status.ok()) << status.message;
    return record;
}

void append_crc_parity(std::vector<std::byte>& bytes)
{
    const auto crc = mffv1::util::crc32_ieee_msb(bytes);
    bytes.push_back(static_cast<std::byte>((crc >> 24) & 0xffu));
    bytes.push_back(static_cast<std::byte>((crc >> 16) & 0xffu));
    bytes.push_back(static_cast<std::byte>((crc >> 8) & 0xffu));
    bytes.push_back(static_cast<std::byte>(crc & 0xffu));
}

void make_configuration_record_with_initial_states(std::vector<std::byte>& record)
{
    mffv1::entropy::RangeEncoder writer;
    ASSERT_TRUE(writer.reset(mffv1::syntax::InitialState{}.size()).ok());

    ASSERT_TRUE(writer.write_unsigned(3).ok()); // version
    ASSERT_TRUE(writer.write_unsigned(4).ok()); // micro_version
    ASSERT_TRUE(writer.write_unsigned(1).ok()); // range coder
    ASSERT_TRUE(writer.write_unsigned(0).ok()); // YCbCr
    ASSERT_TRUE(writer.write_unsigned(8).ok()); // bits_per_raw_sample
    ASSERT_TRUE(writer.write_bool(false).ok()); // chroma_planes
    ASSERT_TRUE(writer.write_unsigned(0).ok()); // log2_h_chroma_subsample
    ASSERT_TRUE(writer.write_unsigned(0).ok()); // log2_v_chroma_subsample
    ASSERT_TRUE(writer.write_bool(false).ok()); // extra_plane
    ASSERT_TRUE(writer.write_unsigned(0).ok()); // num_h_slices - 1
    ASSERT_TRUE(writer.write_unsigned(0).ok()); // num_v_slices - 1
    ASSERT_TRUE(writer.write_unsigned(1).ok()); // quant_table_set_count

    for (std::size_t table = 0;
         table < mffv1::syntax::QuantTableSet::kContextInputs;
         ++table) {
        ASSERT_TRUE(writer.begin_independent_scalar_contexts(1).ok());
        ASSERT_TRUE(writer.write_unsigned(127).ok());
        ASSERT_TRUE(writer.end_independent_scalar_contexts().ok());
    }

    ASSERT_TRUE(writer.write_bool(true).ok()); // states_coded
    for (std::size_t state_index = 0;
         state_index < mffv1::syntax::InitialState{}.size();
         ++state_index) {
        const auto delta = state_index == 31 ? std::int64_t{5} : std::int64_t{0};
        ASSERT_TRUE(writer.write_signed(
            0, static_cast<mffv1::entropy::ContextId>(state_index), delta).ok());
    }
    ASSERT_TRUE(writer.write_unsigned(0).ok()); // ec
    ASSERT_TRUE(writer.write_unsigned(1).ok()); // intra

    ASSERT_TRUE(writer.finalize(record).ok());
    append_crc_parity(record);
}

} // namespace

TEST(ConfigurationRecordParserTest, RejectsEmptyRecord)
{
    const mffv1::codec::ConfigurationRecordParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse({}, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(status.message, "configuration record is empty");
}

TEST(ConfigurationRecordParserTest, EmptyRecordPreservesOutputStream)
{
    const mffv1::codec::ConfigurationRecordParser parser;
    mffv1::syntax::StreamParameters stream;
    stream.version = 2;
    stream.micro_version = 3;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.bits_per_raw_sample = 16;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.extra_plane = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;
    stream.num_h_slices = 4;
    stream.num_v_slices = 3;
    stream.intra_only = false;

    const auto status = parser.parse({}, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::InvalidArgument);
    EXPECT_EQ(stream.version, 2u);
    EXPECT_EQ(stream.micro_version, 3u);
    EXPECT_EQ(stream.entropy_mode, mffv1::EntropyMode::GolombRice);
    EXPECT_EQ(stream.bits_per_raw_sample, 16u);
    EXPECT_EQ(stream.colorspace_type, 1u);
    EXPECT_TRUE(stream.chroma_planes);
    EXPECT_TRUE(stream.extra_plane);
    EXPECT_EQ(stream.log2_h_chroma_subsample, 1u);
    EXPECT_EQ(stream.log2_v_chroma_subsample, 1u);
    EXPECT_EQ(stream.num_h_slices, 4u);
    EXPECT_EQ(stream.num_v_slices, 3u);
    EXPECT_FALSE(stream.intra_only);
}

TEST(ConfigurationRecordParserTest, AcceptsVersionThreeRecordAndStripsCrcParity)
{
    const auto expected = make_initial_profile();
    const auto record = make_configuration_record();
    ASSERT_EQ(mffv1::util::crc32_ieee_msb(record), 0u);
    const mffv1::codec::ConfigurationRecordParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(record, stream);

    ASSERT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.version, expected.version);
    EXPECT_EQ(stream.micro_version, expected.micro_version);
    EXPECT_EQ(stream.entropy_mode, expected.entropy_mode);
    EXPECT_EQ(stream.bits_per_raw_sample, expected.bits_per_raw_sample);
    EXPECT_EQ(stream.colorspace_type, expected.colorspace_type);
    EXPECT_EQ(stream.chroma_planes, expected.chroma_planes);
    EXPECT_EQ(stream.extra_plane, expected.extra_plane);
    EXPECT_EQ(stream.log2_h_chroma_subsample,
              expected.log2_h_chroma_subsample);
    EXPECT_EQ(stream.log2_v_chroma_subsample,
              expected.log2_v_chroma_subsample);
    EXPECT_EQ(stream.num_h_slices, expected.num_h_slices);
    EXPECT_EQ(stream.num_v_slices, expected.num_v_slices);
    EXPECT_EQ(stream.intra_only, expected.intra_only);
    ASSERT_EQ(stream.quant_table_sets.size(), 1u);
    EXPECT_EQ(stream.quant_table_sets[0].context_count, 1u);
    EXPECT_EQ(stream.quant_table_sets[0].tables,
              expected.quant_table_sets[0].tables);
}

TEST(ConfigurationRecordParserTest, ParsesRangeCodedInitialStatesWithAllStateContexts)
{
    std::vector<std::byte> record;
    make_configuration_record_with_initial_states(record);
    ASSERT_EQ(mffv1::util::crc32_ieee_msb(record), 0u);
    const mffv1::codec::ConfigurationRecordParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(record, stream);

    ASSERT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(stream.initial_states.size(), 1u);
    ASSERT_EQ(stream.initial_states[0].contexts.size(), 1u);
    EXPECT_EQ(stream.initial_states[0].contexts[0][0], 128u);
    EXPECT_EQ(stream.initial_states[0].contexts[0][30], 128u);
    EXPECT_EQ(stream.initial_states[0].contexts[0][31], 133u);
}

TEST(ConfigurationRecordParserTest, RejectsVersionThreeRecordTooSmallForCrc)
{
    auto record = make_configuration_record();
    ASSERT_GE(record.size(), 4u);
    record.resize(3);
    const mffv1::codec::ConfigurationRecordParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(record, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "configuration record is too small for CRC parity");
    EXPECT_EQ(status.location.byte_offset, 0u);
}

TEST(ConfigurationRecordParserTest, ShortVersionThreeRecordPreservesOutputStream)
{
    auto record = make_configuration_record();
    ASSERT_GE(record.size(), 4u);
    record.resize(3);
    const mffv1::codec::ConfigurationRecordParser parser;
    mffv1::syntax::StreamParameters stream;
    stream.version = 2;
    stream.micro_version = 3;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.bits_per_raw_sample = 16;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.extra_plane = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;
    stream.num_h_slices = 4;
    stream.num_v_slices = 3;
    stream.intra_only = false;

    const auto status = parser.parse(record, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(stream.version, 2u);
    EXPECT_EQ(stream.micro_version, 3u);
    EXPECT_EQ(stream.entropy_mode, mffv1::EntropyMode::GolombRice);
    EXPECT_EQ(stream.bits_per_raw_sample, 16u);
    EXPECT_EQ(stream.colorspace_type, 1u);
    EXPECT_TRUE(stream.chroma_planes);
    EXPECT_TRUE(stream.extra_plane);
    EXPECT_EQ(stream.log2_h_chroma_subsample, 1u);
    EXPECT_EQ(stream.log2_v_chroma_subsample, 1u);
    EXPECT_EQ(stream.num_h_slices, 4u);
    EXPECT_EQ(stream.num_v_slices, 3u);
    EXPECT_FALSE(stream.intra_only);
}

TEST(ConfigurationRecordParserTest, RejectsVersionThreeRecordWithCrcMismatch)
{
    auto record = make_configuration_record();
    ASSERT_GE(record.size(), 4u);
    record.back() ^= std::byte{0x01};
    const mffv1::codec::ConfigurationRecordParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(record, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::CrcMismatch);
    EXPECT_EQ(status.message, "configuration record CRC remainder is non-zero");
    EXPECT_EQ(status.location.byte_offset, record.size() - 4u);
}

TEST(ConfigurationRecordParserTest, FailedParsePreservesOutputStream)
{
    auto record = make_configuration_record();
    ASSERT_GE(record.size(), 4u);
    record.back() ^= std::byte{0x01};
    const mffv1::codec::ConfigurationRecordParser parser;
    mffv1::syntax::StreamParameters stream;
    stream.version = 2;
    stream.micro_version = 3;
    stream.entropy_mode = mffv1::EntropyMode::GolombRice;
    stream.bits_per_raw_sample = 16;
    stream.colorspace_type = 1;
    stream.chroma_planes = true;
    stream.extra_plane = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;
    stream.num_h_slices = 4;
    stream.num_v_slices = 3;
    stream.intra_only = false;

    const auto status = parser.parse(record, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(stream.version, 2u);
    EXPECT_EQ(stream.micro_version, 3u);
    EXPECT_EQ(stream.entropy_mode, mffv1::EntropyMode::GolombRice);
    EXPECT_EQ(stream.bits_per_raw_sample, 16u);
    EXPECT_EQ(stream.colorspace_type, 1u);
    EXPECT_TRUE(stream.chroma_planes);
    EXPECT_TRUE(stream.extra_plane);
    EXPECT_EQ(stream.log2_h_chroma_subsample, 1u);
    EXPECT_EQ(stream.log2_v_chroma_subsample, 1u);
    EXPECT_EQ(stream.num_h_slices, 4u);
    EXPECT_EQ(stream.num_v_slices, 3u);
    EXPECT_FALSE(stream.intra_only);
}
