#include "codec/configuration_record_writer.hpp"

#include "codec/configuration_record_parser.hpp"
#include "mffv1/codec.hpp"
#include "util/crc32.hpp"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

enum class SymbolKind {
    Bool,
    Unsigned,
    Signed,
};

struct Symbol {
    SymbolKind kind = SymbolKind::Bool;
    std::int64_t value = 0;

    bool operator==(const Symbol&) const = default;
};

class RecordingSymbolWriter final : public mffv1::entropy::SymbolWriter {
public:
    mffv1::Status write_bool(bool value) override
    {
        symbols.push_back({SymbolKind::Bool, value ? 1 : 0});
        return mffv1::ok_status();
    }

    mffv1::Status write_unsigned(std::uint64_t value) override
    {
        symbols.push_back({
            SymbolKind::Unsigned,
            static_cast<std::int64_t>(value),
        });
        return mffv1::ok_status();
    }

    mffv1::Status write_signed(std::int64_t value) override
    {
        symbols.push_back({SymbolKind::Signed, value});
        return mffv1::ok_status();
    }

    std::vector<Symbol> symbols;
};

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
    stream.quant_table_sets.push_back(
        mffv1::syntax::make_zero_quant_table_set());
    stream.intra_only = true;
    return stream;
}

TEST(ConfigurationRecordWriterTest, WritesInitialProfileSyntaxInRfcOrder)
{
    const auto stream = make_initial_profile();
    RecordingSymbolWriter symbols;
    const mffv1::codec::ConfigurationRecordWriter writer;

    const auto status = writer.write_parameters(stream, symbols);

    EXPECT_TRUE(status.ok()) << status.message;
    const std::vector<Symbol> expected{
        {SymbolKind::Unsigned, 3},
        {SymbolKind::Unsigned, 4},
        {SymbolKind::Unsigned, 1},
        {SymbolKind::Unsigned, 0},
        {SymbolKind::Unsigned, 8},
        {SymbolKind::Bool, 0},
        {SymbolKind::Unsigned, 0},
        {SymbolKind::Unsigned, 0},
        {SymbolKind::Bool, 0},
        {SymbolKind::Unsigned, 0},
        {SymbolKind::Unsigned, 0},
        {SymbolKind::Unsigned, 1},
        {SymbolKind::Unsigned, 127},
        {SymbolKind::Unsigned, 127},
        {SymbolKind::Unsigned, 127},
        {SymbolKind::Unsigned, 127},
        {SymbolKind::Unsigned, 127},
        {SymbolKind::Bool, 0},
        {SymbolKind::Unsigned, 0},
        {SymbolKind::Unsigned, 1},
    };
    EXPECT_EQ(symbols.symbols, expected);
}

TEST(ConfigurationRecordWriterTest, GeneratedRecordRoundTripsThroughParser)
{
    const auto stream = make_initial_profile();
    const mffv1::codec::ConfigurationRecordWriter writer;
    std::vector<std::byte> record;

    const auto status = writer.write(stream, record);

    ASSERT_TRUE(status.ok()) << status.message;
    ASSERT_GE(record.size(), 6u);
    EXPECT_EQ(mffv1::util::crc32_ieee_msb(record), 0u);

    mffv1::syntax::StreamParameters parsed;
    const mffv1::codec::ConfigurationRecordParser parser;
    ASSERT_TRUE(parser.parse(record, parsed).ok());
    EXPECT_EQ(parsed.version, stream.version);
    EXPECT_EQ(parsed.micro_version, stream.micro_version);
    EXPECT_EQ(parsed.entropy_mode, stream.entropy_mode);
    EXPECT_EQ(parsed.bits_per_raw_sample, stream.bits_per_raw_sample);
    EXPECT_EQ(parsed.colorspace_type, stream.colorspace_type);
    EXPECT_EQ(parsed.chroma_planes, stream.chroma_planes);
    EXPECT_EQ(parsed.extra_plane, stream.extra_plane);
    EXPECT_EQ(parsed.log2_h_chroma_subsample,
              stream.log2_h_chroma_subsample);
    EXPECT_EQ(parsed.log2_v_chroma_subsample,
              stream.log2_v_chroma_subsample);
    EXPECT_EQ(parsed.num_h_slices, stream.num_h_slices);
    EXPECT_EQ(parsed.num_v_slices, stream.num_v_slices);
    EXPECT_EQ(parsed.error_status_enabled, stream.error_status_enabled);
    EXPECT_EQ(parsed.intra_only, stream.intra_only);
    ASSERT_EQ(parsed.quant_table_sets.size(), 1u);
    EXPECT_EQ(parsed.quant_table_sets[0].context_count, 1u);
    EXPECT_EQ(parsed.quant_table_sets[0].tables,
              stream.quant_table_sets[0].tables);
}

TEST(ConfigurationRecordWriterTest, GeneratedRecordPreservesChromaPlanes)
{
    auto stream = make_initial_profile();
    stream.chroma_planes = true;
    const mffv1::codec::ConfigurationRecordWriter writer;
    std::vector<std::byte> record;

    ASSERT_TRUE(writer.write(stream, record).ok());

    mffv1::syntax::StreamParameters parsed;
    const mffv1::codec::ConfigurationRecordParser parser;
    ASSERT_TRUE(parser.parse(record, parsed).ok());
    EXPECT_TRUE(parsed.chroma_planes);
    EXPECT_EQ(parsed.log2_h_chroma_subsample, 0u);
    EXPECT_EQ(parsed.log2_v_chroma_subsample, 0u);
    EXPECT_EQ(mffv1::syntax::coded_plane_count(parsed), 3u);
}

TEST(ConfigurationRecordWriterTest, GeneratedRecordPreservesChromaSubsampling)
{
    auto stream = make_initial_profile();
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 1;
    stream.log2_v_chroma_subsample = 1;
    const mffv1::codec::ConfigurationRecordWriter writer;
    std::vector<std::byte> record;

    ASSERT_TRUE(writer.write(stream, record).ok());

    mffv1::syntax::StreamParameters parsed;
    const mffv1::codec::ConfigurationRecordParser parser;
    ASSERT_TRUE(parser.parse(record, parsed).ok());
    EXPECT_TRUE(parsed.chroma_planes);
    EXPECT_EQ(parsed.log2_h_chroma_subsample, 1u);
    EXPECT_EQ(parsed.log2_v_chroma_subsample, 1u);
}

TEST(ConfigurationRecordWriterTest, GeneratedRecordPreservesExtraPlane)
{
    auto stream = make_initial_profile();
    stream.extra_plane = true;
    const mffv1::codec::ConfigurationRecordWriter writer;
    std::vector<std::byte> record;

    ASSERT_TRUE(writer.write(stream, record).ok());

    mffv1::syntax::StreamParameters parsed;
    const mffv1::codec::ConfigurationRecordParser parser;
    ASSERT_TRUE(parser.parse(record, parsed).ok());
    EXPECT_TRUE(parsed.extra_plane);
    EXPECT_EQ(mffv1::syntax::coded_plane_count(parsed), 2u);
}

TEST(ConfigurationRecordWriterTest, GeneratedRecordConfiguresPublicDecoder)
{
    const auto stream = make_initial_profile();
    const mffv1::codec::ConfigurationRecordWriter writer;
    std::vector<std::byte> record;
    ASSERT_TRUE(writer.write(stream, record).ok());
    mffv1::DecoderOptions options;
    options.frame_width = stream.width;
    options.frame_height = stream.height;
    auto result = mffv1::create_decoder(options);
    ASSERT_TRUE(result.status.ok());
    ASSERT_NE(result.decoder, nullptr);

    const auto status = result.decoder->configure(record);

    EXPECT_TRUE(status.ok()) << status.message;
}

TEST(ConfigurationRecordWriterTest, RejectsUnsupportedProfileWithoutChangingOutput)
{
    auto stream = make_initial_profile();
    stream.chroma_planes = true;
    stream.log2_h_chroma_subsample = 2;
    std::vector<std::byte> record{std::byte{0xaa}};
    const mffv1::codec::ConfigurationRecordWriter writer;

    const auto status = writer.write(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    ASSERT_EQ(record.size(), 1u);
    EXPECT_EQ(record[0], std::byte{0xaa});
}

TEST(ConfigurationRecordWriterTest, RejectsVerticalOnlySubsampling)
{
    auto stream = make_initial_profile();
    stream.chroma_planes = true;
    stream.log2_v_chroma_subsample = 1;
    std::vector<std::byte> record{std::byte{0xaa}};
    const mffv1::codec::ConfigurationRecordWriter writer;

    const auto status = writer.write(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(record, (std::vector<std::byte>{std::byte{0xaa}}));
}

TEST(ConfigurationRecordWriterTest, AcceptsNormalizedEmptyInitialStateSet)
{
    auto stream = make_initial_profile();
    stream.initial_states.resize(1);
    const mffv1::codec::ConfigurationRecordWriter writer;
    std::vector<std::byte> record;

    const auto status = writer.write(stream, record);

    EXPECT_TRUE(status.ok()) << status.message;
}

TEST(ConfigurationRecordWriterTest, RejectsCustomInitialStates)
{
    auto stream = make_initial_profile();
    stream.initial_states.resize(1);
    stream.initial_states[0].contexts.resize(1);
    const mffv1::codec::ConfigurationRecordWriter writer;
    std::vector<std::byte> record;

    const auto status = writer.write(stream, record);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
}

} // namespace
