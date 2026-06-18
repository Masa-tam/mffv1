#include "ffv1/configuration_parser.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace {

enum class SymbolKind {
    Bool,
    Unsigned,
    Signed,
};

struct Symbol {
    SymbolKind kind;
    std::int64_t value;
};

class ScriptedSymbolReader final : public ffv1::entropy::SymbolReader {
public:
    explicit ScriptedSymbolReader(std::deque<Symbol> symbols)
        : symbols_(std::move(symbols))
    {
    }

    ffv1::Status read_bool(bool& out_value) override
    {
        Symbol symbol{};
        auto status = pop(SymbolKind::Bool, symbol);
        if (!status.ok()) {
            return status;
        }
        out_value = symbol.value != 0;
        return ffv1::ok_status();
    }

    ffv1::Status read_unsigned(std::uint64_t& out_value) override
    {
        Symbol symbol{};
        auto status = pop(SymbolKind::Unsigned, symbol);
        if (!status.ok()) {
            return status;
        }
        if (symbol.value < 0) {
            return ffv1::make_error(ffv1::ErrorCode::SyntaxError, "scripted unsigned symbol is negative");
        }
        out_value = static_cast<std::uint64_t>(symbol.value);
        return ffv1::ok_status();
    }

    ffv1::Status read_signed(std::int64_t& out_value) override
    {
        Symbol symbol{};
        auto status = pop(SymbolKind::Signed, symbol);
        if (!status.ok()) {
            return status;
        }
        out_value = symbol.value;
        return ffv1::ok_status();
    }

    ffv1::Status read_signed(ffv1::entropy::ContextId context, std::int64_t& out_value) override
    {
        if (context != signed_read_count_ % 32) {
            return ffv1::make_error(ffv1::ErrorCode::InternalError,
                                    "unexpected scripted signed context");
        }
        ++signed_read_count_;
        return read_signed(out_value);
    }

private:
    ffv1::Status pop(SymbolKind expected, Symbol& out_symbol)
    {
        if (symbols_.empty()) {
            return ffv1::make_error(ffv1::ErrorCode::SyntaxError, "scripted symbol reader underflow");
        }
        out_symbol = symbols_.front();
        symbols_.pop_front();
        if (out_symbol.kind != expected) {
            return ffv1::make_error(ffv1::ErrorCode::InternalError, "scripted symbol kind mismatch");
        }
        return ffv1::ok_status();
    }

    std::deque<Symbol> symbols_;
    ffv1::entropy::ContextId signed_read_count_ = 0;
};

Symbol b(bool value)
{
    return {SymbolKind::Bool, value ? 1 : 0};
}

Symbol u(std::int64_t value)
{
    return {SymbolKind::Unsigned, value};
}

Symbol s(std::int64_t value)
{
    return {SymbolKind::Signed, value};
}

std::deque<Symbol> minimal_v3_y_only_symbols()
{
    std::deque<Symbol> symbols{
        u(3),     // version
        u(4),     // micro_version
        u(1),     // coder_type: range
        u(0),     // colorspace_type: YCbCr
        u(8),     // bits_per_raw_sample
        b(false), // chroma_planes
        u(0),     // log2_h_chroma_subsample
        u(0),     // log2_v_chroma_subsample
        b(false), // extra_plane
        u(0),     // num_h_slices - 1
        u(0),     // num_v_slices - 1
        u(1),     // quant_table_set_count
    };

    for (int table = 0; table < 5; ++table) {
        symbols.push_back(u(127)); // one run of 128 zero entries
    }

    symbols.push_back(b(false)); // states_coded
    symbols.push_back(u(0));     // ec
    symbols.push_back(u(1));     // intra
    return symbols;
}

TEST(ConfigurationParserTest, ParsesMinimalVersion3YOnlyParameters)
{
    ScriptedSymbolReader reader(minimal_v3_y_only_symbols());
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.version, 3);
    EXPECT_EQ(stream.micro_version, 4);
    EXPECT_EQ(stream.entropy_mode, ffv1::EntropyMode::Range);
    EXPECT_EQ(stream.colorspace_type, 0);
    EXPECT_EQ(stream.bits_per_raw_sample, 8);
    EXPECT_FALSE(stream.chroma_planes);
    EXPECT_FALSE(stream.extra_plane);
    EXPECT_EQ(stream.num_h_slices, 1u);
    EXPECT_EQ(stream.num_v_slices, 1u);
    ASSERT_EQ(stream.quant_table_sets.size(), 1u);
    EXPECT_EQ(stream.quant_table_sets[0].context_count, 1u);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][0], 0);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][127], 0);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][128], 0);
}

TEST(ConfigurationParserTest, RejectsUnstableVersion3MicroVersions)
{
    for (std::int64_t micro_version = 0; micro_version < 4; ++micro_version) {
        auto symbols = minimal_v3_y_only_symbols();
        symbols[1] = u(micro_version);
        ScriptedSymbolReader reader(std::move(symbols));
        ffv1::syntax::ConfigurationParser parser;
        ffv1::syntax::StreamParameters stream;
        stream.version = 1;

        const auto status = parser.parse(reader, stream);

        EXPECT_FALSE(status.ok()) << "micro_version=" << micro_version;
        EXPECT_EQ(status.code, ffv1::ErrorCode::UnsupportedFeature)
            << "micro_version=" << micro_version;
        EXPECT_EQ(stream.version, 1) << "micro_version=" << micro_version;
    }
}

TEST(ConfigurationParserTest, AcceptsFutureStableVersion3MicroVersion)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[1] = u(5);
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.version, 3);
    EXPECT_EQ(stream.micro_version, 5);
}

TEST(ConfigurationParserTest, InterpretsReservedZeroBitDepthAsEight)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[4] = u(0);
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.bits_per_raw_sample, 8u);
}

TEST(ConfigurationParserTest, ParsesLargeChromaSubsamplingExponents)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[6] = u(40);
    symbols[7] = u(255);
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.log2_h_chroma_subsample, 40u);
    EXPECT_EQ(stream.log2_v_chroma_subsample, 255u);
}

TEST(ConfigurationParserTest, RejectsUnrepresentableChromaSubsamplingExponent)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[6] = u(256);
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

TEST(ConfigurationParserTest, RejectsUnsupportedVersion)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols.front() = u(2);
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::UnsupportedFeature);
}

TEST(ConfigurationParserTest, ParsesCustomRangeCoderStateTransitions)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[2] = u(2);
    for (int state = 1; state < 256; ++state) {
        symbols.insert(symbols.begin() + 2 + state, s(state == 8 ? 5 : 0));
    }
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.entropy_mode, ffv1::EntropyMode::Range);
    EXPECT_EQ(stream.state_transition[0], 0u);
    EXPECT_EQ(stream.state_transition[8], 25u);
    EXPECT_EQ(stream.state_transition[255], 0u);
}

TEST(ConfigurationParserTest, RejectsOutOfRangeCustomStateTransition)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[2] = u(2);
    symbols.insert(symbols.begin() + 3, s(-1));
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

TEST(ConfigurationParserTest, RejectsCustomStateTransitionAboveByteRange)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[2] = u(2);
    symbols.insert(symbols.begin() + 3, s(256));
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

TEST(ConfigurationParserTest, RejectsReservedCoderType)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[2] = u(3);
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::UnsupportedFeature);
}

TEST(ConfigurationParserTest, RejectsTruncatedCustomStateTransitions)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[2] = u(2);
    symbols.erase(symbols.begin() + 3, symbols.end());
    symbols.push_back(s(0));
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

TEST(ConfigurationParserTest, RejectsOversizedQuantTableRun)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[12] = u(128);
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

TEST(ConfigurationParserTest, RejectsUnsupportedColorspace)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[3] = u(1);
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::UnsupportedFeature);
}

TEST(ConfigurationParserTest, ParsesCustomRangeCoderInitialStates)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[17] = b(true);
    for (int state = 0; state < 32; ++state) {
        symbols.insert(symbols.begin() + 18 + state, s(state == 0 ? -129 : state - 16));
    }
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(stream.initial_states.size(), 1u);
    ASSERT_EQ(stream.initial_states[0].contexts.size(), 1u);
    EXPECT_EQ(stream.initial_states[0].contexts[0][0], 255u);
    EXPECT_EQ(stream.initial_states[0].contexts[0][1], 113u);
    EXPECT_EQ(stream.initial_states[0].contexts[0][31], 143u);
}

TEST(ConfigurationParserTest, PredictsCustomInitialStatesFromPreviousContext)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[12] = u(0);
    symbols.insert(symbols.begin() + 13, u(126));
    symbols[18] = b(true);
    for (int context = 0; context < 2; ++context) {
        for (int state = 0; state < 32; ++state) {
            const auto position = 19 + context * 32 + state;
            symbols.insert(symbols.begin() + position, s(context + 1));
        }
    }
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(stream.quant_table_sets[0].context_count, 2u);
    ASSERT_EQ(stream.initial_states[0].contexts.size(), 2u);
    EXPECT_EQ(stream.initial_states[0].contexts[0][0], 129u);
    EXPECT_EQ(stream.initial_states[0].contexts[0][31], 129u);
    EXPECT_EQ(stream.initial_states[0].contexts[1][0], 131u);
    EXPECT_EQ(stream.initial_states[0].contexts[1][31], 131u);
}

TEST(ConfigurationParserTest, RejectsTruncatedCustomRangeCoderInitialStates)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[17] = b(true);
    symbols.erase(symbols.begin() + 18, symbols.end());
    symbols.push_back(s(0));
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::SyntaxError);
}

TEST(ConfigurationParserTest, RejectsNonIntraStream)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols.back() = u(0);
    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;
    stream.version = 1;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, ffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(stream.version, 1);
}

TEST(ConfigurationParserTest, Version0UsesDefaultZeroQuantTableSet)
{
    std::deque<Symbol> symbols{
        u(0),     // version
        u(1),     // coder_type: range
        u(0),     // colorspace_type: YCbCr
        b(false), // chroma_planes
        u(0),     // log2_h_chroma_subsample
        u(0),     // log2_v_chroma_subsample
        b(false), // extra_plane
    };

    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.version, 0);
    ASSERT_EQ(stream.quant_table_sets.size(), 1u);
    EXPECT_EQ(stream.quant_table_sets[0].context_count, 1u);
}

TEST(ConfigurationParserTest, QuantTableMirrorsNegativeHalf)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[12] = u(0);  // table 0: value 0 for index 0
    symbols.insert(symbols.begin() + 13, u(126)); // table 0: value 1 for indexes 1..127

    ScriptedSymbolReader reader(std::move(symbols));
    ffv1::syntax::ConfigurationParser parser;
    ffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(stream.quant_table_sets.size(), 1u);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][0], 0);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][1], 1);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][127], 1);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][128], -1);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][255], -1);
}

} // namespace
