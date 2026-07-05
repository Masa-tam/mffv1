#include "codec/configuration_record_writer.hpp"

#include "mffv1/profile_constraints.hpp"
#include "entropy/range_encoder.hpp"
#include "util/crc32.hpp"

#include <cstdint>
#include <limits>
#include <sstream>
#include <utility>

namespace mffv1::codec {

namespace {

Status validate_quant_table_mirror(
    const syntax::QuantTableSet& table_set,
    std::size_t table_index)
{
    for (std::size_t mirror = 1; mirror < 128; ++mirror) {
        if (table_set.tables[table_index][256 - mirror]
            != -table_set.tables[table_index][mirror]) {
            return make_error(
                ErrorCode::InvalidState,
                "configuration quantization table mirror entries are inconsistent");
        }
    }
    if (table_set.tables[table_index][128]
        != -table_set.tables[table_index][127]) {
        return make_error(
            ErrorCode::InvalidState,
            "configuration quantization table center mirror entry is inconsistent");
    }
    return ok_status();
}

bool has_custom_state_transition(
    const syntax::StreamParameters& stream) noexcept
{
    return stream.entropy_mode == EntropyMode::Range
        && stream.state_transition != syntax::kDefaultStateTransition;
}

bool has_coded_initial_state_set(
    const syntax::InitialStateSet& state_set) noexcept
{
    return !state_set.contexts.empty();
}

Status write_u(entropy::SymbolWriter& writer, std::uint64_t value)
{
    return writer.write_unsigned(value);
}

Status write_b(entropy::SymbolWriter& writer, bool value)
{
    return writer.write_bool(value);
}

Status write_quant_table(
    const syntax::QuantTableSet& table_set,
    std::size_t table_index,
    std::int64_t scale,
    entropy::SymbolWriter& writer,
    std::int64_t& out_len_count)
{
    Status status = validate_quant_table_mirror(table_set, table_index);
    if (!status.ok()) {
        return status;
    }

    std::size_t k = 0;
    std::int64_t value = 0;
    while (k < 128) {
        if (value != 0
            && scale > std::numeric_limits<std::int64_t>::max() / value) {
            return make_error(
                ErrorCode::InvalidState,
                "configuration quantization table value overflow");
        }
        const auto expected = scale * value;
        if (table_set.tables[table_index][k] != expected) {
            std::ostringstream message;
            message << "configuration quantization table is not encodable at table "
                    << table_index << ", entry " << k;
            return make_error(ErrorCode::InvalidState, message.str());
        }

        std::size_t len = 0;
        while (k + len < 128
               && table_set.tables[table_index][k + len] == expected) {
            ++len;
        }
        if (len == 0) {
            return make_error(
                ErrorCode::InvalidState,
                "configuration quantization table has an empty run");
        }
        status = write_u(writer, len - 1);
        if (!status.ok()) {
            return status;
        }
        k += len;
        ++value;
    }

    out_len_count = value;
    return ok_status();
}

Status write_quant_table_set(
    const syntax::QuantTableSet& table_set,
    entropy::SymbolWriter& writer)
{
    std::int64_t scale = 1;
    Status status;
    for (std::size_t table = 0;
         table < syntax::QuantTableSet::kContextInputs;
         ++table) {
        status = writer.begin_independent_scalar_contexts(1);
        if (!status.ok()) {
            return status;
        }
        std::int64_t len_count = 0;
        status = write_quant_table(table_set, table, scale, writer, len_count);
        const Status end_status = writer.end_independent_scalar_contexts();
        if (!status.ok()) {
            return end_status.ok() ? status : end_status;
        }
        if (!end_status.ok()) {
            return end_status;
        }

        const std::int64_t multiplier = 2 * len_count - 1;
        if (multiplier <= 0
            || scale > (std::numeric_limits<std::int64_t>::max() / multiplier)) {
            return make_error(
                ErrorCode::InvalidState,
                "configuration quantization table scale overflow");
        }
        scale *= multiplier;
    }

    const auto context_count = static_cast<std::uint32_t>((scale + 1) / 2);
    if (table_set.context_count != context_count) {
        return make_error(
            ErrorCode::InvalidState,
            "configuration quantization table context count does not match encoded tables");
    }
    return ok_status();
}

Status write_initial_state_set(
    const syntax::InitialStateSet& state_set,
    entropy::SymbolWriter& writer)
{
    Status status = write_b(writer, has_coded_initial_state_set(state_set));
    if (!status.ok() || !has_coded_initial_state_set(state_set)) {
        return status;
    }

    for (std::size_t context = 0;
         context < state_set.contexts.size();
         ++context) {
        const auto& current = state_set.contexts[context];
        for (std::size_t state_index = 0;
             state_index < current.size();
             ++state_index) {
            const auto prediction = context == 0
                ? std::uint8_t{128}
                : state_set.contexts[context - 1][state_index];
            const auto delta =
                static_cast<std::int64_t>(current[state_index])
                - static_cast<std::int64_t>(prediction);
            status = writer.write_signed(
                static_cast<entropy::ContextId>(state_index), delta);
            if (!status.ok()) {
                return status;
            }
        }
    }
    return ok_status();
}

Status append_crc_parity(std::vector<std::byte>& bytes)
{
    constexpr std::size_t parity_size = 4;
    if (bytes.size() > bytes.max_size() - parity_size) {
        return make_error(
            ErrorCode::ResourceExhausted,
            "configuration record is too large for CRC parity");
    }
    const auto crc = util::crc32_ieee_msb(bytes);
    bytes.push_back(static_cast<std::byte>((crc >> 24) & 0xffu));
    bytes.push_back(static_cast<std::byte>((crc >> 16) & 0xffu));
    bytes.push_back(static_cast<std::byte>((crc >> 8) & 0xffu));
    bytes.push_back(static_cast<std::byte>(crc & 0xffu));
    return ok_status();
}

} // namespace

Status ConfigurationRecordWriter::write(
    const syntax::StreamParameters& stream,
    std::vector<std::byte>& out_record) const
{
    Status status = validate_initial_profile(stream);
    if (!status.ok()) {
        return status;
    }

    entropy::RangeEncoder writer;
    status = writer.reset(syntax::InitialState{}.size());
    if (!status.ok()) {
        return status;
    }
    status = write_parameters(stream, writer);
    if (!status.ok()) {
        return status;
    }

    std::vector<std::byte> record;
    status = writer.finalize(record);
    if (!status.ok()) {
        return status;
    }
    status = append_crc_parity(record);
    if (!status.ok()) {
        return status;
    }
    out_record = std::move(record);
    return ok_status();
}

Status ConfigurationRecordWriter::write_parameters(
    const syntax::StreamParameters& stream,
    entropy::SymbolWriter& writer) const
{
    Status status = validate_initial_profile(stream);
    if (!status.ok()) {
        return status;
    }

    const auto write_unsigned = [&](std::uint64_t value) {
        return write_u(writer, value);
    };
    const auto write_bool = [&](bool value) {
        return write_b(writer, value);
    };

    status = write_unsigned(3); // version
    if (!status.ok()) {
        return status;
    }
    status = write_unsigned(4); // micro_version
    if (!status.ok()) {
        return status;
    }
    status = write_unsigned(
        stream.entropy_mode == EntropyMode::GolombRice
            ? 0
            : (has_custom_state_transition(stream) ? 2 : 1));
    if (!status.ok()) {
        return status;
    }
    if (has_custom_state_transition(stream)) {
        for (std::size_t state = 1;
             state < stream.state_transition.size();
             ++state) {
            const auto delta =
                static_cast<std::int64_t>(stream.state_transition[state])
                - static_cast<std::int64_t>(
                    syntax::kDefaultStateTransition[state]);
            status = writer.write_signed(delta);
            if (!status.ok()) {
                return status;
            }
        }
    }
    status = write_unsigned(
        static_cast<std::uint64_t>(stream.colorspace_type));
    if (!status.ok()) {
        return status;
    }
    status = write_unsigned(stream.bits_per_raw_sample);
    if (!status.ok()) {
        return status;
    }
    status = write_bool(stream.chroma_planes);
    if (!status.ok()) {
        return status;
    }
    status = write_unsigned(stream.log2_h_chroma_subsample);
    if (!status.ok()) {
        return status;
    }
    status = write_unsigned(stream.log2_v_chroma_subsample);
    if (!status.ok()) {
        return status;
    }
    status = write_bool(stream.extra_plane);
    if (!status.ok()) {
        return status;
    }
    status = write_unsigned(stream.num_h_slices - 1);
    if (!status.ok()) {
        return status;
    }
    status = write_unsigned(stream.num_v_slices - 1);
    if (!status.ok()) {
        return status;
    }
    status = write_unsigned(stream.quant_table_sets.size()); // quant_table_set_count
    if (!status.ok()) {
        return status;
    }

    for (const auto& table_set : stream.quant_table_sets) {
        status = write_quant_table_set(table_set, writer);
        if (!status.ok()) {
            return status;
        }
    }

    if (stream.initial_states.empty()) {
        for (std::size_t i = 0; i < stream.quant_table_sets.size(); ++i) {
            status = write_bool(false); // states_coded
            if (!status.ok()) {
                return status;
            }
        }
    } else {
        for (const auto& state_set : stream.initial_states) {
            status = write_initial_state_set(state_set, writer);
            if (!status.ok()) {
                return status;
            }
        }
    }
    status = write_unsigned(stream.error_status_enabled ? 1 : 0); // ec
    if (!status.ok()) {
        return status;
    }
    return write_unsigned(stream.intra_only ? 1 : 0); // intra
}

Status ConfigurationRecordWriter::validate_initial_profile(
    const syntax::StreamParameters& stream) const
{
    if (stream.version != 3 || stream.micro_version != 4) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "configuration writer supports only FFV1 version 3 micro-version 4");
    }
    if (stream.entropy_mode != EntropyMode::Range
        && stream.entropy_mode != EntropyMode::GolombRice) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "configuration writer supports only range or Golomb-Rice coding");
    }
    if (stream.entropy_mode == EntropyMode::GolombRice
        && !constraints::is_supported_encoder_bit_depth(stream.bits_per_raw_sample)) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "Golomb-Rice configuration supports only 1-16 bit streams");
    }
    if (!constraints::is_supported_syntax_colorspace(stream.colorspace_type)
        || !constraints::is_supported_encoder_bit_depth(stream.bits_per_raw_sample)) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "configuration writer supports only 1-16 bit planar YCbCr or RGB streams, with an optional extra plane");
    }
    if (constraints::has_invalid_rgb_geometry(
            stream.colorspace_type == 1,
            stream.chroma_planes,
            stream.log2_h_chroma_subsample,
            stream.log2_v_chroma_subsample)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "RGB streams require three full-resolution color planes");
    }
    if (constraints::has_subsampling_without_chroma(
            stream.chroma_planes,
            stream.log2_h_chroma_subsample,
            stream.log2_v_chroma_subsample)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "chroma subsampling requires chroma planes");
    }
    if (!constraints::is_supported_chroma_subsampling(
            stream.log2_h_chroma_subsample,
            stream.log2_v_chroma_subsample)) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "configuration writer supports only 4:4:4, 4:2:2, and 4:2:0 chroma geometry");
    }
    if (stream.num_h_slices == 0 || stream.num_v_slices == 0) {
        return make_error(
            ErrorCode::InvalidArgument,
            "configuration slice grid dimensions must be non-zero");
    }
    if (stream.quant_table_sets.empty() || stream.quant_table_sets.size() > 8) {
        return make_error(
            ErrorCode::InvalidArgument,
            "configuration quantization table set count must be in the range 1..8");
    }
    for (const auto& table_set : stream.quant_table_sets) {
        if (table_set.context_count == 0) {
            return make_error(
                ErrorCode::InvalidState,
                "configuration quantization table set has no contexts");
        }
    }
    if (!stream.initial_states.empty()
        && stream.initial_states.size() != stream.quant_table_sets.size()) {
        return make_error(
            ErrorCode::InvalidState,
            "configuration initial state set count does not match quantization table set count");
    }
    for (std::size_t i = 0; i < stream.initial_states.size(); ++i) {
        const auto& contexts = stream.initial_states[i].contexts;
        if (!contexts.empty()
            && contexts.size() != stream.quant_table_sets[i].context_count) {
            return make_error(
                ErrorCode::InvalidState,
                "configuration initial state count does not match quantization contexts");
        }
    }
    return ok_status();
}

} // namespace mffv1::codec
