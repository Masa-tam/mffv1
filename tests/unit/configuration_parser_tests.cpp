#include "mffv1/configuration_parser.hpp"

#include <cstdint>
#include <deque>
#include <limits>
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
    bool maximum_unsigned = false;
};

class ScriptedSymbolReader final : public mffv1::entropy::SymbolReader {
public:
    explicit ScriptedSymbolReader(std::deque<Symbol> symbols)
        : symbols_(std::move(symbols))
    {
    }

    mffv1::Status read_bool(bool& out_value) override
    {
        Symbol symbol{};
        auto status = pop(SymbolKind::Bool, symbol);
        if (!status.ok()) {
            return status;
        }
        out_value = symbol.value != 0;
        return mffv1::ok_status();
    }

    mffv1::Status read_unsigned(std::uint64_t& out_value) override
    {
        Symbol symbol{};
        auto status = pop(SymbolKind::Unsigned, symbol);
        if (!status.ok()) {
            return status;
        }
        if (symbol.maximum_unsigned) {
            out_value = std::numeric_limits<std::uint64_t>::max();
            return mffv1::ok_status();
        }
        if (symbol.value < 0) {
            return mffv1::make_error(mffv1::ErrorCode::SyntaxError, "scripted unsigned symbol is negative");
        }
        out_value = static_cast<std::uint64_t>(symbol.value);
        return mffv1::ok_status();
    }

    mffv1::Status read_signed(std::int64_t& out_value) override
    {
        Symbol symbol{};
        auto status = pop(SymbolKind::Signed, symbol);
        if (!status.ok()) {
            return status;
        }
        out_value = symbol.value;
        return mffv1::ok_status();
    }

    mffv1::Status read_signed(mffv1::entropy::ContextId context, std::int64_t& out_value) override
    {
        if (context != signed_read_count_ % 32) {
            return mffv1::make_error(mffv1::ErrorCode::InternalError,
                                    "unexpected scripted signed context");
        }
        ++signed_read_count_;
        return read_signed(out_value);
    }

    mffv1::Status begin_independent_scalar_contexts(std::size_t scalar_context_count) override
    {
        ++independent_scalar_begin_count_;
        last_independent_scalar_context_count_ = scalar_context_count;
        return mffv1::ok_status();
    }

    mffv1::Status end_independent_scalar_contexts() override
    {
        ++independent_scalar_end_count_;
        return mffv1::ok_status();
    }

    mffv1::Status set_state_transition(
        const mffv1::syntax::StateTransitionTable& state_transition) override
    {
        ++state_transition_update_count_;
        last_state_transition_ = state_transition;
        return mffv1::ok_status();
    }

    [[nodiscard]] std::size_t state_transition_update_count() const noexcept
    {
        return state_transition_update_count_;
    }

    [[nodiscard]] const mffv1::syntax::StateTransitionTable& last_state_transition() const noexcept
    {
        return last_state_transition_;
    }

    [[nodiscard]] std::size_t independent_scalar_begin_count() const noexcept
    {
        return independent_scalar_begin_count_;
    }

    [[nodiscard]] std::size_t independent_scalar_end_count() const noexcept
    {
        return independent_scalar_end_count_;
    }

    [[nodiscard]] std::size_t last_independent_scalar_context_count() const noexcept
    {
        return last_independent_scalar_context_count_;
    }

private:
    mffv1::Status pop(SymbolKind expected, Symbol& out_symbol)
    {
        if (symbols_.empty()) {
            return mffv1::make_error(mffv1::ErrorCode::SyntaxError, "scripted symbol reader underflow");
        }
        out_symbol = symbols_.front();
        symbols_.pop_front();
        if (out_symbol.kind != expected) {
            return mffv1::make_error(mffv1::ErrorCode::InternalError, "scripted symbol kind mismatch");
        }
        return mffv1::ok_status();
    }

    std::deque<Symbol> symbols_;
    mffv1::entropy::ContextId signed_read_count_ = 0;
    std::size_t independent_scalar_begin_count_ = 0;
    std::size_t independent_scalar_end_count_ = 0;
    std::size_t last_independent_scalar_context_count_ = 0;
    std::size_t state_transition_update_count_ = 0;
    mffv1::syntax::StateTransitionTable last_state_transition_{};
};

Symbol b(bool value)
{
    return {SymbolKind::Bool, value ? 1 : 0};
}

Symbol u(std::int64_t value)
{
    return {SymbolKind::Unsigned, value};
}

Symbol u_max()
{
    return {SymbolKind::Unsigned, 0, true};
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
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.version, 3);
    EXPECT_EQ(stream.micro_version, 4);
    EXPECT_EQ(stream.entropy_mode, mffv1::EntropyMode::Range);
    EXPECT_EQ(stream.colorspace_type, 0);
    EXPECT_EQ(stream.bits_per_raw_sample, 8);
    EXPECT_FALSE(stream.chroma_planes);
    EXPECT_FALSE(stream.extra_plane);
    EXPECT_TRUE(stream.intra_only);
    EXPECT_EQ(stream.num_h_slices, 1u);
    EXPECT_EQ(stream.num_v_slices, 1u);
    ASSERT_EQ(stream.quant_table_sets.size(), 1u);
    EXPECT_EQ(stream.quant_table_sets[0].context_count, 1u);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][0], 0);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][127], 0);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][128], 0);
}

TEST(ConfigurationParserTest, QuantTableSetUsesSingleIndependentScalarContextScope)
{
    ScriptedSymbolReader reader(minimal_v3_y_only_symbols());
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(reader.independent_scalar_begin_count(), 1u);
    EXPECT_EQ(reader.independent_scalar_end_count(), 1u);
    EXPECT_EQ(reader.last_independent_scalar_context_count(), 1u);
}

TEST(ConfigurationParserTest, RejectsUnstableVersion3MicroVersions)
{
    for (std::int64_t micro_version = 0; micro_version < 4; ++micro_version) {
        auto symbols = minimal_v3_y_only_symbols();
        symbols[1] = u(micro_version);
        ScriptedSymbolReader reader(std::move(symbols));
        mffv1::syntax::ConfigurationParser parser;
        mffv1::syntax::StreamParameters stream;
        stream.version = 1;

        const auto status = parser.parse(reader, stream);

        EXPECT_FALSE(status.ok()) << "micro_version=" << micro_version;
        EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature)
            << "micro_version=" << micro_version;
        EXPECT_EQ(status.message, "unstable FFV1 version 3 micro-version is not supported")
            << "micro_version=" << micro_version;
        EXPECT_EQ(stream.version, 1) << "micro_version=" << micro_version;
    }
}

TEST(ConfigurationParserTest, AcceptsFutureStableVersion3MicroVersion)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[1] = u(5);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.version, 3);
    EXPECT_EQ(stream.micro_version, 5);
}

TEST(ConfigurationParserTest, RejectsUnrepresentableMicroVersion)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[1] = u_max();
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "micro_version is too large");
}

TEST(ConfigurationParserTest, InterpretsReservedZeroBitDepthAsEight)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[4] = u(0);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.bits_per_raw_sample, 8u);
}

TEST(ConfigurationParserTest, RejectsUnsupportedBitDepth)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[4] = u(17);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(status.message, "only 1-16 bit samples are supported");
}

TEST(ConfigurationParserTest, ParsesLargeChromaSubsamplingExponents)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[6] = u(40);
    symbols[7] = u(255);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

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
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "horizontal chroma subsampling exponent is too large");
}

TEST(ConfigurationParserTest, RejectsUnrepresentableVerticalChromaSubsamplingExponent)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[7] = u(256);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "vertical chroma subsampling exponent is too large");
}

TEST(ConfigurationParserTest, RejectsUnsupportedVersion)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols.front() = u(2);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(status.message, "unsupported FFV1 version");
}

TEST(ConfigurationParserTest, ParsesCustomRangeCoderStateTransitions)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[2] = u(2);
    for (int state = 1; state < 256; ++state) {
        symbols.insert(symbols.begin() + 2 + state, s(state == 8 ? 5 : 0));
    }
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.entropy_mode, mffv1::EntropyMode::Range);
    EXPECT_EQ(stream.state_transition[0], 0u);
    EXPECT_EQ(stream.state_transition[8], 25u);
    EXPECT_EQ(stream.state_transition[255], 0u);
    EXPECT_EQ(reader.state_transition_update_count(), 1u);
    EXPECT_EQ(reader.last_state_transition(), stream.state_transition);
}

TEST(ConfigurationParserTest, RejectsOutOfRangeCustomStateTransition)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[2] = u(2);
    symbols.insert(symbols.begin() + 3, s(-1));
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "custom range coder state transition is outside 0..255");
}

TEST(ConfigurationParserTest, RejectsCustomStateTransitionAboveByteRange)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[2] = u(2);
    symbols.insert(symbols.begin() + 3, s(256));
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "custom range coder state transition is outside 0..255");
}

TEST(ConfigurationParserTest, RejectsReservedCoderType)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[2] = u(3);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(status.message, "unsupported range coder type");
}

TEST(ConfigurationParserTest, RejectsTruncatedCustomStateTransitions)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[2] = u(2);
    symbols.erase(symbols.begin() + 3, symbols.end());
    symbols.push_back(s(0));
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
}

TEST(ConfigurationParserTest, RejectsOversizedQuantTableRun)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[12] = u(128);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "quantization table run exceeds table boundary");
}

TEST(ConfigurationParserTest, RejectsOverflowingQuantTableRun)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[12] = u_max();
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "quantization table run exceeds table boundary");
}

TEST(ConfigurationParserTest, AcceptsFinalQuantTableRunPastTableBoundary)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols.erase(symbols.begin() + 12, symbols.end());
    symbols.push_back(u(126)); // table 0: 127 entries
    symbols.push_back(u(3));   // table 0: final run is clipped to entry 127
    for (int table = 1; table < 5; ++table) {
        symbols.push_back(u(127));
    }
    symbols.push_back(b(false)); // states_coded
    symbols.push_back(u(0));     // ec
    symbols.push_back(u(1));     // intra
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    ASSERT_EQ(stream.quant_table_sets.size(), 1u);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][126], 0);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][127], 1);
    EXPECT_EQ(stream.quant_table_sets[0].tables[0][128], -1);
}

TEST(ConfigurationParserTest, RejectsQuantTableValueOverflow)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols.erase(symbols.begin() + 12, symbols.end());
    for (int table = 0; table < 4; ++table) {
        for (int run = 0; run < 128; ++run) {
            symbols.push_back(u(0));
        }
    }
    symbols.push_back(u(0));
    symbols.push_back(u(0));
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_NE(status.message.find("quantization table value overflow"),
              std::string::npos);
}

TEST(ConfigurationParserTest, RejectsOutOfRangeQuantTableSetCount)
{
    for (const std::uint64_t quant_table_set_count : {0u, 9u}) {
        auto symbols = minimal_v3_y_only_symbols();
        symbols[11] = u(quant_table_set_count);
        ScriptedSymbolReader reader(std::move(symbols));
        mffv1::syntax::ConfigurationParser parser;
        mffv1::syntax::StreamParameters stream;

        const auto status = parser.parse(reader, stream);

        EXPECT_FALSE(status.ok()) << "quant_table_set_count=" << quant_table_set_count;
        EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError)
            << "quant_table_set_count=" << quant_table_set_count;
        const auto expected_prefix =
            "quant_table_set_count must be in the range 1..8: "
            + std::to_string(quant_table_set_count);
        EXPECT_EQ(status.message.find(expected_prefix), 0u)
            << "quant_table_set_count=" << quant_table_set_count;
        EXPECT_TRUE(status.location.has_byte_offset)
            << "quant_table_set_count=" << quant_table_set_count;
        EXPECT_EQ(status.location.byte_offset, 0u)
            << "quant_table_set_count=" << quant_table_set_count;
    }
}

TEST(ConfigurationParserTest, RejectsUnrepresentableSliceCountsBeforeIncrement)
{
    struct Case {
        std::size_t symbol_index;
        const char* message;
    };

    for (const Case& test_case :
         {Case{9, "num_h_slices is too large"}, Case{10, "num_v_slices is too large"}}) {
        auto symbols = minimal_v3_y_only_symbols();
        symbols[test_case.symbol_index] = u_max();
        ScriptedSymbolReader reader(std::move(symbols));
        mffv1::syntax::ConfigurationParser parser;
        mffv1::syntax::StreamParameters stream;

        const auto status = parser.parse(reader, stream);

        EXPECT_FALSE(status.ok()) << "symbol_index=" << test_case.symbol_index;
        EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError)
            << "symbol_index=" << test_case.symbol_index;
        EXPECT_EQ(status.message, test_case.message)
            << "symbol_index=" << test_case.symbol_index;
    }
}

TEST(ConfigurationParserTest, RejectsUnsupportedColorspace)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[3] = u(2);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(status.message, "unsupported colorspace_type");
}

TEST(ConfigurationParserTest, RejectsUnrepresentableColorspace)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[3] = u_max();
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
    EXPECT_EQ(status.message, "colorspace_type is too large");
}

TEST(ConfigurationParserTest, ParsesRgbParameters)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[3] = u(1);
    symbols[5] = b(true);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_EQ(stream.colorspace_type, 1);
    EXPECT_TRUE(stream.chroma_planes);
    EXPECT_EQ(stream.log2_h_chroma_subsample, 0u);
    EXPECT_EQ(stream.log2_v_chroma_subsample, 0u);
}

TEST(ConfigurationParserTest, RejectsInvalidRgbPlaneLayout)
{
    for (const std::size_t symbol_index : {std::size_t{5}, std::size_t{6}, std::size_t{7}}) {
        auto symbols = minimal_v3_y_only_symbols();
        symbols[3] = u(1);
        symbols[5] = b(true);
        symbols[symbol_index] = symbol_index == 5 ? b(false) : u(1);
        ScriptedSymbolReader reader(std::move(symbols));
        mffv1::syntax::ConfigurationParser parser;
        mffv1::syntax::StreamParameters stream;

        const auto status = parser.parse(reader, stream);

        EXPECT_FALSE(status.ok()) << "symbol_index=" << symbol_index;
        EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError)
            << "symbol_index=" << symbol_index;
    }
}

TEST(ConfigurationParserTest, ParsesCustomRangeCoderInitialStates)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[17] = b(true);
    for (int state = 0; state < 32; ++state) {
        symbols.insert(symbols.begin() + 18 + state, s(state == 0 ? -129 : state - 16));
    }
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

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
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

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
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::SyntaxError);
}

TEST(ConfigurationParserTest, AcceptsNonIntraStream)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols.back() = u(0);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;
    const auto status = parser.parse(reader, stream);

    EXPECT_TRUE(status.ok()) << status.message;
    EXPECT_FALSE(stream.intra_only);
}

TEST(ConfigurationParserTest, RejectsReservedErrorCorrectionModeWithAccurateDiagnostic)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols[symbols.size() - 2] = u(2);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(status.message, "unsupported error correction mode");
}

TEST(ConfigurationParserTest, RejectsReservedIntraModeWithAccurateDiagnostic)
{
    auto symbols = minimal_v3_y_only_symbols();
    symbols.back() = u(2);
    ScriptedSymbolReader reader(std::move(symbols));
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

    const auto status = parser.parse(reader, stream);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code, mffv1::ErrorCode::UnsupportedFeature);
    EXPECT_EQ(status.message, "unsupported intra mode");
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
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

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
    mffv1::syntax::ConfigurationParser parser;
    mffv1::syntax::StreamParameters stream;

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
