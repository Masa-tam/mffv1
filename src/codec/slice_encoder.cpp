#include "codec/slice_encoder.hpp"

#include "codec/frame_validator.hpp"
#include "codec/slice_footer_writer.hpp"
#include "codec/slice_header_writer.hpp"
#include "entropy/range_encoder.hpp"
#include "mffv1/context_model.hpp"
#include "mffv1/line_state.hpp"
#include "mffv1/predictor.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace mffv1::codec {

SliceEncoder::SliceEncoder(const syntax::StreamParameters& stream) noexcept
    : stream_(stream)
{
}

Status SliceEncoder::validate_stream() const
{
    if (stream_.entropy_mode != EntropyMode::Range
        || stream_.colorspace_type != 0
        || stream_.bits_per_raw_sample != 8) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "slice encoder supports only range-coded 8-bit planar Y or YCbCr streams, with an optional extra plane");
    }
    if (!stream_.chroma_planes
        && (stream_.log2_h_chroma_subsample != 0
            || stream_.log2_v_chroma_subsample != 0)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "chroma subsampling requires chroma planes");
    }
    if (stream_.log2_h_chroma_subsample > 1
        || stream_.log2_v_chroma_subsample > 1
        || stream_.log2_v_chroma_subsample
            > stream_.log2_h_chroma_subsample) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "slice encoder supports only 4:4:4, 4:2:2, and 4:2:0 chroma geometry");
    }
    if (stream_.quant_table_sets.size() != 1) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "slice encoder requires exactly one quantization table set");
    }
    if (!stream_.initial_states.empty()
        && stream_.initial_states.size() != stream_.quant_table_sets.size()) {
        return make_error(
            ErrorCode::InvalidState,
            "range coder initial state set count does not match quantization table set count");
    }
    const auto context_count = stream_.quant_table_sets[0].context_count;
    if (context_count == 0) {
        return make_error(
            ErrorCode::InvalidState,
            "slice encoder quantization table set has no contexts");
    }
    if (!stream_.initial_states.empty()
        && !stream_.initial_states[0].contexts.empty()
        && stream_.initial_states[0].contexts.size() != context_count) {
        return make_error(
            ErrorCode::InvalidState,
            "range coder initial state count does not match quantization contexts");
    }
    return ok_status();
}

Status SliceEncoder::encode_content(
    FrameView input,
    std::vector<std::byte>& out_payload) const
{
    Status status = validate_stream();
    if (!status.ok()) {
        return status;
    }

    const FrameValidator validator;
    status = validator.validate_input(stream_, input);
    if (!status.ok()) {
        return status;
    }

    entropy::RangeEncoder writer;
    const auto plane_count =
        static_cast<std::size_t>(syntax::coded_plane_count(stream_));
    const auto context_count =
        static_cast<std::size_t>(stream_.quant_table_sets[0].context_count);
    std::vector<std::size_t> context_counts(plane_count, context_count);
    std::vector<std::span<const entropy::RangeEncoder::ScalarContextStates>>
        initial_state_banks(plane_count);
    if (!stream_.initial_states.empty()) {
        for (auto& bank : initial_state_banks) {
            bank = stream_.initial_states[0].contexts;
        }
    }

    status = writer.reset(
        context_counts,
        initial_state_banks,
        stream_.state_transition);
    if (!status.ok()) {
        return status;
    }

    status = encode_samples(input, writer);
    if (!status.ok()) {
        return status;
    }

    std::vector<std::byte> payload;
    status = writer.finalize(payload);
    if (!status.ok()) {
        return status;
    }
    out_payload = std::move(payload);
    return ok_status();
}

Status SliceEncoder::encode_slice(
    FrameView input,
    bool keyframe,
    std::vector<std::byte>& out_payload) const
{
    Status status = validate_stream();
    if (!status.ok()) {
        return status;
    }
    if (stream_.version != 3
        || stream_.num_h_slices != 1
        || stream_.num_v_slices != 1) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "slice assembly supports only one FFV1 version 3 slice");
    }
    if (!keyframe && stream_.intra_only) {
        return make_error(
            ErrorCode::InvalidArgument,
            "non-keyframe is invalid for an intra-only stream");
    }

    const FrameValidator validator;
    status = validator.validate_input(stream_, input);
    if (!status.ok()) {
        return status;
    }

    entropy::RangeEncoder writer;
    status = writer.reset(stream_.state_transition);
    if (!status.ok()) {
        return status;
    }
    status = writer.write_bool(keyframe);
    if (!status.ok()) {
        return status;
    }

    SliceHeaderValues header;
    header.width = 1;
    header.height = 1;
    header.quant_table_set_indexes.assign(
        syntax::quant_table_set_index_count(stream_), 0);
    const SliceHeaderWriter header_writer;
    status = header_writer.write(writer, stream_, header);
    if (!status.ok()) {
        return status;
    }

    const auto plane_count =
        static_cast<std::size_t>(syntax::coded_plane_count(stream_));
    const auto context_count =
        static_cast<std::size_t>(stream_.quant_table_sets[0].context_count);
    std::vector<std::size_t> context_counts(plane_count, context_count);
    std::vector<std::span<const entropy::RangeEncoder::ScalarContextStates>>
        initial_state_banks(plane_count);
    if (!stream_.initial_states.empty()) {
        for (auto& bank : initial_state_banks) {
            bank = stream_.initial_states[0].contexts;
        }
    }
    status = writer.reconfigure_contexts(
        context_counts, initial_state_banks);
    if (!status.ok()) {
        return status;
    }
    status = encode_samples(input, writer);
    if (!status.ok()) {
        return status;
    }

    std::vector<std::byte> payload;
    status = writer.finalize(payload);
    if (!status.ok()) {
        return status;
    }
    const SliceFooterWriter footer_writer;
    status = footer_writer.append(stream_, 0, payload);
    if (!status.ok()) {
        return status;
    }
    out_payload = std::move(payload);
    return ok_status();
}

Status SliceEncoder::encode_samples(
    FrameView input,
    entropy::RangeEncoder& writer) const
{
    const auto plane_count =
        static_cast<std::size_t>(syntax::coded_plane_count(stream_));
    std::vector<syntax::LineState> lines(plane_count);
    for (std::size_t plane_index = 0;
         plane_index < plane_count;
         ++plane_index) {
        Status status = lines[plane_index].reset(
            syntax::plane_width(stream_, plane_index));
        if (!status.ok()) {
            return status;
        }
    }
    const syntax::ContextModel context_model(stream_.quant_table_sets[0]);

    for (std::size_t plane_index = 0;
         plane_index < plane_count;
         ++plane_index) {
        auto& line = lines[plane_index];
        const auto& plane = input.planes[plane_index];
        const auto* base = static_cast<const std::byte*>(plane.data);
        const auto width = syntax::plane_width(stream_, plane_index);
        const auto height = syntax::plane_height(stream_, plane_index);

        for (std::uint32_t y = 0; y < height; ++y) {
            const auto* row = reinterpret_cast<const std::uint8_t*>(
                base + static_cast<std::ptrdiff_t>(y)
                    * plane.info.stride_bytes);
            for (std::uint32_t x = 0; x < width; ++x) {
                const auto neighbors = line.neighbors(x);
                const auto prediction = syntax::Predictor::median_predict(
                    neighbors.left, neighbors.top, neighbors.top_left);
                syntax::ContextDecision context;
                Status status =
                    context_model.derive_context(neighbors, context);
                if (!status.ok()) {
                    return status;
                }

                const auto sample = static_cast<std::int32_t>(row[x]);
                auto difference = syntax::Predictor::difference(
                    sample, prediction, stream_.bits_per_raw_sample);
                if (context.invert_difference) {
                    difference = -difference;
                }
                status = writer.write_signed(
                    plane_index, context.context, difference);
                if (!status.ok()) {
                    return status;
                }
                line.mutable_current()[x] = sample;
            }
            line.swap_lines();
        }
    }
    return ok_status();
}

} // namespace mffv1::codec
