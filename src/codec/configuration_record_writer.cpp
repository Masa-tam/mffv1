#include "codec/configuration_record_writer.hpp"

#include "codec/profile_constraints.hpp"
#include "entropy/range_encoder.hpp"
#include "util/crc32.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace mffv1::codec {

namespace {

bool is_zero_quant_table_set(const syntax::QuantTableSet& table_set) noexcept
{
    if (table_set.context_count != 1) {
        return false;
    }
    return std::all_of(
        table_set.tables.begin(),
        table_set.tables.end(),
        [](const auto& table) {
            return std::all_of(
                table.begin(), table.end(), [](std::int32_t value) {
                    return value == 0;
                });
        });
}

Status write_u(entropy::SymbolWriter& writer, std::uint64_t value)
{
    return writer.write_unsigned(value);
}

Status write_b(entropy::SymbolWriter& writer, bool value)
{
    return writer.write_bool(value);
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
    status = writer.reset();
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
        stream.entropy_mode == EntropyMode::GolombRice ? 0 : 1);
    if (!status.ok()) {
        return status;
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
    status = write_unsigned(1); // quant_table_set_count
    if (!status.ok()) {
        return status;
    }

    for (std::size_t table = 0;
         table < syntax::QuantTableSet::kContextInputs;
         ++table) {
        status = write_unsigned(127); // one run of 128 zero entries
        if (!status.ok()) {
            return status;
        }
    }

    status = write_bool(false); // states_coded
    if (!status.ok()) {
        return status;
    }
    status = write_unsigned(0); // ec
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
    if ((stream.entropy_mode != EntropyMode::Range
         && stream.entropy_mode != EntropyMode::GolombRice)
        || (stream.entropy_mode == EntropyMode::Range
            && stream.state_transition != syntax::kDefaultStateTransition)) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "configuration writer supports Golomb-Rice or the default range coder");
    }
    if (stream.entropy_mode == EntropyMode::GolombRice
        && !is_supported_encoder_bit_depth(stream.bits_per_raw_sample)) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "Golomb-Rice configuration supports only 8-16 bit streams");
    }
    if (!is_supported_syntax_colorspace(stream.colorspace_type)
        || !is_supported_encoder_bit_depth(stream.bits_per_raw_sample)) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "configuration writer supports only 8-16 bit planar YCbCr or RGB streams, with an optional extra plane");
    }
    if (has_invalid_rgb_geometry(
            stream.colorspace_type == 1,
            stream.chroma_planes,
            stream.log2_h_chroma_subsample,
            stream.log2_v_chroma_subsample)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "RGB streams require three full-resolution color planes");
    }
    if (has_subsampling_without_chroma(
            stream.chroma_planes,
            stream.log2_h_chroma_subsample,
            stream.log2_v_chroma_subsample)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "chroma subsampling requires chroma planes");
    }
    if (!is_supported_chroma_subsampling(
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
    if (stream.quant_table_sets.size() != 1
        || !is_zero_quant_table_set(stream.quant_table_sets[0])) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "configuration writer supports only one zero quantization table set");
    }
    if ((!stream.initial_states.empty()
         && (stream.initial_states.size() != 1
             || !stream.initial_states[0].contexts.empty()))
        || stream.error_status_enabled) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "configuration writer supports only streams without custom states or EC");
    }
    return ok_status();
}

} // namespace mffv1::codec
