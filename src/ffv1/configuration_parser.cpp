#include "ffv1/configuration_parser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace ffv1::syntax {

QuantTableSet make_zero_quant_table_set()
{
    QuantTableSet table_set;
    table_set.context_count = 1;
    return table_set;
}

namespace {

constexpr std::uint64_t kMaxQuantTableSetCount = 8;
constexpr std::uint64_t kMaxContextCount = 32768;

Status read_u(entropy::SymbolReader& reader, std::uint64_t& out_value)
{
    return reader.read_unsigned(out_value);
}

Status read_b(entropy::SymbolReader& reader, bool& out_value)
{
    return reader.read_bool(out_value);
}

Status checked_u32(std::uint64_t value, const char* name, std::uint32_t& out_value)
{
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        return make_error(ErrorCode::SyntaxError, std::string{name} + " is too large");
    }
    out_value = static_cast<std::uint32_t>(value);
    return ok_status();
}

} // namespace

Status ConfigurationParser::parse(entropy::SymbolReader& reader,
                                  StreamParameters& out_stream) const
{
    StreamParameters stream;

    std::uint64_t value = 0;
    Status status = read_u(reader, value);
    if (!status.ok()) {
        return status;
    }
    if (value != 0 && value != 1 && value != 3) {
        return make_error(ErrorCode::UnsupportedFeature, "unsupported FFV1 version");
    }
    stream.version = static_cast<int>(value);

    if (stream.version >= 3) {
        status = read_u(reader, value);
        if (!status.ok()) {
            return status;
        }
        if (value > std::numeric_limits<int>::max()) {
            return make_error(ErrorCode::SyntaxError, "micro_version is too large");
        }
        if (value < 4) {
            return make_error(ErrorCode::UnsupportedFeature,
                              "unstable FFV1 version 3 micro-version is not supported");
        }
        stream.micro_version = static_cast<int>(value);
    }

    status = read_u(reader, value);
    if (!status.ok()) {
        return status;
    }
    if (value == 0) {
        stream.entropy_mode = EntropyMode::GolombRice;
    } else if (value == 1) {
        stream.entropy_mode = EntropyMode::Range;
    } else if (value == 2) {
        stream.entropy_mode = EntropyMode::Range;
        for (std::size_t state = 1; state < stream.state_transition.size(); ++state) {
            std::int64_t delta = 0;
            status = reader.read_signed(delta);
            if (!status.ok()) {
                return status;
            }
            const auto transition = static_cast<std::int64_t>(stream.state_transition[state]) + delta;
            if (transition < 0 || transition > 255) {
                return make_error(ErrorCode::SyntaxError,
                                  "custom range coder state transition is outside 0..255");
            }
            stream.state_transition[state] = static_cast<std::uint8_t>(transition);
        }
    } else {
        return make_error(ErrorCode::UnsupportedFeature, "unsupported range coder type");
    }

    status = read_u(reader, value);
    if (!status.ok()) {
        return status;
    }
    if (value > std::numeric_limits<int>::max()) {
        return make_error(ErrorCode::SyntaxError, "colorspace_type is too large");
    }
    stream.colorspace_type = static_cast<int>(value);
    if (stream.colorspace_type != 0) {
        return make_error(ErrorCode::UnsupportedFeature, "only YCbCr colorspace is supported in the base parser");
    }

    if (stream.version >= 1) {
        status = read_u(reader, value);
        if (!status.ok()) {
            return status;
        }
        if (value > 16) {
            return make_error(ErrorCode::UnsupportedFeature, "only 1-16 bit samples are supported");
        }
        stream.bits_per_raw_sample = value == 0 ? std::uint8_t{8} : static_cast<std::uint8_t>(value);
    }

    bool flag = false;
    status = read_b(reader, flag);
    if (!status.ok()) {
        return status;
    }
    stream.chroma_planes = flag;

    status = read_u(reader, value);
    if (!status.ok()) {
        return status;
    }
    if (value > std::numeric_limits<std::uint8_t>::max()) {
        return make_error(ErrorCode::SyntaxError, "horizontal chroma subsampling exponent is too large");
    }
    stream.log2_h_chroma_subsample = static_cast<std::uint8_t>(value);

    status = read_u(reader, value);
    if (!status.ok()) {
        return status;
    }
    if (value > std::numeric_limits<std::uint8_t>::max()) {
        return make_error(ErrorCode::SyntaxError, "vertical chroma subsampling exponent is too large");
    }
    stream.log2_v_chroma_subsample = static_cast<std::uint8_t>(value);

    status = read_b(reader, flag);
    if (!status.ok()) {
        return status;
    }
    stream.extra_plane = flag;

    std::uint64_t quant_table_set_count = 1;
    if (stream.version >= 3) {
        status = read_u(reader, value);
        if (!status.ok()) {
            return status;
        }
        status = checked_u32(value + 1, "num_h_slices", stream.num_h_slices);
        if (!status.ok()) {
            return status;
        }

        status = read_u(reader, value);
        if (!status.ok()) {
            return status;
        }
        status = checked_u32(value + 1, "num_v_slices", stream.num_v_slices);
        if (!status.ok()) {
            return status;
        }

        status = read_u(reader, quant_table_set_count);
        if (!status.ok()) {
            return status;
        }
        if (quant_table_set_count == 0 || quant_table_set_count > kMaxQuantTableSetCount) {
            return make_error(ErrorCode::SyntaxError, "quant_table_set_count must be in the range 1..8");
        }
    }

    if (stream.version == 0) {
        stream.quant_table_sets.push_back(make_zero_quant_table_set());
    } else {
        stream.quant_table_sets.resize(static_cast<std::size_t>(quant_table_set_count));
        for (auto& set : stream.quant_table_sets) {
            status = parse_quant_table_set(reader, set);
            if (!status.ok()) {
                return status;
            }
        }
    }

    if (stream.version >= 3) {
        stream.initial_states.resize(stream.quant_table_sets.size());
        for (std::size_t i = 0; i < stream.quant_table_sets.size(); ++i) {
            status = read_b(reader, flag);
            if (!status.ok()) {
                return status;
            }
            if (flag) {
                auto& states = stream.initial_states[i].contexts;
                states.resize(stream.quant_table_sets[i].context_count);
                for (std::size_t context = 0; context < states.size(); ++context) {
                    for (std::size_t state_index = 0; state_index < InitialState{}.size(); ++state_index) {
                        std::int64_t delta = 0;
                        status = reader.read_signed(static_cast<entropy::ContextId>(state_index), delta);
                        if (!status.ok()) {
                            return status;
                        }
                        const auto prediction = context == 0
                            ? std::uint8_t{128}
                            : states[context - 1][state_index];
                        states[context][state_index] = static_cast<std::uint8_t>(
                            prediction + static_cast<std::uint8_t>(delta));
                    }
                }
            }
        }

        status = read_u(reader, value);
        if (!status.ok()) {
            return status;
        }
        if (value > 1) {
            return make_error(ErrorCode::UnsupportedFeature, "unsupported error correction mode");
        }
        stream.error_status_enabled = value == 1;

        status = read_u(reader, value);
        if (!status.ok()) {
            return status;
        }
        if (value != 1) {
            return make_error(ErrorCode::UnsupportedFeature,
                              "non-intra FFV1 streams are not implemented yet");
        }
    }

    out_stream = std::move(stream);
    return ok_status();
}

Status ConfigurationParser::parse_quant_table_set(entropy::SymbolReader& reader,
                                                  QuantTableSet& out_set) const
{
    std::int64_t scale = 1;
    for (std::size_t table_index = 0; table_index < QuantTableSet::kContextInputs; ++table_index) {
        std::int64_t len_count = 0;
        const Status status = parse_quant_table(reader, out_set, table_index, scale, len_count);
        if (!status.ok()) {
            return status;
        }

        const std::int64_t multiplier = 2 * len_count - 1;
        if (multiplier <= 0 || scale > (std::numeric_limits<std::int64_t>::max() / multiplier)) {
            return make_error(ErrorCode::SyntaxError, "quantization table scale overflow");
        }
        scale *= multiplier;
    }

    const std::int64_t context_count = (scale + 1) / 2;
    if (context_count <= 0 || context_count > static_cast<std::int64_t>(kMaxContextCount)) {
        return make_error(ErrorCode::SyntaxError, "context_count is outside the supported range");
    }
    out_set.context_count = static_cast<std::uint32_t>(context_count);
    return ok_status();
}

Status ConfigurationParser::parse_quant_table(entropy::SymbolReader& reader,
                                              QuantTableSet& table_set,
                                              std::size_t table_index,
                                              std::int64_t scale,
                                              std::int64_t& out_len_count) const
{
    std::int64_t value = 0;
    std::size_t k = 0;
    while (k < 128) {
        std::uint64_t len_minus_one = 0;
        Status status = read_u(reader, len_minus_one);
        if (!status.ok()) {
            return status;
        }
        const std::uint64_t len = len_minus_one + 1;
        if (len > (128 - k)) {
            return make_error(ErrorCode::SyntaxError, "quantization table run exceeds table boundary");
        }
        if (value > std::numeric_limits<std::int32_t>::max() / std::max<std::int64_t>(scale, 1)) {
            return make_error(ErrorCode::SyntaxError, "quantization table value overflow");
        }

        const auto stored = static_cast<std::int32_t>(scale * value);
        for (std::uint64_t n = 0; n < len; ++n) {
            table_set.tables[table_index][k] = stored;
            ++k;
        }
        ++value;
    }

    for (std::size_t mirror = 1; mirror < 128; ++mirror) {
        table_set.tables[table_index][256 - mirror] = -table_set.tables[table_index][mirror];
    }
    table_set.tables[table_index][128] = -table_set.tables[table_index][127];
    out_len_count = value;
    return ok_status();
}

} // namespace ffv1::syntax
